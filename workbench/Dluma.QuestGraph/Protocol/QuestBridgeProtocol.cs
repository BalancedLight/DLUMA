using System.Text.Json;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.Protocol;

public static class QuestBridgeProtocol
{
    public const int SchemaVersion = 2;
    public const int MaximumFrameBytes = 16 * 1024 * 1024;
    public const int MaximumDocumentBytes = 8 * 1024 * 1024;
    public const int MaximumCollectionItems = 100_000;
    public const int MaximumStringCharacters = 8 * 1024 * 1024;
}

public sealed record QuestBridgeEnvelope(
    int SchemaVersion,
    string Type,
    string RequestId,
    string SessionToken,
    JsonElement Payload);

public sealed record HelloRequest(
    int ProtocolVersion,
    int ClientProcessId,
    int EditorProcessId,
    string ExecutableSha256);

public sealed record HelloResult(
    int ProtocolVersion,
    int EditorProcessId,
    string HostBuildId,
    string? ProjectRoot,
    IReadOnlyList<string> InstalledCatalogRoots,
    long ContextGeneration);

public sealed record OpenDocumentMessage(
    string DocumentId,
    string RelativePath,
    long Revision,
    string EncodingName,
    byte[] Bytes,
    string Sha256,
    [property: System.Text.Json.Serialization.JsonRequired] bool WasModified,
    string? DiskPath = null);

public sealed record SourceChangedMessage(
    string DocumentId,
    long Revision,
    string EncodingName,
    byte[] Bytes,
    string Sha256);

public sealed record SaveDocumentRequest(
    string DocumentId,
    long ExpectedRevision,
    string ExpectedSha256,
    string SourceSha256);

public sealed record SaveDocumentResult(
    bool SaveAttempted,
    bool SaveSucceeded,
    bool Cancelled,
    long Revision,
    string Sha256,
    string? DiskPath,
    string Message);

public sealed record CompileDocumentRequest(
    string DocumentId,
    long ExpectedRevision,
    string ExpectedSha256,
    string SourceSha256);

public sealed record CompileDocumentResult(QuestCompileReceipt Receipt);

public sealed record ApplyEditsRequest(
    string DocumentId,
    long ExpectedRevision,
    string ExpectedSha256,
    string ExpectedResultSha256,
    IReadOnlyList<QuestBridgeTextEdit> Edits);

public sealed record QuestBridgeTextEdit(
    int ByteOffset,
    int RemovedLength,
    byte[] ReplacementBytes);

public sealed record ApplyEditsResult(
    bool Applied,
    long Revision,
    string Sha256,
    string Message);

public sealed record GetContextRequest(long? ExpectedGeneration);

public sealed record ContextSnapshotMessage(QuestContextSnapshot Context);

public sealed record ActivateDocumentRequest(string DocumentId, int? ByteOffset);

public sealed record CloseDocumentRequest(string DocumentId);

public sealed record QuestBridgeResult(bool Success, string Message);

public sealed record QuestBridgeError(
    string Code,
    string Message,
    bool Retryable);

public sealed class QuestBridgeException : IOException
{
    public QuestBridgeException(string code, string message, bool retryable)
        : base(message)
    {
        Code = code;
        Retryable = retryable;
    }

    public string Code { get; }
    public bool Retryable { get; }
}
