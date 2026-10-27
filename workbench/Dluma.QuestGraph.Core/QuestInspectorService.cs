using System.Text;

namespace Dluma.QuestGraph.Core;

public sealed record QuestInspectorCard(string Title, string Summary);

public sealed class QuestInspectorIndex
{
    private readonly QuestGraphDocument _document;
    private readonly IReadOnlyDictionary<string, QuestSemanticNode> _nodes;
    private readonly IReadOnlyDictionary<string, QuestInspectorCard> _cards;

    internal QuestInspectorIndex(QuestGraphDocument document)
    {
        _document = document;
        _nodes = document.SemanticNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        _cards = document.SemanticNodes.ToDictionary(
            node => node.Id,
            node => BuildCard(document, node, ResolveEntry(document, node)),
            StringComparer.Ordinal);
    }

    public IReadOnlyDictionary<string, QuestInspectorCard> Cards => _cards;

    public QuestInspectorCard GetCard(string semanticNodeId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(semanticNodeId);
        return _cards.TryGetValue(semanticNodeId, out var card)
            ? card
            : throw new QuestInspectorException(
                QuestInspectorRejection.NodeNotFound,
                "The selected graph node no longer exists in the PXSL source.");
    }

    public QuestInspectorSnapshot Inspect(string semanticNodeId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(semanticNodeId);
        if (!_nodes.TryGetValue(semanticNodeId, out var node))
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.NodeNotFound,
                "The selected graph node no longer exists in the PXSL source.");
        }
        var entry = ResolveEntry(_document, node);
        var fields = new List<QuestInspectorFieldValue>();
        var claimedStarts = new HashSet<int>();
        if (entry is not null)
        {
            foreach (var definition in entry.InspectorFields)
            {
                var value = Bind(node, definition);
                fields.Add(value);
                if (value.Span is PxslTextSpan span)
                {
                    var token = node.ArgumentTokens.FirstOrDefault(candidate =>
                        candidate.Span.Start <= span.Start && candidate.Span.End >= span.End);
                    if (token is not null)
                        claimedStarts.Add(token.Span.Start);
                }
            }
        }
        var unknown = node.ArgumentTokens
            .Where(token => !claimedStarts.Contains(token.Span.Start))
            .ToArray();
        var card = _cards[node.Id];
        return new QuestInspectorSnapshot(
            node.Id,
            entry,
            card.Title,
            card.Summary,
            Decode(_document.Source, node.Span),
            fields,
            unknown);
    }

    private static QuestInspectorCard BuildCard(
        QuestGraphDocument document,
        QuestSemanticNode node,
        QuestCatalogEntry? entry)
    {
        var friendly = FriendlyType(entry?.Name ?? node.Name);
        var phaseDefinition = entry?.InspectorFields.FirstOrDefault(field =>
            field.Role is QuestInspectorFieldRole.PhaseName or QuestInspectorFieldRole.NodeName);
        var phaseName = phaseDefinition is null
            ? string.Empty
            : Bind(node, phaseDefinition).Value;
        var title = string.IsNullOrWhiteSpace(phaseName)
            ? friendly
            : $"{friendly} ({phaseName})";
        if (entry is null)
            return new QuestInspectorCard(title, node.IsOpaque ? "Opaque PXSL" : string.Empty);

        var summaryValues = entry.InspectorFields
            .Where(field => field.Role == QuestInspectorFieldRole.Parameter)
            .Select(field => Bind(node, field))
            .Where(value => value.IsPresent && !value.IsPlaceholder &&
                !string.IsNullOrWhiteSpace(value.Value))
            .Take(2)
            .Select(value => value.Value)
            .ToArray();
        var summary = summaryValues.Length > 0
            ? string.Join(" | ", summaryValues)
            : string.Empty;
        return new QuestInspectorCard(title, summary);
    }

    private static QuestInspectorFieldValue Bind(
        QuestSemanticNode node,
        QuestInspectorFieldDefinition definition)
    {
        if (definition.Role == QuestInspectorFieldRole.NodeName)
        {
            var raw = node.Name;
            var value = DisplayValue(raw);
            return new QuestInspectorFieldValue(
                definition,
                value,
                raw,
                true,
                raw.Equals("_", StringComparison.Ordinal),
                IsWrapped(raw),
                node.NameSpan);
        }

        PxslArgumentToken? token = null;
        if (!string.IsNullOrWhiteSpace(definition.NamedArgument))
        {
            token = node.ArgumentTokens.FirstOrDefault(candidate =>
                candidate.IsNamed && candidate.NamedArgument?.Equals(
                    definition.NamedArgument,
                    StringComparison.OrdinalIgnoreCase) == true);
        }
        if (token is null && definition.Position is int position)
        {
            token = node.ArgumentTokens.FirstOrDefault(candidate =>
                !candidate.IsNamed && candidate.Position == position);
        }
        if (token is null)
        {
            return new QuestInspectorFieldValue(
                definition,
                string.Empty,
                string.Empty,
                false,
                false,
                false,
                null);
        }
        return new QuestInspectorFieldValue(
            definition,
            token.IsPlaceholder ? string.Empty : token.Value,
            token.RawText,
            true,
            token.IsPlaceholder,
            token.IsWrapped,
            token.ValueSpan);
    }

    private static QuestCatalogEntry? ResolveEntry(
        QuestGraphDocument document,
        QuestSemanticNode node)
    {
        var key = node.Kind == QuestSemanticKind.Quest ? "quest" : node.Name;
        return document.Catalog?.TryGetEntry(key, out var entry) == true ? entry : null;
    }

    private static string FriendlyType(string value)
    {
        var unwrapped = DisplayValue(value);
        if (unwrapped.Any(char.IsLetter) &&
            unwrapped.Where(char.IsLetter).All(char.IsUpper))
            return unwrapped;
        return string.Join(' ', unwrapped.Split(new[] { '-', '_', ' ' },
                StringSplitOptions.RemoveEmptyEntries)
            .Select(part => part.Length == 0
                ? part
                : char.ToUpperInvariant(part[0]) + part[1..].ToLowerInvariant()));
    }

    private static string DisplayValue(string raw)
    {
        if (raw.Equals("_", StringComparison.Ordinal))
            return string.Empty;
        if (raw.Length >= 4 && raw.StartsWith("<<", StringComparison.Ordinal) &&
            raw.EndsWith(">>", StringComparison.Ordinal))
            return raw[2..^2];
        if (raw.Length >= 2 && raw.StartsWith('"') && raw.EndsWith('"'))
            return raw[1..^1];
        return raw;
    }

    private static bool IsWrapped(string raw) =>
        raw.Length >= 4 && raw.StartsWith("<<", StringComparison.Ordinal) &&
            raw.EndsWith(">>", StringComparison.Ordinal) ||
        raw.Length >= 2 && raw.StartsWith('"') && raw.EndsWith('"') ||
        raw.Length >= 2 && raw.StartsWith('&') && raw.EndsWith('&');

    private static string Decode(QuestSourceSnapshot source, PxslTextSpan span)
    {
        if (span.Start < 0 || span.Length < 0 || span.End > source.Bytes.Length)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.InvalidSourceRange,
                "The selected graph node is outside the current source snapshot.");
        }
        try
        {
            return QuestEncoding.Resolve(source.EncodingName).GetString(
                source.Bytes.Span.Slice(span.Start, span.Length));
        }
        catch (DecoderFallbackException exception)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.InvalidSourceRange,
                $"The selected graph node could not be decoded: {exception.Message}");
        }
    }
}

public static class QuestInspectorService
{
    public static QuestInspectorIndex CreateIndex(QuestGraphDocument document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return new QuestInspectorIndex(document);
    }

    public static QuestInspectorSnapshot Inspect(
        QuestGraphDocument document,
        string semanticNodeId) => CreateIndex(document).Inspect(semanticNodeId);
}

public enum QuestInspectorRejection
{
    NodeNotFound,
    CatalogUnavailable,
    FieldNotFound,
    OpaqueNode,
    InvalidValue,
    InvalidSourceRange,
    UnrepresentableText
}

public sealed class QuestInspectorException : InvalidOperationException
{
    public QuestInspectorException(QuestInspectorRejection reason, string message)
        : base(message) => Reason = reason;

    public QuestInspectorRejection Reason { get; }
}
