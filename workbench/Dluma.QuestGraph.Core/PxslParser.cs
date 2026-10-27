using System.Text;
using System.Text.RegularExpressions;

namespace Dluma.QuestGraph.Core;

public static partial class PxslParser
{
    private const int MaximumDecodedLines = 100_000;
    private const int MaximumCstNodes = 100_000;

    public static QuestGraphDocument Parse(
        QuestSourceSnapshot source,
        QuestCatalog? catalog = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        var diagnostics = new List<QuestDiagnostic>();
        var syntax = ParseSyntax(source, catalog, diagnostics);
        var projection = QuestGraphProjector.Project(syntax, catalog);
        diagnostics.AddRange(projection.Diagnostics);
        return new QuestGraphDocument(
            source,
            syntax,
            projection.SemanticNodes,
            projection.GraphNodes,
            projection.GraphEdges,
            diagnostics)
        {
            Catalog = catalog
        };
    }

    private static PxslSyntaxDocument ParseSyntax(
        QuestSourceSnapshot source,
        QuestCatalog? catalog,
        List<QuestDiagnostic> diagnostics)
    {
        Encoding encoding;
        string text;
        var preambleLength = 0;
        try
        {
            encoding = QuestEncoding.Resolve(source.EncodingName);
            if (source.HasBom)
                preambleLength = encoding.GetPreamble().Length;
            text = encoding.GetString(source.Bytes.Span[preambleLength..]);
        }
        catch (Exception exception) when (
            exception is ArgumentException or DecoderFallbackException)
        {
            var span = new PxslTextSpan(0, source.Bytes.Length, 1, 1);
            diagnostics.Add(new QuestDiagnostic(
                "PXSL001",
                QuestDiagnosticSeverity.Error,
                $"The source could not be decoded as {source.EncodingName}; it is preserved as opaque bytes.",
                span));
            var opaque = new PxslSyntaxNode(
                PxslSyntaxKind.Opaque,
                string.Empty,
                "invalid-encoding",
                Array.Empty<string>(),
                0,
                span,
                Array.Empty<PxslSyntaxNode>(),
                true);
            return new PxslSyntaxDocument(source, new[] { opaque }, diagnostics.ToArray());
        }

        var decodedLines = DecodeLines(text, encoding, preambleLength);
        var lines = decodedLines.Lines;
        var roots = new List<NodeBuilder>();
        var stack = new Stack<NodeBuilder>();
        var knownIndentations = new HashSet<int> { 0 };
        var previousSignificantIndentation = 0;
        var hasPreviousSignificant = false;
        var cstNodeCount = 0;
        var opaqueTailStart = decodedLines.OpaqueTailStartByte;
        var opaqueTailLine = decodedLines.OpaqueTailLine;
        var parsedNodeBudget = opaqueTailStart.HasValue
            ? MaximumCstNodes - 1
            : MaximumCstNodes;

        for (var lineIndex = 0; lineIndex < lines.Count; ++lineIndex)
        {
            var line = lines[lineIndex];
            if (cstNodeCount >= parsedNodeBudget)
            {
                opaqueTailStart = line.StartByte;
                opaqueTailLine = line.Number;
                break;
            }
            var leadingCharacters = LeadingWhitespaceLength(line.Content);
            var indentationText = line.Content[..leadingCharacters];
            var indentation = IndentationWidth(indentationText);
            var trimmed = line.Content[leadingCharacters..].TrimEnd();
            var contentStart = checked(
                line.StartByte + encoding.GetByteCount(line.Content.AsSpan(0, leadingCharacters)));
            var contentLength = encoding.GetByteCount(trimmed);
            var span = new PxslTextSpan(contentStart, contentLength, line.Number, leadingCharacters + 1);

            var classification = Classify(trimmed, span, catalog, diagnostics);
            var consumedEnd = lineIndex;
            if (classification.Kind == PxslSyntaxKind.RawXml && !trimmed.Contains(">>>", StringComparison.Ordinal))
            {
                var raw = new StringBuilder(trimmed);
                while (++consumedEnd < lines.Count)
                {
                    raw.Append(lines[consumedEnd - 1].Ending);
                    raw.Append(lines[consumedEnd].Content);
                    if (lines[consumedEnd].Content.Contains(">>>", StringComparison.Ordinal))
                        break;
                }

                if (consumedEnd >= lines.Count)
                {
                    consumedEnd = lines.Count - 1;
                    classification = classification with
                    {
                        Kind = PxslSyntaxKind.Opaque,
                        Name = "unterminated-raw-xml",
                        IsMalformed = true
                    };
                    diagnostics.Add(new QuestDiagnostic(
                        "PXSL002",
                        QuestDiagnosticSeverity.Error,
                        "Embedded raw XML is not terminated with >>>; the region is preserved as opaque source.",
                        span));
                }

                var finalLine = lines[consumedEnd];
                var finalEnd = checked(finalLine.StartByte + encoding.GetByteCount(finalLine.Content));
                span = span with { Length = finalEnd - span.Start };
                classification = classification with { Text = raw.ToString() };
                lineIndex = consumedEnd;
            }

            var mixedIndentation = indentationText.Contains(' ') && indentationText.Contains('\t');
            var inconsistentDedent = classification.IsSignificant &&
                hasPreviousSignificant &&
                indentation < previousSignificantIndentation &&
                !knownIndentations.Contains(indentation);
            if (mixedIndentation || inconsistentDedent)
            {
                classification = classification with
                {
                    Kind = PxslSyntaxKind.Opaque,
                    Name = "malformed-indentation",
                    IsMalformed = true
                };
                diagnostics.Add(new QuestDiagnostic(
                    "PXSL004",
                    QuestDiagnosticSeverity.Error,
                    mixedIndentation
                        ? "Mixed tab and space indentation is preserved as an opaque region."
                        : "The line dedents to an indentation level that has no containing block.",
                    span));
            }

            var lexicalTokens = PxslSourceTokenizer.Tokenize(trimmed, span, encoding);
            var headerTokenCount = Math.Max(
                0,
                lexicalTokens.Count - classification.Arguments.Count);
            var argumentTokens = PxslSourceTokenizer.Arguments(
                lexicalTokens,
                headerTokenCount,
                encoding);
            PxslTextSpan? nameSpan = classification.Kind switch
            {
                PxslSyntaxKind.Quest when lexicalTokens.Count > 1 => lexicalTokens[1].Span,
                PxslSyntaxKind.Macro or PxslSyntaxKind.Flow or PxslSyntaxKind.Directive or PxslSyntaxKind.Opaque
                    when lexicalTokens.Count > 0 => lexicalTokens[0].Span,
                _ => null
            };
            var builder = new NodeBuilder(
                classification.Kind,
                classification.Text,
                classification.Name,
                classification.Arguments,
                indentation,
                span,
                classification.IsMalformed,
                argumentTokens,
                nameSpan);
            cstNodeCount++;
            if (!classification.IsSignificant)
            {
                var parent = stack.FirstOrDefault(candidate => candidate.Indentation < indentation);
                if (parent is null)
                    roots.Add(builder);
                else
                    parent.Children.Add(builder);
                continue;
            }

            while (stack.Count > 0 && stack.Peek().Indentation >= indentation)
                stack.Pop();
            if (stack.Count == 0)
                roots.Add(builder);
            else
                stack.Peek().Children.Add(builder);

            stack.Push(builder);
            knownIndentations.Add(indentation);
            previousSignificantIndentation = indentation;
            hasPreviousSignificant = true;
        }

        if (opaqueTailStart is int tailStart && tailStart < source.Bytes.Length)
        {
            var tailSpan = new PxslTextSpan(
                tailStart,
                source.Bytes.Length - tailStart,
                opaqueTailLine ?? 1,
                1);
            diagnostics.Add(new QuestDiagnostic(
                "PXSL007",
                QuestDiagnosticSeverity.Warning,
                "PXSL parsing stopped at the 100,000-line/node safety limit; the remaining source is preserved as one opaque tail.",
                tailSpan));
            roots.Add(new NodeBuilder(
                PxslSyntaxKind.Opaque,
                string.Empty,
                "parser-limit-tail",
                Array.Empty<string>(),
                0,
                tailSpan,
                true));
        }

        return new PxslSyntaxDocument(
            source,
            FreezeSyntax(roots),
            diagnostics.ToArray());
    }

