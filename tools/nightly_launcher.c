/* After-exit script launcher. Update checks and downloads run in DLUMA. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    WCHAR executable[1024], command[32768];
    const WCHAR *arguments=GetCommandLineW();
    STARTUPINFOW startup={0};
    PROCESS_INFORMATION process={0};
    DWORD length, result=1;
    size_t prefix, remaining;
    if (argc<3 || wcscmp(argv[1],L"-File")) return ERROR_INVALID_PARAMETER;
    if (*arguments==L'\"') {
        ++arguments;
        while (*arguments && *arguments!=L'\"') ++arguments;
        if (*arguments) ++arguments;
    } else while (*arguments && *arguments!=L' ' && *arguments!=L'\t') ++arguments;
    while (*arguments==L' ' || *arguments==L'\t') ++arguments;
    length=GetSystemDirectoryW(executable,1024);
    if (!length || length>=1024-48) return ERROR_INSUFFICIENT_BUFFER;
    wcscat_s(executable,1024,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    command[0]=L'\"'; command[1]=0;
    wcscat_s(command,32768,executable);
    wcscat_s(command,32768,L"\" -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass ");
    prefix=wcslen(command); remaining=wcslen(arguments);
    if (prefix+remaining>=32768) return ERROR_INSUFFICIENT_BUFFER;
    wcscat_s(command,32768,arguments);
    startup.cb=sizeof(startup);
    if (!CreateProcessW(executable,command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&startup,&process))
        return (int)GetLastError();
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess,INFINITE)==WAIT_OBJECT_0)
        (void)GetExitCodeProcess(process.hProcess,&result);
    CloseHandle(process.hProcess);
    return (int)result;
}
