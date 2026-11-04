using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Dluma.QuestGraph.Services;

public sealed partial class QuestBridgeClient
{
    internal void RequireGitServerProcess(int editorProcessId)
    {
        if (editorProcessId <= 0 || _pipe is null || !_pipe.IsConnected ||
            !GetGitPipeServerProcessId(_pipe.SafePipeHandle, out var serverProcessId) ||
            serverProcessId != (uint)editorProcessId)
            throw new InvalidDataException("The Editor connection could not be verified.");
    }

    [DllImport("kernel32.dll", EntryPoint = "GetNamedPipeServerProcessId", SetLastError = true)]
    private static extern bool GetGitPipeServerProcessId(SafePipeHandle pipe, out uint processId);
}