    private static IReadOnlyList<PxslSyntaxNode> FreezeSyntax(
        IReadOnlyList<NodeBuilder> roots)
    {
        var frozen = new Dictionary<NodeBuilder, PxslSyntaxNode>();
        var pending = new Stack<FreezeFrame>();
        for (var index = roots.Count - 1; index >= 0; --index)
            pending.Push(new FreezeFrame(roots[index], false));
        while (pending.Count > 0)
        {
            var frame = pending.Pop();
            if (!frame.ChildrenVisited)
            {
                pending.Push(frame with { ChildrenVisited = true });
                for (var index = frame.Node.Children.Count - 1; index >= 0; --index)
                    pending.Push(new FreezeFrame(frame.Node.Children[index], false));
                continue;
            }
            var children = new PxslSyntaxNode[frame.Node.Children.Count];
            for (var index = 0; index < children.Length; ++index)
                children[index] = frozen[frame.Node.Children[index]];
            frozen.Add(frame.Node, new PxslSyntaxNode(
                frame.Node.Kind,
                frame.Node.Text,
                frame.Node.Name,
                frame.Node.Arguments,
                frame.Node.Indentation,
                frame.Node.Span,
                children,
                frame.Node.IsMalformed)
            {
                ArgumentTokens = frame.Node.ArgumentTokens,
                NameSpan = frame.Node.NameSpan
            });
        }
        var result = new PxslSyntaxNode[roots.Count];
        for (var index = 0; index < result.Length; ++index)
            result[index] = frozen[roots[index]];
        return result;
    }

