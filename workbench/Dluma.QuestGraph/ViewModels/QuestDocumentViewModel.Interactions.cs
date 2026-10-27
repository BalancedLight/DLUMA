using System.Collections.ObjectModel;
using System.Text;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class QuestDocumentViewModel
{
    private static QuestGraphClipboardFragment? s_graphClipboard;
    private RelayCommand? _copyNodeCommand;
    private AsyncRelayCommand? _pasteAfterCommand;
    private AsyncRelayCommand? _pasteChildCommand;
    private AsyncRelayCommand? _moveUpCommand;
    private AsyncRelayCommand? _moveDownCommand;
    private AsyncRelayCommand? _connectSelectedCommand;
    private AsyncRelayCommand? _disconnectSelectedCommand;
    private RelayCommand? _groupSelectedCommand;
    private RelayCommand? _ungroupSelectedCommand;
    private string _selectedConnectionPort = "out";
    private QuestGroupViewModel? _selectedGroup;

    public ObservableCollection<QuestGroupViewModel> Groups { get; } = new();

    public IReadOnlyList<string> ConnectionPorts { get; } =
    [
        "out",
        "child",
        "branch:path",
        "branch:fallback",
        "branch:pre",
        "branch:post"
    ];

    public string SelectedConnectionPort
    {
        get => _selectedConnectionPort;
        set => SetProperty(ref _selectedConnectionPort, value);
    }

    public QuestGroupViewModel? SelectedGroup
    {
        get => _selectedGroup;
        set => SetProperty(ref _selectedGroup, value);
    }

    public RelayCommand CopyNodeCommand =>
        _copyNodeCommand ??= new RelayCommand(CopySelectedSubtree);

    public AsyncRelayCommand PasteAfterCommand =>
        _pasteAfterCommand ??= new AsyncRelayCommand(() => PasteAsync(QuestGraphPastePlacement.After));

    public AsyncRelayCommand PasteChildCommand =>
        _pasteChildCommand ??= new AsyncRelayCommand(() => PasteAsync(QuestGraphPastePlacement.Child));

    public AsyncRelayCommand MoveUpCommand =>
        _moveUpCommand ??= new AsyncRelayCommand(() => MoveSelectedAsync(QuestGraphMoveDirection.Up));

    public AsyncRelayCommand MoveDownCommand =>
        _moveDownCommand ??= new AsyncRelayCommand(() => MoveSelectedAsync(QuestGraphMoveDirection.Down));

    public AsyncRelayCommand ConnectSelectedCommand =>
        _connectSelectedCommand ??= new AsyncRelayCommand(ConnectSelectedAsync);

    public AsyncRelayCommand DisconnectSelectedCommand =>
        _disconnectSelectedCommand ??= new AsyncRelayCommand(DisconnectSelectedAsync);

    public RelayCommand GroupSelectedCommand =>
        _groupSelectedCommand ??= new RelayCommand(GroupSelectedNodes);

    public RelayCommand UngroupSelectedCommand =>
        _ungroupSelectedCommand ??= new RelayCommand(UngroupSelectedNodes);

    private void CopySelectedSubtree()
    {
        var selected = SelectedNode;
        if (selected is null)
        {
            Status = "Select a node to copy.";
            return;
        }
        try
        {
            s_graphClipboard = QuestGraphMutationService.CopySubtree(
                _model,
                selected.SemanticNode.Id);
            Status = $"Copied {selected.Title} and its nested source to the quest graph clipboard.";
        }
        catch (QuestGraphOperationException exception)
        {
            Status = exception.Message;
        }
    }

    private Task PasteAsync(QuestGraphPastePlacement placement)
    {
        var target = SelectedNode;
        if (s_graphClipboard is null)
        {
            Status = "Copy an editable node before pasting.";
            return Task.CompletedTask;
        }
        if (target is null)
        {
            Status = "Select the destination node before pasting.";
            return Task.CompletedTask;
        }
        return RunMutationAsync(() => QuestGraphMutationService.Paste(
            _model,
            target.SemanticNode.Id,
            s_graphClipboard,
            placement));
    }

    private Task MoveSelectedAsync(QuestGraphMoveDirection direction)
    {
        var selected = SelectedNode;
        if (selected is null)
        {
            Status = "Select a node to reorder.";
            return Task.CompletedTask;
        }
        return RunMutationAsync(() => QuestGraphMutationService.MoveSibling(
            _model,
            selected.SemanticNode.Id,
            direction));
    }

    private Task ConnectSelectedAsync()
    {
        var selection = SelectedItems
            .DistinctBy(node => node.SemanticNode.Id)
            .ToArray();
        if (selection.Length != 2)
        {
            Status = "Select exactly two nodes: source first, then target.";
            return Task.CompletedTask;
        }
        var target = SelectedNode is not null && selection.Contains(SelectedNode)
            ? SelectedNode
            : selection[^1];
        var source = selection.First(node => !ReferenceEquals(node, target));
        return RunMutationAsync(() => QuestGraphMutationService.Connect(
            _model,
            source.SemanticNode.Id,
            SelectedConnectionPort,
            target.SemanticNode.Id));
    }

    private Task DisconnectSelectedAsync()
    {
        var selection = SelectedItems
            .DistinctBy(node => node.SemanticNode.Id)
            .ToArray();
        if (selection.Length is < 1 or > 2)
        {
            Status = "Select a target node, or select its source and target nodes.";
            return Task.CompletedTask;
        }
        var target = SelectedNode is not null && selection.Contains(SelectedNode)
            ? SelectedNode
            : selection[^1];
        var incoming = _model.GraphEdges
            .Where(edge => edge.TargetNodeId == target.GraphNode.Id)
            .ToArray();
        if (incoming.Length == 0)
        {
            Status = "The selected node is already disconnected.";
            return Task.CompletedTask;
        }
        var selectedSource = selection.FirstOrDefault(node => !ReferenceEquals(node, target));
        var preferred = selectedSource is not null
            ? incoming.FirstOrDefault(edge => edge.SourceNodeId == selectedSource.GraphNode.Id)
            : null;
        return RunMutationAsync(() => QuestGraphMutationService.Disconnect(
            _model,
            (preferred ?? incoming[0]).Id));
    }

    private async Task RunMutationAsync(Func<QuestGraphMutation> createMutation)
    {
        if (!CanEditSource)
        {
            Status = "Source editing is unavailable for this document session.";
            return;
        }
        try
        {
            var mutation = createMutation();
            await ApplyEditsAsync(mutation.Edits, mutation.Description).ConfigureAwait(true);
        }
        catch (QuestGraphOperationException exception)
        {
            Status = exception.Message;
        }
    }

    private void GroupSelectedNodes()
    {
        var selected = SelectedItems
            .DistinctBy(node => node.StableId)
            .ToArray();
        if (selected.Length == 0)
        {
            Status = "Select one or more nodes to create a layout group.";
            return;
        }

        RecordLayoutMutation($"group-create:{Guid.NewGuid():N}");
        _isApplyingState = true;
        try
        {
            var selectedIds = selected
                .Select(node => node.StableId)
                .ToHashSet(StringComparer.Ordinal);
            foreach (var group in Groups.ToArray())
            {
                foreach (var stableId in selectedIds)
                    group.RemoveMember(stableId);
                if (group.MemberCount == 0)
                    Groups.Remove(group);
            }

            const double nodeWidth = 260;
            const double nodeHeight = 150;
            const double padding = 35;
            var left = selected.Min(node => node.Location.X) - padding;
            var top = selected.Min(node => node.Location.Y) - padding - 20;
            var right = selected.Max(node => node.Location.X + nodeWidth) + padding;
            var bottom = selected.Max(node => node.Location.Y + nodeHeight) + padding;
            var createdGroup = new QuestGroupViewModel(
                Guid.NewGuid().ToString("N"),
                $"Group {Groups.Count + 1}",
                left,
                top,
                right - left,
                bottom - top,
                selectedIds,
                OnGroupLayoutChanging);
            Groups.Add(createdGroup);
            SelectedGroup = createdGroup;
        }
        finally
        {
            _isApplyingState = false;
        }
        _redo.Clear();
        NotifyDirty();
        RefreshCommandStates();
        Status = $"Grouped {selected.Length} node(s). PXSL source was not changed.";
    }

    private void UngroupSelectedNodes()
    {
        var selectedIds = SelectedItems
            .Select(node => node.StableId)
            .ToHashSet(StringComparer.Ordinal);
        if (selectedIds.Count == 0 && SelectedGroup is not null)
            selectedIds.UnionWith(SelectedGroup.MemberStableIds);
        if (selectedIds.Count == 0)
        {
            Status = "Select grouped nodes or a group row to ungroup.";
            return;
        }
        var matching = Groups
            .Where(group => group.MemberStableIds.Any(selectedIds.Contains))
            .ToArray();
        if (matching.Length == 0)
        {
            Status = "The selection is not in a layout group.";
            return;
        }

        RecordLayoutMutation($"group-remove:{Guid.NewGuid():N}");
        _isApplyingState = true;
        try
        {
            foreach (var group in matching)
            {
                foreach (var stableId in selectedIds)
                    group.RemoveMember(stableId);
                if (group.MemberCount == 0)
                    Groups.Remove(group);
            }
            if (SelectedGroup is not null && !Groups.Contains(SelectedGroup))
                SelectedGroup = null;
        }
        finally
        {
            _isApplyingState = false;
        }
        _redo.Clear();
        NotifyDirty();
        RefreshCommandStates();
        Status = "Removed the selected node(s) from their layout group. PXSL source was not changed.";
    }

    private string? FindGroupId(string stableId) => Groups
        .FirstOrDefault(group => group.Contains(stableId))?
        .Id;

    private IReadOnlyList<QuestGraphGroupLayout> CreateGroupLayouts() => Groups
        .Select(group => group.ToLayout())
        .ToArray();

    private void ReplaceGroups(IEnumerable<QuestGraphGroupLayout>? layouts)
    {
        Groups.Clear();
        SelectedGroup = null;
        if (layouts is null)
            return;
        foreach (var layout in layouts)
        {
            Groups.Add(new QuestGroupViewModel(
                layout.Id,
                layout.Title,
                layout.X,
                layout.Y,
                layout.Width,
                layout.Height,
                layout.MemberStableIds,
                OnGroupLayoutChanging));
        }
    }

    private void AppendGroupFingerprint(StringBuilder canonical)
    {
        foreach (var group in Groups.OrderBy(group => group.Id, StringComparer.Ordinal))
        {
            canonical.Append("group|").Append(group.Id).Append('|').Append(group.Title).Append('|')
                .Append(group.X).Append('|').Append(group.Y).Append('|')
                .Append(group.Width).Append('|').Append(group.Height).Append('|');
            foreach (var member in group.MemberStableIds.Order(StringComparer.Ordinal))
                canonical.Append(member).Append(',');
            canonical.Append('\n');
        }
    }

    private void OnGroupLayoutChanging(QuestGroupViewModel group)
    {
        RecordLayoutMutation($"group:{group.Id}");
        NotifyDirty();
    }
}
