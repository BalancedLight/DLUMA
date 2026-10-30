#ifndef DLUMA_WIN32_H
#define DLUMA_WIN32_H

/*
 * Tiny Win32 declaration surface used by the prebuilt no-CRT prototype.
 * The normal Visual Studio build may replace this with <windows.h> later.
 */

#if !defined(_WIN64)
#error DLUMA requires a 64-bit Windows target.
#endif

#define WIN32_LEAN_AND_MEAN 1

#if !defined(_MSC_VER) && !defined(__clang__)
#define __declspec(x)
#endif

#ifndef WINAPI
#define WINAPI __stdcall
#endif

#ifndef CALLBACK
#define CALLBACK __stdcall
#endif

#if !defined(_MSC_VER) && !defined(__clang__)
#define __fastcall
#define __cdecl
#define __stdcall
#endif

typedef void VOID;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef unsigned long long ULONG_PTR;
typedef long long LONG_PTR;
typedef ULONG_PTR UINT_PTR;
typedef ULONG_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef unsigned long long SIZE_T;
typedef unsigned short WCHAR;
typedef char CHAR;
typedef void* PVOID;
typedef const void* PCVOID;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* LPVOID;
typedef void* HWND;
typedef void* HMENU;
typedef const void* LPCVOID;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWSTR;
typedef CHAR* LPSTR;
typedef const CHAR* LPCSTR;
typedef void* FARPROC;
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
typedef VOID (CALLBACK *TIMERPROC)(HWND, unsigned int, UINT_PTR, DWORD);
typedef LRESULT (CALLBACK *WNDPROC)(HWND, unsigned int, WPARAM, LPARAM);
typedef BOOL (CALLBACK *WNDENUMPROC)(HWND, LPARAM);

typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    DWORD Reserved;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;

typedef struct _EXCEPTION_POINTERS {
    EXCEPTION_RECORD* ExceptionRecord;
    PVOID ContextRecord;
} EXCEPTION_POINTERS;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } DUMMYSTRUCTNAME;
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
    DWORD dwFileType;
    DWORD dwCreatorType;
    WORD wFinderFlags;
} WIN32_FIND_DATAW;

typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    WORD PartitionId;
    WORD Reserved;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
    DWORD Reserved2;
} MEMORY_BASIC_INFORMATION;

#define TRUE 1
#define FALSE 0
#ifndef NULL
#ifdef __cplusplus
#define NULL nullptr
#else
#define NULL ((void*)0)
#endif
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(LONGLONG)-1)

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1

#define MAX_PATH 260

#define FILE_ATTRIBUTE_READONLY 0x00000001UL
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010UL
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400UL
#define FILE_ATTRIBUTE_NORMAL 0x00000080UL
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFUL
#define ERROR_FILE_NOT_FOUND 2UL
#define ERROR_PATH_NOT_FOUND 3UL
#define ERROR_NO_MORE_FILES 18UL

#define GENERIC_READ 0x80000000UL
#define FILE_READ_ATTRIBUTES 0x00000080UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_APPEND_DATA 0x00000004UL
#define FILE_SHARE_READ 0x00000001UL
#define FILE_SHARE_WRITE 0x00000002UL
#define FILE_SHARE_DELETE 0x00000004UL
#define CREATE_ALWAYS 2UL
#define CREATE_NEW 1UL
#define OPEN_EXISTING 3UL
#define OPEN_ALWAYS 4UL
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000UL
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000UL
#define FILE_BEGIN 0UL
#define FILE_CURRENT 1UL
#define FILE_END 2UL
#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001UL
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002UL
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008UL
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010UL
#define FILE_NOTIFY_CHANGE_CREATION 0x00000040UL
#define MOVEFILE_REPLACE_EXISTING 0x00000001UL
#define MOVEFILE_WRITE_THROUGH 0x00000008UL

#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100UL
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800UL

#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001UL
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002UL
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004UL

#define WAIT_OBJECT_0 0x00000000UL
#define WAIT_TIMEOUT 0x00000102UL
#define INFINITE 0xFFFFFFFFUL

#define WM_DESTROY 0x0002U
#define WM_MOVE 0x0003U
#define WM_SIZE 0x0005U
#define WM_ACTIVATE 0x0006U
#define WM_SETFOCUS 0x0007U
#define WM_KILLFOCUS 0x0008U
#define WM_CLOSE 0x0010U
#define WM_QUERYENDSESSION 0x0011U
#define WM_ENDSESSION 0x0016U
#define WM_ACTIVATEAPP 0x001CU
#define WM_WINDOWPOSCHANGED 0x0047U
#define WM_DISPLAYCHANGE 0x007EU
#define WM_NCDESTROY 0x0082U
#define WM_NCLBUTTONDOWN 0x00A1U
#define WM_COMMAND 0x0111U
#define WM_SYSCOMMAND 0x0112U
#define WM_INITMENUPOPUP 0x0117U
#define WM_ENTERSIZEMOVE 0x0231U
#define WM_EXITSIZEMOVE 0x0232U
#define WM_DPICHANGED 0x02E0U
#define WM_APP 0x8000U

