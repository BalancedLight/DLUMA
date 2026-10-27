namespace Dluma.QuestGraph.Core;

public enum QuestParameterEditorKind
{
    Text,
    Boolean,
    Number,
    Enum,
    TextId,
    Literal,
    Speaker,
    DialogueGroup,
    QuestReference,
    MapObjectClass,
    MapObjectName,
    Scenario
}

public enum QuestInspectorFieldRole
{
    Parameter,
    PhaseName,
    NodeName
}

public sealed record QuestInspectorFieldDefinition(
    string Id,
    string ParameterName,
    string DisplayName,
    QuestParameterEditorKind EditorKind,
    QuestInspectorFieldRole Role,
    string TypeName,
    bool IsRequired,
    int? Position,
    string? NamedArgument,
    IReadOnlyList<string> AllowedValues,
    string? PairedFieldId,
    string Description);

public sealed record PxslArgumentToken(
    string RawText,
    string Value,
    string? NamedArgument,
    int? Position,
    PxslTextSpan Span,
    PxslTextSpan ValueSpan,
    bool IsNamed,
    bool IsPlaceholder,
    bool IsWrapped);

public sealed record QuestInspectorFieldValue(
    QuestInspectorFieldDefinition Definition,
    string Value,
    string RawText,
    bool IsPresent,
    bool IsPlaceholder,
    bool IsWrapped,
    PxslTextSpan? Span);

public sealed record QuestInspectorSnapshot(
    string SemanticNodeId,
    QuestCatalogEntry? Entry,
    string Title,
    string Summary,
    string RawPxsl,
    IReadOnlyList<QuestInspectorFieldValue> Fields,
    IReadOnlyList<PxslArgumentToken> UnknownArguments);
