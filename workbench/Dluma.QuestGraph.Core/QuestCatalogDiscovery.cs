using System.Security;
using System.Text;
using System.Xml;

namespace Dluma.QuestGraph.Core;

public enum QuestCatalogRootScope
{
    ProjectPrivate,
    Installed
}

public sealed record QuestCatalogDiscoveryRoot(
    string Path,
    QuestCatalogRootScope Scope);

public sealed record QuestCatalogDiscoveredInput(
    QuestCatalogRootScope Scope,
    string RootLabel,
    string RelativePath,
    QuestCatalogFormat Format,
    long Length,
    string Sha256);

public sealed record QuestCatalogDiscoveryDiagnostic(
    string Code,
    QuestDiagnosticSeverity Severity,
    string Message,
    string? RootLabel = null,
    string? RelativePath = null);

public sealed record QuestCatalogDiscoveryResult(
    QuestCatalog Catalog,
    IReadOnlyList<QuestCatalogDiscoveredInput> Inputs,
    IReadOnlyList<QuestCatalogDiscoveryDiagnostic> Diagnostics,
    string CacheKey,
    bool CacheHit,
    bool WasTruncated);

public sealed record QuestCatalogDiscoveryOptions
{
    public int MaximumDepth { get; init; } = 12;
    public int MaximumDirectories { get; init; } = 2_048;
    public int MaximumEntries { get; init; } = 20_000;
    public int MaximumFiles { get; init; } = 512;
    public int MaximumBytesPerFile { get; init; } = 8 * 1024 * 1024;
    public long MaximumTotalBytes { get; init; } = 64L * 1024 * 1024;
    public int MaximumDiagnostics { get; init; } = 256;
    public int MaximumCacheEntries { get; init; } = 8;
}

public sealed class QuestCatalogDiscoveryService
{
    private readonly QuestCatalogDiscoveryOptions _options;
    private readonly object _cacheLock = new();
    private readonly Dictionary<string, CacheEntry> _cache = new(StringComparer.Ordinal);
    private readonly LinkedList<string> _cacheOrder = new();

    public QuestCatalogDiscoveryService(QuestCatalogDiscoveryOptions? options = null)
    {
        _options = options ?? new QuestCatalogDiscoveryOptions();
        ValidateOptions(_options);
    }

    public QuestCatalogDiscoveryOptions Options => _options;

