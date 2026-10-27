namespace Dluma.QuestGraph.Protocol;

public sealed record RestoreDocumentRequest(
    string DocumentId,
    byte[] BaselineBytes,
    string BaselineSha256,
    bool WasModified);

public sealed record RestoreDocumentResult(
    bool Restored,
    long Revision,
    string Sha256,
    string Message);
