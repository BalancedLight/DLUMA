namespace Dluma.QuestGraph.Core;

public static class QuestCatalogTemplateBuilder
{
    /// <summary>
    /// Produces a deterministic insertion fragment and expands only child slots
    /// whose stock quantity requires at least one concrete child. Optional slots
    /// remain absent, and required slots without a concrete template deliberately
    /// remain temporarily invalid instead of inventing PXSL.
    /// </summary>
    public static string BuildMinimumTemplate(
        this QuestCatalogEntry entry,
        string indentation = "    ",
        string newline = "\n")
    {
        ArgumentNullException.ThrowIfNull(entry);
        ArgumentNullException.ThrowIfNull(indentation);
        ArgumentNullException.ThrowIfNull(newline);

        var root = string.IsNullOrWhiteSpace(entry.Template)
            ? entry.DirectiveTemplate
            : entry.Template;
        if (string.IsNullOrWhiteSpace(root))
            return string.Empty;

        var lines = new List<string> { ReplacePlaceholders(root.TrimEnd()) };
        foreach (var slot in entry.ChildSlots)
        {
            var count = MinimumCount(slot.Quantity);
            if (count == 0 ||
                string.IsNullOrWhiteSpace(slot.Template) ||
                slot.Kind != QuestCatalogChildSlotKind.Child ||
                slot.AcceptedEntryNames.Count != 1)
                continue;
            var template = slot.Template.Trim();
            for (var index = 0; index < count; ++index)
                lines.Add(indentation + ReplacePlaceholders(template));
        }
        return string.Join(newline, lines);
    }

    private static int MinimumCount(string quantity)
    {
        if (string.Equals(quantity?.Trim(), "+", StringComparison.Ordinal))
            return 1;
        return int.TryParse(quantity, out var value) && value > 0 ? value : 0;
    }

    private static string ReplacePlaceholders(string template)
    {
        var builder = new System.Text.StringBuilder(template.Length);
        for (var index = 0; index < template.Length; ++index)
        {
            if (template[index] != '[')
            {
                builder.Append(template[index]);
                continue;
            }
            var close = template.IndexOf(']', index + 1);
            if (close < 0)
            {
                builder.Append(template[index]);
                continue;
            }
            builder.Append('_');
            index = close;
        }
        return builder.ToString();
    }
}
