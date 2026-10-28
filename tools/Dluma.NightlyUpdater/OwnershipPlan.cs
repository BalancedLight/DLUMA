using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

// Runtime ownership comes from the installed runtime manifest. Shared license
// files are preserved if their existing contents differ from the staged package.
internal static class OwnershipPlan
{
    private static readonly string[] RootNames = {
        "LICENSE", "THIRD_PARTY_NOTICES.md", "dluma.pak", "dluma_lua54.dll",
        "LICENSE_LUA_5.4.8.txt", "dluma_discord_presence.dll", "dluma_nightly_updater.exe"
    };
    private static readonly Regex ModName = new("^DLUMA \\([0-9]+(?:\\.[0-9]+)+\\)\\.mod$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex Digest = new("^[0-9A-F]{64}$", RegexOptions.CultureInvariant);

    internal static FileTransaction.Change[] Create(string installedRoot, string stagedRoot, string installedModulePath)
    {
        var installed = Root(installedRoot);
        var staged = Root(stagedRoot);
        if (Overlap(installed, staged))
            throw new InvalidDataException("Package directories overlap.");
        var module = Path.GetFullPath(installedModulePath);
        if (!string.Equals(Path.GetDirectoryName(module), installed, StringComparison.OrdinalIgnoreCase) || !ModName.IsMatch(Path.GetFileName(module)))
            throw new InvalidDataException("The module is not a versioned DLUMA loader in the installation directory.");
        var currentModuleName = Path.GetFileName(module);
        var stagedModules = Directory.EnumerateFiles(staged, "*", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName).Where(n => n != null && ModName.IsMatch(n)).ToArray();
        if (stagedModules.Length != 1) throw new InvalidDataException("Staged package must contain exactly one loader module.");
        var stagedModuleName = stagedModules[0]!;
        var currentRuntime = RuntimeFiles(installed);
        var stagedRuntime = RuntimeFiles(staged);
        var current = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var next = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var name in RootNames.Where(IsOwnedRoot).Append(currentModuleName))
            current.Add(name, HashFile(Resolve(installed, name)));
        // Generic license names may belong to another package in this directory.
        // Never replace them: add missing files or keep whatever already exists.
        foreach (var name in RootNames.Where(n => !IsOwnedRoot(n))) {
            var stagedHash = HashFile(Resolve(staged, name));
            var existingPath = Resolve(installed, name);
            if (!Exists(existingPath)) next.Add(name, stagedHash);
        }
        foreach (var item in currentRuntime) current.Add(item.Key, item.Value);
        foreach (var name in RootNames.Where(IsOwnedRoot).Append(stagedModuleName)) next.Add(name, HashFile(Resolve(staged, name)));
        foreach (var item in stagedRuntime) next.Add(item.Key, item.Value);
        var all = new HashSet<string>(current.Keys, StringComparer.OrdinalIgnoreCase);
        all.UnionWith(next.Keys);
        var result = new List<FileTransaction.Change>();
        foreach (var path in all.OrderBy(x => x, StringComparer.OrdinalIgnoreCase)) {
            current.TryGetValue(path, out var before);
            next.TryGetValue(path, out var after);
            if (before == null && Exists(Resolve(installed, path)))
                throw new InvalidDataException("A staged package file collides with an unowned installation file.");
            if (before != after) result.Add(new FileTransaction.Change(path, before, after));
        }
        return result.ToArray();
    }

    private static bool IsOwnedRoot(string name) => name.StartsWith("dluma", StringComparison.OrdinalIgnoreCase) || name.Equals("LICENSE_LUA_5.4.8.txt", StringComparison.OrdinalIgnoreCase);

    private static Dictionary<string, string> RuntimeFiles(string root)
    {
        const string manifestPath = "dluma_runtime/runtime-manifest.json";
        var manifest = Resolve(root, manifestPath);
        var info = new FileInfo(manifest);
        if (!info.Exists || info.Length is <= 0 or > 1024 * 1024) throw new InvalidDataException("Invalid runtime manifest size.");
        var bytes = File.ReadAllBytes(manifest);
        using var json = JsonDocument.Parse(bytes);
        var value = json.RootElement;
        if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty("schema", out var schema) || schema.GetString() != "dluma.runtime-tree.v1" ||
            !value.TryGetProperty("files", out var files) || files.ValueKind != JsonValueKind.Array || files.GetArrayLength() is 0 or > 2048)
            throw new InvalidDataException("Invalid runtime manifest.");
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            [manifestPath] = HashFile(manifest)
        };
        foreach (var entry in files.EnumerateArray()) {
            if (entry.ValueKind != JsonValueKind.Object || !entry.TryGetProperty("path", out var nameElement) || nameElement.ValueKind != JsonValueKind.String ||
                !entry.TryGetProperty("sha256", out var hashElement) || hashElement.ValueKind != JsonValueKind.String ||
                !entry.TryGetProperty("size", out var sizeElement) || !sizeElement.TryGetInt64(out var size) || size <= 0)
                throw new InvalidDataException("Invalid runtime file record.");
            var relative = "dluma_runtime/" + nameElement.GetString();
            if (relative.Split('/').Any(part => part.Equals("dluma.ini", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("Packages must not replace user configuration.");
            var digest = hashElement.GetString()!;
            if (!Digest.IsMatch(digest) || !result.TryAdd(relative, digest)) throw new InvalidDataException("Duplicate or invalid runtime file record.");
            var path = Resolve(root, relative);
            if (new FileInfo(path).Length != size || HashFile(path) != digest) throw new InvalidDataException("Runtime file does not match its manifest.");
        }
        return result;
    }
    private static string Root(string path)
    {
        var full = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
        var dir = new DirectoryInfo(full);
        if (!dir.Exists) throw new DirectoryNotFoundException("Package directory is missing.");
        for (DirectoryInfo? p = dir; p != null; p = p.Parent)
            if ((p.Attributes & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("Package path contains a reparse point.");
        return full;
    }
    private static bool Overlap(string a, string b) => a.Equals(b, StringComparison.OrdinalIgnoreCase) || a.StartsWith(b + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) || b.StartsWith(a + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    private static bool Exists(string path) => File.Exists(path) || Directory.Exists(path);
    private static string HashFile(string path)
    {
        if (!File.Exists(path) || (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("Package file is missing or is a reparse point.");
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        return Convert.ToHexString(SHA256.HashData(stream));
    }
    private static string Resolve(string root, string relative)
    {
        if (string.IsNullOrEmpty(relative) || relative.Length > 240 || Path.IsPathRooted(relative) || relative.Contains('\\') || relative.Contains(':'))
            throw new InvalidDataException("Invalid package path.");
        var current = root;
        var parts = relative.Split('/');
        foreach (var part in parts) {
            var stem = part.Split('.')[0].ToUpperInvariant();
            if (part.Length == 0 || part is "." or ".." || part.EndsWith('.') || part.EndsWith(' ') || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                stem is "CON" or "PRN" or "AUX" or "NUL" || (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] is >= '1' and <= '9'))
                throw new InvalidDataException("Invalid package path.");
            current = Path.Combine(current, part);
            if (Exists(current) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("Package path contains a reparse point.");
        }
        return current;
    }
}
