#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>
#include "dlw_nightly_download.h"

#define NIGHTLY_URL_CAPACITY 4096U
#define NIGHTLY_PATH_CAPACITY 32768U

static struct {
    SRWLOCK lock;
    HANDLE thread;
    HINTERNET volatile request;
    volatile LONG status, cancelled, error;
    WCHAR url[NIGHTLY_URL_CAPACITY], path[NIGHTLY_PATH_CAPACITY];
    char digest[65];
    unsigned long long limit;
} download;

static int copy_wide(WCHAR *out, unsigned capacity, const WCHAR *text)
{
    unsigned n=0;
    if (!out || !capacity || !text) return 0;
    while (text[n] && n+1<capacity) { out[n]=text[n]; ++n; }
    out[n]=0;
    return !text[n];
}

static int allowed_host(const WCHAR *host)
{
    static const WCHAR *const hosts[]={L"github.com",L"api.github.com",
        L"release-assets.githubusercontent.com",L"objects.githubusercontent.com",
        L"github-releases.githubusercontent.com"};
    unsigned i;
    for (i=0;i<sizeof(hosts)/sizeof(hosts[0]);++i)
        if (CompareStringOrdinal(host,-1,hosts[i],-1,TRUE)==CSTR_EQUAL) return 1;
    return 0;
}

static void close_request(void)
{
    HINTERNET request=(HINTERNET)InterlockedExchangePointer((PVOID volatile *)&download.request,0);
    if (request) WinHttpCloseHandle(request);
}

static DWORD WINAPI download_worker(void *unused)
{
    HINTERNET session=0, connection=0, request=0;
    HANDLE file=INVALID_HANDLE_VALUE;
    WCHAR url[NIGHTLY_URL_CAPACITY], host[256], resource[NIGHTLY_URL_CAPACITY];
    BYTE buffer[16384];
    DWORD error=ERROR_INVALID_DATA, status=0, size, received, written;
    unsigned redirects;
    unsigned long long total=0;
    int ok=0;
    BCRYPT_ALG_HANDLE algorithm=0;
    BCRYPT_HASH_HANDLE hash=0;
    BYTE digest[32];
    (void)unused;
    if (!copy_wide(url,NIGHTLY_URL_CAPACITY,download.url)) goto done;
    session=WinHttpOpen(L"DLUMA Nightly/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if (!session) { error=GetLastError(); goto done; }
    if (!WinHttpSetTimeouts(session,5000,10000,10000,10000)) { error=GetLastError(); goto done; }
    for (redirects=0;redirects<=5;++redirects) {
        URL_COMPONENTS parts;
        unsigned n=0,i;
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        ZeroMemory(&parts,sizeof(parts)); parts.dwStructSize=sizeof(parts);
        parts.dwHostNameLength=(DWORD)-1; parts.dwUrlPathLength=(DWORD)-1;
        parts.dwExtraInfoLength=(DWORD)-1; parts.dwUserNameLength=(DWORD)-1;
        parts.dwPasswordLength=(DWORD)-1;
        if (!WinHttpCrackUrl(url,0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS ||
            parts.nPort!=INTERNET_DEFAULT_HTTPS_PORT || parts.dwUserNameLength ||
            parts.dwPasswordLength || !parts.dwHostNameLength || parts.dwHostNameLength>=256)
            goto done;
        for (i=0;i<parts.dwHostNameLength;++i) host[i]=parts.lpszHostName[i];
        host[i]=0;
        if (!allowed_host(host)) goto done;
        if (!parts.dwUrlPathLength) resource[n++]=L'/';
        if (parts.dwUrlPathLength+parts.dwExtraInfoLength+n>=NIGHTLY_URL_CAPACITY) goto done;
        for (i=0;i<parts.dwUrlPathLength;++i) resource[n++]=parts.lpszUrlPath[i];
        for (i=0;i<parts.dwExtraInfoLength;++i) resource[n++]=parts.lpszExtraInfo[i];
        resource[n]=0;
        if (InterlockedCompareExchange(&download.cancelled,0,0)) { error=ERROR_CANCELLED; goto done; }
        connection=WinHttpConnect(session,host,parts.nPort,0);
        if (!connection) { error=GetLastError(); goto done; }
        request=WinHttpOpenRequest(connection,L"GET",resource,0,WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
        if (!request) { error=GetLastError(); goto done; }
        InterlockedExchangePointer((PVOID volatile *)&download.request,request);
        if (!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)) ||
            !WinHttpSendRequest(request,L"Accept: application/json\r\n",(DWORD)-1,
                WINHTTP_NO_REQUEST_DATA,0,0,0) ||
            InterlockedCompareExchange(&download.cancelled,0,0) ||
            !WinHttpReceiveResponse(request,0)) { error=GetLastError(); goto done; }
        size=sizeof(status);
        if (!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)) {
            error=GetLastError(); goto done;
        }
        if (status==200) break;
        if (status!=301 && status!=302 && status!=303 && status!=307 && status!=308) {
            error=ERROR_WINHTTP_INVALID_SERVER_RESPONSE; goto done;
        }
        size=sizeof(url);
        if (redirects==5 || !WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX,url,&size,WINHTTP_NO_HEADER_INDEX) ||
            !size || size>=sizeof(url)) goto done;
        url[NIGHTLY_URL_CAPACITY-1]=0;
        close_request(); request=0;
        WinHttpCloseHandle(connection); connection=0;
    }
    if (status!=200 || InterlockedCompareExchange(&download.cancelled,0,0)) goto done;
    file=CreateFileW(download.path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
    if (file==INVALID_HANDLE_VALUE) { error=GetLastError(); goto done; }
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,0,0)<0 ||
        BCryptCreateHash(algorithm,&hash,0,0,0,0,0)<0) goto done;
    for (;;) {
        if (InterlockedCompareExchange(&download.cancelled,0,0)) { error=ERROR_CANCELLED; goto done; }
        received=0;
        if (!WinHttpReadData(request,buffer,sizeof(buffer),&received)) { error=GetLastError(); goto done; }
        if (!received) break;
        if (total+received>download.limit) { error=ERROR_FILE_TOO_LARGE; goto done; }
        if (!WriteFile(file,buffer,received,&written,0) || written!=received) { error=GetLastError(); goto done; }
        if (BCryptHashData(hash,buffer,received,0)<0) goto done;
        total+=received;
    }
    if (!total || !FlushFileBuffers(file)) { error=ERROR_INVALID_DATA; goto done; }
    if (BCryptFinishHash(hash,digest,sizeof(digest),0)<0) goto done;
    {
        static const char hex[]="0123456789abcdef";
        unsigned i;
        for (i=0;i<32;++i) { download.digest[2*i]=hex[digest[i]>>4]; download.digest[2*i+1]=hex[digest[i]&15]; }
        download.digest[64]=0;
    }
    ok=1; error=ERROR_SUCCESS;