#define HTCAPTION 2

#define MF_STRING 0x0000U
#define MF_BYPOSITION 0x0400U

#define LOWORD(value) ((WORD)((ULONG_PTR)(value) & 0xFFFFU))
#define WA_INACTIVE 0U
#define SIZE_MINIMIZED 1U
#define SC_CLOSE 0xF060UL
#define GW_OWNER 4U
#define GWLP_WNDPROC (-4)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define WS_CAPTION 0x00C00000UL
#define WS_SYSMENU 0x00080000UL
#define WS_THICKFRAME 0x00040000UL
#define WS_EX_TOOLWINDOW 0x00000080UL

#define MB_OK 0x00000000UL
#define MB_YESNO 0x00000004UL
#define MB_ICONERROR 0x00000010UL
#define MB_ICONWARNING 0x00000030UL
#define MB_SYSTEMMODAL 0x00001000UL
#define MB_DEFBUTTON2 0x00000100UL
#define MB_SETFOREGROUND 0x00010000UL
#define MB_TOPMOST 0x00040000UL
#define IDYES 6
#define IDNO 7

#define MEM_COMMIT 0x00001000UL
#define MEM_RESERVE 0x00002000UL
#define MEM_RELEASE 0x00008000UL
#define PAGE_NOACCESS 0x00000001UL
#define PAGE_GUARD 0x00000100UL

#define CP_UTF8 65001UL
#define WC_ERR_INVALID_CHARS 0x00000080UL
#define PAGE_READONLY 0x02UL
#define PAGE_READWRITE 0x04UL
#define PAGE_WRITECOPY 0x08UL
#define PAGE_EXECUTE 0x10UL
#define PAGE_EXECUTE_READ 0x20UL
#define PAGE_EXECUTE_READWRITE 0x40UL
#define PAGE_EXECUTE_WRITECOPY 0x80UL

#define VK_F1 0x70
#define VK_MENU 0x12
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllimport) BOOL WINAPI DisableThreadLibraryCalls(HMODULE hLibModule);
__declspec(dllimport) HANDLE WINAPI CreateThread(void* lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, DWORD* lpThreadId);
__declspec(dllimport) HANDLE WINAPI CreateEventW(void* lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);
__declspec(dllimport) BOOL WINAPI SetEvent(HANDLE hEvent);
__declspec(dllimport) BOOL WINAPI ResetEvent(HANDLE hEvent);
__declspec(dllimport) DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE hObject);
__declspec(dllimport) VOID WINAPI Sleep(DWORD dwMilliseconds);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);
__declspec(dllimport) BOOL WINAPI GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
__declspec(dllimport) HMODULE WINAPI LoadLibraryW(LPCWSTR lpLibFileName);
__declspec(dllimport) HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
__declspec(dllimport) BOOL WINAPI FreeLibrary(HMODULE hLibModule);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
__declspec(dllimport) LPWSTR WINAPI GetCommandLineW(void);
__declspec(dllimport) HANDLE WINAPI LocalFree(HANDLE hMem);
__declspec(dllimport) DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart);
__declspec(dllimport) DWORD WINAPI GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);
__declspec(dllimport) DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
__declspec(dllimport) DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName);
__declspec(dllimport) BOOL WINAPI CreateDirectoryW(LPCWSTR lpPathName, void* lpSecurityAttributes);
__declspec(dllimport) BOOL WINAPI RemoveDirectoryW(LPCWSTR lpPathName);
__declspec(dllimport) HANDLE WINAPI FindFirstFileW(LPCWSTR lpFileName, WIN32_FIND_DATAW* lpFindFileData);
__declspec(dllimport) BOOL WINAPI FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW* lpFindFileData);
__declspec(dllimport) BOOL WINAPI FindClose(HANDLE hFindFile);
__declspec(dllimport) HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter);
__declspec(dllimport) BOOL WINAPI FindNextChangeNotification(HANDLE hChangeHandle);
__declspec(dllimport) BOOL WINAPI FindCloseChangeNotification(HANDLE hChangeHandle);
__declspec(dllimport) HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, void* lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, DWORD* lpNumberOfBytesRead, void* lpOverlapped);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, DWORD* lpNumberOfBytesWritten, void* lpOverlapped);
__declspec(dllimport) BOOL WINAPI GetFileSizeEx(HANDLE hFile, LARGE_INTEGER* lpFileSize);
__declspec(dllimport) BOOL WINAPI GetFileInformationByHandle(HANDLE hFile, BY_HANDLE_FILE_INFORMATION* lpFileInformation);
__declspec(dllimport) DWORD WINAPI GetFinalPathNameByHandleW(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
__declspec(dllimport) BOOL WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, LARGE_INTEGER* lpNewFilePointer, DWORD dwMoveMethod);
__declspec(dllimport) BOOL WINAPI FlushFileBuffers(HANDLE hFile);
__declspec(dllimport) BOOL WINAPI DeleteFileW(LPCWSTR lpFileName);
__declspec(dllimport) BOOL WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);
__declspec(dllimport) BOOL WINAPI MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
__declspec(dllimport) DWORD WINAPI GetLastError(void);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
__declspec(dllimport) HANDLE WINAPI GetCurrentThread(void);
__declspec(dllimport) VOID WINAPI GetSystemTime(SYSTEMTIME* lpSystemTime);
__declspec(dllimport) BOOL WINAPI GetProcessTimes(
    HANDLE hProcess, FILETIME* lpCreationTime, FILETIME* lpExitTime,
    FILETIME* lpKernelTime, FILETIME* lpUserTime);
