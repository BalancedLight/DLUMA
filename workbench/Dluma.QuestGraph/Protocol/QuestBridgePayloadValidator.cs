using System.Security.Cryptography;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.Protocol;

public static class QuestBridgePayloadValidator
{
    public static void Validate<T>(T payload)
    {
        ArgumentNullException.ThrowIfNull(payload);
        switch (payload)
        {
            case HelloRequest value:
                if (value.ProtocolVersion != QuestBridgeProtocol.SchemaVersion)
                    throw new InvalidDataException("Bridge protocol version is unsupported.");
                RequirePositive(value.ClientProcessId, nameof(value.ClientProcessId));
                RequirePositive(value.EditorProcessId, nameof(value.EditorProcessId));
                RequireHash(value.ExecutableSha256, nameof(value.ExecutableSha256));
                break;
            case HelloResult value:
                if (value.ProtocolVersion != QuestBridgeProtocol.SchemaVersion)
                    throw new InvalidDataException("Bridge protocol version is unsupported.");
                RequirePositive(value.EditorProcessId, nameof(value.EditorProcessId));
                RequireString(value.HostBuildId, nameof(value.HostBuildId), 256);
                OptionalString(value.ProjectRoot, nameof(value.ProjectRoot));
                if (value.ProjectRoot is not null &&
                    !Path.IsPathRooted(value.ProjectRoot))
                    throw new InvalidDataException("Bridge projectRoot must be absolute runtime data.");
                var rootCount = 0;
                RequireCollection(
                    value.InstalledCatalogRoots,
                    ref rootCount,
                    nameof(value.InstalledCatalogRoots));
                if (value.InstalledCatalogRoots.Count > 16)
                    throw new InvalidDataException("Bridge installed catalog roots exceed the root cap.");
                foreach (var root in value.InstalledCatalogRoots)
                {
                    RequireString(
                        root,
                        nameof(value.InstalledCatalogRoots),
                        4_096);
                    if (!Path.IsPathRooted(root))
                        throw new InvalidDataException("Bridge installed catalog roots must be absolute runtime data.");
                }
                RequireNonNegative(value.ContextGeneration, nameof(value.ContextGeneration));
                break;
            case OpenDocumentMessage value:
                ValidateDocumentIdentity(
                    value.DocumentId,
                    value.RelativePath,
                    value.Revision,
                    value.EncodingName,
                    value.Bytes,
                    value.Sha256);
                OptionalAbsolutePath(value.DiskPath, nameof(value.DiskPath));
                break;
            case OpenDocumentWireMessage value:
                ValidateDocumentIdentity(
                    value.DocumentId,
                    value.RelativePath,
                    value.Revision,
                    value.EncodingName,
                    value.Bytes,
                    value.Sha256);
                RequireString(value.ProjectRoot, nameof(value.ProjectRoot));
                if (!Path.IsPathRooted(value.ProjectRoot))
                    throw new InvalidDataException("Bridge projectRoot must be absolute runtime data.");
                break;
            case SourceChangedMessage value:
                ValidateDocumentIdentity(
                    value.DocumentId,
                    "sourceChanged",
                    value.Revision,
                    value.EncodingName,
                    value.Bytes,
                    value.Sha256);
                break;
            case SaveDocumentRequest value:
                ValidateSourceRequest(
                    value.DocumentId,
                    value.ExpectedRevision,
                    value.ExpectedSha256,
                    value.SourceSha256);
                break;
            case SaveDocumentWireRequest value:
                ValidateSourceRequest(
                    value.DocumentId,
                    value.ExpectedRevision,
                    value.ExpectedSha256,
                    value.SourceSha256);
                break;
            case CompileDocumentRequest value:
                ValidateSourceRequest(
                    value.DocumentId,
                    value.ExpectedRevision,
                    value.ExpectedSha256,
                    value.SourceSha256);
                break;
            case CompileDocumentWireRequest value:
                ValidateSourceRequest(
                    value.DocumentId,
                    value.ExpectedRevision,
                    value.ExpectedSha256,
                    value.SourceSha256);
                break;
            case SaveDocumentResult value:
                RequireNonNegative(value.Revision, nameof(value.Revision));
                if (value.SaveSucceeded && !value.SaveAttempted)
                    throw new InvalidDataException("A successful bridge save must have been attempted.");
                if (value.SaveSucceeded && value.Cancelled)
                    throw new InvalidDataException("A bridge save cannot both succeed and be cancelled.");
                if (value.SaveSucceeded)
                    RequireHash(value.Sha256, nameof(value.Sha256));
                else
                    OptionalString(value.Sha256, nameof(value.Sha256));
                OptionalString(value.DiskPath, nameof(value.DiskPath));
                if (value.SaveSucceeded)
                    OptionalAbsolutePath(value.DiskPath, nameof(value.DiskPath), required: true);
                RequireString(value.Message, nameof(value.Message));
                break;
            case CompileDocumentResult value:
                ValidateCompileReceipt(value.Receipt);
                break;
            case ApplyEditsResult value:
                RequireNonNegative(value.Revision, nameof(value.Revision));
                if (value.Applied)
                    RequireHash(value.Sha256, nameof(value.Sha256));
                else
                    OptionalString(value.Sha256, nameof(value.Sha256));
                RequireString(value.Message, nameof(value.Message));
                break;
            case RestoreDocumentRequest value:
                RequireHash(value.DocumentId, nameof(value.DocumentId));
                ArgumentNullException.ThrowIfNull(value.BaselineBytes);
                if (value.BaselineBytes.Length > QuestBridgeProtocol.MaximumDocumentBytes)
                    throw new InvalidDataException("Bridge restore baseline exceeds the document cap.");
                RequireHash(value.BaselineSha256, nameof(value.BaselineSha256));
                var baselineHash = Convert.ToHexString(SHA256.HashData(value.BaselineBytes));
                if (!string.Equals(baselineHash, value.BaselineSha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Bridge restore baseline does not match its SHA-256 identity.");
                break;
            case RestoreDocumentResult value:
                RequireNonNegative(value.Revision, nameof(value.Revision));
                if (value.Restored)
                    RequireHash(value.Sha256, nameof(value.Sha256));
                else
                    OptionalString(value.Sha256, nameof(value.Sha256));
                RequireString(value.Message, nameof(value.Message));
                break;
            case ApplyEditsRequest value:
                RequireString(value.DocumentId, nameof(value.DocumentId), 256);
                RequireNonNegative(value.ExpectedRevision, nameof(value.ExpectedRevision));
                RequireHash(value.ExpectedSha256, nameof(value.ExpectedSha256));
                RequireHash(value.ExpectedResultSha256, nameof(value.ExpectedResultSha256));
                var editCount = 0;
                RequireCollection(value.Edits, ref editCount, nameof(value.Edits));
                if (value.Edits.Count == 0 || value.Edits.Count > 4_096)
                    throw new InvalidDataException("Bridge edit collection has an invalid count.");
                long previousOffset = QuestBridgeProtocol.MaximumDocumentBytes;
                long replacementTotal = 0;
                foreach (var edit in value.Edits)
                {
                    if (edit.ByteOffset < 0 || edit.RemovedLength < 0)
                        throw new InvalidDataException("Bridge edit ranges cannot be negative.");
                    var editEnd = checked(
                        (long)edit.ByteOffset + edit.RemovedLength);
                    if (editEnd > QuestBridgeProtocol.MaximumDocumentBytes ||
                        editEnd > previousOffset)
                        throw new InvalidDataException(
                            "Bridge edits must be bounded, non-overlapping, and descending.");
                    ArgumentNullException.ThrowIfNull(edit.ReplacementBytes);
                    if (edit.ReplacementBytes.Length > QuestBridgeProtocol.MaximumDocumentBytes)
                        throw new InvalidDataException("Bridge edit replacement exceeds the document cap.");
                    replacementTotal = checked(
                        replacementTotal + edit.ReplacementBytes.Length);
                    if (replacementTotal > QuestBridgeProtocol.MaximumDocumentBytes)
                        throw new InvalidDataException(
                            "Bridge edit replacements exceed the document cap.");
                    previousOffset = edit.ByteOffset;
                }
                break;
            case GetContextRequest value:
                if (value.ExpectedGeneration is < 0)
                    throw new InvalidDataException("Bridge context generation cannot be negative.");
                break;
            case ContextSnapshotMessage value:
                ValidateContext(value.Context);
                break;
            case ActivateDocumentRequest value:
                RequireHash(value.DocumentId, nameof(value.DocumentId));
                if (value.ByteOffset is < 0)
                    throw new InvalidDataException("Bridge byteOffset cannot be negative.");
                break;
            case CloseDocumentRequest value:
                RequireHash(value.DocumentId, nameof(value.DocumentId));
                break;
            case QuestBridgeResult value:
                RequireString(value.Message, nameof(value.Message));
                break;
            case QuestBridgeError value:
                RequireString(value.Code, nameof(value.Code), 128);
                RequireString(value.Message, nameof(value.Message));
                break;
            default:
                throw new InvalidDataException(
                    $"Bridge payload type '{payload.GetType().Name}' is unsupported.");
        }
    }

    private static void ValidateDocumentIdentity(
        string documentId,
        string relativePath,
        long revision,
        string encodingName,
        byte[] bytes,
        string sha256)
    {
        RequireHash(documentId, nameof(documentId));
        RequireString(relativePath, nameof(relativePath), 4_096);
        ValidateRelativePath(relativePath);
        RequireNonNegative(revision, nameof(revision));
        RequireString(encodingName, nameof(encodingName), 128);
        try
        {
            _ = QuestEncoding.Resolve(encodingName);
        }
        catch (ArgumentException exception)
        {
            throw new InvalidDataException(
                "Bridge source encoding is unsupported.",
                exception);
        }
        ArgumentNullException.ThrowIfNull(bytes);
        if (bytes.Length > QuestBridgeProtocol.MaximumDocumentBytes)
            throw new InvalidDataException("Bridge source bytes exceed the 8 MiB document cap.");
        RequireHash(sha256, nameof(sha256));
        var actualHash = Convert.ToHexString(SHA256.HashData(bytes));
        if (!string.Equals(actualHash, sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(
                "Bridge source bytes do not match their SHA-256 identity.");
    }

    private static void ValidateSourceRequest(
        string documentId,
        long expectedRevision,
        string expectedSha256,
        string sourceSha256)
    {
        RequireHash(documentId, nameof(documentId));
        RequireNonNegative(expectedRevision, nameof(expectedRevision));
        RequireHash(expectedSha256, nameof(expectedSha256));
        RequireHash(sourceSha256, nameof(sourceSha256));
    }

    private static void ValidateRelativePath(string value)
    {
        if (Path.IsPathRooted(value) ||
            value.IndexOfAny(['\0', ':']) >= 0 ||
            value.Split(['/', '\\'], StringSplitOptions.RemoveEmptyEntries)
                .Any(part => part is "." or ".."))
            throw new InvalidDataException(
                "Bridge relativePath must remain inside the admitted project.");
    }

    private static void OptionalAbsolutePath(
        string? value,
        string name,
        bool required = false)
    {
        if (value is null)
        {
            if (required)
                throw new InvalidDataException(
                    $"Bridge path '{name}' is required.");
            return;
        }
        RequireString(value, name, 4_096);
        if (!Path.IsPathRooted(value))
            throw new InvalidDataException(
                $"Bridge path '{name}' must be absolute runtime data.");
    }

    private static void ValidateContext(QuestContextSnapshot context)
    {
        ArgumentNullException.ThrowIfNull(context);
        RequireNonNegative(context.Generation, nameof(context.Generation));
        RequireString(context.ProjectIdentity, nameof(context.ProjectIdentity), 4_096);
        OptionalString(context.MapIdentity, nameof(context.MapIdentity), 4_096);
        var total = 0;
        RequireCollection(context.Objects, ref total, nameof(context.Objects));
        foreach (var item in context.Objects)
        {
            RequireString(item.Name, nameof(item.Name), 4_096);
            RequireString(item.ClassName, nameof(item.ClassName), 4_096);
            RequireCollection(item.Roles, ref total, nameof(item.Roles));
            foreach (var role in item.Roles)
                RequireString(role, nameof(role), 4_096);
        }
        RequireCollection(context.Texts, ref total, nameof(context.Texts));
        foreach (var item in context.Texts)
        {
            RequireString(item.Id, nameof(item.Id), 4_096);
            RequireString(item.Value, nameof(item.Value), allowEmpty: true);
        }
        ValidateStrings(context.Speakers, ref total, nameof(context.Speakers));
        ValidateStrings(context.DialogueGroups, ref total, nameof(context.DialogueGroups));
        ValidateStrings(context.QuestReferences, ref total, nameof(context.QuestReferences));
    }

    private static void ValidateCompileReceipt(QuestCompileReceipt receipt)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        var total = 0;
        RequireCollection(receipt.Diagnostics, ref total, nameof(receipt.Diagnostics));
        foreach (var diagnostic in receipt.Diagnostics)
        {
            RequireString(diagnostic.Code, nameof(diagnostic.Code), 128);
            RequireString(diagnostic.Message, nameof(diagnostic.Message));
            if (!Enum.IsDefined(diagnostic.Severity))
                throw new InvalidDataException("Bridge diagnostic severity is invalid.");
            if (diagnostic.Span is { } span &&
                (span.Start < 0 || span.Length < 0 || span.Line < 1 || span.Column < 1))
                throw new InvalidDataException("Bridge diagnostic span is invalid.");
        }
        if (receipt.OutputSha256 is not null)
            RequireHash(receipt.OutputSha256, nameof(receipt.OutputSha256));
        RequireString(receipt.Message, nameof(receipt.Message));
    }

    private static void ValidateStrings(
        IReadOnlyList<string> values,
        ref int total,
        string name)
    {
        RequireCollection(values, ref total, name);
        foreach (var value in values)
            RequireString(value, name, 4_096);
    }

    private static void RequireCollection<T>(
        IReadOnlyList<T> values,
        ref int total,
        string name)
    {
        ArgumentNullException.ThrowIfNull(values);
        total = checked(total + values.Count);
        if (values.Count > QuestBridgeProtocol.MaximumCollectionItems ||
            total > QuestBridgeProtocol.MaximumCollectionItems)
            throw new InvalidDataException($"Bridge collection '{name}' exceeds the item cap.");
    }

    private static void RequireString(
        string value,
        string name,
        int maximum = QuestBridgeProtocol.MaximumStringCharacters,
        bool allowEmpty = false)
    {
        ArgumentNullException.ThrowIfNull(value);
        if ((!allowEmpty && value.Length == 0) || value.Length > maximum)
            throw new InvalidDataException($"Bridge string '{name}' has an invalid length.");
    }

    private static void OptionalString(
        string? value,
        string name,
        int maximum = QuestBridgeProtocol.MaximumStringCharacters)
    {
        if (value is not null && value.Length > maximum)
            throw new InvalidDataException($"Bridge string '{name}' exceeds the length cap.");
    }

    private static void RequireHash(string value, string name)
    {
        RequireString(value, name, 64);
        if (value.Length != 64 || value.Any(character => !Uri.IsHexDigit(character)))
            throw new InvalidDataException($"Bridge hash '{name}' is not SHA-256 hex.");
    }

    private static void RequireNonNegative(long value, string name)
    {
        if (value < 0)
            throw new InvalidDataException($"Bridge value '{name}' cannot be negative.");
    }

    private static void RequirePositive(int value, string name)
    {
        if (value <= 0)
            throw new InvalidDataException($"Bridge value '{name}' must be positive.");
    }
}