done:
    if (!ok && InterlockedCompareExchange(&download.cancelled,0,0)) error=ERROR_CANCELLED;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    if (file!=INVALID_HANDLE_VALUE) { CloseHandle(file); if (!ok) DeleteFileW(download.path); }
    close_request();
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    InterlockedExchange(&download.error,(LONG)error);
    InterlockedExchange(&download.status,ok ? 2 : -1);
    return 0;
}

int dlw_nightly_download_start(const wchar_t *url, const wchar_t *destination,
                                unsigned long long byte_limit)
{
    DWORD exit_code;
    HMODULE pinned;
    int result=0;
    if (!url || !destination || !byte_limit || byte_limit>512ULL*1024*1024) return 0;
    AcquireSRWLockExclusive(&download.lock);
    if (download.thread) {
        if (!GetExitCodeThread(download.thread,&exit_code)) goto done;
        if (exit_code==STILL_ACTIVE &&
            (InterlockedCompareExchange(&download.status,0,0)==1 ||
             WaitForSingleObject(download.thread,1000)!=WAIT_OBJECT_0)) goto done;
        CloseHandle(download.thread); download.thread=0;
    }
    if (!copy_wide(download.url,NIGHTLY_URL_CAPACITY,url) ||
        !copy_wide(download.path,NIGHTLY_PATH_CAPACITY,destination) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)(const void *)&download_worker,&pinned)) goto done;
    download.limit=byte_limit;
    InterlockedExchange(&download.cancelled,0);
    InterlockedExchange(&download.error,0);
    InterlockedExchange(&download.status,1);
    download.thread=CreateThread(0,0,download_worker,0,0,0);
    if (download.thread) result=1;
    else { InterlockedExchange(&download.error,(LONG)GetLastError()); InterlockedExchange(&download.status,-1); }
done:
    ReleaseSRWLockExclusive(&download.lock);
    return result;
}

int dlw_nightly_download_poll(void)
{
    LONG status=InterlockedCompareExchange(&download.status,0,0);
    return status==1 ? 0 : status==2 ? 1 : -1;
}

unsigned long dlw_nightly_download_error(void)
{
    return (unsigned long)InterlockedCompareExchange(&download.error,0,0);
}

