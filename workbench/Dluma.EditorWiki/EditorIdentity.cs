using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Dluma.EditorWiki;

internal sealed class EditorIdentity : IDisposable
{
    private readonly Process _process;
    private readonly LaunchOptions _options;

    internal EditorIdentity(LaunchOptions options)
    {
        _options = options;
        _process = Process.GetProcessById(options.EditorPid);
        try
        {
            RequireCurrent();
            var path = new StringBuilder(32768);
            var length = path.Capacity;
            if (!QueryFullProcessImageName(_process.SafeHandle, 0, path, ref length) ||
                !string.Equals(Path.GetFileName(path.ToString()), "DyingLightEditor.exe", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The Editor process could not be verified.");
            using var file = new FileStream(path.ToString(), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var hash = Convert.ToHexString(SHA256.HashData(file));
            if (!string.Equals(hash, options.EditorSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The Editor build does not match this panel.");
            RequireCurrent();
        }
        catch { _process.Dispose(); throw; }
    }

    internal void MonitorExit(EventHandler handler)
    {
        _process.Exited += handler;
        _process.EnableRaisingEvents = true;
        RequireCurrent();
    }

    internal void RequireCurrent()
    {
        if (_process.HasExited || !GetProcessTimes(_process.SafeHandle, out var created, out _, out _, out _) ||
            created != _options.EditorStartTime)
            throw new InvalidDataException("The Editor session is no longer available.");
    }

    internal void RequireParent()
    {
        RequireCurrent();
        var parent = new IntPtr(_options.PanelParent);
        if (!IsWindow(parent) || GetWindowThreadProcessId(parent, out var owner) == 0 || owner != (uint)_options.EditorPid ||
            (GetWindowLongPtr(parent, -16).ToInt64() & 0x40000000L) == 0 || GetParent(parent) == IntPtr.Zero)
            throw new InvalidDataException("The Editor panel is unavailable.");
        var root = GetAncestor(parent, 2);
        if (root == IntPtr.Zero || GetWindowThreadProcessId(root, out owner) == 0 || owner != (uint)_options.EditorPid)
            throw new InvalidDataException("The Editor panel owner is unavailable.");
        RequireCurrent();
    }

    public void Dispose() => _process.Dispose();

    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetProcessTimes(
        SafeProcessHandle process, out long created, out long exited, out long kernel, out long user);
    [DllImport("kernel32.dll", EntryPoint = "QueryFullProcessImageNameW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool QueryFullProcessImageName(SafeProcessHandle process, uint flags, StringBuilder path, ref int size);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll")] private static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr window, uint flags);
}