    private static Classification Classify(
        string text,
        PxslTextSpan span,
        QuestCatalog? catalog,
        List<QuestDiagnostic> diagnostics)
    {
        if (text.Length == 0)
            return new(PxslSyntaxKind.Blank, text, string.Empty, Array.Empty<string>(), false);
        if (text.StartsWith("--", StringComparison.Ordinal))
            return new(PxslSyntaxKind.Comment, text, string.Empty, Array.Empty<string>(), false);
        if (text.StartsWith("<<<", StringComparison.Ordinal))
            return new(PxslSyntaxKind.RawXml, text, "raw-xml", Array.Empty<string>(), false);

        var tokens = Tokenize(text);
        if (tokens.Count == 0)
            return new(PxslSyntaxKind.Blank, text, string.Empty, Array.Empty<string>(), false);

        if (tokens[0].Equals("QuestsDefinitions", StringComparison.OrdinalIgnoreCase))
        {
            return new(
                PxslSyntaxKind.Directive,
                text,
                tokens[0],
                tokens.Skip(1).ToArray(),
                false);
        }

        if (tokens[0].Equals("quest", StringComparison.OrdinalIgnoreCase))
        {
            if (tokens.Count >= 2)
            {
                return new(
                    PxslSyntaxKind.Quest,
                    text,
                    tokens[1],
                    tokens.Skip(2).ToArray(),
                    false);
            }
            return Malformed(text, "quest", span, diagnostics, "A quest declaration requires a name.");
        }

        if (IsFlow(tokens[0]))
        {
            return new(
                PxslSyntaxKind.Flow,
                text,
                tokens[0].ToLowerInvariant(),
                tokens.Skip(1).ToArray(),
                false);
        }

        if (tokens[0].StartsWith(",", StringComparison.Ordinal))
        {
            var name = tokens[0][1..];
            if (name.Length == 0 || !MacroNamePattern().IsMatch(name))
                return Malformed(text, "macro", span, diagnostics, "A comma macro requires a valid name.");
            if (catalog is not null && !catalog.ContainsMacro(name))
            {
                diagnostics.Add(new QuestDiagnostic(
                    "PXSL003",
                    QuestDiagnosticSeverity.Warning,
                    $"Macro '{name}' is absent from the active catalog and is preserved as opaque source.",
                    span));
                return new(
                    PxslSyntaxKind.Opaque,
                    text,
                    name,
                    tokens.Skip(1).ToArray(),
                    false);
            }
            return new(
                PxslSyntaxKind.Macro,
                text,
                name,
                tokens.Skip(1).ToArray(),
                false);
        }

        diagnostics.Add(new QuestDiagnostic(
            "PXSL005",
            QuestDiagnosticSeverity.Warning,
            "Unrecognized PXSL source is preserved as an opaque node.",
            span));
        return new(
            PxslSyntaxKind.Opaque,
            text,
            tokens[0],
            tokens.Skip(1).ToArray(),
            false);
    }

    private static Classification Malformed(
        string text,
        string name,
        PxslTextSpan span,
        List<QuestDiagnostic> diagnostics,
        string message)
    {
        diagnostics.Add(new QuestDiagnostic(
            "PXSL006",
            QuestDiagnosticSeverity.Error,
            message,
            span));
        return new(PxslSyntaxKind.Opaque, text, name, Array.Empty<string>(), true);
    }