int dlw_nightly_download_sha256(char out[65])
{
    int ready;
    if (!out) return 0;
    AcquireSRWLockShared(&download.lock);
    ready=InterlockedCompareExchange(&download.status,0,0)==2;
    if (ready) CopyMemory(out,download.digest,65);
    ReleaseSRWLockShared(&download.lock);
    return ready;
}

int dlw_nightly_private_directory(const wchar_t *path)
{
    HANDLE token=0;
    DWORD size=0, result, attributes, i;
    TOKEN_USER *user=0;
    LPWSTR sid_text=0;
    WCHAR sddl[1024], ancestors[NIGHTLY_PATH_CAPACITY];
    PSECURITY_DESCRIPTOR descriptor=0, actual=0;
    PSID owner=0;
    PACL acl=0;
    BYTE admins_buffer[SECURITY_MAX_SID_SIZE], system_buffer[SECURITY_MAX_SID_SIZE];
    DWORD admin_size=sizeof(admins_buffer), system_size=sizeof(system_buffer);
    SECURITY_ATTRIBUTES security={sizeof(security),0,FALSE};
    int ok=0;
    unsigned n=0;
    if (!path || !copy_wide(ancestors,NIGHTLY_PATH_CAPACITY,path)) return 0;
    for (i=3;ancestors[i];++i) if (ancestors[i]==L'\\' || ancestors[i]==L'/') {
        WCHAR separator=ancestors[i]; ancestors[i]=0;
        attributes=GetFileAttributesW(ancestors); ancestors[i]=separator;
        if (attributes==INVALID_FILE_ATTRIBUTES || attributes&FILE_ATTRIBUTE_REPARSE_POINT) return 0;
    }
    if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) goto done;
    GetTokenInformation(token,TokenUser,0,0,&size);
    if (!size || size>65536) goto done;
    user=(TOKEN_USER *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size);
    if (!user || !GetTokenInformation(token,TokenUser,user,size,&size) ||
        !ConvertSidToStringSidW(user->User.Sid,&sid_text) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid,0,admins_buffer,&admin_size) ||
        !CreateWellKnownSid(WinLocalSystemSid,0,system_buffer,&system_size)) goto done;
    {
        static const WCHAR start[]=L"D:P(A;OICI;FA;;;";
        static const WCHAR end[]=L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
        unsigned j;
        for (j=0;start[j] && n+1<1024;++j) sddl[n++]=start[j];
        for (j=0;sid_text[j] && n+1<1024;++j) sddl[n++]=sid_text[j];
        for (j=0;end[j] && n+1<1024;++j) sddl[n++]=end[j];
        sddl[n]=0;
    }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&descriptor,0)) goto done;
    security.lpSecurityDescriptor=descriptor;
    if (!CreateDirectoryW(path,&security) && GetLastError()!=ERROR_ALREADY_EXISTS) goto done;
    attributes=GetFileAttributesW(path);
    if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || attributes&FILE_ATTRIBUTE_REPARSE_POINT) goto done;
    result=GetNamedSecurityInfoW((LPWSTR)path,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &owner,0,&acl,0,&actual);
    if (result!=ERROR_SUCCESS || !owner || !acl ||
        (!EqualSid(owner,user->User.Sid) && !EqualSid(owner,admins_buffer))) goto done;
    for (i=0;i<acl->AceCount;++i) {
        ACE_HEADER *ace=0;
        if (!GetAce(acl,i,(void **)&ace) || !ace) goto done;
        if (ace->AceType==ACCESS_ALLOWED_ACE_TYPE) {
            PSID allowed=(PSID)&((ACCESS_ALLOWED_ACE *)ace)->SidStart;
            if (!EqualSid(allowed,user->User.Sid) && !EqualSid(allowed,admins_buffer) && !EqualSid(allowed,system_buffer)) goto done;
        } else if (ace->AceType!=ACCESS_DENIED_ACE_TYPE) goto done;
    }
    ok=1;
done:
    if (actual) LocalFree(actual);
    if (descriptor) LocalFree(descriptor);
    if (sid_text) LocalFree(sid_text);
    if (user) HeapFree(GetProcessHeap(),0,user);
    if (token) CloseHandle(token);
    return ok;
}

void dlw_nightly_download_shutdown(void)
{
    AcquireSRWLockExclusive(&download.lock);
    InterlockedExchange(&download.cancelled,1);
    close_request();
    if (download.thread && WaitForSingleObject(download.thread,5000)==WAIT_OBJECT_0) {
        CloseHandle(download.thread); download.thread=0;
    }
    ReleaseSRWLockExclusive(&download.lock);
}