    public QuestCatalogDiscoveryResult Discover(
        IEnumerable<QuestCatalogDiscoveryRoot> roots,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(roots);
        var supplied = roots.Select((root, index) => new SuppliedRoot(root, index)).ToArray();
        if (supplied.Any(item => item.Root is null))
            throw new ArgumentException("Catalog discovery roots must not contain null values.", nameof(roots));

        var orderedRoots = supplied
            .OrderBy(item => item.Root.Scope)
            .ThenBy(item => item.Index)
            .ToArray();
        var diagnostics = new List<QuestCatalogDiscoveryDiagnostic>();
        var candidates = new List<Candidate>();
        var visitedFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var directoryCount = 0;
        var entryCount = 0;
        var fileCount = 0;
        long totalBytes = 0;
        var wasTruncated = false;
        var stop = false;

        foreach (var suppliedRoot in orderedRoots)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (stop)
                break;
            var rootLabel = $"{suppliedRoot.Root.Scope.ToString().ToLowerInvariant()}[{suppliedRoot.Index}]";
            string rootPath;
            try
            {
                ArgumentException.ThrowIfNullOrWhiteSpace(suppliedRoot.Root.Path);
                rootPath = Path.TrimEndingDirectorySeparator(
                    Path.GetFullPath(suppliedRoot.Root.Path));
                var attributes = File.GetAttributes(rootPath);
                if ((attributes & FileAttributes.Directory) == 0)
                {
                    AddDiagnostic(diagnostics, new(
                        "QCAT001",
                        QuestDiagnosticSeverity.Warning,
                        "The supplied catalog root is not a directory and was skipped.",
                        rootLabel));
                    continue;
                }
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                {
                    AddDiagnostic(diagnostics, new(
                        "QCAT002",
                        QuestDiagnosticSeverity.Warning,
                        "The supplied catalog root is a reparse point and was rejected.",
                        rootLabel));
                    continue;
                }
            }
            catch (Exception exception) when (IsFileSystemException(exception))
            {
                AddDiagnostic(diagnostics, new(
                    "QCAT003",
                    QuestDiagnosticSeverity.Warning,
                    "The supplied catalog root is unavailable and was skipped.",
                    rootLabel));
                continue;
            }

            var rootPrefix = rootPath + Path.DirectorySeparatorChar;
            var pending = new Stack<PendingDirectory>();
            pending.Push(new PendingDirectory(rootPath, 0));
            while (pending.Count > 0 && !stop)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var current = pending.Pop();
                if (++directoryCount > _options.MaximumDirectories)
                {
                    wasTruncated = true;
                    stop = true;
                    AddDiagnostic(diagnostics, new(
                        "QCAT004",
                        QuestDiagnosticSeverity.Warning,
                        "Catalog discovery stopped at the directory limit.",
                        rootLabel));
                    break;
                }

                List<string> entries;
                try
                {
                    entries = new List<string>();
                    foreach (var entry in Directory.EnumerateFileSystemEntries(
                        current.Path,
                        "*",
                        SearchOption.TopDirectoryOnly))
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        if (++entryCount > _options.MaximumEntries)
                        {
                            wasTruncated = true;
                            stop = true;
                            AddDiagnostic(diagnostics, new(
                                "QCAT005",
                                QuestDiagnosticSeverity.Warning,
                                "Catalog discovery stopped at the filesystem-entry limit.",
                                rootLabel));
                            break;
                        }
                        entries.Add(entry);
                    }
                }
                catch (Exception exception) when (IsFileSystemException(exception))
                {
                    AddDiagnostic(diagnostics, new(
                        "QCAT006",
                        QuestDiagnosticSeverity.Warning,
                        "A catalog directory could not be enumerated and was skipped.",
                        rootLabel,
                        SafeRelativePath(rootPath, current.Path)));
                    continue;
                }
                if (stop)
                    break;

