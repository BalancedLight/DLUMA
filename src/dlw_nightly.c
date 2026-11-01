#include "dlw_common.h"
#include "dlw_build_identity.h"
#include "dlw_nightly.h"
#include "dlw_nightly_download.h"
#include "dlw_project_owner_identity.h"
#include "core/dlw_nightly_offer_core.h"
#include "core/dlw_json_reader.h"
#include <string.h>

typedef struct NightlyStartup {
    DWORD size; LPWSTR reserved, desktop, title; DWORD x, y, width, height;
    DWORD x_chars, y_chars, fill, flags; WORD show, reserved_size;
    BYTE* reserved_data; HANDLE input, output, error;
} NightlyStartup;
typedef struct NightlyProcess { HANDLE process, thread; DWORD pid, tid; } NightlyProcess;
__declspec(dllimport) BOOL WINAPI CreateProcessW(LPCWSTR, LPWSTR, LPVOID, LPVOID, BOOL, DWORD, LPVOID, LPCWSTR, NightlyStartup*, NightlyProcess*);
__declspec(dllimport) BOOL WINAPI GetExitCodeProcess(HANDLE, DWORD*);
__declspec(dllimport) BOOL WINAPI TerminateProcess(HANDLE, unsigned int);
__declspec(dllimport) HANDLE WINAPI CreateJobObjectW(void *, LPCWSTR);
__declspec(dllimport) BOOL WINAPI AssignProcessToJobObject(HANDLE, HANDLE);
__declspec(dllimport) BOOL WINAPI TerminateJobObject(HANDLE, unsigned int);
__declspec(dllimport) DWORD WINAPI ResumeThread(HANDLE);

enum NightlyPhase { NIGHTLY_IDLE, NIGHTLY_MANIFEST, NIGHTLY_COMPARE, NIGHTLY_OFFER,
    NIGHTLY_ARCHIVE, NIGHTLY_QUEUED, NIGHTLY_FINISHED };
enum NightlyStatus { STATUS_DISABLED, STATUS_CHECKING, STATUS_CURRENT, STATUS_AVAILABLE,
    STATUS_DOWNLOADING, STATUS_READY, STATUS_FAILED, STATUS_RESTORE, STATUS_UNKNOWN, STATUS_DECLINED };

static const char *const status_text[]={"Off","Checking for updates","Up to date","Update available",
    "Downloading update","Ready; close Player to apply","Update check failed",
    "Stable build available","Current build version is unavailable","Update declined"};
static volatile LONG initialized, requested_enabled, snapshot_lock, current_status;
static struct {
    WCHAR module[DLUMA_MAX_WPATH], directory[DLUMA_MAX_WPATH], root[DLUMA_MAX_WPATH];
    WCHAR stage[DLUMA_MAX_WPATH], manifest_path[DLUMA_MAX_WPATH], compare_path[DLUMA_MAX_WPATH];
    WCHAR archive_path[DLUMA_MAX_WPATH], result_path[DLUMA_MAX_WPATH];
    char current[41], digest[65];
    DlumaNightlyManifest manifest;
    DlwNightlyOffer published;
    unsigned int sequence, response_ticket;
    int response, enabled, phase;
    HANDLE installer;
    HANDLE installer_job;
} state;

