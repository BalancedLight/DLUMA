namespace Dluma.QuestGraph.Core;

public readonly record struct QuestGraphLayoutPoint(double X, double Y);

public enum QuestGraphFlowOrientation
{
    /// <summary>An ordered flow reads left to right; nesting descends.</summary>
    FlowRightNestDown,

    /// <summary>An ordered flow reads top to bottom; nesting extends to the right.</summary>
    FlowDownNestRight
}

public sealed record QuestGraphAutoLayoutOptions
{
    /// <summary>Matches the node template's MaxWidth so estimates never under-reserve.</summary>
    public double NodeWidth { get; init; } = 310;

    public double HeaderHeight { get; init; } = 58;
    public double PortRowHeight { get; init; } = 24;
    public double NodePadding { get; init; } = 18;
    public double CollapsedHeight { get; init; } = 56;

    public QuestGraphFlowOrientation Orientation { get; init; } =
        QuestGraphFlowOrientation.FlowRightNestDown;

    /// <summary>Gap between adjacent subtrees along the flow axis.</summary>
    public double FlowGap { get; init; } = 40;

    /// <summary>Gap between successive nesting bands.</summary>
    public double BandGap { get; init; } = 90;

    public double MarginX { get; init; } = 80;
    public double MarginY { get; init; } = 80;

    public static QuestGraphAutoLayoutOptions Default { get; } = new();

    internal double FlowOrigin => Orientation == QuestGraphFlowOrientation.FlowRightNestDown
        ? MarginX
        : MarginY;

    internal double BandOrigin => Orientation == QuestGraphFlowOrientation.FlowRightNestDown
        ? MarginY
        : MarginX;
}

public sealed record QuestGraphAutoLayoutResult(
    IReadOnlyDictionary<string, QuestGraphLayoutPoint> Positions,
    double Width,
    double Height);

/// <summary>
/// Arranges a projected quest graph as a tidy tree. By default an ordered flow reads
/// left to right along a band and a container's contents hang in the band below it;
/// <see cref="QuestGraphFlowOrientation.FlowDownNestRight"/> transposes that. Either
/// way a subtree reserves its own extent along the flow axis, so sibling branches
/// never overlap and edges do not cross between unrelated containers.
/// </summary>
/// <remarks>
/// Containers align with the leading edge of their first child rather than centring
/// over the whole block. Ports are mounted on the node's sides, so a leading-edge
/// container keeps the wire to its first child short. The walk is iterative:
/// authored PXSL nests arbitrarily deep.
/// </remarks>
public static class QuestGraphAutoLayout
{
    public static QuestGraphAutoLayoutResult Compute(
        QuestGraphDocument document,
        QuestGraphAutoLayoutOptions? options = null,
        IReadOnlySet<string>? collapsedGraphNodeIds = null)
    {
        ArgumentNullException.ThrowIfNull(document);
        var settings = options ?? QuestGraphAutoLayoutOptions.Default;
        var positions = new Dictionary<string, QuestGraphLayoutPoint>(StringComparer.Ordinal);
        if (document.GraphNodes.Count == 0)
            return new QuestGraphAutoLayoutResult(positions, 0, 0);

        var tree = BuildTree(document);
        var sizes = document.GraphNodes.ToDictionary(
            node => node.Id,
            node => Size(node, settings, collapsedGraphNodeIds),
            StringComparer.Ordinal);

        var depths = Depths(tree);
        var bandStarts = BandStarts(depths, sizes, settings);
        var extents = Extents(tree, sizes, settings);

        var cursor = settings.FlowOrigin;
        var flowEnd = settings.FlowOrigin;
        foreach (var root in tree.Roots)
        {
            Place(root, cursor, tree, extents, depths, bandStarts, positions, settings);
            flowEnd = Math.Max(flowEnd, cursor + extents[root]);
            cursor += extents[root] + settings.FlowGap;
        }

        var bandEnd = positions.Count == 0
            ? settings.BandOrigin
            : positions.Max(entry => Band(entry.Value, settings) + sizes[entry.Key].Band);
        var flowLength = flowEnd - settings.FlowOrigin;
        var bandLength = bandEnd - settings.BandOrigin;
        return settings.Orientation == QuestGraphFlowOrientation.FlowRightNestDown
            ? new QuestGraphAutoLayoutResult(positions, flowLength, bandLength)
            : new QuestGraphAutoLayoutResult(positions, bandLength, flowLength);
    }

