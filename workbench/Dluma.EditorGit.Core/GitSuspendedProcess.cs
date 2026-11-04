using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Dluma.EditorGit.Core;

// The child inherits only these three pipe ends and cannot execute until admitted.
internal sealed class GitSuspendedProcess : IDisposable
{
    private const uint HandleInherit = 1, StartUseStdHandles = 0x100;
    private const uint CreateSuspended = 4, CreateUnicodeEnvironment = 0x400;
    private const uint ExtendedStartupInfo = 0x80000, CreateNoWindow = 0x08000000;
    private const uint AttributeHandleList = 0x00020002;
    private readonly SafeProcessHandle _originalProcess;
    private readonly SafeWaitHandle _thread;
    internal Process Process { get; }
    internal FileStream Input { get; }
    internal FileStream Output { get; }
    internal FileStream Error { get; }
    internal ulong CreationTime { get; }
    internal long ProcessHandle => _originalProcess.DangerousGetHandle().ToInt64();

    private GitSuspendedProcess(SafeProcessHandle originalProcess, SafeWaitHandle thread, Process process,
        FileStream input, FileStream output, FileStream error, ulong creationTime)
    {
        _originalProcess = originalProcess; _thread = thread; Process = process;
        Input = input; Output = output; Error = error; CreationTime = creationTime;
    }

    internal static GitSuspendedProcess Start(ProcessStartInfo start, Action quarantine)
    {
        SafeFileHandle? stdinRead = null, stdinWrite = null, stdoutRead = null, stdoutWrite = null, stderrRead = null, stderrWrite = null;
        SafeProcessHandle? original = null;
        SafeWaitHandle? thread = null;
        Process? process = null;
        FileStream? input = null, output = null, error = null;
        IntPtr attributes = IntPtr.Zero, handles = IntPtr.Zero, environment = IntPtr.Zero;
        var attributesInitialized = false;
        var success = false;
        try
        {
            var security = new SecurityAttributes { Length = Marshal.SizeOf<SecurityAttributes>(), InheritHandle = true };
            if (!CreatePipe(out stdinRead, out stdinWrite, ref security, 0) ||
                !CreatePipe(out stdoutRead, out stdoutWrite, ref security, 0) ||
                !CreatePipe(out stderrRead, out stderrWrite, ref security, 0) ||
                !SetHandleInformation(stdinWrite, HandleInherit, 0) ||
                !SetHandleInformation(stdoutRead, HandleInherit, 0) ||
                !SetHandleInformation(stderrRead, HandleInherit, 0)) throw new Win32Exception();

            nuint size = 0;
            _ = InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
            if (size == 0 || size > int.MaxValue) throw new Win32Exception();
            attributes = Marshal.AllocHGlobal((int)size);
            if (!InitializeProcThreadAttributeList(attributes, 1, 0, ref size)) throw new Win32Exception();
            attributesInitialized = true;
            handles = Marshal.AllocHGlobal(IntPtr.Size * 3);
            Marshal.WriteIntPtr(handles, 0, stdinRead.DangerousGetHandle());
            Marshal.WriteIntPtr(handles, IntPtr.Size, stdoutWrite.DangerousGetHandle());
            Marshal.WriteIntPtr(handles, IntPtr.Size * 2, stderrWrite.DangerousGetHandle());
            if (!UpdateProcThreadAttribute(attributes, 0, AttributeHandleList, handles,
                    (nuint)(IntPtr.Size * 3), IntPtr.Zero, IntPtr.Zero)) throw new Win32Exception();
            var startup = new StartupInfoEx
            {
                StartupInfo = new StartupInfo
                {
                    Size = Marshal.SizeOf<StartupInfoEx>(), Flags = StartUseStdHandles,
                    StdInput = stdinRead.DangerousGetHandle(), StdOutput = stdoutWrite.DangerousGetHandle(),
                    StdError = stderrWrite.DangerousGetHandle()
                },
                AttributeList = attributes
            };
            var command = new StringBuilder(Quote(start.FileName));
            foreach (var argument in start.ArgumentList) command.Append(' ').Append(Quote(argument));
            if (command.Length >= 32767) throw new GitException("The Git command is too long.", GitFailureKind.InvalidInput);
            var block = string.Concat(start.Environment.OrderBy(pair => pair.Key, StringComparer.OrdinalIgnoreCase)
                .Select(pair => pair.Key + "=" + pair.Value + "\0")) + "\0";
            environment = Marshal.StringToHGlobalUni(block);
            if (!CreateProcess(start.FileName, command, IntPtr.Zero, IntPtr.Zero, true,
                    CreateSuspended | CreateUnicodeEnvironment | ExtendedStartupInfo | CreateNoWindow,
                    environment, start.WorkingDirectory, ref startup, out var created)) throw new Win32Exception();
            original = new SafeProcessHandle(created.Process, true);
            thread = new SafeWaitHandle(created.Thread, true);
            stdinRead.Dispose(); stdinRead = null;
            stdoutWrite.Dispose(); stdoutWrite = null;
            stderrWrite.Dispose(); stderrWrite = null;
            if (!GetProcessTimes(original, out var createdTime, out _, out _, out _) || createdTime == 0)
                throw new Win32Exception();
            process = Process.GetProcessById(checked((int)created.ProcessId));
            if (!GetProcessTimes(process.SafeHandle, out var observedTime, out _, out _, out _) || observedTime != createdTime)
                throw new InvalidOperationException("Git child identity changed.");
            input = new FileStream(stdinWrite, FileAccess.Write, 4096, isAsync: false); stdinWrite = null;
            output = new FileStream(stdoutRead, FileAccess.Read, 4096, isAsync: false); stdoutRead = null;
            error = new FileStream(stderrRead, FileAccess.Read, 4096, isAsync: false); stderrRead = null;
            var child = new GitSuspendedProcess(original, thread, process, input, output, error, createdTime);
            success = true;
            return child;
        }
        finally
        {
            if (!success)
            {
                if (original is not null && !original.IsInvalid &&
                    (!TerminateProcess(original, 1) || WaitForSingleObject(original, 5000) != 0)) quarantine();
                input?.Dispose(); output?.Dispose(); error?.Dispose(); process?.Dispose(); thread?.Dispose(); original?.Dispose();
            }
            stdinRead?.Dispose(); stdinWrite?.Dispose(); stdoutRead?.Dispose(); stdoutWrite?.Dispose(); stderrRead?.Dispose(); stderrWrite?.Dispose();
            if (attributesInitialized) DeleteProcThreadAttributeList(attributes);
            if (attributes != IntPtr.Zero) Marshal.FreeHGlobal(attributes);
            if (handles != IntPtr.Zero) Marshal.FreeHGlobal(handles);
            if (environment != IntPtr.Zero) Marshal.FreeHGlobal(environment);
        }
    }

