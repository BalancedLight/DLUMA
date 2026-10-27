using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed record QuestPaletteItemViewModel(
    string Name,
    string Category,
    string InsertionText,
    string Description,
    QuestCatalogEntry? CatalogEntry = null,
    string HeaderColor = QuestPaletteColors.Default);

public sealed record QuestVariableViewModel(
    string Name,
    string Value,
    int Line,
    string NodeId);

public sealed record QuestProblemViewModel(
    string Severity,
    string Code,
    string Message,
    int? Line,
    int? Column,
    int? ByteOffset);
