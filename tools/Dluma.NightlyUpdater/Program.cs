using System.IO.Compression;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

internal static class Program
{
    private const long MaxArchiveBytes = 512L * 1024 * 1024;
    private const long MaxExtractedBytes = 1024L * 1024 * 1024;
    private const int MaxEntries = 2048;
    private const string ManifestUrl = "https://github.com/BalancedLight/DLUMA-public/releases/download/nightly/nightly.json";
    private static readonly Regex Hex40 = new("^[0-9a-f]{40}$", RegexOptions.CultureInvariant);
    private static readonly Regex Hex64 = new("^[0-9a-f]{64}$", RegexOptions.CultureInvariant);
    private static readonly Regex FileName = new("^DLUMA-[0-9]+(?:\\.[0-9]+)+-([0-9a-f]{40})\\.zip$", RegexOptions.CultureInvariant);
    private static readonly Regex ModName = new("^DLUMA \\([0-9]+(?:\\.[0-9]+)+\\)\\.mod$", RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);
    private static readonly HashSet<string> RootFiles = new(StringComparer.OrdinalIgnoreCase) {
        "LICENSE", "THIRD_PARTY_NOTICES.md", "dluma.pak", "dluma_lua54.dll",
        "LICENSE_LUA_5.4.8.txt", "dluma_discord_presence.dll", "dluma_nightly_updater.exe"
    };
    private sealed record Manifest(string Schema, string Commit, string Filename, string Sha256);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateDirectoryW(string path, IntPtr securityAttributes);

