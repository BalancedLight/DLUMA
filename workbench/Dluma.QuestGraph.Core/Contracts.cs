using System.Security.Cryptography;
using System.Text;

namespace Dluma.QuestGraph.Core;

public enum QuestLineEnding
{
    None,
    Lf,
    CrLf,
    Cr,
    Mixed
}

public sealed class QuestSourceSnapshot
{
    private readonly byte[] _bytes;

    public QuestSourceSnapshot(
        string documentId,
        string relativePath,
        long revision,
        ReadOnlyMemory<byte> bytes,
        string encodingName = "utf-8")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(documentId);
        ArgumentNullException.ThrowIfNull(relativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(encodingName);
        if (revision < 0)
            throw new ArgumentOutOfRangeException(nameof(revision));

        DocumentId = documentId;
        RelativePath = relativePath;
        Revision = revision;
        _bytes = bytes.ToArray();
        EncodingName = encodingName;
        Sha256 = QuestHash.Sha256(_bytes);

        var encoding = QuestEncoding.Resolve(encodingName);
        var preamble = encoding.GetPreamble();
        HasBom = preamble.Length > 0 && _bytes.AsSpan().StartsWith(preamble);
        LineEnding = QuestEncoding.DetectLineEnding(_bytes, encoding, HasBom);
    }

    public string DocumentId { get; }
    public string RelativePath { get; }
    public long Revision { get; }
    public ReadOnlyMemory<byte> Bytes => _bytes;
    public string EncodingName { get; }
    public bool HasBom { get; }
    public QuestLineEnding LineEnding { get; }
    public string Sha256 { get; }

    public byte[] ToArray() => (byte[])_bytes.Clone();

    internal QuestSourceSnapshot Next(ReadOnlyMemory<byte> bytes) =>
        new(DocumentId, RelativePath, checked(Revision + 1), bytes, EncodingName);
}

public sealed record QuestTextEdit(
    long ExpectedRevision,
    string ExpectedSha256,
    int ByteOffset,
    int RemovedLength,
    ReadOnlyMemory<byte> ReplacementBytes);

public sealed record QuestEditResult(
    QuestSourceSnapshot Source,
    IReadOnlyList<QuestTextEdit> AppliedEdits);

public enum QuestEditRejection
{
    StaleRevision,
    StaleHash,
    InvalidRange,
    OverlappingRanges
}

public sealed class QuestEditRejectedException : InvalidOperationException
{
    public QuestEditRejectedException(QuestEditRejection reason, string message)
        : base(message) => Reason = reason;

    public QuestEditRejection Reason { get; }
}

public readonly record struct PxslTextSpan(int Start, int Length, int Line, int Column)
{
    public int End => checked(Start + Length);
}

public enum QuestDiagnosticSeverity
{
    Info,
    Warning,
    Error
}

public sealed record QuestDiagnostic(
    string Code,
    QuestDiagnosticSeverity Severity,
    string Message,
    PxslTextSpan? Span = null);

public enum PxslSyntaxKind
{
    Blank,
    Comment,
    RawXml,
    Directive,
    Quest,
    Flow,
    Macro,
    Opaque
}

public sealed record PxslSyntaxNode(
    PxslSyntaxKind Kind,
    string Text,
    string Name,
    IReadOnlyList<string> Arguments,
    int Indentation,
    PxslTextSpan Span,
    IReadOnlyList<PxslSyntaxNode> Children,
    bool IsMalformed = false)
{
    public IReadOnlyList<PxslArgumentToken> ArgumentTokens { get; init; } = Array.Empty<PxslArgumentToken>();
    public PxslTextSpan? NameSpan { get; init; }
}

public sealed class PxslSyntaxDocument
{
    public PxslSyntaxDocument(
        QuestSourceSnapshot source,
        IReadOnlyList<PxslSyntaxNode> roots,
        IReadOnlyList<QuestDiagnostic> diagnostics)
    {
        Source = source;
        Roots = roots;
        Diagnostics = diagnostics;
    }

    public QuestSourceSnapshot Source { get; }
    public IReadOnlyList<PxslSyntaxNode> Roots { get; }
    public IReadOnlyList<QuestDiagnostic> Diagnostics { get; }

    public byte[] ToBytes() => Source.ToArray();
}

public enum QuestSemanticKind
{
    Directive,
    Quest,
    OrderedFlow,
    Branch,
    Macro,
    RawXml,
    Opaque
}

public sealed record QuestSemanticNode(
    string Id,
    QuestSemanticKind Kind,
    string Name,
    IReadOnlyList<string> Arguments,
    PxslTextSpan Span,
    string? ParentId,
    int Order,
    string SemanticFingerprint,
    string ContainerFingerprint,
    string SourceTextSha256,
    bool IsOpaque)
{
    public IReadOnlyList<PxslArgumentToken> ArgumentTokens { get; init; } = Array.Empty<PxslArgumentToken>();
    public PxslTextSpan? NameSpan { get; init; }
}

public enum QuestGraphPortDirection
{
    Input,
    Output
}

public sealed record QuestGraphPort(
    string Id,
    string DisplayName,
    QuestGraphPortDirection Direction,
    string Type,
    bool AllowsMultiple);

public sealed record QuestGraphNode(
    string Id,
    string SemanticNodeId,
    QuestSemanticKind Kind,
    string Title,
    bool IsOpaque,
    int Order,
    IReadOnlyList<QuestGraphPort> Ports)
{
    public bool IsDetached { get; init; }
}

public enum QuestGraphEdgeKind
{
    Ordered,
    Branch,
    Nested
}

