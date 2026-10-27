using System.Text;

namespace Dluma.QuestGraph.Core;

public readonly record struct QuestSourceBlockRange(int Start, int End)
{
    public int Length => checked(End - Start);
}

public static class QuestSourceBlockService
{
    public static QuestSourceBlockRange GetSubtreeRange(
        QuestGraphDocument document,
        string semanticNodeId)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentException.ThrowIfNullOrWhiteSpace(semanticNodeId);
        var semantic = document.SemanticNodes.FirstOrDefault(node =>
            string.Equals(node.Id, semanticNodeId, StringComparison.Ordinal)) ??
            throw new QuestGraphOperationException(
                QuestGraphOperationRejection.NodeNotFound,
                "The selected graph node no longer exists in the PXSL source.");
        return GetSubtreeRange(document, semantic);
    }

    public static QuestSourceBlockRange GetSubtreeRange(
        QuestGraphDocument document,
        QuestSemanticNode semantic)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(semantic);
        var syntax = FindSyntax(document.Syntax.Roots, semantic) ??
            throw new QuestGraphOperationException(
                QuestGraphOperationRejection.InvalidSourceRange,
                "The selected semantic node has no matching lossless syntax region.");
        var lastEnd = LastDescendantEnd(syntax);
        var navigator = new ByteLineNavigator(document.Source);
        var start = navigator.LineStart(semantic.Span.Start);
        var end = navigator.LineEndIncludingEnding(lastEnd);
        if (start < navigator.PreambleLength || end < start || end > document.Source.Bytes.Length)
            throw new QuestGraphOperationException(
                QuestGraphOperationRejection.InvalidSourceRange,
                "The selected source subtree is outside the document bounds.");
        return new QuestSourceBlockRange(start, end);
    }

    public static QuestSourceBlockRange GetExactNodeRange(
        QuestGraphDocument document,
        QuestSemanticNode semantic)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(semantic);
        var navigator = new ByteLineNavigator(document.Source);
        var start = navigator.LineStart(semantic.Span.Start);
        var end = navigator.LineEndIncludingEnding(semantic.Span.End);
        if (start < navigator.PreambleLength || end < start || end > document.Source.Bytes.Length)
            throw new QuestGraphOperationException(
                QuestGraphOperationRejection.InvalidSourceRange,
                "The selected exact source region is outside the document bounds.");
        return new QuestSourceBlockRange(start, end);
    }

    public static QuestGraphMutation DeleteSubtree(
        QuestGraphDocument document,
        string semanticNodeId)
    {
        ArgumentNullException.ThrowIfNull(document);
        var semantic = document.SemanticNodes.FirstOrDefault(node =>
            string.Equals(node.Id, semanticNodeId, StringComparison.Ordinal)) ??
            throw new QuestGraphOperationException(
                QuestGraphOperationRejection.NodeNotFound,
                "The selected graph node no longer exists in the PXSL source.");
        var range = semantic.IsOpaque
            ? GetExactNodeRange(document, semantic)
            : GetSubtreeRange(document, semantic);
        var edit = new QuestTextEdit(
            document.Source.Revision,
            document.Source.Sha256,
            range.Start,
            range.Length,
            ReadOnlyMemory<byte>.Empty);
        return new QuestGraphMutation(
            new[] { edit },
            $"Deleted {semantic.Name} and its nested source.");
    }

    private static PxslSyntaxNode? FindSyntax(
        IEnumerable<PxslSyntaxNode> nodes,
        QuestSemanticNode semantic)
    {
        var pending = new Stack<PxslSyntaxNode>(nodes);
        while (pending.Count > 0)
        {
            var node = pending.Pop();
            if (node.Span.Start == semantic.Span.Start &&
                node.Span.Length == semantic.Span.Length &&
                string.Equals(node.Name, semantic.Name, StringComparison.OrdinalIgnoreCase))
                return node;
            foreach (var child in node.Children)
                pending.Push(child);
        }
        return null;
    }

    private static int LastDescendantEnd(PxslSyntaxNode root)
    {
        var lastEnd = root.Span.End;
        var pending = new Stack<PxslSyntaxNode>(root.Children);
        while (pending.Count > 0)
        {
            var node = pending.Pop();
            lastEnd = Math.Max(lastEnd, node.Span.End);
            foreach (var child in node.Children)
                pending.Push(child);
        }
        return lastEnd;
    }

    private sealed class ByteLineNavigator
    {
        private readonly byte[] _bytes;
        private readonly byte[][] _newlines;

        public ByteLineNavigator(QuestSourceSnapshot source)
        {
            _bytes = source.ToArray();
            var encoding = QuestEncoding.Resolve(source.EncodingName);
            _newlines = new[] { "\r\n", "\n", "\r" }
                .Select(encoding.GetBytes)
                .OrderByDescending(value => value.Length)
                .ToArray();
            PreambleLength = source.HasBom ? encoding.GetPreamble().Length : 0;
        }

        public int PreambleLength { get; }

        public int LineStart(int offset)
        {
            if (offset < PreambleLength || offset > _bytes.Length)
                throw new QuestGraphOperationException(
                    QuestGraphOperationRejection.InvalidSourceRange,
                    "The selected node starts outside the decodable source.");
            var start = PreambleLength;
            var index = PreambleLength;
            while (index < offset)
            {
                var newline = MatchNewline(index);
                if (newline is null)
                {
                    ++index;
                    continue;
                }
                start = index + newline.Length;
                index += newline.Length;
            }
            return start;
        }

        public int LineEndIncludingEnding(int offset)
        {
            if (offset < PreambleLength || offset > _bytes.Length)
                throw new QuestGraphOperationException(
                    QuestGraphOperationRejection.InvalidSourceRange,
                    "The selected node ends outside the decodable source.");
            var index = offset;
            while (index < _bytes.Length)
            {
                var newline = MatchNewline(index);
                if (newline is not null)
                    return index + newline.Length;
                ++index;
            }
            return _bytes.Length;
        }

        private byte[]? MatchNewline(int offset) => _newlines.FirstOrDefault(candidate =>
            offset + candidate.Length <= _bytes.Length &&
            _bytes.AsSpan(offset, candidate.Length).SequenceEqual(candidate));
    }
}
