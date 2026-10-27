using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class MainViewModel
{
    internal IReadOnlyList<string> ResolveInspectorOptions(
        QuestParameterEditorKind editorKind,
        string? selectedObjectClass)
    {
        if (editorKind == QuestParameterEditorKind.Scenario)
            return ResolveScenarioOptions(selectedObjectClass);

        IEnumerable<string> values = editorKind switch
        {
            QuestParameterEditorKind.TextId => ContextTexts.Select(item => item.Id),
            QuestParameterEditorKind.Speaker => Speakers,
            QuestParameterEditorKind.DialogueGroup => DialogueGroups,
            QuestParameterEditorKind.QuestReference => QuestReferences,
            QuestParameterEditorKind.MapObjectClass => ContextObjects.Select(item => item.ClassName),
            QuestParameterEditorKind.MapObjectName => ContextObjects
                .Where(item => string.IsNullOrWhiteSpace(selectedObjectClass) ||
                    string.Equals(
                        item.ClassName,
                        selectedObjectClass,
                        StringComparison.OrdinalIgnoreCase))
                .Select(item => item.Name),
            _ => Array.Empty<string>()
        };

        return values
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(value => value, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private IReadOnlyList<string> ResolveScenarioOptions(string? selectedObjectClass)
    {
        if (!string.IsNullOrWhiteSpace(selectedObjectClass))
        {
            return ContextObjects
                .Where(item => string.Equals(
                    item.ClassName,
                    selectedObjectClass,
                    StringComparison.OrdinalIgnoreCase))
                .Select(item => item.Name)
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
                .ToArray();
        }

        return ContextObjects
            .Select(item => new
            {
                item.Name,
                Rank = item.ClassName.Equals(
                        "InteractiveMovieScenario",
                        StringComparison.OrdinalIgnoreCase)
                    ? 0
                    : item.ClassName.Contains(
                        "InteractiveMovieScenario",
                        StringComparison.OrdinalIgnoreCase)
                        ? 1
                        : int.MaxValue
            })
            .Where(item => item.Rank != int.MaxValue && !string.IsNullOrWhiteSpace(item.Name))
            .GroupBy(item => item.Name, StringComparer.OrdinalIgnoreCase)
            .Select(group => group.OrderBy(item => item.Rank).First())
            .OrderBy(item => item.Rank)
            .ThenBy(item => item.Name, StringComparer.OrdinalIgnoreCase)
            .Select(item => item.Name)
            .ToArray();
    }
}