    private static async Task<int> Main(string[] args)
    {
        try {
            if (args.Length == 5 && args[0] == "probe") {
                var helper = InstallLock.Provision(args[2]);
                string? installed; bool pending;
                using (InstallLock.Acquire(args[1])) {
                    var target = Path.GetDirectoryName(Path.GetFullPath(args[1]))!;
                    pending = TransactionChain.HasPending(args[2], target);
                    installed = pending ? null : TransactionChain.CurrentCommit(args[2], target);
                }
                var result = "none"; string commit = "", digest = "";
                if (pending) result = "restore";
                else if (args[4] == "1") {
                    var bytes = await FetchManifest();
                    var manifest = ParseManifest(new System.Text.UTF8Encoding(false, true).GetString(bytes));
                    if (manifest.Commit != installed) { result = "update"; commit = manifest.Commit; digest = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(); }
                } else if (installed != null) result = "restore";
                await File.WriteAllTextAsync(args[3], $"[offer]\nkind={result}\ncommit={commit}\ndigest={digest}\nhelper={helper}\n", System.Text.Encoding.Unicode);
            } else if (args.Length == 2 && args[0] == "provision") {
                Console.WriteLine(InstallLock.Provision(args[1]));
            } else if (args.Length == 1 && args[0] == "check") {
                Console.WriteLine(FormatCheckResult(await FetchManifest()));
            } else if (args.Length == 2 && args[0] == "download") {
                await DownloadAndStage(args[1]);
            } else if (args.Length == 4 && args[0] == "stage") {
                var manifest = ParseManifest(await File.ReadAllTextAsync(args[1]));
                await Stage(manifest, args[2], args[3]);
            } else if (args.Length == 6 && args[0] == "update") {
                await Update(args[1], args[2], args[3], args[4], args[5], null, null);
            } else if (args.Length == 8 && args[0] == "update-local") {
                await Update(args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            } else if (args.Length == 3 && args[0] == "status") {
                InstallLock.RequirePrivateRoot(args[2]);
                using var held = InstallLock.Acquire(args[1]);
                var target = Path.GetDirectoryName(Path.GetFullPath(args[1]))!;
                var commit = TransactionChain.CurrentCommit(args[2], target);
                Console.WriteLine(JsonSerializer.Serialize(new { commit, stable_backup = commit != null }));
            } else if (args.Length == 5 && args[0] == "restore-stable") {
                WaitForProcess(args[3], args[4]);
                using var held = InstallLock.Acquire(args[1]);
                InstallLock.RequirePrivateRoot(args[2]);
                TransactionChain.RestoreStable(args[2], Path.GetDirectoryName(Path.GetFullPath(args[1]))!);
            } else {
                Console.Error.WriteLine("Usage: provision <private-root> | check | update <module> <manifest-sha256> <private-root> <pid> <start-utc-ticks> | update-local <module> <manifest-sha256> <private-root> <pid> <start-utc-ticks> <manifest> <archive> | download <new-stage-directory> | stage <nightly.json> <archive.zip> <new-stage-directory> | status <module-path> <private-root> | restore-stable <module-path> <private-root> <pid> <start-utc-ticks>");
                return 2;
            }
            return 0;
        } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or JsonException or HttpRequestException or TaskCanceledException or ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception or System.Text.DecoderFallbackException) {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }

    private static async Task Update(string module, string expectedManifestSha256, string privateRoot, string processId, string startTicks, string? localManifest, string? localArchive)
    {
        if (!Hex64.IsMatch(expectedManifestSha256)) throw new InvalidDataException("Invalid manifest checksum.");
        InstallLock.RequirePrivateRoot(privateRoot);
        // Capture a process handle before any download. It remains valid after exit.
        using var player = PinProcess(processId, startTicks);
        var bytes = localManifest == null ? await FetchManifest() : await ReadManifestFile(localManifest);
        if (Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant() != expectedManifestSha256)
            throw new InvalidDataException("Manifest checksum does not match.");
        var manifest = ParseManifest(new System.Text.UTF8Encoding(false, true).GetString(bytes));
        var id = Guid.NewGuid().ToString("N");
        var root = Path.GetFullPath(privateRoot);
        var archive = localArchive ?? Path.Combine(root, "archive-" + id + ".zip");
        var stage = Path.Combine(root, "stage-" + id);
        try {
            if (localArchive == null) await FetchArchive(manifest, archive);
            await Stage(manifest, archive, stage);
            using var held = InstallLock.Acquire(module);
            player.WaitForExit();
            var modulePath = Path.GetFullPath(module);
            var target = Path.GetDirectoryName(modulePath) ?? throw new InvalidDataException("Invalid module path.");
            var changes = OwnershipPlan.Create(target, stage, modulePath);
            if (changes.Length == 0) { Console.WriteLine("Package is already installed."); return; }
            TransactionChain.Add(root, id, target, manifest.Commit);
            FileTransaction.Apply(target, stage, TransactionPath(root, id), changes);
            Console.WriteLine("Package transaction completed: " + id);
        } finally {
            if (Directory.Exists(stage)) Directory.Delete(stage, true);
            if (localArchive == null) File.Delete(archive);
        }
    }

    private static async Task<byte[]> ReadManifestFile(string path)
    {
        if (new FileInfo(path).Length > 16384) throw new InvalidDataException("Manifest is too large.");
        return await File.ReadAllBytesAsync(path);
    }

    private static async Task FetchArchive(Manifest manifest, string destination)
    {
        using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("DLUMA-Nightly-Updater/1.0");
        var url = "https://github.com/BalancedLight/DLUMA-public/releases/download/nightly/" + manifest.Filename;
        using var response = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength is > MaxArchiveBytes) throw new InvalidDataException("Archive is too large.");
        await using var input = await response.Content.ReadAsStreamAsync();
        await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        var buffer = new byte[65536]; long total = 0; int count;
        while ((count = await input.ReadAsync(buffer)) != 0) {
            total += count;
            if (total > MaxArchiveBytes) throw new InvalidDataException("Archive is too large.");
            await output.WriteAsync(buffer.AsMemory(0, count));
        }
    }

    private static string TransactionPath(string root, string id)
    {
        if (id.Length != 32 || id.Any(c => c is not (>= '0' and <= '9') and not (>= 'a' and <= 'f')))
            throw new InvalidDataException("Invalid transaction ID.");
        return Path.Combine(Path.GetFullPath(root), "transaction-" + id);
    }

    private static void WaitForProcess(string processId, string startTicks)
    {
        using var process = PinProcess(processId, startTicks);
        process.WaitForExit();
    }

    private static Process PinProcess(string processId, string startTicks)
    {
        if (!int.TryParse(processId, out var pid) || pid <= 0 || !long.TryParse(startTicks, out var ticks) || ticks <= 0)
            throw new InvalidDataException("Invalid process identity.");
        var process = Process.GetProcessById(pid);
        try {
            _ = process.SafeHandle;
            if (process.StartTime.ToUniversalTime().Ticks != ticks) throw new InvalidDataException("Process identity does not match.");
            return process;
        } catch { process.Dispose(); throw; }
    }