    private static bool IsFlow(string token) =>
        token.Equals("path", StringComparison.OrdinalIgnoreCase) ||
        token.Equals("fallback", StringComparison.OrdinalIgnoreCase) ||
        token.Equals("pre", StringComparison.OrdinalIgnoreCase) ||
        token.Equals("post", StringComparison.OrdinalIgnoreCase);

    private static IReadOnlyList<string> Tokenize(string text)
    {
        var tokens = new List<string>();
        var token = new StringBuilder();
        var quoted = false;
        var angleDepth = 0;
        for (var index = 0; index < text.Length; ++index)
        {
            var character = text[index];
            if (character == '"' && angleDepth == 0)
            {
                quoted = !quoted;
                token.Append(character);
                continue;
            }
            if (!quoted && index + 1 < text.Length && character == '<' && text[index + 1] == '<')
            {
                angleDepth++;
                token.Append("<<");
                index++;
                continue;
            }
            if (!quoted && angleDepth > 0 && index + 1 < text.Length && character == '>' && text[index + 1] == '>')
            {
                angleDepth--;
                token.Append(">>");
                index++;
                continue;
            }
            if (!quoted && angleDepth == 0 && char.IsWhiteSpace(character))
            {
                if (token.Length > 0)
                {
                    tokens.Add(token.ToString());
                    token.Clear();
                }
                continue;
            }
            token.Append(character);
        }
        if (token.Length > 0)
            tokens.Add(token.ToString());
        return tokens;
    }

    private static DecodedLines DecodeLines(
        string text,
        Encoding encoding,
        int initialByteOffset)
    {
        var lines = new List<DecodedLine>();
        var characterOffset = 0;
        var byteOffset = initialByteOffset;
        var lineNumber = 1;
        while (characterOffset < text.Length && lines.Count < MaximumDecodedLines)
        {
            var start = characterOffset;
            while (characterOffset < text.Length &&
                text[characterOffset] != '\r' && text[characterOffset] != '\n')
                characterOffset++;

            var content = text[start..characterOffset];
            var endingStart = characterOffset;
            if (characterOffset < text.Length && text[characterOffset] == '\r')
                characterOffset++;
            if (characterOffset < text.Length && text[characterOffset] == '\n')
                characterOffset++;
            var ending = text[endingStart..characterOffset];
            var contentBytes = encoding.GetByteCount(content);
            var endingBytes = encoding.GetByteCount(ending);
            lines.Add(new DecodedLine(
                content,
                ending,
                byteOffset,
                contentBytes,
                endingBytes,
                lineNumber++));
            byteOffset = checked(byteOffset + contentBytes + endingBytes);
        }

        int? opaqueTailStartByte = null;
        int? opaqueTailLine = null;
        if (characterOffset < text.Length)
        {
            opaqueTailStartByte = byteOffset;
            opaqueTailLine = lineNumber;
        }
        else if ((text.Length == 0 || text[^1] is '\r' or '\n') &&
            lines.Count < MaximumDecodedLines)
        {
            lines.Add(new DecodedLine(string.Empty, string.Empty, byteOffset, 0, 0, lineNumber));
        }
        return new DecodedLines(lines, opaqueTailStartByte, opaqueTailLine);
    }

    private static int LeadingWhitespaceLength(string value)
    {
        var index = 0;
        while (index < value.Length && value[index] is ' ' or '\t')
            index++;
        return index;
    }

    private static int IndentationWidth(string value)
    {
        var width = 0;
        foreach (var character in value)
            width = character == '\t' ? checked((width / 4 + 1) * 4) : checked(width + 1);
        return width;
    }

    [GeneratedRegex("^[A-Za-z0-9_][A-Za-z0-9_.-]*$", RegexOptions.CultureInvariant)]
    private static partial Regex MacroNamePattern();

    private sealed record DecodedLine(
        string Content,
        string Ending,
        int StartByte,
        int ContentByteLength,
        int EndingByteLength,
        int Number);

    private sealed record DecodedLines(
        IReadOnlyList<DecodedLine> Lines,
        int? OpaqueTailStartByte,
        int? OpaqueTailLine);

