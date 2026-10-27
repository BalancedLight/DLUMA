using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace Dluma.QuestGraph.Core;

public enum QuestGraphPastePlacement
{
    After,
    Child
}

public enum QuestGraphMoveDirection
{
    Up,
    Down
}

public enum QuestGraphOperationRejection
{
    NodeNotFound,
    EdgeNotFound,
    OpaqueNode,
    IllegalParent,
    IncompatiblePort,
    Cycle,
    NoSibling,
    AlreadyConnected,
    UnrepresentableText,
    InvalidSourceRange
}

public sealed class QuestGraphOperationException : InvalidOperationException
{
    public QuestGraphOperationException(
        QuestGraphOperationRejection reason,
        string message)
        : base(message) => Reason = reason;

    public QuestGraphOperationRejection Reason { get; }
}

public sealed record QuestGraphClipboardFragment(
    string Text,
    string BaseIndentation,
    QuestSemanticKind Kind,
    string Name);

public sealed record QuestGraphMutation(
    IReadOnlyList<QuestTextEdit> Edits,
    string Description);

public static partial class QuestGraphMutationService
{
    public static QuestGraphClipboardFragment CopySubtree(
        QuestGraphDocument document,
        string semanticNodeId)
    {
        ArgumentNullException.ThrowIfNull(document);
        var node = FindNode(document, semanticNodeId);
        RequireEditable(node);
        var navigator = new SourceNavigator(document);
        var block = navigator.Block(node);
        return new QuestGraphClipboardFragment(
            navigator.Decode(block.Start, block.Length),
            navigator.Indentation(node),
            node.Kind,
            node.Name);
    }

    public static QuestGraphMutation DeleteSubtree(
        QuestGraphDocument document,
        string semanticNodeId) => DeleteMany(document, new[] { semanticNodeId });

    public static QuestGraphMutation Paste(
        QuestGraphDocument document,
        string targetSemanticNodeId,
        QuestGraphClipboardFragment fragment,
        QuestGraphPastePlacement placement)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(fragment);
        var target = FindNode(document, targetSemanticNodeId);
        RequireEditable(target);
        var navigator = new SourceNavigator(document);
        string desiredIndentation;
        int insertionOffset;
        if (placement == QuestGraphPastePlacement.Child)
        {
            RequireCanParent(target.Kind, fragment.Kind);
            desiredIndentation = navigator.Indentation(target) + navigator.IndentationUnit();
            insertionOffset = navigator.AfterDeclaration(target);
        }
        else
        {
            if (target.ParentId is null)
            {
                if (fragment.Kind != QuestSemanticKind.Quest)
                    Reject(QuestGraphOperationRejection.IllegalParent,
                        "Only another quest can be pasted beside a root quest node.");
            }
            else
            {
                var parent = FindNode(document, target.ParentId);
                RequireCanParent(parent.Kind, fragment.Kind);
            }
            desiredIndentation = navigator.Indentation(target);
            insertionOffset = navigator.Block(target).End;
        }

