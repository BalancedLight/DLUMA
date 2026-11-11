using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Dluma.EditorGit.Core;

// This check is used only by the isolated local diagnostic, never by normal Git.
internal static class DiagnosticGitMetadata
{
    private const uint ReadAttributes = 0x80, ShareReadWrite = 3, OpenExisting = 3;
    private const uint BackupSemantics = 0x02000000, OpenReparsePoint = 0x00200000;
    private const uint DirectoryAttribute = 0x10, ReparseAttribute = 0x400;

    [StructLayout(LayoutKind.Sequential)]
    private struct FileInformation
    {
        public uint Attributes, CreationLow, CreationHigh, AccessLow, AccessHigh, WriteLow, WriteHigh;
        public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInformation information);

    private static bool OpenDirectory(string path, out SafeFileHandle handle, out FileInformation info)
    {
        handle = CreateFileW(path, ReadAttributes, ShareReadWrite, IntPtr.Zero, OpenExisting,
            BackupSemantics | OpenReparsePoint, IntPtr.Zero);
        info = default;
        return !handle.IsInvalid && GetFileInformationByHandle(handle, out info) &&
            (info.Attributes & DirectoryAttribute) != 0 && (info.Attributes & ReparseAttribute) == 0;
    }

    internal static bool Matches(string projectRoot, string gitDirectory, string commonDirectory)
    {
        if (!OperatingSystem.IsWindows() || !Path.IsPathFullyQualified(gitDirectory) ||
            !Path.IsPathFullyQualified(commonDirectory)) return false;
        using var expected = CreateFileW(Path.Combine(projectRoot, ".git"), ReadAttributes, ShareReadWrite,
            IntPtr.Zero, OpenExisting, BackupSemantics | OpenReparsePoint, IntPtr.Zero);
        if (expected.IsInvalid || !GetFileInformationByHandle(expected, out var info) ||
            (info.Attributes & DirectoryAttribute) == 0 || (info.Attributes & ReparseAttribute) != 0) return false;
        var openedGit = OpenDirectory(gitDirectory, out var git, out var gitInfo);
        using (git)
        {
            if (!openedGit || !Same(info, gitInfo)) return false;
            var openedCommon = OpenDirectory(commonDirectory, out var common, out var commonInfo);
            using (common) return openedCommon && Same(info, commonInfo);
        }
    }

    private static bool Same(FileInformation a, FileInformation b) =>
        a.Volume == b.Volume && a.IndexHigh == b.IndexHigh && a.IndexLow == b.IndexLow;
}
