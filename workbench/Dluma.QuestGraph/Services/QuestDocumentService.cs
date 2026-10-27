using System.Security.Cryptography;
using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Protocol;

namespace Dluma.QuestGraph.Services;

public sealed record OpenedQuestDocument(
    QuestGraphDocument Document,
    string? SourcePath,
    QuestGraphSidecarV1? Sidecar,
    bool PreserveExistingSidecar,
    string? SidecarWarning,
    bool RequiresConnection,
    QuestCatalog? Catalog,
    bool WasModified = false,
    string? ConnectedProjectRoot = null);

public sealed class QuestDocumentService
{
    public const int MaximumDocumentBytes = QuestBridgeProtocol.MaximumDocumentBytes;

    static QuestDocumentService() => Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

    public async Task<OpenedQuestDocument> OpenFileAsync(
        string path,
        QuestCatalog? catalog,
        CancellationToken cancellationToken = default)
    {
        var fullPath = Path.GetFullPath(path);
        var bytes = await ReadBoundedAsync(fullPath, cancellationToken).ConfigureAwait(false);
        var sidecarPath = SidecarPath(fullPath);
        var (sidecar, preserve, warning) = await TryLoadSidecarAsync(
            sidecarPath,
            cancellationToken).ConfigureAwait(false);
        var documentId = sidecar?.DocumentId ?? Guid.NewGuid().ToString("N");
        var encodingName = DetectEncoding(bytes);
        var snapshot = new QuestSourceSnapshot(
            documentId,
            Path.GetFileName(fullPath),
            0,
            bytes,
            encodingName);
        return new OpenedQuestDocument(
            PxslParser.Parse(snapshot, catalog),
            fullPath,
            sidecar,
            preserve,
            warning,
            false,
            catalog,
            false);
    }