        var text = Reindent(fragment.Text, fragment.BaseIndentation, desiredIndentation);
        var replacement = navigator.EncodeInsertion(insertionOffset, text);
        return new QuestGraphMutation(
            new[] { Edit(document, insertionOffset, 0, replacement) },
            placement == QuestGraphPastePlacement.Child
                ? $"Pasted {fragment.Name} as a child."
                : $"Pasted {fragment.Name} after {target.Name}.");
    }

    public static QuestGraphMutation MoveSibling(
        QuestGraphDocument document,
        string semanticNodeId,
        QuestGraphMoveDirection direction)
    {
        ArgumentNullException.ThrowIfNull(document);
        var selected = FindNode(document, semanticNodeId);
        RequireEditable(selected);
        if (selected.ParentId is null)
            Reject(QuestGraphOperationRejection.IllegalParent,
                "Root quest nodes are not part of an ordered sibling flow.");
        var parent = FindNode(document, selected.ParentId!);
        if (!OrdersChildren(document, parent))
            Reject(QuestGraphOperationRejection.IllegalParent,
                "This PXSL container owns nested records rather than an ordered flow.");
        var siblings = document.SemanticNodes
            .Where(node => string.Equals(node.ParentId, selected.ParentId, StringComparison.Ordinal))
            .OrderBy(node => node.Order)
            .ToArray();
        var selectedIndex = Array.FindIndex(siblings, node => node.Id == selected.Id);
        var adjacentIndex = direction == QuestGraphMoveDirection.Up
            ? selectedIndex - 1
            : selectedIndex + 1;
        if (selectedIndex < 0 || adjacentIndex < 0 || adjacentIndex >= siblings.Length)
            Reject(QuestGraphOperationRejection.NoSibling,
                direction == QuestGraphMoveDirection.Up
                    ? "The selected node is already first in its PXSL flow."
                    : "The selected node is already last in its PXSL flow.");
        var adjacent = siblings[adjacentIndex];
        RequireEditable(adjacent);
        var first = direction == QuestGraphMoveDirection.Up ? adjacent : selected;
        var second = direction == QuestGraphMoveDirection.Up ? selected : adjacent;
        var navigator = new SourceNavigator(document);
        var firstBlock = navigator.Block(first);
        var secondBlock = navigator.Block(second);
        if (firstBlock.End > secondBlock.Start)
            Reject(QuestGraphOperationRejection.InvalidSourceRange,
                "Ordered sibling source ranges overlap.");
        var firstBytes = navigator.Bytes(firstBlock);
        var trivia = navigator.Bytes(new ByteRange(firstBlock.End, secondBlock.Start));
        var secondBytes = navigator.Bytes(secondBlock);
        var replacement = Join(secondBytes, trivia, firstBytes);
        return new QuestGraphMutation(
            new[]
            {
                Edit(
                    document,
                    firstBlock.Start,
                    secondBlock.End - firstBlock.Start,
                    replacement)
            },
            direction == QuestGraphMoveDirection.Up
                ? $"Moved {selected.Name} up in its PXSL flow."
                : $"Moved {selected.Name} down in its PXSL flow.");
    }

    public static QuestGraphMutation Connect(
        QuestGraphDocument document,
        string sourceSemanticNodeId,
        string sourcePortId,
        string targetSemanticNodeId)
    {
        ArgumentNullException.ThrowIfNull(document);
        var source = FindNode(document, sourceSemanticNodeId);
        var target = FindNode(document, targetSemanticNodeId);
        RequireEditable(source);
        RequireEditable(target);
        if (source.Id == target.Id || IsAncestor(document, target.Id, source.Id))
            Reject(QuestGraphOperationRejection.Cycle,
                "Connecting these nodes would create a PXSL containment cycle.");
        if (!PortExists(document, source, sourcePortId))
            Reject(QuestGraphOperationRejection.IncompatiblePort,
                $"Node '{source.Name}' does not expose output port '{sourcePortId}'.");

        if (sourcePortId == "out")
        {
            if (source.ParentId is null)
                Reject(QuestGraphOperationRejection.IllegalParent,
                    "A root node cannot be used as an ordered predecessor.");
            var parent = FindNode(document, source.ParentId!);
            if (!OrdersChildren(document, parent) ||
                target.Kind is QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch)
                Reject(QuestGraphOperationRejection.IncompatiblePort,
                    "The Out port accepts another executable sibling in an ordered PXSL container.");
            RequireCanParent(parent.Kind, target.Kind);
            if (target.ParentId == source.ParentId && target.Order == source.Order + 1)
                Reject(QuestGraphOperationRejection.AlreadyConnected,
                    "These nodes are already adjacent in the PXSL flow.");
            return MoveSubtree(
                document,
                target,
                newParent: parent,
                desiredIndentation: new SourceNavigator(document).Indentation(source),
                insertionOffset: new SourceNavigator(document).Block(source).End,
                $"Connected {source.Name} to {target.Name} in source order.");
        }

        if (sourcePortId == "body")
        {
            if (!OrdersChildren(document, source) ||
                target.Kind is QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch)
                Reject(QuestGraphOperationRejection.IncompatiblePort,
                    "The Body port accepts executable nodes in an ordered PXSL container.");
            RequireCanParent(source.Kind, target.Kind);
            if (target.ParentId == source.Id && target.Order == 0)
                Reject(QuestGraphOperationRejection.AlreadyConnected,
                    "The target is already the first node in this PXSL body.");
            var navigator = new SourceNavigator(document);
            return MoveSubtree(
                document,
                target,
                source,
                navigator.Indentation(source) + navigator.IndentationUnit(),
                navigator.AfterDeclaration(source),
                $"Connected {target.Name} to the ordered body of {source.Name}.");
        }

        if (sourcePortId == "child" || sourcePortId.StartsWith("child:", StringComparison.Ordinal))
        {
            if (source.Kind != QuestSemanticKind.Macro || target.Kind != QuestSemanticKind.Macro)
                Reject(QuestGraphOperationRejection.IncompatiblePort,
                    "The Child port connects a nested macro/object record to a macro owner.");
            if (document.Catalog?.TryGetEntry(source.Name, out var sourceEntry) == true &&
                sourceEntry is not null)
            {
                var slot = sourceEntry.ChildSlots.FirstOrDefault(candidate =>
                    candidate.PortId.Equals(sourcePortId, StringComparison.Ordinal));
                if (slot is null)
                    Reject(QuestGraphOperationRejection.IncompatiblePort,
                        $"Node '{source.Name}' does not declare child slot '{sourcePortId}'.");
                if (slot.AcceptedEntryNames.Count > 0 &&
                    !slot.AcceptedEntryNames.Contains(target.Name, StringComparer.OrdinalIgnoreCase))
                    Reject(QuestGraphOperationRejection.IncompatiblePort,
                        $"Child slot '{slot.DisplayName}' does not accept '{target.Name}'.");
            }
            if (target.ParentId == source.Id)
                Reject(QuestGraphOperationRejection.AlreadyConnected,
                    "The target is already nested under this owner.");
            var navigator = new SourceNavigator(document);
            return MoveSubtree(
                document,
                target,
                source,
                navigator.Indentation(source) + navigator.IndentationUnit(),
                navigator.AfterDeclaration(source),
                $"Nested {target.Name} under {source.Name}.");
        }

        const string branchPrefix = "branch:";
        if (sourcePortId.StartsWith(branchPrefix, StringComparison.Ordinal))
        {
            if (source.Kind is not (QuestSemanticKind.Macro or QuestSemanticKind.Quest))
                Reject(QuestGraphOperationRejection.IncompatiblePort,
                    "Typed branch ports are available only on quest or macro containers.");
            var branchName = sourcePortId[branchPrefix.Length..];
            var navigator = new SourceNavigator(document);
            if (target.Kind is QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch)
            {
                if (!string.Equals(target.Name, branchName, StringComparison.OrdinalIgnoreCase))
                    Reject(QuestGraphOperationRejection.IncompatiblePort,
                        $"Port '{sourcePortId}' requires a '{branchName}' PXSL branch node.");
                if (target.ParentId == source.Id)
                    Reject(QuestGraphOperationRejection.AlreadyConnected,
                        "The branch is already attached to this source node.");
                return MoveSubtree(
                    document,
                    target,
                    source,
                    navigator.Indentation(source) + navigator.IndentationUnit(),
                    navigator.AfterDeclaration(source),
                    $"Attached {branchName} to {source.Name}.");
            }
            if (target.Kind != QuestSemanticKind.Macro)
                Reject(QuestGraphOperationRejection.IncompatiblePort,
                    "A typed branch can wrap only an executable macro node.");
            return WrapInBranch(document, source, branchName, target);
        }

        Reject(QuestGraphOperationRejection.IncompatiblePort,
            $"Output port '{sourcePortId}' is not editable.");
        throw new UnreachableException();
    }

    public static QuestGraphMutation Disconnect(
        QuestGraphDocument document,
        string edgeId)
    {
        ArgumentNullException.ThrowIfNull(document);
        var edge = document.GraphEdges.FirstOrDefault(item => item.Id == edgeId) ??
            throw new QuestGraphOperationException(
                QuestGraphOperationRejection.EdgeNotFound,
                "The selected graph connection no longer exists.");
        var graphById = document.GraphNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        if (!graphById.TryGetValue(edge.SourceNodeId, out var sourceGraph))
            Reject(QuestGraphOperationRejection.EdgeNotFound,
                "The selected connection references a missing source graph node.");
        if (!graphById.TryGetValue(edge.TargetNodeId, out var targetGraph))
            Reject(QuestGraphOperationRejection.EdgeNotFound,
                "The selected connection references a missing target graph node.");
        var source = FindNode(document, sourceGraph!.SemanticNodeId);
        var target = FindNode(document, targetGraph!.SemanticNodeId);
        return DetachSubtreeToRoot(
            document,
            target,
            $"Disconnected {target.Name} from {source.Name}; it is staged at the PXSL root until reconnected.");
    }

    private static QuestGraphMutation DetachSubtreeToRoot(
        QuestGraphDocument document,
        QuestSemanticNode target,
        string description)
    {
        var navigator = new SourceNavigator(document);
        var block = target.IsOpaque
            ? navigator.ExactBlock(target)
            : navigator.Block(target);
        var fragment = navigator.Decode(block.Start, block.Length);
        var detached = Reindent(fragment, navigator.Indentation(target), string.Empty);
        var insertionOffset = document.Source.Bytes.Length;
        var insertion = navigator.EncodeInsertion(insertionOffset, detached);
        return new QuestGraphMutation(
            SortEditsDescending(
                Edit(document, block.Start, block.Length, Array.Empty<byte>()),
                Edit(document, insertionOffset, 0, insertion)),
            description);
    }

    private static QuestGraphMutation MoveSubtree(
        QuestGraphDocument document,
        QuestSemanticNode target,
        QuestSemanticNode newParent,
        string desiredIndentation,
        int insertionOffset,
        string description)
    {
        RequireCanParent(newParent.Kind, target.Kind);
        if (IsAncestor(document, target.Id, newParent.Id))
            Reject(QuestGraphOperationRejection.Cycle,
                "Moving the target below one of its descendants would create a cycle.");
        var navigator = new SourceNavigator(document);
        var block = navigator.Block(target);
        if (insertionOffset > block.Start && insertionOffset < block.End)
            Reject(QuestGraphOperationRejection.Cycle,
                "The target insertion point is inside its own source block.");
        var fragment = navigator.Decode(block.Start, block.Length);
        var reindented = Reindent(fragment, navigator.Indentation(target), desiredIndentation);
        var insertion = navigator.EncodeInsertion(insertionOffset, reindented);
        if (insertionOffset == block.Start)
        {
            return new QuestGraphMutation(
                new[] { Edit(document, block.Start, block.Length, insertion) },
                description);
        }
        return new QuestGraphMutation(
            SortEditsDescending(
                Edit(document, block.Start, block.Length, Array.Empty<byte>()),
                Edit(document, insertionOffset, 0, insertion)),
            description);
    }

    private static QuestGraphMutation WrapInBranch(
        QuestGraphDocument document,
        QuestSemanticNode source,
        string branchName,
        QuestSemanticNode target)
    {
        if (IsAncestor(document, target.Id, source.Id))
            Reject(QuestGraphOperationRejection.Cycle,
                "Wrapping an ancestor below its descendant would create a cycle.");
        var navigator = new SourceNavigator(document);
        var targetBlock = navigator.Block(target);
        var branchIndent = navigator.Indentation(source) + navigator.IndentationUnit();
        var targetIndent = branchIndent + navigator.IndentationUnit();
        var targetText = Reindent(
            navigator.Decode(targetBlock.Start, targetBlock.Length),
            navigator.Indentation(target),
            targetIndent);
        var branchText = branchIndent + branchName + navigator.Newline + targetText;
        var insertionOffset = navigator.AfterDeclaration(source);
        var insertion = navigator.EncodeInsertion(insertionOffset, branchText);
        if (insertionOffset == targetBlock.Start)
        {
            return new QuestGraphMutation(
                new[] { Edit(document, targetBlock.Start, targetBlock.Length, insertion) },
                $"Created {branchName} and connected {target.Name}.");
        }
        return new QuestGraphMutation(
            SortEditsDescending(
                Edit(document, targetBlock.Start, targetBlock.Length, Array.Empty<byte>()),
                Edit(document, insertionOffset, 0, insertion)),
            $"Created {branchName} and connected {target.Name}.");
    }

    private static QuestTextEdit Edit(
        QuestGraphDocument document,
        int offset,
        int removedLength,
        ReadOnlyMemory<byte> replacement) => new(
            document.Source.Revision,
            document.Source.Sha256,
            offset,
            removedLength,
            replacement);

    private static IReadOnlyList<QuestTextEdit> SortEditsDescending(
        params QuestTextEdit[] edits) => edits
            .OrderByDescending(edit => edit.ByteOffset)
            .ToArray();

    private static QuestSemanticNode FindNode(
        QuestGraphDocument document,
        string semanticNodeId) =>
        document.SemanticNodes.FirstOrDefault(node => node.Id == semanticNodeId) ??
        throw new QuestGraphOperationException(
            QuestGraphOperationRejection.NodeNotFound,
            "The selected graph node no longer exists in the PXSL source.");

    private static bool PortExists(
        QuestGraphDocument document,
        QuestSemanticNode node,
        string portId)
    {
        var graph = document.GraphNodes.FirstOrDefault(item => item.SemanticNodeId == node.Id);
        return graph?.Ports.Any(port =>
            port.Direction == QuestGraphPortDirection.Output &&
            port.Id == portId) == true;
    }

    private static bool IsAncestor(
        QuestGraphDocument document,
        string candidateAncestorId,
        string nodeId)
    {
        var byId = document.SemanticNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        var current = nodeId;
        var visited = new HashSet<string>(StringComparer.Ordinal);
        while (byId.TryGetValue(current, out var node) &&
            node.ParentId is { } parentId &&
            visited.Add(parentId))
        {
            if (parentId == candidateAncestorId)
                return true;
            current = parentId;
        }
        return false;
    }

    private static void RequireEditable(QuestSemanticNode node)
    {
        if (node.IsOpaque)
            Reject(QuestGraphOperationRejection.OpaqueNode,
                "Opaque source nodes are movable for layout only and cannot be structurally edited.");
    }

    private static void RequireCanParent(
        QuestSemanticKind parent,
        QuestSemanticKind child)
    {
        var compatible = parent switch
        {
            QuestSemanticKind.Quest => child is QuestSemanticKind.Macro or
                QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch,
            QuestSemanticKind.Macro => child is QuestSemanticKind.Macro or
                QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch,
            QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch =>
                child == QuestSemanticKind.Macro,
            _ => false
        };
        if (!compatible)
            Reject(QuestGraphOperationRejection.IllegalParent,
                $"A {parent} PXSL node cannot contain a {child} node.");
    }

    private static bool OrdersChildren(
        QuestGraphDocument document,
        QuestSemanticNode node) =>
        node.Kind is QuestSemanticKind.Quest or
            QuestSemanticKind.OrderedFlow or
            QuestSemanticKind.Branch ||
        document.GraphNodes.FirstOrDefault(graph => graph.SemanticNodeId == node.Id)?.Ports.Any(
            port => port.Direction == QuestGraphPortDirection.Output && port.Id == "body") == true;

    private static string Reindent(
        string text,
        string originalIndentation,
        string desiredIndentation)
    {
        if (text.Length == 0 || originalIndentation == desiredIndentation)
            return text;
        var result = new StringBuilder(text.Length + desiredIndentation.Length * 4);
        var lineStart = 0;
        while (lineStart < text.Length)
        {
            var lineEnd = lineStart;
            while (lineEnd < text.Length && text[lineEnd] is not ('\r' or '\n'))
                lineEnd++;
            var content = text.AsSpan(lineStart, lineEnd - lineStart);
            if (content.StartsWith(originalIndentation.AsSpan(), StringComparison.Ordinal))
            {
                result.Append(desiredIndentation);
                result.Append(content[originalIndentation.Length..]);
            }
            else
            {
                result.Append(content);
            }
            if (lineEnd < text.Length)
            {
                result.Append(text[lineEnd++]);
                if (lineEnd < text.Length && text[lineEnd - 1] == '\r' && text[lineEnd] == '\n')
                    result.Append(text[lineEnd++]);
            }
            lineStart = lineEnd;
        }
        return result.ToString();
    }

    private static byte[] Join(params byte[][] parts)
    {
        var result = new byte[checked(parts.Sum(part => part.Length))];
        var offset = 0;
        foreach (var part in parts)
        {
            Buffer.BlockCopy(part, 0, result, offset, part.Length);
            offset += part.Length;
        }
        return result;
    }

    [DoesNotReturn]
    private static void Reject(
        QuestGraphOperationRejection reason,
        string message) => throw new QuestGraphOperationException(reason, message);

    private readonly record struct ByteRange(int Start, int End)
    {
        public int Length => checked(End - Start);
    }

    private sealed class SourceNavigator
    {
        private readonly QuestGraphDocument _document;
        private readonly byte[] _bytes;
        private readonly Encoding _encoding;
        private readonly byte[][] _newlines;
        private readonly int _preambleLength;

        public SourceNavigator(QuestGraphDocument document)
        {
            _document = document;
            _bytes = document.Source.ToArray();
            _encoding = QuestEncoding.Resolve(document.Source.EncodingName);
            _newlines = new[] { "\r\n", "\n", "\r" }
                .Select(_encoding.GetBytes)
                .OrderByDescending(value => value.Length)
                .ToArray();
            _preambleLength = document.Source.HasBom ? _encoding.GetPreamble().Length : 0;
        }

        public string Newline => _document.Source.LineEnding switch
        {
            QuestLineEnding.Cr => "\r",
            QuestLineEnding.Lf => "\n",
            _ => "\r\n"
        };

        public ByteRange Block(QuestSemanticNode node)
        {
            var owned = QuestSourceBlockService.GetSubtreeRange(_document, node);
            return new ByteRange(owned.Start, owned.End);
        }

        public ByteRange ExactBlock(QuestSemanticNode node)
        {
            var owned = QuestSourceBlockService.GetExactNodeRange(_document, node);
            return new ByteRange(owned.Start, owned.End);
        }

        public string Indentation(QuestSemanticNode node)
        {
            var lineStart = LineStart(node.Span.Start);
            if (lineStart > node.Span.Start)
                Reject(QuestGraphOperationRejection.InvalidSourceRange,
                    "The selected node indentation is invalid.");
            return Decode(lineStart, node.Span.Start - lineStart);
        }

        public string IndentationUnit()
        {
            foreach (var node in _document.SemanticNodes)
            {
                if (node.ParentId is null)
                    continue;
                var parent = FindNode(_document, node.ParentId);
                var parentIndent = Indentation(parent);
                var childIndent = Indentation(node);
                if (childIndent.StartsWith(parentIndent, StringComparison.Ordinal) &&
                    childIndent.Length > parentIndent.Length)
                    return childIndent[parentIndent.Length..];
            }
            return "    ";
        }

        public int AfterDeclaration(QuestSemanticNode node) =>
            LineEndIncludingEnding(node.Span.End);

        public byte[] EncodeInsertion(int offset, string text)
        {
            byte[] encoded;
            try
            {
                encoded = _encoding.GetBytes(text);
            }
            catch (EncoderFallbackException exception)
            {
                throw new QuestGraphOperationException(
                    QuestGraphOperationRejection.UnrepresentableText,
                    $"The operation cannot be represented as {_document.Source.EncodingName}: {exception.Message}");
            }
            var prefix = NeedsLeadingNewline(offset) ? _encoding.GetBytes(Newline) : Array.Empty<byte>();
            var suffix = offset < _bytes.Length && !EndsWithNewline(encoded)
                ? _encoding.GetBytes(Newline)
                : Array.Empty<byte>();
            return Join(prefix, encoded, suffix);
        }

        public string Decode(int offset, int length)
        {
            try
            {
                return _encoding.GetString(_bytes, offset, length);
            }
            catch (DecoderFallbackException exception)
            {
                throw new QuestGraphOperationException(
                    QuestGraphOperationRejection.OpaqueNode,
                    $"Undecodable source can be moved for layout only: {exception.Message}");
            }
        }

        public byte[] Bytes(ByteRange range) =>
            _bytes.AsSpan(range.Start, range.Length).ToArray();

        private int LineStart(int offset)
        {
            if (offset < _preambleLength || offset > _bytes.Length)
                Reject(QuestGraphOperationRejection.InvalidSourceRange,
                    "The selected node starts outside the decodable source.");
            var start = _preambleLength;
            var index = _preambleLength;
            while (index < offset)
            {
                var newline = MatchNewline(index);
                if (newline is not null)
                {
                    start = index + newline.Length;
                    index += newline.Length;
                }
                else
                {
                    index++;
                }
            }
            return start;
        }

        private int LineEndIncludingEnding(int offset)
        {
            var index = Math.Clamp(offset, _preambleLength, _bytes.Length);
            while (index < _bytes.Length)
            {
                var newline = MatchNewline(index);
                if (newline is not null)
                    return index + newline.Length;
                index++;
            }
            return _bytes.Length;
        }

        private byte[]? MatchNewline(int offset) => _newlines.FirstOrDefault(candidate =>
            offset + candidate.Length <= _bytes.Length &&
            _bytes.AsSpan(offset, candidate.Length).SequenceEqual(candidate));

        private bool NeedsLeadingNewline(int offset)
        {
            if (offset <= _preambleLength)
                return false;
            return !_newlines.Any(newline =>
                offset >= newline.Length &&
                _bytes.AsSpan(offset - newline.Length, newline.Length).SequenceEqual(newline));
        }

        private bool EndsWithNewline(ReadOnlySpan<byte> bytes)
        {
            foreach (var newline in _newlines)
                if (bytes.EndsWith(newline))
                    return true;
            return false;
        }
    }
}