public sealed record QuestGraphEdge(
    string Id,
    string SourceNodeId,
    string SourcePortId,
    string TargetNodeId,
    string TargetPortId,
    QuestGraphEdgeKind Kind,
    int Order);

public sealed record QuestGraphDocument(
    QuestSourceSnapshot Source,
    PxslSyntaxDocument Syntax,
    IReadOnlyList<QuestSemanticNode> SemanticNodes,
    IReadOnlyList<QuestGraphNode> GraphNodes,
    IReadOnlyList<QuestGraphEdge> GraphEdges,
    IReadOnlyList<QuestDiagnostic> Diagnostics)
{
    public QuestCatalog? Catalog { get; init; }
}

public enum QuestCatalogEntryKind
{
    Macro,
    Phase,
    QuestObject,
    Branch
}

public sealed record QuestCatalogParameter(
    string Name,
    string Type,
    bool IsRequired,
    int? Position,
    IReadOnlyList<string> Values);

public sealed record QuestCatalogChild(
    string Name,
    string Kind,
    string Quantity);

public sealed record QuestCatalogEntry(
    string Name,
    QuestCatalogEntryKind Kind,
    IReadOnlyList<QuestCatalogParameter> Parameters,
    IReadOnlyList<QuestCatalogChild> AllowedChildren,
    IReadOnlyList<string> Sources)
{
    public bool IsInsertable { get; init; }
    public bool IsCompileEligible { get; init; }
    public QuestCatalogFunctionalCategory Category { get; init; }
    public string DefinitionSection { get; init; } = string.Empty;
    public string? ResolvedPhaseType { get; init; }
    public string? AliasTarget { get; init; }
    public QuestCatalogStyle? Style { get; init; }
    public IReadOnlyList<string> DescriptionLines { get; init; } = Array.Empty<string>();
    public string Template { get; init; } = string.Empty;
    public string DirectiveTemplate { get; init; } = string.Empty;
    public IReadOnlyList<string> Aliases { get; init; } = Array.Empty<string>();
    public IReadOnlyList<QuestCatalogChildSlot> ChildSlots { get; init; } = Array.Empty<QuestCatalogChildSlot>();
    public IReadOnlyList<QuestInspectorFieldDefinition> InspectorFields { get; init; } = Array.Empty<QuestInspectorFieldDefinition>();
    public IReadOnlyList<string> ParameterNames => Parameters.Select(parameter => parameter.Name).ToArray();
    public IReadOnlyList<string> PathSlotNames => ChildSlots.Where(slot => slot.Kind == QuestCatalogChildSlotKind.Branch).Select(slot => slot.Id).ToArray();
}

public enum QuestCatalogFormat
{
    Auto,
    Edf,
    QuestEditorConfig,
    PhaseDefinitionsXml,
    QuestObjectDefinitionsXml,
    QuestColorsXml
}

public sealed record QuestCatalogInput(
    string Name,
    QuestCatalogFormat Format,
    Stream Content);

public sealed record QuestCatalogProvenance(
    string Name,
    QuestCatalogFormat Format,
    string Sha256);

public sealed record QuestContextObject(
    string Name,
    string ClassName,
    IReadOnlyList<string> Roles);

public sealed record QuestContextText(string Id, string Value);

public sealed record QuestContextSnapshot(
    long Generation,
    string ProjectIdentity,
    string MapIdentity,
    IReadOnlyList<QuestContextObject> Objects,
    IReadOnlyList<QuestContextText> Texts,
    IReadOnlyList<string> Speakers,
    IReadOnlyList<string> DialogueGroups,
    IReadOnlyList<string> QuestReferences);

public sealed record QuestCompileReceipt(
    bool SaveAttempted,
    bool SaveSucceeded,
    bool StockCommandDispatched,
    bool StockCommandCompleted,
    bool DiagnosticsAvailable,
    IReadOnlyList<QuestDiagnostic> Diagnostics,
    string? OutputSha256,
    DateTimeOffset? OutputLastWriteUtc,
    bool CustomSidecarPublished,
    string Message);

internal static class QuestHash
{
    public static string Sha256(ReadOnlySpan<byte> bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes));

    public static string Sha256(string value) => Sha256(Encoding.UTF8.GetBytes(value));
}

public static class QuestEncoding
{
    static QuestEncoding() => Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

    public static Encoding Resolve(string name) => Encoding.GetEncoding(
        name,
        EncoderFallback.ExceptionFallback,
        DecoderFallback.ExceptionFallback);

    public static QuestLineEnding DetectLineEnding(
        ReadOnlySpan<byte> bytes,
        Encoding encoding,
        bool hasBom)
    {
        try
        {
            var preambleLength = hasBom ? encoding.GetPreamble().Length : 0;
            var text = encoding.GetString(bytes[preambleLength..]);
            var crlf = 0;
            var lf = 0;
            var cr = 0;
            for (var index = 0; index < text.Length; ++index)
            {
                if (text[index] == '\r')
                {
                    if (index + 1 < text.Length && text[index + 1] == '\n')
                    {
                        crlf++;
                        index++;
                    }
                    else
                    {
                        cr++;
                    }
                }
                else if (text[index] == '\n')
                {
                    lf++;
                }
            }

            var kinds = (crlf > 0 ? 1 : 0) + (lf > 0 ? 1 : 0) + (cr > 0 ? 1 : 0);
            if (kinds == 0)
                return QuestLineEnding.None;
            if (kinds > 1)
                return QuestLineEnding.Mixed;
            if (crlf > 0)
                return QuestLineEnding.CrLf;
            return lf > 0 ? QuestLineEnding.Lf : QuestLineEnding.Cr;
        }
        catch (DecoderFallbackException)
        {
            return QuestLineEnding.None;
        }
    }
}
