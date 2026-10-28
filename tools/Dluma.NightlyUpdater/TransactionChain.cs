using System.Text.Json;

// The caller holds the per-install mutex and has checked the private root.
internal static class TransactionChain
{
    private sealed record Link(int Version, string Id, string Target, string? Previous, string Commit);

    internal static void Add(string root, string id, string target, string commit)
    {
        target = Path.GetFullPath(target);
        RecoverPending(root, target);
        var previous = Head(root, target);
        if (commit.Length != 40 || commit.Any(c => c is not (>= '0' and <= '9') and not (>= 'a' and <= 'f'))) throw new InvalidDataException("Invalid nightly commit.");
        var link = new Link(1, id, target, previous, commit);
        using var file = new FileStream(LinkPath(root, id), FileMode.CreateNew, FileAccess.Write, FileShare.None);
        JsonSerializer.Serialize(file, link);
        file.Flush(true);
    }

    internal static bool HasPending(string root, string target)
    {
        target = Path.GetFullPath(target);
        return Links(root, target).Any(link => {
            var transaction = TransactionPath(root, link.Id);
            return File.Exists(Path.Combine(transaction, "journal.json")) && !File.Exists(Path.Combine(transaction, "committed")) && !File.Exists(Path.Combine(transaction, "restored"));
        });
    }

    internal static string? CurrentCommit(string root, string target)
    {
        target = Path.GetFullPath(target);
        foreach (var link in Links(root, target)) {
            var transaction = TransactionPath(root, link.Id);
            if (File.Exists(Path.Combine(transaction, "journal.json")) && !File.Exists(Path.Combine(transaction, "committed")) && !File.Exists(Path.Combine(transaction, "restored")))
                throw new IOException("A previous transaction needs recovery.");
        }
        var head = Head(root, target);
        if (head == null) return null;
        FileTransaction.VerifyCurrent(TransactionPath(root, head), target);
        return ReadLink(LinkPath(root, head)).Commit;
    }

    internal static void RestoreStable(string root, string target)
    {
        target = Path.GetFullPath(target);
        var recovered = RecoverPending(root, target);
        var head = Head(root, target);
        if (head == null && !recovered) throw new IOException("No stable backup is available.");
        while (head != null) {
            var link = ReadLink(LinkPath(root, head));
            FileTransaction.Restore(TransactionPath(root, head), target);
            head = link.Previous;
            if (head != null && !File.Exists(Path.Combine(TransactionPath(root, head), "committed")))
                throw new IOException("The backup chain is incomplete.");
        }
    }

    private static bool RecoverPending(string root, string target)
    {
        var recovered = false;
        foreach (var link in Links(root, target)) {
            var transaction = TransactionPath(root, link.Id);
            if (!File.Exists(Path.Combine(transaction, "journal.json")) || File.Exists(Path.Combine(transaction, "committed")) || File.Exists(Path.Combine(transaction, "restored"))) continue;
            FileTransaction.Recover(transaction, target);
            recovered = true;
        }
        return recovered;
    }
    private static string? Head(string root, string target)
    {
        var active = Links(root, target).Where(link => File.Exists(Path.Combine(TransactionPath(root, link.Id), "committed"))).ToDictionary(x => x.Id, StringComparer.Ordinal);
        if (active.Count == 0) return null;
        var referenced = new HashSet<string>(StringComparer.Ordinal);
        foreach (var link in active.Values) {
            if (link.Previous == null) continue;
            if (!active.ContainsKey(link.Previous) || !referenced.Add(link.Previous)) throw new IOException("The backup chain is incomplete or ambiguous.");
        }
        var heads = active.Keys.Where(id => !referenced.Contains(id)).ToArray();
        if (heads.Length != 1) throw new IOException("The backup chain is ambiguous.");
        var visited = new HashSet<string>(StringComparer.Ordinal);
        string? current = heads[0];
        while (current != null) {
            if (!visited.Add(current) || !active.TryGetValue(current, out var link)) throw new IOException("The backup chain is invalid.");
            current = link.Previous;
        }
        if (visited.Count != active.Count) throw new IOException("The backup chain is disconnected.");
        return heads[0];
    }
    private static IEnumerable<Link> Links(string root, string target)
    {
        foreach (var path in Directory.EnumerateFiles(root, "chain-*.json", SearchOption.TopDirectoryOnly)) {
            var link = ReadLink(path);
            if (string.Equals(link.Target, target, StringComparison.OrdinalIgnoreCase)) yield return link;
        }
    }
    private static Link ReadLink(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0 || new FileInfo(path).Length > 4096) throw new InvalidDataException("Invalid backup chain entry.");
        var link = JsonSerializer.Deserialize<Link>(File.ReadAllBytes(path)) ?? throw new InvalidDataException("Invalid backup chain entry.");
        if (link.Version != 1 || !ValidId(link.Id) || (link.Previous != null && !ValidId(link.Previous)) ||
            !string.Equals(Path.GetFileName(path), "chain-" + link.Id + ".json", StringComparison.Ordinal) || !Path.IsPathFullyQualified(link.Target) || link.Commit == null || link.Commit.Length != 40 || link.Commit.Any(c => c is not (>= '0' and <= '9') and not (>= 'a' and <= 'f')))
            throw new InvalidDataException("Invalid backup chain entry.");
        return link;
    }
    private static bool ValidId(string id) => id.Length == 32 && id.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
    private static string LinkPath(string root, string id) => Path.Combine(root, "chain-" + id + ".json");
    private static string TransactionPath(string root, string id) => Path.Combine(root, "transaction-" + id);
}