    private static QuestGraphLayoutPoint Point(
        double flow,
        double band,
        QuestGraphAutoLayoutOptions settings) =>
        settings.Orientation == QuestGraphFlowOrientation.FlowRightNestDown
            ? new QuestGraphLayoutPoint(flow, band)
            : new QuestGraphLayoutPoint(band, flow);

    private static double Band(
        QuestGraphLayoutPoint point,
        QuestGraphAutoLayoutOptions settings) =>
        settings.Orientation == QuestGraphFlowOrientation.FlowRightNestDown
            ? point.Y
            : point.X;

    /// <summary>
    /// Maps each graph node to the graph node that contains it, skipping semantic
    /// nodes that project nothing. Callers placing a single new node use this to
    /// anchor it under its container instead of re-arranging the whole graph.
    /// </summary>
    public static IReadOnlyDictionary<string, string> ContainerByGraphNodeId(
        QuestGraphDocument document)
    {
        ArgumentNullException.ThrowIfNull(document);
        var tree = BuildTree(document);
        var containers = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var (parent, children) in tree.Children)
        {
            foreach (var child in children)
                containers[child] = parent;
        }
        return containers;
    }

    private static void Place(
        string root,
        double flowStart,
        LayoutTree tree,
        IReadOnlyDictionary<string, double> extents,
        IReadOnlyDictionary<string, int> depths,
        IReadOnlyDictionary<int, double> bandStarts,
        Dictionary<string, QuestGraphLayoutPoint> positions,
        QuestGraphAutoLayoutOptions settings)
    {
        var pending = new Stack<(string Id, double Flow)>();
        pending.Push((root, flowStart));
        while (pending.Count > 0)
        {
            var (id, flow) = pending.Pop();
            positions[id] = Point(flow, bandStarts[depths[id]], settings);
            if (!tree.Children.TryGetValue(id, out var children))
                continue;
            var childFlow = flow;
            var placements = new List<(string Id, double Flow)>(children.Count);
            foreach (var child in children)
            {
                placements.Add((child, childFlow));
                childFlow += extents[child] + settings.FlowGap;
            }
            for (var index = placements.Count - 1; index >= 0; --index)
                pending.Push(placements[index]);
        }
    }

    private static IReadOnlyDictionary<string, double> Extents(
        LayoutTree tree,
        IReadOnlyDictionary<string, QuestGraphSize> sizes,
        QuestGraphAutoLayoutOptions settings)
    {
        var extents = new Dictionary<string, double>(StringComparer.Ordinal);
        foreach (var root in tree.Roots)
        {
            var pending = new Stack<(string Id, bool ChildrenVisited)>();
            pending.Push((root, false));
            while (pending.Count > 0)
            {
                var (id, childrenVisited) = pending.Pop();
                if (extents.ContainsKey(id))
                    continue;
                var children = tree.Children.GetValueOrDefault(id) ?? Array.Empty<string>();
                if (!childrenVisited && children.Count > 0)
                {
                    pending.Push((id, true));
                    for (var index = children.Count - 1; index >= 0; --index)
                        pending.Push((children[index], false));
                    continue;
                }
                var block = 0.0;
                foreach (var child in children)
                    block += extents.GetValueOrDefault(child) + settings.FlowGap;
                if (children.Count > 0)
                    block -= settings.FlowGap;
                extents[id] = Math.Max(sizes[id].Flow, block);
            }
        }
        return extents;
    }

    private static IReadOnlyDictionary<string, int> Depths(LayoutTree tree)
    {
        var depths = new Dictionary<string, int>(StringComparer.Ordinal);
        var pending = new Stack<(string Id, int Depth)>();
        for (var index = tree.Roots.Count - 1; index >= 0; --index)
            pending.Push((tree.Roots[index], 0));
        while (pending.Count > 0)
        {
            var (id, depth) = pending.Pop();
            if (!depths.TryAdd(id, depth))
                continue;
            if (!tree.Children.TryGetValue(id, out var children))
                continue;
            for (var index = children.Count - 1; index >= 0; --index)
                pending.Push((children[index], depth + 1));
        }
        return depths;
    }

    private static IReadOnlyDictionary<int, double> BandStarts(
        IReadOnlyDictionary<string, int> depths,
        IReadOnlyDictionary<string, QuestGraphSize> sizes,
        QuestGraphAutoLayoutOptions settings)
    {
        var thickness = new Dictionary<int, double>();
        foreach (var (id, depth) in depths)
        {
            var band = sizes[id].Band;
            if (!thickness.TryGetValue(depth, out var current) || band > current)
                thickness[depth] = band;
        }
        var starts = new Dictionary<int, double>();
        var offset = settings.BandOrigin;
        foreach (var depth in thickness.Keys.Order())
        {
            starts[depth] = offset;
            offset += thickness[depth] + settings.BandGap;
        }
        return starts;
    }

    /// <summary>
    /// The height this layout reserves for a node. Ports are stacked, so the port
    /// count drives the card's height; callers that need to bound a node -- fitting a
    /// group box, avoiding a collision -- should use this rather than guessing.
    /// </summary>
    public static double EstimateHeight(
        QuestGraphNode node,
        QuestGraphAutoLayoutOptions? options = null,
        bool isCollapsed = false)
    {
        ArgumentNullException.ThrowIfNull(node);
        var settings = options ?? QuestGraphAutoLayoutOptions.Default;
        if (isCollapsed)
            return settings.CollapsedHeight;
        var rows = Math.Max(
            node.Ports.Count(port => port.Direction == QuestGraphPortDirection.Input),
            node.Ports.Count(port => port.Direction == QuestGraphPortDirection.Output));
        return settings.HeaderHeight + rows * settings.PortRowHeight + settings.NodePadding;
    }

    private static QuestGraphSize Size(
        QuestGraphNode node,
        QuestGraphAutoLayoutOptions settings,
        IReadOnlySet<string>? collapsedGraphNodeIds)
    {
        var height = EstimateHeight(
            node, settings, collapsedGraphNodeIds?.Contains(node.Id) == true);
        return settings.Orientation == QuestGraphFlowOrientation.FlowRightNestDown
            ? new QuestGraphSize(settings.NodeWidth, height)
            : new QuestGraphSize(height, settings.NodeWidth);
    }

    /// <summary>
    /// Rebuilds the containment forest over graph nodes. Semantic nodes that project
    /// no graph node -- directives such as QuestsDefinitions -- are transparent, so
    /// their children reattach to the nearest ancestor that does have one.
    /// </summary>
    private static LayoutTree BuildTree(QuestGraphDocument document)
    {
        var semanticById = document.SemanticNodes.ToDictionary(
            node => node.Id, StringComparer.Ordinal);
        var graphBySemantic = document.GraphNodes.ToDictionary(
            node => node.SemanticNodeId, StringComparer.Ordinal);

        var roots = new List<QuestGraphNode>();
        var children = new Dictionary<string, List<QuestGraphNode>>(StringComparer.Ordinal);
        foreach (var graphNode in document.GraphNodes)
        {
            if (!semanticById.TryGetValue(graphNode.SemanticNodeId, out var semantic))
            {
                roots.Add(graphNode);
                continue;
            }
            var ancestorId = semantic.ParentId;
            QuestGraphNode? parent = null;
            var guard = 0;
            while (ancestorId is not null && guard++ < document.SemanticNodes.Count)
            {
                if (graphBySemantic.TryGetValue(ancestorId, out var candidate))
                {
                    parent = candidate;
                    break;
                }
                if (!semanticById.TryGetValue(ancestorId, out var ancestor))
                    break;
                ancestorId = ancestor.ParentId;
            }
            if (parent is null)
            {
                roots.Add(graphNode);
                continue;
            }
            if (!children.TryGetValue(parent.Id, out var bucket))
                children[parent.Id] = bucket = new List<QuestGraphNode>();
            bucket.Add(graphNode);
        }

        return new LayoutTree(
            roots
                .OrderBy(node => Span(semanticById, node))
                .Select(node => node.Id)
                .ToArray(),
            children.ToDictionary(
                entry => entry.Key,
                entry => (IReadOnlyList<string>)entry.Value
                    .OrderBy(node => node.Order)
                    .ThenBy(node => Span(semanticById, node))
                    .Select(node => node.Id)
                    .ToArray(),
                StringComparer.Ordinal));
    }

    private static int Span(
        IReadOnlyDictionary<string, QuestSemanticNode> semanticById,
        QuestGraphNode node) =>
        semanticById.TryGetValue(node.SemanticNodeId, out var semantic)
            ? semantic.Span.Start
            : int.MaxValue;

    /// <summary>
    /// A node's extent along the flow axis and its thickness across the band axis.
    /// The orientation decides which of those is the card's width.
    /// </summary>
    private readonly record struct QuestGraphSize(double Flow, double Band);

    private sealed record LayoutTree(
        IReadOnlyList<string> Roots,
        IReadOnlyDictionary<string, IReadOnlyList<string>> Children);
}