__declspec(dllimport) BOOL WINAPI GetThreadTimes(
    HANDLE hThread, FILETIME* lpCreationTime, FILETIME* lpExitTime,
    FILETIME* lpKernelTime, FILETIME* lpUserTime);
__declspec(dllimport) BOOL WINAPI IsDebuggerPresent(void);
__declspec(dllimport) SIZE_T WINAPI VirtualQuery(LPCVOID lpAddress, MEMORY_BASIC_INFORMATION* lpBuffer, SIZE_T dwLength);
__declspec(dllimport) BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount);
__declspec(dllimport) VOID WINAPI GetSystemTimeAsFileTime(FILETIME* lpSystemTimeAsFileTime);
__declspec(dllimport) VOID WINAPI GetLocalTime(SYSTEMTIME* lpSystemTime);
__declspec(dllimport) ULONGLONG WINAPI GetTickCount64(void);
__declspec(dllimport) HANDLE WINAPI GetProcessHeap(void);
__declspec(dllimport) LPVOID WINAPI HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
__declspec(dllimport) BOOL WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
__declspec(dllimport) DWORD WINAPI GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpDefault, LPWSTR lpReturnedString, DWORD nSize, LPCWSTR lpFileName);
__declspec(dllimport) BOOL WINAPI WritePrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpString, LPCWSTR lpFileName);
__declspec(dllimport) int WINAPI MultiByteToWideChar(unsigned int CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
__declspec(dllimport) int WINAPI WideCharToMultiByte(unsigned int CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, BOOL* lpUsedDefaultChar);
__declspec(dllimport) VOID WINAPI OutputDebugStringA(LPCSTR lpOutputString);
/*
 * Do not import InterlockedExchange/InterlockedCompareExchange by name.
 * On x64 these are compiler-intrinsic operations and a hand-written import
 * table can cause STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139) before DllMain.
 */
#if defined(__clang__)
static __forceinline LONG dluma_atomic_compare_exchange(
    volatile LONG* destination,
    LONG exchange,
    LONG comparand)
{
    LONG expected = comparand;
    __atomic_compare_exchange_n(
        destination,
        &expected,
        exchange,
        0,
        __ATOMIC_SEQ_CST,
        __ATOMIC_SEQ_CST);
    return expected;
}

static __forceinline LONG dluma_atomic_exchange(volatile LONG* target, LONG value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static __forceinline LONG dluma_atomic_increment(volatile LONG* target)
{
    return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}

static __forceinline LONG dluma_atomic_decrement(volatile LONG* target)
{
    return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}

static __forceinline PVOID dluma_atomic_pointer_compare_exchange(
    PVOID volatile* destination,
    PVOID exchange,
    PVOID comparand)
{
    PVOID expected = comparand;
    __atomic_compare_exchange_n(
        destination,
        &expected,
        exchange,
        0,
        __ATOMIC_SEQ_CST,
        __ATOMIC_SEQ_CST);
    return expected;
}

static __forceinline PVOID dluma_atomic_pointer_exchange(
    PVOID volatile* target,
    PVOID value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
#else
LONG _InterlockedCompareExchange(volatile LONG* Destination, LONG Exchange, LONG Comparand);
LONG _InterlockedExchange(volatile LONG* Target, LONG Value);
LONG _InterlockedIncrement(volatile LONG* Addend);
LONG _InterlockedDecrement(volatile LONG* Addend);
PVOID _InterlockedCompareExchangePointer(PVOID volatile* Destination, PVOID Exchange, PVOID Comparand);
PVOID _InterlockedExchangePointer(PVOID volatile* Target, PVOID Value);
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedCompareExchangePointer)
#pragma intrinsic(_InterlockedExchangePointer)
#define dluma_atomic_compare_exchange _InterlockedCompareExchange
#define dluma_atomic_exchange _InterlockedExchange
#define dluma_atomic_increment _InterlockedIncrement
#define dluma_atomic_decrement _InterlockedDecrement
#define dluma_atomic_pointer_compare_exchange _InterlockedCompareExchangePointer
#define dluma_atomic_pointer_exchange _InterlockedExchangePointer
#endif

#if defined(_MSC_VER)
void* __cdecl memset(void* destination, int value, SIZE_T count);
void* __cdecl memcpy(void* destination, const void* source, SIZE_T count);
int __cdecl memcmp(const void* first, const void* second, SIZE_T count);
SIZE_T __cdecl strlen(const char* string);
#endif

#ifdef __cplusplus
}
#endif

#endif