    private sealed record Classification(
        PxslSyntaxKind Kind,
        string Text,
        string Name,
        IReadOnlyList<string> Arguments,
        bool IsMalformed)
    {
        public bool IsSignificant => Kind is not PxslSyntaxKind.Blank and not PxslSyntaxKind.Comment;
    }

    private sealed class NodeBuilder
    {
        public NodeBuilder(
            PxslSyntaxKind kind,
            string text,
            string name,
            IReadOnlyList<string> arguments,
            int indentation,
            PxslTextSpan span,
            bool isMalformed,
            IReadOnlyList<PxslArgumentToken>? argumentTokens = null,
            PxslTextSpan? nameSpan = null)
        {
            Kind = kind;
            Text = text;
            Name = name;
            Arguments = arguments;
            Indentation = indentation;
            Span = span;
            IsMalformed = isMalformed;
            ArgumentTokens = argumentTokens ?? Array.Empty<PxslArgumentToken>();
            NameSpan = nameSpan;
        }

        public PxslSyntaxKind Kind { get; }
        public string Text { get; }
        public string Name { get; }
        public IReadOnlyList<string> Arguments { get; }
        public int Indentation { get; }
        public PxslTextSpan Span { get; }
        public bool IsMalformed { get; }
        public IReadOnlyList<PxslArgumentToken> ArgumentTokens { get; }
        public PxslTextSpan? NameSpan { get; }
        public List<NodeBuilder> Children { get; } = new();

    }

    private readonly record struct FreezeFrame(
        NodeBuilder Node,
        bool ChildrenVisited);
}

internal static class QuestGraphProjector
{
    public static ProjectionResult Project(PxslSyntaxDocument syntax, QuestCatalog? catalog)
    {
        var semantic = new List<QuestSemanticNode>();
        var graphNodes = new List<QuestGraphNode>();
        var graphEdges = new List<QuestGraphEdge>();
        var diagnostics = new List<QuestDiagnostic>();
        var semanticById = new Dictionary<string, QuestSemanticNode>(StringComparer.Ordinal);
        var graphById = new Dictionary<string, QuestGraphNode>(StringComparer.Ordinal);

        foreach (var root in syntax.Roots)
        {
            if (root.Kind is PxslSyntaxKind.Blank or PxslSyntaxKind.Comment)
                continue;
            var pending = new Stack<ProjectionFrame>();
            pending.Push(new ProjectionFrame(root, null, "ROOT", null, null, 0));
            while (pending.Count > 0)
            {
                var frame = pending.Peek();
                if (!frame.IsEntered)
                {
                    Enter(frame, syntax.Source, catalog, semantic, semanticById,
                        graphNodes, graphById, graphEdges, diagnostics);
                }
                PxslSyntaxNode? childToVisit = null;
                var childOrder = 0;
                while (frame.NextChildIndex < frame.Syntax.Children.Count)
                {
                    var child = frame.Syntax.Children[frame.NextChildIndex++];
                    if (child.Kind is PxslSyntaxKind.Blank or PxslSyntaxKind.Comment)
                        continue;
                    childToVisit = child;
                    childOrder = frame.NextChildOrder++;
                    break;
                }
                if (childToVisit is not null)
                {
                    pending.Push(new ProjectionFrame(
                        childToVisit,
                        frame.SemanticId,
                        frame.DescendantContainerFingerprint,
                        frame.NextGraphParentId,
                        frame.LastOrderedChildGraphId,
                        childOrder));
                    continue;
                }
                pending.Pop();
                // The chain advances only for a child that actually joined its
                // container's ordered body, and only if that child exposes a successor
                // port. A record in a typed child slot -- or a node with no out port --
                // is stepped over, so the next sibling falls back to the container
                // rather than naming a port that is not there.
                if (pending.Count > 0 &&
                    frame.ResultGraphId is not null &&
                    frame.ChildRole == QuestGraphChildRole.Ordered &&
                    graphById.TryGetValue(frame.ResultGraphId, out var popped) &&
                    QuestGraphPortPolicy.ResolveOrderedSuccessorPort(popped.Ports) is not null)
                {
                    pending.Peek().LastOrderedChildGraphId = frame.ResultGraphId;
                }
            }
        }

        return new ProjectionResult(semantic, graphNodes, graphEdges, diagnostics);
    }