static void lock_snapshot(void)
{
    while (dluma_atomic_compare_exchange(&snapshot_lock,1,0)) Sleep(0);
}
static void unlock_snapshot(void) { dluma_atomic_exchange(&snapshot_lock,0); }
static void publish(DlwNightlyKind kind)
{
    DlwNightlyOffer offer;
    memset(&offer,0,sizeof(offer)); offer.kind=kind;
    memcpy(offer.commit,state.manifest.commit,sizeof(offer.commit));
    offer.ticket=++state.sequence;
    if (!offer.ticket) offer.ticket=++state.sequence;
    lock_snapshot(); state.published=offer; state.response=0; state.response_ticket=0; unlock_snapshot();
}
static void status_set(int value)
{
    if (dluma_atomic_exchange(&current_status,value)!=value)
        dluma_log_pair("INFO","nightly_status",status_text[value]);
}
const char *dlw_nightly_status_text(void)
{
    LONG value=dluma_atomic_compare_exchange(&current_status,0,0);
    return value>=0 && value<(LONG)DLUMA_ARRAY_COUNT(status_text) ? status_text[value] : "Update check failed";
}
static void append_number(WCHAR *text,unsigned int capacity,ULONGLONG value)
{
    WCHAR reverse[32],number[32]; unsigned int n=0,i;
    do { reverse[n++]=(WCHAR)(L'0'+value%10); value/=10; } while (value && n<31);
    for (i=0;i<n;++i) number[i]=reverse[n-i-1]; number[n]=0;
    dluma_wstr_append(text,capacity,number);
}
static void argument(WCHAR *text,unsigned int capacity,const WCHAR *value)
{
    if (text[0]) dluma_wstr_append(text,capacity,L" ");
    dluma_wstr_append(text,capacity,L"\""); dluma_wstr_append(text,capacity,value);
    dluma_wstr_append(text,capacity,L"\"");
}
static void lower_digest(char digest[65])
{
    unsigned int i; for(i=0;i<64;++i) if(digest[i]>='A' && digest[i]<='F') digest[i]+='a'-'A';
}
static unsigned int wide_length(const WCHAR *text)
{
    unsigned int count=0;while(text[count]) ++count;return count;
}
static unsigned char *read_file(const WCHAR *path,unsigned int limit,unsigned int *length)
{
    HANDLE file; LARGE_INTEGER size; DWORD got=0; unsigned char *bytes=0;
    *length=0;
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE) return 0;
    if(GetFileSizeEx(file,&size) && size.QuadPart>0 && (ULONGLONG)size.QuadPart<=limit) {
        bytes=(unsigned char *)dluma_alloc((SIZE_T)size.QuadPart+1);
        if(bytes && ReadFile(file,bytes,(DWORD)size.QuadPart,&got,0) && got==(DWORD)size.QuadPart) {
            bytes[got]=0; *length=got;
        } else { if(bytes) dluma_free(bytes); bytes=0; }
    }
    CloseHandle(file); return bytes;
}
static void cleanup_stage(void)
{
    if(!state.stage[0]) return;
    DeleteFileW(state.manifest_path); DeleteFileW(state.compare_path); DeleteFileW(state.archive_path);
    RemoveDirectoryW(state.stage); state.stage[0]=0;
}
static void failed(const char *reason)
{
    state.phase=NIGHTLY_FINISHED; status_set(STATUS_FAILED); publish(DLW_NIGHTLY_ERROR);
    dluma_log_pair("WARN","nightly_check_result",reason);
    dluma_log_hex("WARN","nightly_download_error",dlw_nightly_download_error());
}

/* A local backup may offer restoration; the apply script verifies the complete
   chain and hashes before changing any files. No network request runs here. */
