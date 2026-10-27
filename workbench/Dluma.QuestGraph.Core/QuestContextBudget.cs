namespace Dluma.QuestGraph.Core;

public static class QuestContextBudget
{
    public static QuestContextSnapshot Apply(
        QuestContextSnapshot snapshot,
        int maximumItems)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (maximumItems <= 0)
            throw new ArgumentOutOfRangeException(nameof(maximumItems));

        var remaining = maximumItems;
        var objects = new List<QuestContextObject>();
        foreach (var value in LastByKey(snapshot.Objects, item => item.Name)
            .OrderBy(item => item.Name, StringComparer.OrdinalIgnoreCase))
        {
            if (remaining == 0)
                break;
            --remaining;
            var roles = value.Roles
                .Where(role => !string.IsNullOrWhiteSpace(role))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(role => role, StringComparer.OrdinalIgnoreCase)
                .Take(remaining)
                .ToArray();
            remaining -= roles.Length;
            objects.Add(value with { Roles = roles });
        }

        var texts = LastByKey(snapshot.Texts, item => item.Id)
            .OrderBy(item => item.Id, StringComparer.OrdinalIgnoreCase)
            .Take(remaining)
            .ToArray();
        remaining -= texts.Length;
        var speakers = TakeStrings(snapshot.Speakers, ref remaining);
        var dialogueGroups = TakeStrings(snapshot.DialogueGroups, ref remaining);
        var questReferences = TakeStrings(snapshot.QuestReferences, ref remaining);

        return snapshot with
        {
            Objects = objects,
            Texts = texts,
            Speakers = speakers,
            DialogueGroups = dialogueGroups,
            QuestReferences = questReferences
        };
    }

    private static IEnumerable<T> LastByKey<T>(
        IEnumerable<T> values,
        Func<T, string> keySelector)
    {
        var byKey = new Dictionary<string, T>(StringComparer.OrdinalIgnoreCase);
        foreach (var value in values)
        {
            ArgumentNullException.ThrowIfNull(value);
            var key = keySelector(value);
            if (!string.IsNullOrWhiteSpace(key))
                byKey[key] = value;
        }
        return byKey.Values;
    }

    private static IReadOnlyList<string> TakeStrings(
        IEnumerable<string> values,
        ref int remaining)
    {
        if (remaining == 0)
            return Array.Empty<string>();
        var result = values
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(value => value, StringComparer.OrdinalIgnoreCase)
            .Take(remaining)
            .ToArray();
        remaining -= result.Length;
        return result;
    }
}