                entries.Sort(StringComparer.OrdinalIgnoreCase);
                var childDirectories = new List<string>();
                foreach (var entry in entries)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    string fullPath;
                    FileAttributes attributes;
                    try
                    {
                        fullPath = Path.GetFullPath(entry);
                        if (!fullPath.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase))
                        {
                            AddDiagnostic(diagnostics, new(
                                "QCAT007",
                                QuestDiagnosticSeverity.Warning,
                                "A filesystem entry escaped its supplied root and was rejected.",
                                rootLabel));
                            continue;
                        }
                        attributes = File.GetAttributes(fullPath);
                    }
                    catch (Exception exception) when (IsFileSystemException(exception))
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT008",
                            QuestDiagnosticSeverity.Warning,
                            "A catalog filesystem entry became unavailable and was skipped.",
                            rootLabel));
                        continue;
                    }

                    var relativePath = SafeRelativePath(rootPath, fullPath);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT009",
                            QuestDiagnosticSeverity.Warning,
                            "A reparse-point entry was not traversed or read.",
                            rootLabel,
                            relativePath));
                        continue;
                    }
                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        if (current.Depth >= _options.MaximumDepth)
                        {
                            AddDiagnostic(diagnostics, new(
                                "QCAT010",
                                QuestDiagnosticSeverity.Info,
                                "A directory below the maximum discovery depth was skipped.",
                                rootLabel,
                                relativePath));
                            continue;
                        }
                        childDirectories.Add(fullPath);
                        continue;
                    }
                    if (!LooksLikeCatalogFile(fullPath) || !visitedFiles.Add(fullPath))
                        continue;
                    if (++fileCount > _options.MaximumFiles)
                    {
                        wasTruncated = true;
                        stop = true;
                        AddDiagnostic(diagnostics, new(
                            "QCAT011",
                            QuestDiagnosticSeverity.Warning,
                            "Catalog discovery stopped at the candidate-file limit.",
                            rootLabel));
                        break;
                    }

                    long announcedLength;
                    try
                    {
                        announcedLength = new FileInfo(fullPath).Length;
                    }
                    catch (Exception exception) when (IsFileSystemException(exception))
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT012",
                            QuestDiagnosticSeverity.Warning,
                            "A catalog candidate could not be inspected and was skipped.",
                            rootLabel,
                            relativePath));
                        continue;
                    }
                    if (announcedLength < 0 || announcedLength > _options.MaximumBytesPerFile)
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT013",
                            QuestDiagnosticSeverity.Warning,
                            "A catalog candidate exceeded the per-file byte limit and was skipped.",
                            rootLabel,
                            relativePath));
                        continue;
                    }
                    if (announcedLength > _options.MaximumTotalBytes - totalBytes)
                    {
                        wasTruncated = true;
                        stop = true;
                        AddDiagnostic(diagnostics, new(
                            "QCAT014",
                            QuestDiagnosticSeverity.Warning,
                            "Catalog discovery stopped at the aggregate byte limit.",
                            rootLabel));
                        break;
                    }

                    byte[] bytes;
                    try
                    {
                        bytes = ReadBoundedFile(
                            fullPath,
                            Math.Min(
                                _options.MaximumBytesPerFile,
                                checked((int)Math.Min(int.MaxValue, _options.MaximumTotalBytes - totalBytes))),
                            cancellationToken);
                    }
                    catch (Exception exception) when (IsFileSystemException(exception) || exception is InvalidDataException)
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT015",
                            QuestDiagnosticSeverity.Warning,
                            "A catalog candidate could not be read within configured limits and was skipped.",
                            rootLabel,
                            relativePath));
                        continue;
                    }
                    totalBytes = checked(totalBytes + bytes.Length);

                    QuestCatalogFormat? format;
                    try
                    {
                        format = DetectFormat(fullPath, bytes);
                    }
                    catch (Exception exception) when (
                        exception is XmlException or DecoderFallbackException or InvalidDataException)
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT016",
                            QuestDiagnosticSeverity.Warning,
                            "A catalog candidate was malformed and was skipped.",
                            rootLabel,
                            relativePath));
                        continue;
                    }
                    if (format is null)
                        continue;

                    try
                    {
                        using var validationStream = new MemoryStream(bytes, writable: false);
                        _ = QuestCatalogExtractor.Extract(
                        [
                            new QuestCatalogInput(relativePath, format.Value, validationStream)
                        ]);
                    }
                    catch (Exception exception) when (IsCatalogParseException(exception))
                    {
                        AddDiagnostic(diagnostics, new(
                            "QCAT017",
                            QuestDiagnosticSeverity.Warning,
                            "A catalog definition could not be parsed and was skipped.",
                            rootLabel,
                            relativePath));
                        continue;
                    }

                    candidates.Add(new Candidate(
                        suppliedRoot.Root.Scope,
                        suppliedRoot.Index,
                        rootLabel,
                        relativePath,
                        format.Value,
                        bytes,
                        QuestHash.Sha256(bytes)));
                }

                for (var index = childDirectories.Count - 1; index >= 0; --index)
                    pending.Push(new PendingDirectory(childDirectories[index], current.Depth + 1));
            }
        }

        var orderedCandidates = candidates
            .OrderBy(candidate => candidate.Scope)
            .ThenBy(candidate => candidate.RootIndex)
            .ThenBy(candidate => candidate.RelativePath, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        var discoveredInputs = orderedCandidates.Select(candidate => new QuestCatalogDiscoveredInput(
            candidate.Scope,
            candidate.RootLabel,
            candidate.RelativePath,
            candidate.Format,
            candidate.Bytes.Length,
            candidate.Sha256)).ToArray();
        var cacheKey = CreateCacheKey(orderedCandidates);
        if (TryGetCached(cacheKey, out var cachedCatalog))
        {
            return new QuestCatalogDiscoveryResult(
                cachedCatalog,
                discoveredInputs,
                diagnostics.ToArray(),
                cacheKey,
                true,
                wasTruncated);
        }

        var streams = new List<MemoryStream>(orderedCandidates.Length);
        try
        {
            var inputs = new QuestCatalogInput[orderedCandidates.Length];
            for (var index = 0; index < orderedCandidates.Length; ++index)
            {
                var candidate = orderedCandidates[index];
                var stream = new MemoryStream(candidate.Bytes, writable: false);
                streams.Add(stream);
                inputs[index] = new QuestCatalogInput(
                    candidate.RelativePath,
                    candidate.Format,
                    stream);
            }
            var catalog = QuestCatalogExtractor.Extract(inputs);
            AddCached(cacheKey, catalog);
            return new QuestCatalogDiscoveryResult(
                catalog,
                discoveredInputs,
                diagnostics.ToArray(),
                cacheKey,
                false,
                wasTruncated);
        }
        finally
        {
            foreach (var stream in streams)
                stream.Dispose();
        }
    }

    public Task<QuestCatalogDiscoveryResult> DiscoverAsync(
        IEnumerable<QuestCatalogDiscoveryRoot> roots,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(roots);
        var snapshot = roots.ToArray();
        return Task.Run(() => Discover(snapshot, cancellationToken), cancellationToken);
    }

    private static void ValidateOptions(QuestCatalogDiscoveryOptions options)
    {
        if (options.MaximumDepth < 0 ||
            options.MaximumDirectories <= 0 ||
            options.MaximumEntries <= 0 ||
            options.MaximumFiles <= 0 ||
            options.MaximumBytesPerFile <= 0 ||
            options.MaximumTotalBytes <= 0 ||
            options.MaximumBytesPerFile > options.MaximumTotalBytes ||
            options.MaximumDiagnostics <= 0 ||
            options.MaximumCacheEntries <= 0)
            throw new ArgumentOutOfRangeException(nameof(options), "Catalog discovery limits must be positive and internally consistent.");
    }

    private static bool LooksLikeCatalogFile(string path)
    {
        var name = Path.GetFileName(path);
        return name.Equals("QuestEditorConfig.scr", StringComparison.OrdinalIgnoreCase) ||
            Path.GetExtension(name).Equals(".edf", StringComparison.OrdinalIgnoreCase) ||
            Path.GetExtension(name).Equals(".xml", StringComparison.OrdinalIgnoreCase);
    }

    private static QuestCatalogFormat? DetectFormat(string path, byte[] bytes)
    {
        var name = Path.GetFileName(path);
        if (name.Equals("QuestEditorConfig.scr", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFormat.QuestEditorConfig;
        if (Path.GetExtension(name).Equals(".edf", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFormat.Edf;
        if (!Path.GetExtension(name).Equals(".xml", StringComparison.OrdinalIgnoreCase))
            return null;

        using var stream = new MemoryStream(bytes, writable: false);
        using var reader = XmlReader.Create(stream, new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Prohibit,
            XmlResolver = null,
            IgnoreComments = true,
            MaxCharactersInDocument = bytes.Length
        });
        reader.MoveToContent();
        return reader.LocalName switch
        {
            "PhaseTypeDefinitions" => QuestCatalogFormat.PhaseDefinitionsXml,
            "QuestObjectDefinitions" => QuestCatalogFormat.QuestObjectDefinitionsXml,
            "ColorDefs" => QuestCatalogFormat.QuestColorsXml,
            _ => null
        };
    }

    private static byte[] ReadBoundedFile(
        string path,
        int maximumBytes,
        CancellationToken cancellationToken)
    {
        using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.ReadWrite | FileShare.Delete,
            81920,
            FileOptions.SequentialScan);
        using var memory = new MemoryStream(Math.Min(maximumBytes, 81920));
        var buffer = new byte[81920];
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var read = stream.Read(buffer, 0, Math.Min(buffer.Length, maximumBytes - checked((int)memory.Length) + 1));
            if (read == 0)
                break;
            if (memory.Length + read > maximumBytes)
                throw new InvalidDataException("Catalog input changed beyond its configured byte limit while being read.");
            memory.Write(buffer, 0, read);
        }
        return memory.ToArray();
    }

    private static string SafeRelativePath(string rootPath, string fullPath)
    {
        var relative = Path.GetRelativePath(rootPath, fullPath).Replace('\\', '/');
        return relative == "." ? string.Empty : relative;
    }

    private static string CreateCacheKey(IReadOnlyList<Candidate> candidates)
    {
        var canonical = new StringBuilder();
        foreach (var candidate in candidates)
        {
            canonical.Append(candidate.Scope).Append('|')
                .Append(candidate.RootIndex).Append('|')
                .Append(candidate.RelativePath.ToLowerInvariant()).Append('|')
                .Append(candidate.Format).Append('|')
                .Append(candidate.Bytes.Length).Append('|')
                .Append(candidate.Sha256).Append('\n');
        }
        return QuestHash.Sha256(canonical.ToString());
    }

    private bool TryGetCached(string key, out QuestCatalog catalog)
    {
        lock (_cacheLock)
        {
            if (!_cache.TryGetValue(key, out var entry))
            {
                catalog = null!;
                return false;
            }
            _cacheOrder.Remove(entry.OrderNode);
            _cacheOrder.AddLast(entry.OrderNode);
            catalog = entry.Catalog;
            return true;
        }
    }

    private void AddCached(string key, QuestCatalog catalog)
    {
        lock (_cacheLock)
        {
            if (_cache.TryGetValue(key, out var existing))
            {
                _cacheOrder.Remove(existing.OrderNode);
                _cacheOrder.AddLast(existing.OrderNode);
                return;
            }
            while (_cache.Count >= _options.MaximumCacheEntries)
            {
                var oldest = _cacheOrder.First;
                if (oldest is null)
                    break;
                _cacheOrder.RemoveFirst();
                _cache.Remove(oldest.Value);
            }
            var node = _cacheOrder.AddLast(key);
            _cache.Add(key, new CacheEntry(catalog, node));
        }
    }

    private void AddDiagnostic(
        List<QuestCatalogDiscoveryDiagnostic> diagnostics,
        QuestCatalogDiscoveryDiagnostic diagnostic)
    {
        if (diagnostics.Count < _options.MaximumDiagnostics)
            diagnostics.Add(diagnostic);
    }

    private static bool IsFileSystemException(Exception exception) =>
        exception is IOException or UnauthorizedAccessException or SecurityException or
            NotSupportedException or PathTooLongException or ArgumentException;

    private static bool IsCatalogParseException(Exception exception) =>
        exception is InvalidDataException or InvalidOperationException or XmlException or
            DecoderFallbackException or ArgumentException;

    private sealed record SuppliedRoot(QuestCatalogDiscoveryRoot Root, int Index);
    private sealed record PendingDirectory(string Path, int Depth);
    private sealed record Candidate(
        QuestCatalogRootScope Scope,
        int RootIndex,
        string RootLabel,
        string RelativePath,
        QuestCatalogFormat Format,
        byte[] Bytes,
        string Sha256);
    private sealed record CacheEntry(QuestCatalog Catalog, LinkedListNode<string> OrderNode);
}