    private static Manifest ParseManifest(string json)
    {
        if (json.Length > 16384) throw new InvalidDataException("Manifest is too large.");
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object) throw new InvalidDataException("Invalid manifest.");
        var fields = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var field in root.EnumerateObject()) {
            if (field.Value.ValueKind != JsonValueKind.String || !fields.TryAdd(field.Name, field.Value.GetString()!))
                throw new InvalidDataException("Invalid or duplicate manifest field.");
        }
        if (fields.Count != 4 || !fields.TryGetValue("schema", out var schema) || schema != "dluma.nightly.v1" ||
            !fields.TryGetValue("commit", out var commit) || !Hex40.IsMatch(commit) ||
            !fields.TryGetValue("filename", out var filename) || !fields.TryGetValue("sha256", out var sha) || !Hex64.IsMatch(sha))
            throw new InvalidDataException("Invalid nightly manifest.");
        var match = FileName.Match(filename);
        if (!match.Success || match.Groups[1].Value != commit) throw new InvalidDataException("Invalid nightly archive name.");
        return new Manifest(schema, commit, filename, sha);
    }

    internal static string FormatCheckResult(byte[] bytes)
    {
        if (bytes.Length == 0 || bytes.Length > 16384) throw new InvalidDataException("Invalid manifest size.");
        var utf8 = new System.Text.UTF8Encoding(false, true);
        var manifest = ParseManifest(utf8.GetString(bytes));
        var digest = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        return JsonSerializer.Serialize(new { commit = manifest.Commit, filename = manifest.Filename, manifest_sha256 = digest });
    }

    private static async Task<byte[]> FetchManifest()
    {
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(20) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("DLUMA-Nightly-Updater/1.0");
        using var response = await client.GetAsync(ManifestUrl, HttpCompletionOption.ResponseHeadersRead);
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength is > 16384) throw new InvalidDataException("Manifest is too large.");
        return await ReadBounded(response.Content, 16384);
    }

    private static async Task DownloadAndStage(string output)
    {
        using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("DLUMA-Nightly-Updater/1.0");
        using var response = await client.GetAsync(ManifestUrl, HttpCompletionOption.ResponseHeadersRead);
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength is > 16384) throw new InvalidDataException("Manifest is too large.");
        var manifestBytes = await ReadBounded(response.Content, 16384);
        var manifest = ParseManifest(System.Text.Encoding.UTF8.GetString(manifestBytes));
        var temp = Path.Combine(Path.GetTempPath(), "dluma-nightly-" + Guid.NewGuid().ToString("N") + ".zip");
        try {
            var url = "https://github.com/BalancedLight/DLUMA-public/releases/download/nightly/" + manifest.Filename;
            using var archiveResponse = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
            archiveResponse.EnsureSuccessStatusCode();
            if (archiveResponse.Content.Headers.ContentLength is > MaxArchiveBytes) throw new InvalidDataException("Archive is too large.");
            await using (var input = await archiveResponse.Content.ReadAsStreamAsync())
            await using (var outputFile = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                var buffer = new byte[65536]; long total = 0; int count;
                while ((count = await input.ReadAsync(buffer)) != 0) {
                    total += count;
                    if (total > MaxArchiveBytes) throw new InvalidDataException("Archive is too large.");
                    await outputFile.WriteAsync(buffer.AsMemory(0, count));
                }
            }
            await Stage(manifest, temp, output);
        } finally { File.Delete(temp); }
    }

    private static async Task<byte[]> ReadBounded(HttpContent content, int limit)
    {
        await using var input = await content.ReadAsStreamAsync();
        using var output = new MemoryStream();
        var buffer = new byte[4096]; int count;
        while ((count = await input.ReadAsync(buffer)) != 0) {
            if (output.Length + count > limit) throw new InvalidDataException("Response is too large.");
            output.Write(buffer, 0, count);
        }
        return output.ToArray();
    }

    private static async Task Stage(Manifest manifest, string archivePath, string destination)
    {
        var target = Path.GetFullPath(destination);
        if (Directory.Exists(target) || File.Exists(target)) throw new IOException("Stage directory already exists.");
        var parent = Directory.GetParent(target) ?? throw new InvalidDataException("Invalid stage directory.");
        if (!parent.Exists) throw new DirectoryNotFoundException("Stage parent directory does not exist.");
        for (DirectoryInfo? part = parent; part != null; part = part.Parent)
            if ((part.Attributes & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("Stage path contains a reparse point.");
        await using var file = new FileStream(archivePath, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (file.Length <= 0 || file.Length > MaxArchiveBytes) throw new InvalidDataException("Invalid archive size.");
        var hash = Convert.ToHexString(await SHA256.HashDataAsync(file)).ToLowerInvariant();
        if (!CryptographicOperations.FixedTimeEquals(System.Text.Encoding.ASCII.GetBytes(hash), System.Text.Encoding.ASCII.GetBytes(manifest.Sha256)))
            throw new InvalidDataException("Archive checksum does not match the manifest.");
        file.Position = 0;
        using var zip = new ZipArchive(file, ZipArchiveMode.Read);
        if (zip.Entries.Count == 0 || zip.Entries.Count > MaxEntries) throw new InvalidDataException("Invalid archive entry count.");
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int mods = 0; long total = 0;
        foreach (var entry in zip.Entries) {
            var name = entry.FullName;
            if (name.EndsWith('/') || name.Contains('\\') || name.StartsWith('/') || name.Contains(':') || name.Length > 240)
                throw new InvalidDataException("Invalid archive path.");
            var parts = name.Split('/');
            if (parts.Any(part => part.Equals("dluma.ini", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("Packages must not contain user configuration.");
            if (parts.Any(p => p.Length == 0 || p == "." || p == ".." || p.EndsWith('.') || p.EndsWith(' ') || p.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || IsReserved(p)))
                throw new InvalidDataException("Invalid archive path.");
            // Reject Unix special files and links as well as Windows reparse points.
            var mode = (entry.ExternalAttributes >> 16) & 0xF000;
            if ((mode != 0 && mode != 0x8000) || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Archive contains a non-regular file.");
            if (!names.Add(name)) throw new InvalidDataException("Duplicate archive path.");
            if (parts.Length == 1) {
                if (ModName.IsMatch(name)) mods++;
                else if (!RootFiles.Contains(name)) throw new InvalidDataException("Unexpected package file.");
                roots.Add(name);
            } else if (!parts[0].Equals("dluma_runtime", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Unexpected package directory.");
            if (entry.Length < 1 || entry.Length > MaxExtractedBytes - total) throw new InvalidDataException("Invalid extracted size.");
            total += entry.Length;
        }
        if (mods != 1 || !RootFiles.IsSubsetOf(roots) || !names.Any(n => n.StartsWith("dluma_runtime/", StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException("Package is incomplete.");
        // CreateDirectoryW fails if a competing process created the directory.
        if (!OperatingSystem.IsWindows() || !CreateDirectoryW(target, IntPtr.Zero))
            throw new IOException("Could not create a new stage directory.");
        try {
            foreach (var entry in zip.Entries) {
                var output = Path.Combine(target, entry.FullName.Replace('/', Path.DirectorySeparatorChar));
                Directory.CreateDirectory(Path.GetDirectoryName(output)!);
                await using var source = entry.Open();
                await using var sink = new FileStream(output, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                var buffer = new byte[65536]; long written = 0; int count;
                while ((count = await source.ReadAsync(buffer)) != 0) {
                    written += count;
                    if (written > entry.Length) throw new InvalidDataException("Archive entry exceeds its declared size.");
                    await sink.WriteAsync(buffer.AsMemory(0, count));
                }
                if (written != entry.Length) throw new InvalidDataException("Archive entry is truncated.");
            }
            Console.WriteLine($"Prepared nightly {manifest.Commit} at {target}");
        } catch {
            Directory.Delete(target, true);
            throw;
        }
    }

    private static bool IsReserved(string part)
    {
        var stem = part.Split('.')[0].ToUpperInvariant();
        return stem is "CON" or "PRN" or "AUX" or "NUL" ||
            (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] is >= '1' and <= '9');
    }
}
