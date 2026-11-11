#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <cstring>
#include <cwchar>
#include "dlw_xui_document_native.h"
#include "dlw_xui_document_sources.h"
namespace {
// All public strings and each joined path are bounded before copying.
static void CopyText(char* out,const char* value)
{std::memcpy(out,value,std::strlen(value)+1);}
static void CopyWide(WCHAR* out,const WCHAR* value)
{std::memcpy(out,value,(std::wcslen(value)+1)*sizeof(WCHAR));}
struct Work {
    DlumaXuiDocumentIdentity identity;
    WCHAR project[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
    char original[DLUMA_XUI_DOCUMENT_PATH_CAPACITY],replacement[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
    WCHAR root[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
};
SRWLOCK g_lock=SRWLOCK_INIT;
DlumaXuiDocumentBackend g_backend={};
HANDLE g_worker=nullptr;
uint64_t g_next_token=0;
bool g_restore=false,g_cancel=false,g_stopping=false;
uint32_t g_destroyed_map=0;
bool Match(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token)
{const auto& i=g_backend.identity;return owner&&owner==i.owner&&generation==i.owner_generation&&map==i.map_generation&&token&&token==i.token;}
bool Relative(const char* p)
{
 if(!p||!*p)return false;size_t n=0,start=0;
 for(;n<DLUMA_XUI_DOCUMENT_PATH_CAPACITY&&p[n];++n){unsigned char c=static_cast<unsigned char>(p[n]);
  if(c<32||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return false;
  if(c=='/'||c=='\\'){if(n==start||(n-start==1&&p[start]=='.')||(n-start==2&&p[start]=='.'&&p[start+1]=='.'))return false;start=n+1;}}
 if(n<5||n>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY||n==start||
    (n-start==1&&p[start]=='.')||(n-start==2&&p[start]=='.'&&p[start+1]=='.'))return false;
 return _stricmp(p+n-4,".xui")==0;
}
bool Under(const WCHAR* root,const WCHAR* file)
{
 size_t n=std::wcslen(root);return n&&std::wcslen(file)>n&&
  CompareStringOrdinal(root,static_cast<int>(n),file,static_cast<int>(n),TRUE)==CSTR_EQUAL&&
  (file[n]==L'\\'||file[n]==L'/');
}
int AcquireProject(void* user,const char* relative,DlumaXuiDocumentSource* source)
{
 auto* work=static_cast<Work*>(user);WCHAR suffix[DLUMA_XUI_DOCUMENT_PATH_CAPACITY],full[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];char native[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
 if(!work||!Relative(relative)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,-1,suffix,DLUMA_XUI_DOCUMENT_PATH_CAPACITY))return 0;
 size_t root_size=std::wcslen(work->root),part=std::wcslen(suffix);if(root_size+1+part>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY)return 0;
 CopyWide(full,work->root);full[root_size]=L'\\';CopyWide(full+root_size+1,suffix);
 if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,full,-1,native,sizeof(native),nullptr,nullptr)||
    !dluma_xui_document_source_acquire(nullptr,native,source))return 0;
 WCHAR final_path[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
 if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source->path,-1,final_path,DLUMA_XUI_DOCUMENT_PATH_CAPACITY)||!Under(work->root,final_path)){
  (void)dluma_xui_document_source_release(nullptr,source);return 0;}
 return 1;
}
bool ProjectRoot(Work* work,HANDLE* pin)
{
 size_t n=std::wcslen(work->project);if(!n||n+6>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY)return false;
 WCHAR data[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];CopyWide(data,work->project);CopyWide(data+n,L"\\data");
 *pin=CreateFileW(data,FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
 if(*pin==INVALID_HANDLE_VALUE)return false;WCHAR final_path[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
 DWORD size=GetFinalPathNameByHandleW(*pin,final_path,DLUMA_XUI_DOCUMENT_PATH_CAPACITY,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
 if(!size||size>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY)return false;
 const WCHAR* p=final_path;if(!std::wcsncmp(p,L"\\\\?\\",4))p+=4;CopyWide(work->root,p);return true;
}
DWORD WINAPI Prepare(void* value)
{
 auto* work=static_cast<Work*>(value);DlumaXuiDocumentBackend prepared={};HANDLE root=INVALID_HANDLE_VALUE;
 DlumaXuiDocumentOps ops={work,AcquireProject,dluma_xui_document_source_compatible,dluma_xui_document_source_release,
  dluma_xui_document_native_current,dluma_xui_document_native_snapshot,dluma_xui_document_native_rebuild};
 dluma_xui_document_initialize(&prepared,&ops);
 if(ProjectRoot(work,&root))(void)dluma_xui_document_prepare(&prepared,work->identity,work->original,work->replacement);
 else {prepared.identity=work->identity;prepared.state=DLUMA_XUI_DOCUMENT_FAILED;prepared.error=DLUMA_XUI_DOCUMENT_SOURCE_FAILED;}
 if(root!=INVALID_HANDLE_VALUE)CloseHandle(root);
 // No later callback may use worker storage. Acquire is used only in prepare.
 prepared.ops.user=nullptr;prepared.ops.acquire=nullptr;
 AcquireSRWLockExclusive(&g_lock);
 if(g_destroyed_map==prepared.identity.map_generation)(void)dluma_xui_document_map_destroyed(&prepared,g_destroyed_map);
 g_backend=prepared;ReleaseSRWLockExclusive(&g_lock);
 HeapFree(GetProcessHeap(),0,work);return 0;
}
void CloseWorker()
{if(g_worker&&WaitForSingleObject(g_worker,0)==WAIT_OBJECT_0){CloseHandle(g_worker);g_worker=nullptr;}}
}
extern "C" int dluma_xui_document_service_begin(uint64_t owner,uint32_t generation,uint32_t map,const wchar_t* project,const char* original,const char* replacement,uint64_t* token)
{
 if(token)*token=0;if(!token||!owner||!generation||!map||!project||!Relative(original)||!Relative(replacement)||!std::strcmp(original,replacement))return DLUMA_XUI_DOCUMENT_INVALID;
 if(!dluma_xui_document_native_ready())return DLUMA_XUI_DOCUMENT_UNAVAILABLE;
 if(!TryAcquireSRWLockExclusive(&g_lock))return DLUMA_XUI_DOCUMENT_BUSY;
 CloseWorker();
 if(g_stopping||g_worker||g_backend.entered||g_backend.original.handle||g_backend.replacement.handle){ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_BUSY;}
 if(g_next_token==static_cast<uint64_t>(INT64_MAX)){ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_UNAVAILABLE;}
 size_t length=0;for(;length<DLUMA_XUI_DOCUMENT_PATH_CAPACITY&&project[length];++length){}
 if(!length||length>=DLUMA_XUI_DOCUMENT_PATH_CAPACITY){ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_INVALID;}
 auto* work=static_cast<Work*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Work)));
 if(!work){ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_SOURCE_FAILED;}
 work->identity={owner,++g_next_token,generation,map};CopyWide(work->project,project);CopyText(work->original,original);CopyText(work->replacement,replacement);
 if(!dluma_xui_document_native_bind(&work->identity)){HeapFree(GetProcessHeap(),0,work);ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_UNAVAILABLE;}
 g_backend={};g_backend.identity=work->identity;g_backend.state=DLUMA_XUI_DOCUMENT_PREPARING;g_restore=g_cancel=false;g_destroyed_map=0;
 g_worker=CreateThread(nullptr,0,Prepare,work,0,nullptr);
 if(!g_worker){g_backend.state=DLUMA_XUI_DOCUMENT_FAILED;g_backend.error=DLUMA_XUI_DOCUMENT_SOURCE_FAILED;HeapFree(GetProcessHeap(),0,work);ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_SOURCE_FAILED;}
 *token=g_backend.identity.token;ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_OK;
}
extern "C" int dluma_xui_document_service_available(void)
{AcquireSRWLockShared(&g_lock);bool token=g_backend.identity.token!=0;ReleaseSRWLockShared(&g_lock);return token||dluma_xui_document_native_configured();}
extern "C" int dluma_xui_document_service_status(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token,DlumaXuiDocumentStatus* status)
{
 if(!status)return DLUMA_XUI_DOCUMENT_INVALID;*status={};AcquireSRWLockShared(&g_lock);
 if(!Match(owner,generation,map,token)){ReleaseSRWLockShared(&g_lock);return DLUMA_XUI_DOCUMENT_INVALID;}
 *status={token,static_cast<uint32_t>(g_backend.state),static_cast<uint32_t>(g_backend.error)};ReleaseSRWLockShared(&g_lock);return DLUMA_XUI_DOCUMENT_OK;
}
extern "C" int dluma_xui_document_service_restore(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token)
{
 AcquireSRWLockExclusive(&g_lock);if(!Match(owner,generation,map,token)){ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_INVALID;}
 g_restore=true;ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_OK;
}
extern "C" int dluma_xui_document_service_cancel(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token)
{
 AcquireSRWLockExclusive(&g_lock);if(!Match(owner,generation,map,token)){ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_INVALID;}
 g_cancel=true;ReleaseSRWLockExclusive(&g_lock);return DLUMA_XUI_DOCUMENT_OK;
}
extern "C" void dluma_xui_document_service_revoke(uint64_t owner,uint32_t generation)
{AcquireSRWLockExclusive(&g_lock);if(owner&&g_backend.identity.owner==owner&&(!generation||g_backend.identity.owner_generation==generation))g_restore=true;ReleaseSRWLockExclusive(&g_lock);}
extern "C" void dluma_xui_document_service_pump(void)
{
 // Native calls may invoke managed/Lua callbacks. Never retain the service
 // lock across them; such callbacks only append cancellation/restoration intent.
 if(!TryAcquireSRWLockExclusive(&g_lock))return;CloseWorker();
 if(g_worker||g_backend.entered||g_backend.state==DLUMA_XUI_DOCUMENT_PREPARING){ReleaseSRWLockExclusive(&g_lock);return;}
 DlumaXuiDocumentBackend operation=g_backend;bool restore=g_restore||(g_stopping&&operation.state!=DLUMA_XUI_DOCUMENT_RECOVERY_REQUIRED),cancel=g_cancel;
 bool execute=restore||cancel||operation.state==DLUMA_XUI_DOCUMENT_READY||operation.state==DLUMA_XUI_DOCUMENT_RELEASE_PENDING;
 bool needs_native=!operation.original_valid;
 if(!execute||(needs_native&&!dluma_xui_document_native_current(nullptr,&operation.identity))){ReleaseSRWLockExclusive(&g_lock);return;}
 if(restore)g_restore=false;if(cancel)g_cancel=false;
 g_backend.entered=1;g_backend.state=restore?DLUMA_XUI_DOCUMENT_RESTORING:cancel?g_backend.state:DLUMA_XUI_DOCUMENT_APPLYING;ReleaseSRWLockExclusive(&g_lock);
 if(restore)(void)dluma_xui_document_restore(&operation,operation.identity);
 else if(cancel)(void)dluma_xui_document_cancel(&operation,operation.identity);
 else if(operation.state==DLUMA_XUI_DOCUMENT_READY)(void)dluma_xui_document_apply(&operation);
 else (void)dluma_xui_document_release(&operation);
 AcquireSRWLockExclusive(&g_lock);
 if(g_destroyed_map==operation.identity.map_generation)(void)dluma_xui_document_map_destroyed(&operation,g_destroyed_map);
 g_backend=operation;
 // A reentrant revoke/cancel is serviced on the next serialized frame.
 ReleaseSRWLockExclusive(&g_lock);
}
extern "C" void dluma_xui_document_service_map_destroyed(uint32_t map)
{
 AcquireSRWLockExclusive(&g_lock);if(map&&g_backend.identity.map_generation==map){g_destroyed_map=map;if(!g_backend.entered&&!g_worker)(void)dluma_xui_document_map_destroyed(&g_backend,map);}
 ReleaseSRWLockExclusive(&g_lock);
}
extern "C" int dluma_xui_document_service_shutdown(void)
{
 AcquireSRWLockExclusive(&g_lock);g_stopping=true;g_restore=true;CloseWorker();
 int ready=!g_worker&&!g_backend.entered&&!g_backend.original.handle&&!g_backend.replacement.handle;ReleaseSRWLockExclusive(&g_lock);return ready;
}
