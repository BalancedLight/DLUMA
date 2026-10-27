namespace Dluma.QuestGraph.Core;

/// <summary>
/// How a PXSL child attaches to its container.
/// </summary>
internal enum QuestGraphChildRole
{
    /// <summary>A named flow branch such as <c>path</c>, <c>pre</c>, or <c>post</c>.</summary>
    Branch,

    /// <summary>A nested record occupying a typed child slot.</summary>
    NestedSlot,

    /// <summary>An executable step in the container's ordered body.</summary>
    Ordered
}

internal readonly record struct QuestGraphChildBinding(
    QuestGraphChildRole Role,
    string PortId,
    string DisplayName,
    string PortType);

internal static class QuestGraphPortPolicy
{
    public const string InputPortId = "in";
    public const string OrderedSuccessorPortId = "out";
    public const string OrderedBodyPortId = "body";
    public const string GenericChildPortId = "child";
    public const string BranchPortPrefix = "branch:";

    /// <summary>
    /// The node's port set: everything its catalog entry declares, unioned with every
    /// port its own PXSL children actually bind to. The projector wires children through
    /// <see cref="BindChild"/>, so deriving the same bindings here keeps the port set a
    /// superset of the edges that reference it -- a projected edge can never name a port
    /// the node does not expose.
    /// </summary>
    public static IReadOnlyList<QuestGraphPort> Ports(
        PxslSyntaxNode syntax,
        QuestSemanticKind kind,
        QuestCatalog? catalog)
    {
        var ports = DeclaredPorts(syntax, kind, catalog);
        foreach (var child in syntax.Children)
        {
            if (child.Kind is PxslSyntaxKind.Blank or
                PxslSyntaxKind.Comment or
                PxslSyntaxKind.Directive)
                continue;
            var binding = BindChild(kind, syntax.Name, child, catalog);
            AddPort(ports, binding.PortId, binding.DisplayName, binding.PortType);
        }
        return ports;
    }

    /// <summary>
    /// Resolves which container port owns <paramref name="child"/>. This is the single
    /// source of truth shared by <see cref="Ports"/> and the graph projector.
    /// </summary>
    public static QuestGraphChildBinding BindChild(
        QuestSemanticKind parentKind,
        string? parentName,
        PxslSyntaxNode child,
        QuestCatalog? catalog)
    {
        if (child.Kind == PxslSyntaxKind.Flow)
        {
            return new(
                QuestGraphChildRole.Branch,
                BranchPortPrefix + child.Name.ToLowerInvariant(),
                Humanize(child.Name),
                "flow");
        }

        var entry = Entry(parentName, catalog);
        var declared = entry?.ChildSlots.FirstOrDefault(slot =>
            slot.Kind == QuestCatalogChildSlotKind.Child &&
            (slot.Id.Equals(child.Name, StringComparison.OrdinalIgnoreCase) ||
                slot.AcceptedEntryNames.Contains(child.Name, StringComparer.OrdinalIgnoreCase)));
        if (declared is not null)
            return new(QuestGraphChildRole.NestedSlot, declared.PortId, declared.DisplayName, "child");

        // PXSL lets a compound's body branch stay implicit, so ordinary macros nest
        // straight under ,BLOCK and friends. Those are ordered steps, not records.
        if (OrdersChildren(parentKind, parentName, catalog))
        {
            return new(
                QuestGraphChildRole.Ordered,
                OrderedParentPort(parentKind, parentName, catalog),
                "Out",
                "flow");
        }

        if (entry is not null)
        {
            var onlyChild = entry.ChildSlots
                .Where(slot => slot.Kind == QuestCatalogChildSlotKind.Child)
                .Take(2)
                .ToArray();
            if (onlyChild.Length == 1)
            {
                return new(
                    QuestGraphChildRole.NestedSlot,
                    onlyChild[0].PortId,
                    onlyChild[0].DisplayName,
                    "child");
            }
        }

        return new(QuestGraphChildRole.NestedSlot, GenericChildPortId, "Child", "child");
    }

    /// <summary>
    /// Picks the port an ordered edge leaves from, guaranteed to exist on
    /// <paramref name="ports"/>. Returns <see langword="null"/> when the node exposes no
    /// usable output at all, which lets the caller chain from the container instead.
    /// </summary>
    public static string? ResolveOrderedSuccessorPort(IReadOnlyList<QuestGraphPort> ports) =>
        Has(ports, OrderedSuccessorPortId) ? OrderedSuccessorPortId : null;