    private static void Enter(
        ProjectionFrame frame,
        QuestSourceSnapshot source,
        QuestCatalog? catalog,
        List<QuestSemanticNode> semanticNodes,
        Dictionary<string, QuestSemanticNode> semanticById,
        List<QuestGraphNode> graphNodes,
        Dictionary<string, QuestGraphNode> graphById,
        List<QuestGraphEdge> graphEdges,
        List<QuestDiagnostic> diagnostics)
    {
        var syntax = frame.Syntax;
        var kind = SemanticKind(syntax);
        var sourceHash = SpanHash(source, syntax.Span, diagnostics);
        var fingerprint = QuestHash.Sha256(string.Join(
            "\u001f",
            kind.ToString(),
            syntax.Name.ToLowerInvariant(),
            string.Join("\u001e", syntax.Arguments.Select(argument => argument.Trim()))));
        var descendantContainerFingerprint = QuestHash.Sha256(
            frame.ContainerFingerprint + "\u001f" + fingerprint);
        var semanticId = $"n-{syntax.Span.Start:x}-{sourceHash[..Math.Min(12, sourceHash.Length)].ToLowerInvariant()}";
        var node = new QuestSemanticNode(
            semanticId,
            kind,
            syntax.Name,
            syntax.Arguments,
            syntax.Span,
            frame.SemanticParentId,
            frame.Order,
            fingerprint,
            frame.ContainerFingerprint,
            sourceHash,
            kind is QuestSemanticKind.Opaque or QuestSemanticKind.RawXml)
        {
            ArgumentTokens = syntax.ArgumentTokens,
            NameSpan = syntax.NameSpan
        };
        semanticNodes.Add(node);
        semanticById.Add(node.Id, node);

        var nextGraphParent = frame.GraphParentId;
        string? resultGraphId = null;
        if (kind != QuestSemanticKind.Directive)
        {
            var graphId = "g-" + semanticId;
            resultGraphId = graphId;
            var graphNode = new QuestGraphNode(
                graphId,
                semanticId,
                kind,
                syntax.Name,
                node.IsOpaque,
                frame.Order,
                Ports(syntax, kind, catalog))
            {
                IsDetached = frame.SemanticParentId is null && kind != QuestSemanticKind.Quest
            };
            graphNodes.Add(graphNode);
            graphById.Add(graphId, graphNode);

            if (frame.GraphParentId is not null &&
                graphById.TryGetValue(frame.GraphParentId, out var parentGraph))
            {
                var parentSemantic = frame.SemanticParentId is null
                    ? null
                    : semanticById[frame.SemanticParentId];
                var binding = QuestGraphPortPolicy.BindChild(
                    parentSemantic?.Kind ?? QuestSemanticKind.Opaque,
                    parentSemantic?.Name,
                    syntax,
                    catalog);
                var edgeKind = binding.Role switch
                {
                    QuestGraphChildRole.Branch => QuestGraphEdgeKind.Branch,
                    QuestGraphChildRole.Ordered => QuestGraphEdgeKind.Ordered,
                    _ => QuestGraphEdgeKind.Nested
                };
                frame.ChildRole = binding.Role;

                string edgeSource;
                string? sourcePort;
                if (binding.Role == QuestGraphChildRole.Ordered &&
                    frame.PreviousSiblingGraphId is not null &&
                    graphById.TryGetValue(frame.PreviousSiblingGraphId, out var predecessor))
                {
                    edgeSource = frame.PreviousSiblingGraphId;
                    sourcePort = QuestGraphPortPolicy.ResolveOrderedSuccessorPort(predecessor.Ports);
                }
                else
                {
                    edgeSource = frame.GraphParentId;
                    sourcePort = QuestGraphPortPolicy.ResolveParentPort(
                        parentGraph.Ports,
                        binding.PortId);
                    if (sourcePort is not null &&
                        !string.Equals(sourcePort, binding.PortId, StringComparison.Ordinal))
                    {
                        diagnostics.Add(new QuestDiagnostic(
                            "PXSL101",
                            QuestDiagnosticSeverity.Warning,
                            $"'{parentSemantic?.Name ?? "container"}' does not expose port " +
                            $"'{binding.PortId}' for nested '{syntax.Name}'; the connection " +
                            $"was routed through '{sourcePort}' instead.",
                            syntax.Span));
                    }
                }

                if (sourcePort is null)
                {
                    diagnostics.Add(new QuestDiagnostic(
                        "PXSL102",
                        QuestDiagnosticSeverity.Warning,
                        $"'{parentSemantic?.Name ?? "container"}' exposes no output port, so " +
                        $"nested '{syntax.Name}' is shown unconnected.",
                        syntax.Span));
                }
                else
                {
                    graphEdges.Add(new QuestGraphEdge(
                        $"e-{edgeSource}-{graphId}",
                        edgeSource,
                        sourcePort,
                        graphId,
                        QuestGraphPortPolicy.InputPortId,
                        edgeKind,
                        frame.Order));
                }
            }
            nextGraphParent = graphId;
        }
        frame.Kind = kind;
        frame.SemanticId = semanticId;
        frame.DescendantContainerFingerprint = descendantContainerFingerprint;
        frame.NextGraphParentId = nextGraphParent;
        frame.ResultGraphId = resultGraphId;
        frame.IsEntered = true;
    }