    public async Task<OpenedQuestDocument> OpenConnectedAsync(
        OpenDocumentMessage message,
        string? projectRoot,
        QuestCatalog? catalog,
        CancellationToken cancellationToken = default)
    {
        ValidateSnapshotMessage(message);
        var sourcePath = ResolveConnectedPath(projectRoot, message.RelativePath, message.DiskPath);
        QuestGraphSidecarV1? sidecar = null;
        var preserve = false;
        string? warning = null;
        if (sourcePath is not null)
        {
            (sidecar, preserve, warning) = await TryLoadSidecarAsync(
                SidecarPath(sourcePath),
                cancellationToken).ConfigureAwait(false);
        }
        var snapshot = new QuestSourceSnapshot(
            message.DocumentId,
            NormalizeRelativePath(message.RelativePath),
            message.Revision,
            message.Bytes,
            message.EncodingName);
        if (!string.Equals(snapshot.Sha256, message.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The connected source bytes do not match the advertised hash.");
        return new OpenedQuestDocument(
            PxslParser.Parse(snapshot, catalog),
            sourcePath,
            sidecar,
            preserve,
            warning,
            true,
            catalog,
            message.WasModified,
            projectRoot is null ? null : Path.GetFullPath(projectRoot));
    }

    public async Task SaveSourceAsync(
        string path,
        QuestSourceSnapshot snapshot,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.Bytes.Length > MaximumDocumentBytes)
            throw new InvalidDataException("PXSL source exceeds the 8 MiB document cap.");
        var fullPath = Path.GetFullPath(path);
        var directory = Path.GetDirectoryName(fullPath) ??
            throw new InvalidOperationException("PXSL path has no parent directory.");
        Directory.CreateDirectory(directory);
        var temporary = Path.Combine(
            directory,
            $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            await using (var stream = new FileStream(
                temporary,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.None,
                81920,
                FileOptions.Asynchronous | FileOptions.WriteThrough))
            {
                await stream.WriteAsync(snapshot.Bytes, cancellationToken).ConfigureAwait(false);
                await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
            }
            var written = await ReadBoundedAsync(temporary, cancellationToken).ConfigureAwait(false);
            var writtenHash = Convert.ToHexString(SHA256.HashData(written));
            if (!string.Equals(writtenHash, snapshot.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new IOException("PXSL transactional save verification failed.");
            File.Move(temporary, fullPath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }

    public Task SaveSidecarAsync(
        string sourcePath,
        QuestGraphSidecarV1 sidecar,
        CancellationToken cancellationToken = default) =>
        QuestGraphSidecarService.SaveFileAsync(
            SidecarPath(Path.GetFullPath(sourcePath)),
            sidecar,
            cancellationToken);

    public static string SidecarPath(string sourcePath) => sourcePath + ".dlumagraph.json";

    public static string DecodeSpan(QuestSourceSnapshot source, PxslTextSpan span)
    {
        if (span.Start < 0 || span.Length < 0 || span.End > source.Bytes.Length)
            throw new InvalidDataException("PXSL source span is outside the source snapshot.");
        var encoding = ResolveEncoding(source.EncodingName);
        var bytes = source.Bytes.Span.Slice(span.Start, span.Length);
        try
        {
            return encoding.GetString(bytes);
        }
        catch (DecoderFallbackException)
        {
            const int maximumPreviewBytes = 32;
            var previewLength = Math.Min(bytes.Length, maximumPreviewBytes);
            var preview = Convert.ToHexString(bytes[..previewLength]);
            var suffix = previewLength < bytes.Length ? "..." : string.Empty;
            return $"<undecodable {bytes.Length} bytes: {preview}{suffix}>";
        }
    }

    public static byte[] EncodeText(QuestSourceSnapshot source, string text) =>
        ResolveEncoding(source.EncodingName).GetBytes(text);

    private static async Task<(QuestGraphSidecarV1? Sidecar, bool Preserve, string? Warning)>
        TryLoadSidecarAsync(string path, CancellationToken cancellationToken)
    {
        if (!File.Exists(path))
            return (null, false, null);
        try
        {
            var sidecar = await QuestGraphSidecarService.LoadFileAsync(path, cancellationToken)
                .ConfigureAwait(false);
            return (sidecar, false, null);
        }
        catch (Exception exception) when (
            exception is InvalidDataException or JsonException or IOException or UnauthorizedAccessException)
        {
            return (
                null,
                true,
                $"The existing layout sidecar is invalid or from a future schema and will be preserved: {exception.Message}");
        }
    }

    private static async Task<byte[]> ReadBoundedAsync(
        string path,
        CancellationToken cancellationToken)
    {
        var info = new FileInfo(path);
        if (!info.Exists)
            throw new FileNotFoundException("PXSL source was not found.", path);
        if (info.Length > MaximumDocumentBytes)
            throw new InvalidDataException("PXSL source exceeds the 8 MiB document cap.");
        await using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            81920,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        var bytes = new byte[checked((int)stream.Length)];
        var offset = 0;
        while (offset < bytes.Length)
        {
            var read = await stream.ReadAsync(bytes.AsMemory(offset), cancellationToken)
                .ConfigureAwait(false);
            if (read == 0)
                throw new EndOfStreamException("PXSL source ended before its reported length.");
            offset += read;
        }
        return bytes;
    }

    private static void ValidateSnapshotMessage(OpenDocumentMessage message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(message.DocumentId);
        ArgumentNullException.ThrowIfNull(message.RelativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(message.EncodingName);
        ArgumentNullException.ThrowIfNull(message.Bytes);
        if (message.Revision < 0 || message.Bytes.Length > MaximumDocumentBytes ||
            message.Sha256.Length != 64)
            throw new InvalidDataException("The connected PXSL snapshot is invalid.");
        _ = NormalizeRelativePath(message.RelativePath);
    }

    private static string? ResolveConnectedPath(
        string? projectRoot,
        string relativePath,
        string? advertisedDiskPath)
    {
        var normalized = NormalizeRelativePath(relativePath);
        if (!string.IsNullOrWhiteSpace(projectRoot))
        {
            var root = Path.GetFullPath(projectRoot);
            var combined = Path.GetFullPath(Path.Combine(root, normalized));
            if (!IsWithin(root, combined))
                throw new InvalidDataException("Connected PXSL path escapes the active project root.");
            SafeProjectPath.RequireContainedNonReparsePath(root, combined);
            if (!string.IsNullOrWhiteSpace(advertisedDiskPath) &&
                !string.Equals(combined, Path.GetFullPath(advertisedDiskPath), StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Connected PXSL paths disagree.");
            return combined;
        }
        return string.IsNullOrWhiteSpace(advertisedDiskPath)
            ? null
            : Path.GetFullPath(advertisedDiskPath);
    }

    private static bool IsWithin(string root, string candidate)
    {
        root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        candidate = Path.GetFullPath(candidate);
        var rootWithSeparator = Path.EndsInDirectorySeparator(root)
            ? root
            : root + Path.DirectorySeparatorChar;
        return candidate.StartsWith(rootWithSeparator, StringComparison.OrdinalIgnoreCase);
    }

    private static string NormalizeRelativePath(string relativePath)
    {
        if (Path.IsPathRooted(relativePath))
            throw new InvalidDataException("Connected PXSL identity must be project-relative.");
        var parts = relativePath.Replace('\\', '/').Split(
            '/',
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (parts.Length == 0 || parts.Contains("..", StringComparer.Ordinal) ||
            parts.Contains(".", StringComparer.Ordinal))
            throw new InvalidDataException("Connected PXSL identity is invalid.");
        return Path.Combine(parts);
    }

    private static string DetectEncoding(ReadOnlySpan<byte> bytes)
    {
        if (bytes.StartsWith(Encoding.UTF8.GetPreamble()))
            return "utf-8";
        if (bytes.StartsWith(Encoding.Unicode.GetPreamble()))
            return "utf-16";
        if (bytes.StartsWith(Encoding.BigEndianUnicode.GetPreamble()))
            return "utf-16BE";
        try
        {
            _ = new UTF8Encoding(false, true).GetCharCount(bytes);
            return "utf-8";
        }
        catch (DecoderFallbackException)
        {
            return "windows-1252";
        }
    }

    private static Encoding ResolveEncoding(string name) => Encoding.GetEncoding(
        name,
        EncoderFallback.ExceptionFallback,
        DecoderFallback.ExceptionFallback);
}
