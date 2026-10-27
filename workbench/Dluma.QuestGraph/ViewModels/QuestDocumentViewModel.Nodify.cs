using System.Runtime.CompilerServices;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class QuestDocumentViewModel
{
    private AsyncRelayCommand? _completeConnectorConnectionCommand;
    private AsyncRelayCommand? _disconnectConnectorCommand;

    public Func<
        QuestPortViewModel,
        IReadOnlyList<QuestPaletteItemViewModel>>? ConnectionPaletteProvider { get; set; }
    public Func<IReadOnlyList<QuestPaletteItemViewModel>>? PaletteProvider { get; set; }

    public IReadOnlyList<QuestPaletteItemViewModel> GetPaletteItems(bool asChild)
    {
        if (PaletteProvider is null)
            return Array.Empty<QuestPaletteItemViewModel>();
        return PaletteProvider()
            .Where(item => item.CatalogEntry?.IsInsertable != false)
            .Where(item => asChild
                ? item.CatalogEntry?.Category == QuestCatalogFunctionalCategory.ChildRecords
                : item.CatalogEntry?.Category != QuestCatalogFunctionalCategory.ChildRecords)
            .OrderBy(item => item.Category, StringComparer.OrdinalIgnoreCase)
            .ThenBy(item => item.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public Task<bool> InsertFromPaletteAsync(
        QuestPaletteItemViewModel item,
        bool asChild) => asChild
            ? InsertCompatibleChildAsync(item)
            : InsertAfterCompatibleAsync(item);

    public bool CanInsertCompatibleChild(QuestPaletteItemViewModel? item) =>
        item is not null && SelectedNode is not null &&
        TryGetCompatibleChildPort(SelectedNode, item, out _, reportError: false);

    public async Task<bool> InsertCompatibleChildAsync(
        QuestPaletteItemViewModel item,
        QuestNodeViewModel? explicitTarget = null,
        System.Windows.Point? graphLocation = null)
    {
        var target = explicitTarget ?? SelectedNode;
        if (target is null)
        {
            Status = "Select a compatible container before adding a child.";
            return false;
        }
        if (!TryGetCompatibleChildPort(target, item, out var port, reportError: true))
            return false;
        var before = SourceSha256;
        await InsertConnectedAsync(
            port,
            item,
            graphLocation ?? new System.Windows.Point(
                target.Location.X + 320,
                target.Location.Y + 40)).ConfigureAwait(true);
        return !string.Equals(before, SourceSha256, StringComparison.Ordinal);
    }

    public async Task<bool> InsertAfterCompatibleAsync(
        QuestPaletteItemViewModel item,
        QuestNodeViewModel? explicitTarget = null,
        System.Windows.Point? graphLocation = null)
    {
        var target = explicitTarget ?? SelectedNode;
        var isQuestDeclaration = item.InsertionText.TrimStart().StartsWith(
            "quest ", StringComparison.OrdinalIgnoreCase);
        if (target is null && isQuestDeclaration)
            return await InsertAsync(item, false, graphLocation).ConfigureAwait(true);
        if (target is not null && isQuestDeclaration)
        {
            Status = "A quest declaration cannot be inserted after another node.";
            return false;
        }
        if (target is null)
        {
            Status = "Select an executable node before inserting after it.";
            return false;
        }
        var port = target.Outputs.FirstOrDefault(candidate => candidate.Id == "out") ??
            target.Outputs.FirstOrDefault(candidate => candidate.Id == "body");
        if (port is null || item.CatalogEntry?.Category == QuestCatalogFunctionalCategory.ChildRecords)
        {
            Status = "The selected node has no compatible ordered output for this palette entry.";
            return false;
        }
        var before = SourceSha256;
        await InsertConnectedAsync(
            port,
            item,
            graphLocation ?? new System.Windows.Point(
                target.Location.X + 320,
                target.Location.Y)).ConfigureAwait(true);
        return !string.Equals(before, SourceSha256, StringComparison.Ordinal);
    }

    public Task<bool> DropPaletteItemAsync(
        QuestPaletteItemViewModel item,
        QuestNodeViewModel? target,
        System.Windows.Point graphLocation)
    {
        target ??= SelectedNode ?? Nodes.FirstOrDefault(node =>
            node.SemanticNode.Kind == QuestSemanticKind.Quest);
        if (target is null)
            return InsertAfterCompatibleAsync(item, null, graphLocation);
        return InsertCompatibleChildAsync(item, target, graphLocation);
    }

    public async Task DuplicateSelectedAsync()
    {
        if (SelectedNode is null)
        {
            Status = "Select a node to duplicate.";
            return;
        }
        CopySelectedSubtree();
        await PasteAsync(QuestGraphPastePlacement.After).ConfigureAwait(true);
    }

    public Task DisconnectEdgeAsync(string edgeId) => RunMutationAsync(() =>
        QuestGraphMutationService.Disconnect(_model, edgeId));

    private bool TryGetCompatibleChildPort(
        QuestNodeViewModel target,
        QuestPaletteItemViewModel item,
        out QuestPortViewModel port,
        bool reportError)
    {
        port = null!;
        if (item.InsertionText.TrimStart().StartsWith("quest ", StringComparison.OrdinalIgnoreCase))
        {
            if (reportError)
                Status = "A quest declaration cannot be nested under another node.";
            return false;
        }
        var isChildRecord = item.CatalogEntry?.Category ==
            QuestCatalogFunctionalCategory.ChildRecords;
        var candidates = target.Outputs.Where(candidate => isChildRecord
            ? candidate.Type == "child"
            : candidate.Id == "body" || candidate.Id.StartsWith("branch:", StringComparison.Ordinal))
            .ToArray();
        if (isChildRecord && _catalog?.TryGetEntry(target.SemanticNode.Name, out var owner) == true &&
            owner is not null)
        {
            candidates = candidates.Where(candidate =>
            {
                var slot = owner.ChildSlots.FirstOrDefault(child =>
                    child.PortId.Equals(candidate.Id, StringComparison.Ordinal));
                return slot is not null &&
                    (slot.AcceptedEntryNames.Count == 0 ||
                     slot.AcceptedEntryNames.Contains(item.Name, StringComparer.OrdinalIgnoreCase));
            }).ToArray();
        }
        if (candidates.Length == 1)
        {
            port = candidates[0];
            return true;
        }
        if (reportError)
        {
            Status = candidates.Length == 0
                ? "The selected node has no compatible child slot for this palette entry."
                : "The selected node has multiple compatible branches; drag from the intended output connector.";
        }
        return false;
    }

    public AsyncRelayCommand CompleteConnectorConnectionCommand =>
        _completeConnectorConnectionCommand ??= new AsyncRelayCommand(
            CompleteConnectorConnectionAsync);

    public AsyncRelayCommand DisconnectConnectorCommand =>
        _disconnectConnectorCommand ??= new AsyncRelayCommand(
            DisconnectConnectorAsync);

    public IReadOnlyList<QuestPaletteItemViewModel> GetConnectionPaletteItems(
        QuestPortViewModel sourcePort)
    {
        if (sourcePort.Direction != QuestGraphPortDirection.Output ||
            ConnectionPaletteProvider is null)
            return Array.Empty<QuestPaletteItemViewModel>();
        var candidates = ConnectionPaletteProvider(sourcePort).AsEnumerable();
        var owner = FindPortOwner(sourcePort);
        if (owner is not null && sourcePort.Id.StartsWith("child:", StringComparison.Ordinal) &&
            _catalog?.TryGetEntry(owner.SemanticNode.Name, out var ownerEntry) == true &&
            ownerEntry is not null)
        {
            var slot = ownerEntry.ChildSlots.FirstOrDefault(candidate =>
                candidate.PortId.Equals(sourcePort.Id, StringComparison.Ordinal));
            if (slot?.AcceptedEntryNames.Count > 0)
            {
                candidates = candidates.Where(item =>
                    slot.AcceptedEntryNames.Contains(item.Name, StringComparer.OrdinalIgnoreCase));
            }
        }
        return candidates
            .Where(item => !item.InsertionText.TrimStart().StartsWith(
                "quest ",
                StringComparison.OrdinalIgnoreCase))
            .OrderBy(item => item.Category, StringComparer.OrdinalIgnoreCase)
            .ThenBy(item => item.Name, StringComparer.OrdinalIgnoreCase)
            .Take(500)
            .ToArray();
    }

    public Task ConnectPortsAsync(
        QuestPortViewModel first,
        QuestPortViewModel second)
    {
        ArgumentNullException.ThrowIfNull(first);
        ArgumentNullException.ThrowIfNull(second);
        var sourcePort = first.Direction == QuestGraphPortDirection.Output
            ? first
            : second.Direction == QuestGraphPortDirection.Output
                ? second
                : null;
        var targetPort = first.Direction == QuestGraphPortDirection.Input
            ? first
            : second.Direction == QuestGraphPortDirection.Input
                ? second
                : null;
        if (sourcePort is null || targetPort is null)
        {
            Status = "Connect an output port to an input port.";
            return Task.CompletedTask;
        }

        var sourceNode = FindPortOwner(sourcePort);
        var targetNode = FindPortOwner(targetPort);
        if (sourceNode is null || targetNode is null)
        {
            Status = "The connector no longer belongs to the current graph projection.";
            return Task.CompletedTask;
        }
        if (ReferenceEquals(sourceNode, targetNode))
        {
            Status = "A quest node cannot connect to itself.";
            return Task.CompletedTask;
        }
        return RunMutationAsync(() => QuestGraphMutationService.Connect(
            _model,
            sourceNode.SemanticNode.Id,
            sourcePort.Id,
            targetNode.SemanticNode.Id));
    }

    public Task DisconnectPortAsync(QuestPortViewModel port)
    {
        ArgumentNullException.ThrowIfNull(port);
        var owner = FindPortOwner(port);
        if (owner is null)
        {
            Status = "The connector no longer belongs to the current graph projection.";
            return Task.CompletedTask;
        }
        var edges = port.Direction == QuestGraphPortDirection.Output
            ? _model.GraphEdges.Where(candidate =>
                string.Equals(candidate.SourceNodeId, owner.GraphNode.Id, StringComparison.Ordinal) &&
                string.Equals(candidate.SourcePortId, port.Id, StringComparison.Ordinal)).ToArray()
            : _model.GraphEdges.Where(candidate =>
                string.Equals(candidate.TargetNodeId, owner.GraphNode.Id, StringComparison.Ordinal) &&
                string.Equals(candidate.TargetPortId, port.Id, StringComparison.Ordinal)).ToArray();
        if (edges.Length == 0)
        {
            Status = "The selected connector is already disconnected.";
            return Task.CompletedTask;
        }
        if (edges.Length > 1)
        {
            Status = "This output has multiple connections; choose the exact outgoing edge from the node context menu.";
            return Task.CompletedTask;
        }
        return RunMutationAsync(() => QuestGraphMutationService.Disconnect(
            _model,
            edges[0].Id));
    }

    public async Task InsertConnectedAsync(
        QuestPortViewModel sourcePort,
        QuestPaletteItemViewModel item,
        System.Windows.Point graphLocation)
    {
        ArgumentNullException.ThrowIfNull(sourcePort);
        ArgumentNullException.ThrowIfNull(item);
        if (sourcePort.Direction != QuestGraphPortDirection.Output)
        {
            Status = "New nodes can only be created from an output connector.";
            return;
        }
        var sourceNode = FindPortOwner(sourcePort);
        if (sourceNode is null)
        {
            Status = "The source connector no longer belongs to the current graph projection.";
            return;
        }
        var sourceStableId = sourceNode.StableId;
        var sourcePortId = sourcePort.Id;
        var insertionItem = item;
        var insertionAsChild = sourcePortId != "out";
        var insertionTarget = sourceNode;
        var wrapsBranch = sourcePortId.StartsWith("branch:", StringComparison.Ordinal);
        if (wrapsBranch)
        {
            var branchName = sourcePortId["branch:".Length..];
            var normalized = item.InsertionText
                .Replace("\r\n", "\n", StringComparison.Ordinal)
                .Replace('\r', '\n');
            insertionItem = item with
            {
                InsertionText = branchName + "\n    " +
                    normalized.Replace("\n", "\n    ", StringComparison.Ordinal)
            };
        }
        if (!await InsertAsync(
                insertionItem,
                insertionAsChild,
                wrapsBranch ? null : graphLocation,
                insertionTarget).ConfigureAwait(true))
            return;

        var targetNode = SelectedNode;
        if (wrapsBranch)
        {
            targetNode = Nodes
                .Where(node => node.SemanticNode.Name.Equals(
                    item.CatalogEntry?.Name ?? item.Name,
                    StringComparison.OrdinalIgnoreCase))
                .OrderByDescending(node => node.SemanticNode.Span.Start)
                .FirstOrDefault() ?? SelectedNode;
            if (targetNode is not null)
            {
                _isApplyingState = true;
                try
                {
                    foreach (var selected in SelectedItems)
                        selected.IsSelected = false;
                    SelectedItems.Clear();
                    targetNode.Location = graphLocation;
                    targetNode.IsSelected = true;
                    SelectedItems.Add(targetNode);
                    SelectedNode = targetNode;
                }
                finally
                {
                    _isApplyingState = false;
                }
                NotifyDirty();
            }
        }
        var currentSource = Nodes.SingleOrDefault(node =>
            string.Equals(node.StableId, sourceStableId, StringComparison.Ordinal));
        if (currentSource is null || targetNode is null || ReferenceEquals(currentSource, targetNode))
        {
            Status = "The node was inserted, but its source connector could not be reconciled after the source edit.";
            return;
        }
        var expectedTargetGraphId = targetNode.GraphNode.Id;
        if (wrapsBranch && targetNode.SemanticNode.ParentId is { } branchSemanticId)
        {
            expectedTargetGraphId = _model.GraphNodes.FirstOrDefault(node =>
                node.SemanticNodeId == branchSemanticId)?.Id ?? expectedTargetGraphId;
        }
        if (_model.GraphEdges.Any(edge =>
                string.Equals(edge.SourceNodeId, currentSource.GraphNode.Id, StringComparison.Ordinal) &&
                string.Equals(edge.SourcePortId, sourcePortId, StringComparison.Ordinal) &&
                string.Equals(edge.TargetNodeId, expectedTargetGraphId, StringComparison.Ordinal)))
            return;
        Status = "The node was inserted, but its requested source port did not project a compatible connection.";
    }

    private Task CompleteConnectorConnectionAsync(object? parameter)
    {
        if (!TryGetConnectorPair(parameter, out var first, out var second))
        {
            Status = "Drop the pending connection on another connector to connect it.";
            return Task.CompletedTask;
        }
        return ConnectPortsAsync(first, second);
    }

    private Task DisconnectConnectorAsync(object? parameter) =>
        parameter is QuestPortViewModel port
            ? DisconnectPortAsync(port)
            : Task.CompletedTask;

    private QuestNodeViewModel? FindPortOwner(QuestPortViewModel port) =>
        Nodes.FirstOrDefault(node =>
            node.Inputs.Contains(port) || node.Outputs.Contains(port));

    private static bool TryGetConnectorPair(
        object? parameter,
        out QuestPortViewModel first,
        out QuestPortViewModel second)
    {
        if (parameter is ITuple { Length: 2 } tuple &&
            tuple[0] is QuestPortViewModel firstPort &&
            tuple[1] is QuestPortViewModel secondPort)
        {
            first = firstPort;
            second = secondPort;
            return true;
        }
        first = null!;
        second = null!;
        return false;
    }
}
