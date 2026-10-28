using System.Security.AccessControl;
using System.Security.Principal;

internal static class InstallLock
{
    internal static IDisposable Acquire(string modulePath)
    {
        var target = (Path.GetDirectoryName(Path.GetFullPath(modulePath)) ?? throw new IOException("Invalid module path.")).ToUpperInvariant();
        var name = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(target)));
        var mutex = new Mutex(false, @"Global\DLUMA-Installer-" + name);
        try {
            try { if (!mutex.WaitOne(TimeSpan.FromSeconds(30))) throw new IOException("Another updater is active."); }
            catch (AbandonedMutexException) { }
            return new HeldMutex(mutex);
        } catch { mutex.Dispose(); throw; }
    }

    internal static string Provision(string path)
    {
        if (!OperatingSystem.IsWindows()) throw new IOException("Windows is required.");
        var root = Path.GetFullPath(path);
        var parent = Directory.GetParent(root) ?? throw new IOException("Invalid updater directory.");
        if (!parent.Exists) throw new DirectoryNotFoundException("Updater parent directory is missing.");
        for (DirectoryInfo? part = parent; part != null; part = part.Parent)
            if ((part.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Updater parent contains a reparse point.");
        if (!Directory.Exists(root)) {
            var current = WindowsIdentity.GetCurrent().User ?? throw new IOException("Current user is unavailable.");
            var security = new DirectorySecurity();
            security.SetOwner(current);
            security.SetAccessRuleProtection(true, false);
            security.AddAccessRule(new FileSystemAccessRule(current, FileSystemRights.FullControl,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            new DirectoryInfo(root).Create(security);
        }
        RequirePrivateRoot(root);
        var source = Environment.ProcessPath ?? throw new IOException("Helper path is unavailable.");
        if (!source.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) throw new IOException("A standalone helper is required.");
        var sourceHash = Hash(source);
        var destination = Path.Combine(root, "helper-" + sourceHash + ".exe");
        if (!File.Exists(destination)) {
            var temp = Path.Combine(root, "copy-" + Guid.NewGuid().ToString("N") + ".tmp");
            try {
                using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var output = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                    input.CopyTo(output); output.Flush(true);
                }
                if (Hash(temp) != sourceHash) throw new IOException("Helper copy did not match its source.");
                try { File.Move(temp, destination); }
                catch (IOException) when (File.Exists(destination)) { }
            } finally { File.Delete(temp); }
        }
        if ((File.GetAttributes(destination) & FileAttributes.ReparsePoint) != 0 || Hash(destination) != sourceHash)
            throw new IOException("Private helper does not match its source.");
        return destination;
    }
    private static string Hash(string path)
    {
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(file));
    }

    internal static void RequirePrivateRoot(string path)
    {
        if (!OperatingSystem.IsWindows()) throw new IOException("Windows is required.");
        var directory = new DirectoryInfo(Path.GetFullPath(path));
        if (!directory.Exists) throw new DirectoryNotFoundException("Private updater directory is missing.");
        for (DirectoryInfo? part = directory; part != null; part = part.Parent)
            if ((part.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Updater directory contains a reparse point.");
        var security = directory.GetAccessControl(AccessControlSections.Access | AccessControlSections.Owner);
        var current = WindowsIdentity.GetCurrent().User ?? throw new IOException("Current user is unavailable.");
        var system = new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
        var admins = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);
        var owner = security.GetOwner(typeof(SecurityIdentifier));
        if (owner == null || (!owner.Equals(current) && !owner.Equals(admins))) throw new IOException("Updater directory owner is not trusted.");
        foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier))) {
            if (rule.AccessControlType != AccessControlType.Allow) continue;
            var sid = (SecurityIdentifier)rule.IdentityReference;
            if (!sid.Equals(current) && !sid.Equals(system) && !sid.Equals(admins))
                throw new IOException("Updater directory grants access to another account or group.");
        }
    }
    private sealed class HeldMutex(Mutex mutex) : IDisposable
    {
        public void Dispose() { mutex.ReleaseMutex(); mutex.Dispose(); }
    }
}