    /// <summary>
    /// Falls back to a port that exists when the preferred one is absent, so an edge is
    /// never emitted against a port the node does not expose.
    /// </summary>
    public static string? ResolveParentPort(
        IReadOnlyList<QuestGraphPort> ports,
        string preferred)
    {
        if (Has(ports, preferred))
            return preferred;
        foreach (var candidate in new[]
        {
            OrderedBodyPortId,
            OrderedSuccessorPortId,
            GenericChildPortId
        })
        {
            if (Has(ports, candidate))
                return candidate;
        }
        return ports.FirstOrDefault(port =>
            port.Direction == QuestGraphPortDirection.Output)?.Id;
    }

    public static bool OrdersChildren(
        QuestSemanticKind? kind,
        string? name,
        QuestCatalog? catalog)
    {
        if (kind is QuestSemanticKind.Quest or QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch)
            return true;
        return kind == QuestSemanticKind.Macro &&
            Entry(name, catalog)?.ChildSlots.Any(slot =>
                slot.Kind == QuestCatalogChildSlotKind.OrderedBody) == true;
    }

    public static string OrderedParentPort(
        QuestSemanticKind? kind,
        string? name,
        QuestCatalog? catalog)
    {
        if (kind == QuestSemanticKind.Quest)
            return OrderedBodyPortId;
        if (kind == QuestSemanticKind.Macro &&
            Entry(name, catalog)?.ChildSlots.Any(slot =>
                slot.Kind == QuestCatalogChildSlotKind.OrderedBody) == true)
            return OrderedBodyPortId;
        return OrderedSuccessorPortId;
    }

    private static List<QuestGraphPort> DeclaredPorts(
        PxslSyntaxNode syntax,
        QuestSemanticKind kind,
        QuestCatalog? catalog)
    {
        if (kind == QuestSemanticKind.Quest)
            return [Input(), Output(OrderedBodyPortId, "Out", "flow", true)];

        if (kind is QuestSemanticKind.OrderedFlow or QuestSemanticKind.Branch)
            return [Input(), Output(OrderedSuccessorPortId, "Out", "flow", true)];

        if (kind == QuestSemanticKind.Macro &&
            catalog?.TryGetMacro(syntax.Name, out var entry) == true &&
            entry is not null)
        {
            var ports = new List<QuestGraphPort> { Input() };
            if (entry.Category is not (
                QuestCatalogFunctionalCategory.ChildRecords or
                QuestCatalogFunctionalCategory.SupportingDefinition))
            {
                ports.Add(Output(OrderedSuccessorPortId, "Out", "flow", true));
            }
            foreach (var slot in entry.ChildSlots)
            {
                AddPort(
                    ports,
                    slot.PortId,
                    slot.DisplayName,
                    slot.Kind == QuestCatalogChildSlotKind.Child ? "child" : "flow",
                    AllowsMultiple(slot.Quantity));
            }
            return ports;
        }

        if (kind is QuestSemanticKind.RawXml or QuestSemanticKind.Opaque)
            return [Input()];

        // Unknown/custom macros retain only roles proven by their PXSL shape.
        // Never invent the old universal branch/child port set.
        return [Input(), Output(OrderedSuccessorPortId, "Out", "flow", true)];
    }

    private static QuestCatalogEntry? Entry(string? name, QuestCatalog? catalog) =>
        name is not null && catalog?.TryGetMacro(name, out var entry) == true
            ? entry
            : null;

    private static bool Has(IReadOnlyList<QuestGraphPort> ports, string id) =>
        ports.Any(port =>
            port.Direction == QuestGraphPortDirection.Output &&
            port.Id.Equals(id, StringComparison.Ordinal));

    private static void AddPort(
        List<QuestGraphPort> ports,
        string id,
        string displayName,
        string type,
        bool allowsMultiple = true)
    {
        if (ports.Any(port => port.Id.Equals(id, StringComparison.Ordinal)))
            return;
        ports.Add(Output(id, displayName, type, allowsMultiple));
    }

    private static bool AllowsMultiple(string quantity)
    {
        var normalized = quantity?.Trim();
        return normalized is "*" or "+" ||
            int.TryParse(normalized, out var value) && value > 1;
    }

    private static QuestGraphPort Input() =>
        new(InputPortId, "In", QuestGraphPortDirection.Input, "flow", false);

    private static QuestGraphPort Output(
        string id,
        string displayName,
        string type,
        bool allowsMultiple) =>
        new(id, displayName, QuestGraphPortDirection.Output, type, allowsMultiple);

    private static string Humanize(string value) => value.Length == 0
        ? value
        : char.ToUpperInvariant(value[0]) + value[1..];
}