static int restore_available(void)
{
    WCHAR pattern[DLUMA_MAX_WPATH],path[DLUMA_MAX_WPATH],transaction[DLUMA_MAX_WPATH];
    WCHAR target_wide[DLUMA_MAX_WPATH],marker[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW entry; HANDLE find; unsigned int count=0; int available=0;
    dluma_path_join(pattern,DLUMA_ARRAY_COUNT(pattern),state.root,L"chain-*.json");
    find=FindFirstFileW(pattern,&entry); if(find==INVALID_HANDLE_VALUE) return 0;
    do {
        unsigned int length=0,seen=0,i; unsigned char *bytes;
        char id[33]={0},target[DLUMA_MAX_WPATH*3]={0},key[64];
        DlumaJsonReader reader; int first=1,next,valid=1;
        if(++count>256) break;
        if(entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        dluma_path_join(path,DLUMA_ARRAY_COUNT(path),state.root,entry.cFileName);
        bytes=read_file(path,4096,&length); if(!bytes) continue;
        dluma_json_reader_initialize(&reader,bytes,length);
        if(!dluma_json_object_begin(&reader)) valid=0;
        while(valid && (next=dluma_json_object_next(&reader,&first))!=0) {
            if(next<0 || !dluma_json_string(&reader,key,sizeof(key)) || !dluma_json_take(&reader,':')) {valid=0;break;}
            if(!dluma_nightly_text_compare(key,"Id")) {
                if((seen&1) || !dluma_json_string(&reader,id,sizeof(id))) {valid=0;break;} seen|=1;
            } else if(!dluma_nightly_text_compare(key,"Target")) {
                if((seen&2) || !dluma_json_string(&reader,target,sizeof(target))) {valid=0;break;} seen|=2;
            } else if(!dluma_json_skip_value(&reader)) {valid=0;break;}
        }
        if(!valid || seen!=3 || !dluma_json_object_end(&reader) || !dluma_json_finished(&reader)) valid=0;
        dluma_free(bytes);
        for(i=0;valid && i<32;++i) if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f'))) valid=0;
        if(!valid || id[32] || !dluma_utf8_to_wide(target,target_wide,DLUMA_ARRAY_COUNT(target_wide)) ||
            dluma_wstricmp(target_wide,state.directory)) continue;
        dluma_wstr_copy(transaction,DLUMA_ARRAY_COUNT(transaction),state.root);
        dluma_wstr_append(transaction,DLUMA_ARRAY_COUNT(transaction),L"\\transaction-");
        dluma_utf8_to_wide(id,target_wide,DLUMA_ARRAY_COUNT(target_wide));
        dluma_wstr_append(transaction,DLUMA_ARRAY_COUNT(transaction),target_wide);
        dluma_path_join(marker,DLUMA_ARRAY_COUNT(marker),transaction,L"restored");
        if(GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES) continue;
        dluma_path_join(marker,DLUMA_ARRAY_COUNT(marker),transaction,L"committed");
        if(GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES) available=1;
        else {
            dluma_path_join(marker,DLUMA_ARRAY_COUNT(marker),transaction,L"journal.json");
            if(GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES) {available=2;break;}
        }
    } while(FindNextFileW(find,&entry));
    FindClose(find); return available;
}
static int prepare_stage(void)
{
    unsigned int i;
    WCHAR name[96];
    for(i=0;i<8;++i) {
        dluma_wstr_copy(name,DLUMA_ARRAY_COUNT(name),L"stage-");
        append_number(name,DLUMA_ARRAY_COUNT(name),GetCurrentProcessId());
        dluma_wstr_append(name,DLUMA_ARRAY_COUNT(name),L"-");
        append_number(name,DLUMA_ARRAY_COUNT(name),GetTickCount64());
        dluma_wstr_append(name,DLUMA_ARRAY_COUNT(name),L"-");
        append_number(name,DLUMA_ARRAY_COUNT(name),++state.sequence);
        if(!dluma_checked_path_join(state.stage,DLUMA_ARRAY_COUNT(state.stage),state.root,name) ||
            !dluma_checked_path_join(state.manifest_path,DLUMA_ARRAY_COUNT(state.manifest_path),state.stage,L"nightly.json") ||
            !dluma_checked_path_join(state.compare_path,DLUMA_ARRAY_COUNT(state.compare_path),state.stage,L"compare.json") ||
            !dluma_checked_path_join(state.archive_path,DLUMA_ARRAY_COUNT(state.archive_path),state.stage,L"archive.zip")) return 0;
        if(CreateDirectoryW(state.stage,0)) return 1;
        if(GetLastError()!=183) return 0;
    }
    return 0;
}
static void begin_check(void)
{
    int restore;
    WCHAR local[DLUMA_MAX_WPATH];
    char component_hash[65];
    if(GetFileAttributesW(state.root)==INVALID_FILE_ATTRIBUTES && !state.enabled) {
        state.phase=NIGHTLY_FINISHED; status_set(STATUS_DISABLED); return;
    }
    if(!dlw_nightly_private_directory(state.root)) {failed("private_staging_unavailable");return;}
    restore=restore_available();
    if(state.enabled || restore) {
        if(!dluma_checked_path_join(local,DLUMA_ARRAY_COUNT(local),state.directory,L"dluma_nightly_updater.exe") ||
            !dluma_sha256_file_hex(local,256*1024,component_hash) ||
            !dluma_checked_path_join(local,DLUMA_ARRAY_COUNT(local),state.directory,L"dluma_runtime\\updater\\apply_nightly_update.ps1") ||
            !dluma_sha256_file_hex(local,1024*1024,component_hash)) {
            failed("updater_package_incomplete");return;
        }
    }
    if(restore==2 || (!state.enabled && restore)) {
        state.phase=NIGHTLY_OFFER; status_set(STATUS_RESTORE); publish(DLW_NIGHTLY_RESTORE); return;
    }
    if(!state.enabled) {state.phase=NIGHTLY_FINISHED;status_set(STATUS_DISABLED);return;}
    if(!dluma_nightly_commit_valid(state.current)) {
        state.phase=NIGHTLY_FINISHED;status_set(STATUS_UNKNOWN);publish(DLW_NIGHTLY_ERROR);return;
    }
    if(!prepare_stage()) {failed("stage_directory_unavailable");return;}
    dluma_wstr_copy(local,DLUMA_ARRAY_COUNT(local),L"https://github.com/BalancedLight/DLUMA/releases/download/nightly/nightly.json");
    if(!dlw_nightly_download_start(local,state.manifest_path,16384)) {failed("check_could_not_start");return;}
    state.phase=NIGHTLY_MANIFEST;status_set(STATUS_CHECKING);
}
static int take_response(void)
{
    int response;
    lock_snapshot();
    response=state.response_ticket==state.published.ticket ? state.response : 0;
    state.response=0;state.response_ticket=0;
    unlock_snapshot();return response;
}
static int launch_apply(int restore)
{
    WCHAR source[DLUMA_MAX_WPATH],script[DLUMA_MAX_WPATH],system[DLUMA_MAX_WPATH];
    WCHAR command[DLUMA_MAX_WPATH*10],digest_wide[65];
    char digest[65],copied[65];
    FILETIME created,exited,kernel,user; ULONGLONG ticks;
    NightlyStartup startup; NightlyProcess process; HANDLE job;
    if(!dluma_checked_path_join(source,DLUMA_ARRAY_COUNT(source),state.directory,L"dluma_runtime\\updater\\apply_nightly_update.ps1")) return 0;
    if(!dluma_sha256_file_hex(source,1024*1024,digest)) return 0;
    lower_digest(digest);
    if(!dluma_utf8_to_wide(digest,digest_wide,DLUMA_ARRAY_COUNT(digest_wide))) return 0;
    if(wide_length(state.root)+80>=DLUMA_ARRAY_COUNT(script)) return 0;
    dluma_wstr_copy(script,DLUMA_ARRAY_COUNT(script),state.root);
    dluma_wstr_append(script,DLUMA_ARRAY_COUNT(script),L"\\apply-");
    dluma_wstr_append(script,DLUMA_ARRAY_COUNT(script),digest_wide);
    dluma_wstr_append(script,DLUMA_ARRAY_COUNT(script),L".ps1");
    if(!CopyFileW(source,script,TRUE) && GetLastError()!=80 && GetLastError()!=183) return 0;
    if(!dluma_sha256_file_hex(script,1024*1024,copied)) return 0;
    lower_digest(copied);if(dluma_nightly_text_compare(copied,digest)) return 0;
    if(!dluma_checked_path_join(source,DLUMA_ARRAY_COUNT(source),state.directory,L"dluma_nightly_updater.exe")) return 0;
    if(!dluma_sha256_file_hex(source,256*1024,digest)) return 0;
    lower_digest(digest);
    dluma_utf8_to_wide(digest,digest_wide,DLUMA_ARRAY_COUNT(digest_wide));
    dluma_wstr_copy(system,DLUMA_ARRAY_COUNT(system),state.root);
    dluma_wstr_append(system,DLUMA_ARRAY_COUNT(system),L"\\installer-");
    dluma_wstr_append(system,DLUMA_ARRAY_COUNT(system),digest_wide);
    dluma_wstr_append(system,DLUMA_ARRAY_COUNT(system),L".exe");
    if(!CopyFileW(source,system,TRUE) && GetLastError()!=80 && GetLastError()!=183) return 0;
    if(!dluma_sha256_file_hex(system,256*1024,copied)) return 0;
    lower_digest(copied);if(dluma_nightly_text_compare(copied,digest)) return 0;
    if(!GetProcessTimes((HANDLE)(LONG_PTR)-1,&created,&exited,&kernel,&user)) return 0;
    ticks=(((ULONGLONG)created.dwHighDateTime<<32)|created.dwLowDateTime)+504911232000000000ULL;
    command[0]=0;argument(command,DLUMA_ARRAY_COUNT(command),system);
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -File");
    argument(command,DLUMA_ARRAY_COUNT(command),script);
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -ModulePath");argument(command,DLUMA_ARRAY_COUNT(command),state.module);
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -PrivateRoot");argument(command,DLUMA_ARRAY_COUNT(command),state.root);
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -ResultPath");argument(command,DLUMA_ARRAY_COUNT(command),state.result_path);
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -PlayerPid ");append_number(command,DLUMA_ARRAY_COUNT(command),GetCurrentProcessId());
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -PlayerStartTicks ");append_number(command,DLUMA_ARRAY_COUNT(command),ticks);
    if(!restore) {
        dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -StageDirectory");argument(command,DLUMA_ARRAY_COUNT(command),state.stage);
        dluma_utf8_to_wide(state.digest,digest_wide,DLUMA_ARRAY_COUNT(digest_wide));
        dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),L" -ManifestSha256");argument(command,DLUMA_ARRAY_COUNT(command),digest_wide);
    }
    dluma_wstr_append(command,DLUMA_ARRAY_COUNT(command),restore ? L" -Action Restore" : L" -Action Update");
    memset(&startup,0,sizeof(startup));memset(&process,0,sizeof(process));startup.size=sizeof(startup);
    DeleteFileW(state.result_path);
    job=CreateJobObjectW(0,0);if(!job) return 0;
    if(!CreateProcessW(system,command,0,0,FALSE,0x08000004,0,0,&startup,&process)) {CloseHandle(job);return 0;}
    if(!AssignProcessToJobObject(job,process.process) || ResumeThread(process.thread)==(DWORD)-1) {
        TerminateProcess(process.process,1);CloseHandle(process.thread);CloseHandle(process.process);CloseHandle(job);return 0;
    }
    state.installer_job=job;
    CloseHandle(process.thread);state.installer=process.process;state.phase=NIGHTLY_QUEUED;
    status_set(STATUS_READY);publish(DLW_NIGHTLY_READY);return 1;
}
void dlw_nightly_initialize(int enabled)
{
    WCHAR local[DLUMA_MAX_WPATH],name[96]; char key[65]; unsigned int i;
    DlumaDirectoryIdentity identity; DWORD identity_key[3];
    DlumaSha256Context hash; BYTE directory_digest[32];
    DWORD n;
    if(dluma_atomic_compare_exchange(&initialized,0,0)) return;
    memset(&state,0,sizeof(state));state.enabled=!!enabled;
    dluma_atomic_exchange(&requested_enabled,state.enabled);
    n=GetModuleFileNameW(g_dluma.self_module,state.module,DLUMA_ARRAY_COUNT(state.module));
    if(!n || n>=DLUMA_ARRAY_COUNT(state.module)) {failed("module_path_unavailable");dluma_atomic_exchange(&initialized,1);return;}
    dluma_wstr_copy(state.directory,DLUMA_ARRAY_COUNT(state.directory),state.module);dluma_path_dirname(state.directory);
    n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DLUMA_ARRAY_COUNT(local));
    if(!n || n>=DLUMA_ARRAY_COUNT(local)) {failed("local_appdata_unavailable");dluma_atomic_exchange(&initialized,1);return;}
    if(!dluma_checked_path_join(state.root,DLUMA_ARRAY_COUNT(state.root),local,L"DLUMA-nightly") ||
        !dluma_open_directory_identity(state.directory,&identity)) {
        failed("installation_directory_unavailable");dluma_atomic_exchange(&initialized,1);return;
    }
    identity_key[0]=identity.volume_serial;identity_key[1]=identity.file_index_high;identity_key[2]=identity.file_index_low;
    dluma_sha256_init(&hash);
    dluma_sha256_update(&hash,identity_key,sizeof(identity_key));
    dluma_sha256_update(&hash,identity.final_path,wide_length(identity.final_path)*sizeof(WCHAR));
    dluma_sha256_final(&hash,directory_digest);dluma_sha256_hex(directory_digest,key);lower_digest(key);
    dluma_wstr_copy(name,DLUMA_ARRAY_COUNT(name),L"result-");
    for(i=0;i<64;++i) name[7+i]=(WCHAR)key[i];name[71]=0;
    dluma_wstr_append(name,DLUMA_ARRAY_COUNT(name),L".ini");
    if(!dluma_checked_path_join(state.result_path,DLUMA_ARRAY_COUNT(state.result_path),state.root,name)) {failed("updater_path_too_long");dluma_atomic_exchange(&initialized,1);return;}
    dluma_str_copy(state.current,sizeof(state.current),dluma_build_identity_git_head());
    if(GetFileAttributesW(state.result_path)!=INVALID_FILE_ATTRIBUTES && dlw_nightly_private_directory(state.root)) {
        WCHAR success[8];
        GetPrivateProfileStringW(L"result",L"success",L"",success,DLUMA_ARRAY_COUNT(success),state.result_path);
        dluma_log_pair(!dluma_wstricmp(success,L"1") ? "INFO" : "WARN","nightly_previous_apply",!dluma_wstricmp(success,L"1") ? "completed" : "failed");
        DeleteFileW(state.result_path);
    }
    begin_check();dluma_atomic_exchange(&initialized,1);
}
void dlw_nightly_set_enabled(int enabled) {dluma_atomic_exchange(&requested_enabled,!!enabled);}
const DlwNightlyOffer *dlw_nightly_get_offer(void)
{
    static DlwNightlyOffer copy;
    static const DlwNightlyOffer empty={0};
    if(!dluma_atomic_compare_exchange(&initialized,0,0)) return &empty;
    lock_snapshot();copy=state.published;unlock_snapshot();return &copy;
}
void dlw_nightly_respond_to(unsigned int ticket,int accept)
{
    if(!dluma_atomic_compare_exchange(&initialized,0,0)) return;
    lock_snapshot();
    if(ticket && ticket==state.published.ticket) {state.response_ticket=ticket;state.response=accept ? 2 : 1;}
    unlock_snapshot();
}
void dlw_nightly_respond(int accept)
{
    DlwNightlyOffer offer=*dlw_nightly_get_offer();dlw_nightly_respond_to(offer.ticket,accept);
}
void dlw_nightly_tick(void)
{
    int result,response;DWORD exit_code;unsigned char *bytes;unsigned int length;
    if(!dluma_atomic_compare_exchange(&initialized,0,0)) return;
    if(state.enabled!=dluma_atomic_compare_exchange(&requested_enabled,0,0)) {
        dlw_nightly_download_shutdown();
        if(state.installer) {
            if(state.installer_job) {TerminateJobObject(state.installer_job,1);CloseHandle(state.installer_job);state.installer_job=0;}
            else TerminateProcess(state.installer,1);
            CloseHandle(state.installer);state.installer=0;
        }
        cleanup_stage();state.enabled=dluma_atomic_compare_exchange(&requested_enabled,0,0);
        publish(DLW_NIGHTLY_NONE);begin_check();return;
    }
    response=take_response();
    if(state.phase==NIGHTLY_OFFER && response) {
        DlwNightlyKind kind=state.published.kind;
        publish(DLW_NIGHTLY_NONE);
        if(response==1) {state.phase=NIGHTLY_FINISHED;status_set(state.enabled ? STATUS_DECLINED : STATUS_DISABLED);cleanup_stage();return;}
        if(kind==DLW_NIGHTLY_RESTORE) {if(!launch_apply(1)) failed("restore_could_not_start");return;}
        {
            WCHAR url[DLUMA_MAX_WPATH],filename[160];
            dluma_utf8_to_wide(state.manifest.filename,filename,DLUMA_ARRAY_COUNT(filename));
            dluma_wstr_copy(url,DLUMA_ARRAY_COUNT(url),L"https://github.com/BalancedLight/DLUMA/releases/download/nightly/");
            dluma_wstr_append(url,DLUMA_ARRAY_COUNT(url),filename);
            if(!dlw_nightly_download_start(url,state.archive_path,512ULL*1024*1024)) {failed("download_could_not_start");return;}
            state.phase=NIGHTLY_ARCHIVE;status_set(STATUS_DOWNLOADING);publish(DLW_NIGHTLY_DOWNLOADING);return;
        }
    }
    if(state.phase==NIGHTLY_FINISHED && response) {publish(DLW_NIGHTLY_NONE);return;}
    if((state.phase==NIGHTLY_ARCHIVE || state.phase==NIGHTLY_QUEUED) && response) publish(DLW_NIGHTLY_NONE);
    if(state.installer && GetExitCodeProcess(state.installer,&exit_code) && exit_code!=259) {
        CloseHandle(state.installer);state.installer=0;state.phase=NIGHTLY_FINISHED;
        if(state.installer_job) {CloseHandle(state.installer_job);state.installer_job=0;}
        if(exit_code) failed("installer_could_not_wait_for_exit");
        return;
    }
    if(state.phase!=NIGHTLY_MANIFEST && state.phase!=NIGHTLY_COMPARE && state.phase!=NIGHTLY_ARCHIVE) return;
    result=dlw_nightly_download_poll();if(!result) return;
    if(result<0) {failed("request_failed");return;}
    if(state.phase==NIGHTLY_ARCHIVE) {
        char digest[65];
        if(!dlw_nightly_download_sha256(digest) || dluma_nightly_text_compare(digest,state.manifest.sha256)) {failed("archive_checksum_mismatch");return;}
        if(!launch_apply(0)) failed("installer_could_not_start");return;
    }
    bytes=read_file(state.phase==NIGHTLY_MANIFEST ? state.manifest_path : state.compare_path,
        state.phase==NIGHTLY_MANIFEST ? 16384 : 1024*1024,&length);
    if(!bytes) {failed("response_file_unavailable");return;}
    if(state.phase==NIGHTLY_MANIFEST) {
        WCHAR url[512],base[41],head[41];
        result=dluma_nightly_manifest_parse(bytes,length,&state.manifest);
        dluma_free(bytes);
        if(!result || !dlw_nightly_download_sha256(state.digest)) {failed("manifest_invalid");return;}
        if(!dluma_nightly_text_compare(state.current,state.manifest.commit)) {state.phase=NIGHTLY_FINISHED;status_set(STATUS_CURRENT);cleanup_stage();return;}
        dluma_utf8_to_wide(state.current,base,DLUMA_ARRAY_COUNT(base));
        dluma_utf8_to_wide(state.manifest.commit,head,DLUMA_ARRAY_COUNT(head));
        dluma_wstr_copy(url,DLUMA_ARRAY_COUNT(url),L"https://api.github.com/repos/BalancedLight/DLUMA/compare/");
        dluma_wstr_append(url,DLUMA_ARRAY_COUNT(url),base);dluma_wstr_append(url,DLUMA_ARRAY_COUNT(url),L"...");
        dluma_wstr_append(url,DLUMA_ARRAY_COUNT(url),head);
        dluma_wstr_append(url,DLUMA_ARRAY_COUNT(url),L"?per_page=1&page=2");
        if(!dlw_nightly_download_start(url,state.compare_path,1024*1024)) {failed("comparison_could_not_start");return;}
        state.phase=NIGHTLY_COMPARE;return;
    }
    result=dluma_nightly_compare_status(bytes,length);dluma_free(bytes);
    if(result<0) {failed("build_comparison_unavailable");return;}
    if(!result) {state.phase=NIGHTLY_FINISHED;status_set(STATUS_CURRENT);cleanup_stage();return;}
    state.phase=NIGHTLY_OFFER;status_set(STATUS_AVAILABLE);publish(DLW_NIGHTLY_UPDATE);
    dluma_log_pair("INFO","nightly_update_available",state.manifest.commit);
}
void dlw_nightly_shutdown(void)
{
    if(!dluma_atomic_compare_exchange(&initialized,0,0)) return;
    dlw_nightly_tick();dlw_nightly_download_shutdown();
    if(state.installer) {CloseHandle(state.installer);state.installer=0;}
    else cleanup_stage();
    /* No kill-on-close limit: an accepted installer must outlive Player. */
    if(state.installer_job) {CloseHandle(state.installer_job);state.installer_job=0;}
    dluma_atomic_exchange(&initialized,0);
}
