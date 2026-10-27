namespace Dluma.QuestGraph.Protocol;

// These wire-only records keep runtime path data out of the persisted graph model.
public sealed record OpenDocumentWireMessage(
    string DocumentId,
    string ProjectRoot,
    string RelativePath,
    long Revision,
    string EncodingName,
    byte[] Bytes,
    string Sha256,
    [property: System.Text.Json.Serialization.JsonRequired] bool WasModified);

public sealed record SaveDocumentWireRequest(
    string DocumentId,
    long ExpectedRevision,
    string ExpectedSha256,
    string SourceSha256);

public sealed record CompileDocumentWireRequest(
    string DocumentId,
    long ExpectedRevision,
    string ExpectedSha256,
    string SourceSha256);
