using System.Security.Cryptography;
using System.Runtime.InteropServices;
using System.Text.Json;

// File plans must be built from validated package ownership manifests by the caller.
// This component is not connected to the loader or exposed as an installer command.
internal static class FileTransaction
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateDirectoryW(string path, IntPtr securityAttributes);
    internal sealed record Change(string Path, string? BeforeSha256, string? AfterSha256);
    private sealed record Journal(int Version, string Target, Change[] Changes);
    private const string JournalName = "journal.json";
    private const string CommittedName = "committed";

    internal static void Apply(string target, string preparedFiles, string transactionDirectory, IReadOnlyList<Change> changes)
    {
        target = ExistingDirectory(target);
        preparedFiles = ExistingDirectory(preparedFiles);
        var transaction = Path.GetFullPath(transactionDirectory);
        EnsureSeparate(target, preparedFiles, transaction);
        if (Directory.Exists(transaction) || File.Exists(transaction)) throw new IOException("Transaction directory already exists.");
        var parent = Directory.GetParent(transaction) ?? throw new IOException("Invalid transaction directory.");
        ExistingDirectory(parent.FullName);
        // The caller must choose a private, exclusively owned transaction parent.
        if (!OperatingSystem.IsWindows() || !CreateDirectoryW(transaction, IntPtr.Zero))
            throw new IOException("Could not create a new transaction directory.");
        using var transactionLock = Lock(transaction);
        var journal = new Journal(1, target, ValidateChanges(changes));
        var ready = false;
        try {
            for (var i = 0; i < journal.Changes.Length; i++) {
                var change = journal.Changes[i];
                var destination = Resolve(target, change.Path);
                RequireHash(destination, change.BeforeSha256);
                if (change.BeforeSha256 != null) CopyVerified(destination, Backup(transaction, i), change.BeforeSha256);
                if (change.AfterSha256 != null) {
                    var source = Resolve(preparedFiles, change.Path);
                    CopyVerified(source, Payload(transaction, i), change.AfterSha256);
                }
            }
            WriteDurable(Path.Combine(transaction, JournalName), JsonSerializer.SerializeToUtf8Bytes(journal));
            ready = true;
            foreach (var (change, i) in journal.Changes.Select((value, index) => (value, index)))
                ReplaceOne(target, transaction, change, i, false);
            WriteDurable(Path.Combine(transaction, CommittedName), new byte[] { 1 });
        } catch {
            if (ready) {
                RecoverLocked(transaction, journal);
                WriteDurable(Path.Combine(transaction, "restored"), new byte[] { 1 });
            }
            throw;
        }
    }

    // An uncommitted journal is rolled back. A committed journal is kept as the
    // stable backup until Restore is explicitly requested.
    internal static void Recover(string transactionDirectory, string expectedTarget)
    {
        var transaction = ExistingDirectory(transactionDirectory);
        using var transactionLock = Lock(transaction);
        var journal = ReadJournal(transaction);
        RequireTarget(journal, expectedTarget);
        if (!File.Exists(Path.Combine(transaction, CommittedName)) && !File.Exists(Path.Combine(transaction, "restored"))) {
            RecoverLocked(transaction, journal);
            WriteDurable(Path.Combine(transaction, "restored"), new byte[] { 1 });
        }
    }

    internal static void Restore(string transactionDirectory, string expectedTarget)
    {
        var transaction = ExistingDirectory(transactionDirectory);
        using var transactionLock = Lock(transaction);
        var journal = ReadJournal(transaction);
        RequireTarget(journal, expectedTarget);
        if (!File.Exists(Path.Combine(transaction, CommittedName))) throw new IOException("Transaction has not completed.");
        // Preflight all paths before the first change, to avoid overwriting later edits.
        foreach (var change in journal.Changes) RequireHash(Resolve(journal.Target, change.Path), change.AfterSha256);
        // Removing the marker first makes an interrupted restore recoverable.
        File.Delete(Path.Combine(transaction, CommittedName));
        RecoverLocked(transaction, journal);
        WriteDurable(Path.Combine(transaction, "restored"), new byte[] { 1 });
    }

    internal static void VerifyCurrent(string transactionDirectory, string expectedTarget)
    {
        var journal = ReadJournal(ExistingDirectory(transactionDirectory));
        RequireTarget(journal, expectedTarget);
        foreach (var change in journal.Changes) RequireHash(Resolve(journal.Target, change.Path), change.AfterSha256);
    }

    private static void RequireTarget(Journal journal, string expectedTarget)
    {
        if (!string.Equals(journal.Target, ExistingDirectory(expectedTarget), StringComparison.OrdinalIgnoreCase))
            throw new IOException("Transaction does not belong to the selected installation.");
    }

    private static void RecoverLocked(string transaction, Journal journal)
    {
        // Check every path before touching any of them. A third-party modification
        // is never overwritten during recovery.
        for (var i = 0; i < journal.Changes.Length; i++) {
            var change = journal.Changes[i];
            if (change.BeforeSha256 != null) RequireHash(Backup(transaction, i), change.BeforeSha256);
            var path = Resolve(journal.Target, change.Path);
            var actual = HashOrMissing(path);
            if (!HashEquals(actual, change.BeforeSha256) && !HashEquals(actual, change.AfterSha256))
                throw new IOException("A transaction destination was changed outside the updater.");
        }
        for (var i = journal.Changes.Length - 1; i >= 0; i--)
            ReplaceOne(journal.Target, transaction, journal.Changes[i], i, true);
    }

    private static void ReplaceOne(string target, string transaction, Change change, int index, bool rollback)
    {
        var destination = Resolve(target, change.Path);
        var expected = rollback ? change.AfterSha256 : change.BeforeSha256;
        var desired = rollback ? change.BeforeSha256 : change.AfterSha256;
        var current = HashOrMissing(destination);
        if (rollback && HashEquals(current, desired)) return;
        if (!HashEquals(current, expected)) throw new IOException("A transaction destination changed.");
        if (desired == null) { File.Delete(destination); return; }
        var source = rollback ? Backup(transaction, index) : Payload(transaction, index);
        RequireHash(source, desired);
        var parent = Path.GetDirectoryName(destination)!;
        CreateDirectories(target, parent);
        var temporary = Path.Combine(parent, ".dluma-transaction-" + Guid.NewGuid().ToString("N") + ".tmp");
        try {
            CopyVerified(source, temporary, desired);
            // Recheck after copying so a concurrent edit is not knowingly replaced.
            RequireHash(destination, expected);
            File.Move(temporary, destination, true);
        } finally { File.Delete(temporary); }
    }

    private static Change[] ValidateChanges(IReadOnlyList<Change> changes)
    {
        if (changes.Count is 0 or > 2048) throw new InvalidDataException("Invalid transaction size.");
        var result = new Change[changes.Count];
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < changes.Count; i++) {
            var change = changes[i];
            ValidateRelative(change.Path);
            if (!names.Add(change.Path) || !ValidHash(change.BeforeSha256) || !ValidHash(change.AfterSha256) ||
                (change.BeforeSha256 == null && change.AfterSha256 == null)) throw new InvalidDataException("Invalid transaction plan.");
            result[i] = change;
        }
        return result;
    }

    private static bool ValidHash(string? hash) => hash == null || (hash.Length == 64 && hash.All(c => c is >= '0' and <= '9' or >= 'A' and <= 'F'));
    private static bool HashEquals(string? a, string? b) => string.Equals(a, b, StringComparison.Ordinal);
    private static string? HashOrMissing(string path)
    {
        if (Directory.Exists(path)) throw new IOException("Transaction file is a directory.");
        if (!File.Exists(path)) return null;
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new IOException("Transaction file is a reparse point.");
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return Convert.ToHexString(SHA256.HashData(stream));
    }
    private static void RequireHash(string path, string? expected)
    {
        if (!HashEquals(HashOrMissing(path), expected)) throw new IOException("Transaction file does not match its expected contents.");
    }
    private static void CopyVerified(string source, string destination, string expected)
    {
        RequireHash(source, expected);
        using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
            input.CopyTo(output);
            output.Flush(true);
        }
        RequireHash(destination, expected);
    }
    private static void WriteDurable(string path, byte[] bytes)
    {
        using var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        file.Write(bytes); file.Flush(true);
    }
    private static FileStream Lock(string transaction) => new(Path.Combine(transaction, ".lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    private static string Backup(string root, int index) => Path.Combine(root, $"before-{index:D4}");
    private static string Payload(string root, int index) => Path.Combine(root, $"after-{index:D4}");

    private static Journal ReadJournal(string transaction)
    {
        var path = Path.Combine(transaction, JournalName);
        if (new FileInfo(path).Length > 1024 * 1024) throw new InvalidDataException("Transaction journal is too large.");
        var journal = JsonSerializer.Deserialize<Journal>(File.ReadAllBytes(path)) ?? throw new InvalidDataException("Missing transaction journal.");
        if (journal.Version != 1 || journal.Changes == null) throw new InvalidDataException("Invalid transaction journal.");
        var target = ExistingDirectory(journal.Target);
        EnsureSeparate(target, target, transaction, allowSameInput: true);
        return new Journal(1, target, ValidateChanges(journal.Changes));
    }
    private static string ExistingDirectory(string path)
    {
        var full = Path.GetFullPath(path);
        var directory = new DirectoryInfo(full);
        if (!directory.Exists) throw new DirectoryNotFoundException("Required transaction directory does not exist.");
        for (DirectoryInfo? part = directory; part != null; part = part.Parent)
            if ((part.Attributes & FileAttributes.ReparsePoint) != 0) throw new IOException("Transaction path contains a reparse point.");
        return full.TrimEnd(Path.DirectorySeparatorChar);
    }
    private static void EnsureSeparate(string target, string prepared, string transaction, bool allowSameInput = false)
    {
        if ((!allowSameInput && (Contains(target, prepared) || Contains(prepared, target))) ||
            Contains(target, transaction) || Contains(transaction, target) || Contains(prepared, transaction) || Contains(transaction, prepared))
            throw new IOException("Transaction paths overlap.");
    }
    private static bool Contains(string root, string path) => path.Equals(root, StringComparison.OrdinalIgnoreCase) || path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    private static void ValidateRelative(string path)
    {
        if (string.IsNullOrEmpty(path) || Path.IsPathRooted(path) || path.Contains('\\') || path.Contains(':') || path.Length > 240)
            throw new InvalidDataException("Invalid transaction path.");
        foreach (var part in path.Split('/')) {
            var stem = part.Split('.')[0].ToUpperInvariant();
            if (part.Length == 0 || part is "." or ".." || part.EndsWith('.') || part.EndsWith(' ') || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                stem is "CON" or "PRN" or "AUX" or "NUL" || (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] is >= '1' and <= '9'))
                throw new InvalidDataException("Invalid transaction path.");
        }
    }
    private static string Resolve(string root, string relative)
    {
        ValidateRelative(relative);
        var current = root;
        var parts = relative.Split('/');
        for (var i = 0; i < parts.Length - 1; i++) {
            current = Path.Combine(current, parts[i]);
            if (File.Exists(current)) throw new IOException("Transaction parent is a file.");
            if (Directory.Exists(current) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new IOException("Transaction parent is a reparse point.");
        }
        return Path.Combine(current, parts[^1]);
    }
    private static void CreateDirectories(string root, string parent)
    {
        var relative = Path.GetRelativePath(root, parent);
        if (relative == ".") return;
        var current = root;
        foreach (var part in relative.Split(Path.DirectorySeparatorChar)) {
            current = Path.Combine(current, part);
            if (File.Exists(current)) throw new IOException("Transaction parent is a file.");
            if (Directory.Exists(current) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new IOException("Transaction parent is a reparse point.");
            Directory.CreateDirectory(current);
        }
    }
}
