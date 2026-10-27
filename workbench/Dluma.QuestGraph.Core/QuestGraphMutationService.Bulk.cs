namespace Dluma.QuestGraph.Core;

public static partial class QuestGraphMutationService
{
    /// <summary>
    /// Deletes a selection atomically. Valid nodes own their full source subtree;
    /// opaque nodes own exactly their lossless line/span region so a temporarily
    /// invalid document can still be repaired from the graph surface.
    /// </summary>
    public static QuestGraphMutation DeleteMany(
        QuestGraphDocument document,
        IEnumerable<string> semanticNodeIds)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(semanticNodeIds);
        var requested = semanticNodeIds
            .Where(id => !string.IsNullOrWhiteSpace(id))
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        if (requested.Length == 0)
            throw new ArgumentException("At least one semantic node must be selected.", nameof(semanticNodeIds));

        var byId = document.SemanticNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        var ranges = new List<QuestSourceBlockRange>(requested.Length);
        foreach (var id in requested)
        {
            if (!byId.TryGetValue(id, out var node))
            {
                throw new QuestGraphOperationException(
                    QuestGraphOperationRejection.NodeNotFound,
                    "A selected graph node no longer exists in the PXSL source.");
            }
            ranges.Add(node.IsOpaque
                ? QuestSourceBlockService.GetExactNodeRange(document, node)
                : QuestSourceBlockService.GetSubtreeRange(document, node));
        }

        var coalesced = new List<QuestSourceBlockRange>();
        foreach (var range in ranges
            .OrderBy(range => range.Start)
            .ThenByDescending(range => range.End))
        {
            if (coalesced.Count == 0 || range.Start > coalesced[^1].End)
            {
                coalesced.Add(range);
                continue;
            }
            var previous = coalesced[^1];
            coalesced[^1] = new QuestSourceBlockRange(previous.Start, Math.Max(previous.End, range.End));
        }

        var edits = coalesced
            .OrderByDescending(range => range.Start)
            .Select(range => new QuestTextEdit(
                document.Source.Revision,
                document.Source.Sha256,
                range.Start,
                range.Length,
                ReadOnlyMemory<byte>.Empty))
            .ToArray();
        return new QuestGraphMutation(
            edits,
            requested.Length == 1
                ? "Deleted the selected PXSL source region."
                : $"Deleted {requested.Length} selected PXSL source regions atomically.");
    }
}