    internal void Resume()
    {
        if (ResumeThread(_thread) != 1U) throw new GitException("Git could not be started safely.", GitFailureKind.RecoveryRequired);
    }

    // CommandLineToArgvW / CRT-compatible quoting, including trailing backslashes.
    private static string Quote(string value)
    {
        var result = new StringBuilder("\"");
        var slashes = 0;
        foreach (var c in value)
        {
            if (c == '\\') { slashes++; continue; }
            if (c == '"') { result.Append('\\', slashes * 2 + 1).Append('"'); slashes = 0; continue; }
            result.Append('\\', slashes).Append(c); slashes = 0;
        }
        return result.Append('\\', slashes * 2).Append('"').ToString();
    }

    public void Dispose()
    {
        Input.Dispose(); Output.Dispose(); Error.Dispose(); Process.Dispose(); _thread.Dispose(); _originalProcess.Dispose();
    }

    [StructLayout(LayoutKind.Sequential)] private struct SecurityAttributes { internal int Length; internal IntPtr SecurityDescriptor; [MarshalAs(UnmanagedType.Bool)] internal bool InheritHandle; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct StartupInfo
    {
        internal int Size; internal IntPtr Reserved, Desktop, Title; internal int X, Y, XSize, YSize, XCountChars, YCountChars, FillAttribute;
        internal uint Flags; internal short ShowWindow, Reserved2; internal IntPtr Reserved2Pointer, StdInput, StdOutput, StdError;
    }
    [StructLayout(LayoutKind.Sequential)] private struct StartupInfoEx { internal StartupInfo StartupInfo; internal IntPtr AttributeList; }
    [StructLayout(LayoutKind.Sequential)] private struct ProcessInformation { internal IntPtr Process, Thread; internal uint ProcessId, ThreadId; }

    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool CreatePipe(out SafeFileHandle read, out SafeFileHandle write, ref SecurityAttributes attributes, uint size);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool SetHandleInformation(SafeFileHandle handle, uint mask, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref nuint size);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, nuint attribute, IntPtr value, nuint size, IntPtr previous, IntPtr returned);
    [DllImport("kernel32.dll")] private static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll", EntryPoint = "CreateProcessW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string application, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandles, uint flags, IntPtr environment, string directory, ref StartupInfoEx startup, out ProcessInformation process);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetProcessTimes(SafeProcessHandle process, out ulong created, out ulong exited, out ulong kernel, out ulong user);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint ResumeThread(SafeWaitHandle thread);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool TerminateProcess(SafeProcessHandle process, uint exitCode);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint WaitForSingleObject(SafeProcessHandle handle, uint milliseconds);
}