    private static QuestSemanticKind SemanticKind(PxslSyntaxNode syntax) => syntax.Kind switch
    {
        PxslSyntaxKind.Directive => QuestSemanticKind.Directive,
        PxslSyntaxKind.Quest => QuestSemanticKind.Quest,
        PxslSyntaxKind.Flow when syntax.Name == "path" => QuestSemanticKind.OrderedFlow,
        PxslSyntaxKind.Flow => QuestSemanticKind.Branch,
        PxslSyntaxKind.Macro => QuestSemanticKind.Macro,
        PxslSyntaxKind.RawXml => QuestSemanticKind.RawXml,
        _ => QuestSemanticKind.Opaque
    };

    private static string SpanHash(
        QuestSourceSnapshot source,
        PxslTextSpan span,
        List<QuestDiagnostic> diagnostics)
    {
        if (span.Start < 0 || span.Length < 0 || span.End > source.Bytes.Length)
        {
            diagnostics.Add(new QuestDiagnostic(
                "PXSL100",
                QuestDiagnosticSeverity.Error,
                "A projected source span is outside the source snapshot.",
                span));
            return QuestHash.Sha256(ReadOnlySpan<byte>.Empty);
        }
        return QuestHash.Sha256(source.Bytes.Span.Slice(span.Start, span.Length));
    }

    private static IReadOnlyList<QuestGraphPort> Ports(
        PxslSyntaxNode syntax,
        QuestSemanticKind kind,
        QuestCatalog? catalog) =>
        QuestGraphPortPolicy.Ports(syntax, kind, catalog);

    private sealed class ProjectionFrame
    {
        public ProjectionFrame(PxslSyntaxNode syntax, string? semanticParentId,
            string containerFingerprint, string? graphParentId,
            string? previousSiblingGraphId, int order)
        {
            Syntax = syntax;
            SemanticParentId = semanticParentId;
            ContainerFingerprint = containerFingerprint;
            GraphParentId = graphParentId;
            PreviousSiblingGraphId = previousSiblingGraphId;
            Order = order;
        }

        public PxslSyntaxNode Syntax { get; }
        public string? SemanticParentId { get; }
        public string ContainerFingerprint { get; }
        public string? GraphParentId { get; }
        public int Order { get; }
        public bool IsEntered { get; set; }
        public QuestSemanticKind Kind { get; set; }
        public string? SemanticId { get; set; }
        public string DescendantContainerFingerprint { get; set; } = string.Empty;
        public string? NextGraphParentId { get; set; }

        /// <summary>
        /// The ordered sibling that precedes this node, supplied by the parent at push
        /// time and consumed once when this node's incoming edge is built.
        /// </summary>
        public string? PreviousSiblingGraphId { get; }

        /// <summary>How this node attached to its container, once it has been entered.</summary>
        public QuestGraphChildRole? ChildRole { get; set; }

        /// <summary>
        /// The last ordered child this node has finished projecting. It always starts
        /// empty: inheriting the parent's chain here used to wire a container's first
        /// child to the container's own previous sibling.
        /// </summary>
        public string? LastOrderedChildGraphId { get; set; }

        public string? ResultGraphId { get; set; }
        public int NextChildIndex { get; set; }
        public int NextChildOrder { get; set; }
    }

    internal sealed record ProjectionResult(
        IReadOnlyList<QuestSemanticNode> SemanticNodes,
        IReadOnlyList<QuestGraphNode> GraphNodes,
        IReadOnlyList<QuestGraphEdge> GraphEdges,
        IReadOnlyList<QuestDiagnostic> Diagnostics);
}
