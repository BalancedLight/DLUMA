using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Dluma.QuestGraph.Services;

public sealed partial class QuestBridgeClient
{
    internal void RequireServerProcess(int expectedProcessId)
    {
        if (_pipe is null || !_pipe.IsConnected ||
            !GetNamedPipeServerProcessId(_pipe.SafePipeHandle, out var processId) ||
            processId != (uint)expectedProcessId)
            throw new InvalidDataException("The Editor connection could not be authenticated.");
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint processId);
}
