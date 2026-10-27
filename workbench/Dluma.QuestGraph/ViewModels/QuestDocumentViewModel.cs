using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class QuestDocumentViewModel : ObservableObject
{
    private const int MaximumUndoStates = 100;
    private readonly QuestDocumentService _documents;
    private readonly Stack<DocumentState> _undo = new();
    private readonly Stack<DocumentState> _redo = new();
    private readonly List<QuestGraphNodeLayout> _orphans = new();
    private QuestCatalog? _catalog;
    private QuestGraphDocument _model;
    private QuestNodeViewModel? _selectedNode;
    private Point _viewportLocation;
    private double _viewportZoom = 1;
    private string _status = "Ready";
    private string? _sourcePath;
    private string _savedSourceSha256;
    private byte[] _savedSourceBytes;
    private string _savedLayoutFingerprint = string.Empty;
    private QuestGraphSidecarV1 _savedSidecar = null!;
    private bool _isConnected;
    private bool _isStaleSession;
    private bool _isApplyingState;
    private string? _lastLayoutMutationNodeId;
    private DateTime _lastLayoutMutationUtc;

    public QuestDocumentViewModel(
        QuestDocumentService documents,
        OpenedQuestDocument opened)
    {
        _documents = documents;
        _catalog = opened.Catalog;
        _model = opened.Document;
        _sourcePath = opened.SourcePath;
        RequiresConnection = opened.RequiresConnection;
        _isConnected = !RequiresConnection;
        PreserveExistingSidecar = opened.PreserveExistingSidecar;
        SidecarWarning = opened.SidecarWarning;
        _savedSourceSha256 = _model.Source.Sha256;
        _savedSourceBytes = _model.Source.ToArray();
        _savedWasModified = opened.WasModified;
        ConnectedProjectRoot = opened.ConnectedProjectRoot;
        SelectedItems.CollectionChanged += OnSelectedItemsChanged;
        ApplySourceCommand = new AsyncRelayCommand(
            ApplySelectedSourceAsync,
            () => CanEditSource && SelectedNode is { IsOpaque: false });
        DeleteNodeCommand = new AsyncRelayCommand(
            DeleteSelectedAsync,
            () => CanEditSource && (SelectedItems.Count > 0 || SelectedNode is not null));
        FocusSourceCommand = new AsyncRelayCommand(FocusSelectedSourceAsync, () => SelectedNode is not null && IsConnected);
        UndoCommand = new AsyncRelayCommand(
            UndoAsync,
            () => _undo.Count > 0 && CanRestoreState(_undo.Peek()));
        RedoCommand = new AsyncRelayCommand(
            RedoAsync,
            () => _redo.Count > 0 && CanRestoreState(_redo.Peek()));
        AutoLayoutCommand = new RelayCommand(AutoLayout);
        ToggleCollapseCommand = new RelayCommand(
            () =>
            {
                if (SelectedNode is not null)
                    SelectedNode.IsCollapsed = !SelectedNode.IsCollapsed;
            },
            () => SelectedNode is not null);

        RebuildGraph(opened.Sidecar);
        if (!string.IsNullOrWhiteSpace(SidecarWarning))
            Status = SidecarWarning;
        _savedLayoutFingerprint = LayoutFingerprint();
        _savedSidecar = CreateSidecar();
    }

    public ObservableCollection<QuestNodeViewModel> Nodes { get; } = new();
    public ObservableCollection<QuestConnectionViewModel> Connections { get; } = new();
    public ObservableCollection<QuestNodeViewModel> SelectedItems { get; } = new();
    public ObservableCollection<QuestProblemViewModel> Problems { get; } = new();
    public ObservableCollection<QuestVariableViewModel> Variables { get; } = new();

    public AsyncRelayCommand ApplySourceCommand { get; }
    public AsyncRelayCommand DeleteNodeCommand { get; }
    public AsyncRelayCommand FocusSourceCommand { get; }
    public AsyncRelayCommand UndoCommand { get; }
    public AsyncRelayCommand RedoCommand { get; }
    public RelayCommand AutoLayoutCommand { get; }
    public RelayCommand ToggleCollapseCommand { get; }

    public Func<QuestDocumentViewModel, IReadOnlyList<QuestTextEdit>, Task<bool>>? ConnectedEditHandler { get; set; }
    public Func<QuestDocumentViewModel, int?, Task>? FocusSourceHandler { get; set; }

    public QuestGraphDocument Model => _model;
    public string? ConnectedProjectRoot { get; }
    public string DocumentId => _model.Source.DocumentId;
    public string RelativePath => _model.Source.RelativePath;
    public string DisplayName => Path.GetFileName(RelativePath);
    public string TabTitle => $"{DisplayName}{(IsDirty ? " *" : string.Empty)}";
    public long Revision => _model.Source.Revision;
    public string SourceSha256 => _model.Source.Sha256;
    public string EncodingDescription =>
        $"{_model.Source.EncodingName} · {_model.Source.LineEnding} · rev {_model.Source.Revision}";
    public bool RequiresConnection { get; }
    public bool PreserveExistingSidecar { get; }
    public string? SidecarWarning { get; }
    public bool IsConnected => _isConnected;
    public bool IsStaleSession => _isStaleSession;
    public bool CanEditSource => !RequiresConnection || (IsConnected && !IsStaleSession);
    public bool CanSaveOriginal => !IsStaleSession && SourcePath is not null;
    public bool IsSourceDirty => !string.Equals(
        _model.Source.Sha256,
        _savedSourceSha256,
        StringComparison.OrdinalIgnoreCase);
    public bool IsLayoutDirty => !string.Equals(
        LayoutFingerprint(),
        _savedLayoutFingerprint,
        StringComparison.Ordinal);
    public bool IsDirty => IsSourceDirty || IsLayoutDirty;

    public string? SourcePath
    {
        get => _sourcePath;
        private set
        {
            if (SetProperty(ref _sourcePath, value))
                OnPropertyChanged(nameof(CanSaveOriginal));
        }
    }

    public QuestNodeViewModel? SelectedNode
    {
        get => _selectedNode;
        set
        {
            if (!SetProperty(ref _selectedNode, value))
                return;
            ApplySourceCommand.RaiseCanExecuteChanged();
            DeleteNodeCommand.RaiseCanExecuteChanged();
            FocusSourceCommand.RaiseCanExecuteChanged();
            ToggleCollapseCommand.RaiseCanExecuteChanged();
            RefreshInspector();
        }
    }

    public Point ViewportLocation
    {
        get => _viewportLocation;
        set
        {
            if (_viewportLocation == value)
                return;
            RecordLayoutMutation("viewport");
            if (SetProperty(ref _viewportLocation, value))
                NotifyDirty();
        }
    }

    public double ViewportZoom
    {
        get => _viewportZoom;
        set
        {
            var clamped = Math.Clamp(value, 0.1, 3.0);
            if (Math.Abs(_viewportZoom - clamped) < 0.0001)
                return;
            RecordLayoutMutation("viewport");
            if (SetProperty(ref _viewportZoom, clamped))
                NotifyDirty();
        }
    }

    public string Status
    {
        get => _status;
        set => SetProperty(ref _status, value);
    }

    public void MarkConnected(bool connected)
    {
        _isConnected = connected;
        _isStaleSession = RequiresConnection && !connected;
        OnPropertyChanged(nameof(IsConnected));
        OnPropertyChanged(nameof(IsStaleSession));
        OnPropertyChanged(nameof(CanEditSource));
        OnPropertyChanged(nameof(CanSaveOriginal));
        RefreshCommandStates();
        RefreshInspector();
        if (_isStaleSession)
            Status = "Developer Tools disconnected. Source editing and original-file saves are frozen; Save Copy remains available.";
    }

    public void ReplaceConnectedSnapshot(SourceChangedMessage message, QuestCatalog? catalog)
    {
        if (!string.Equals(message.DocumentId, DocumentId, StringComparison.Ordinal))
            throw new InvalidDataException("Connected source update targets a different document.");
        var snapshot = new QuestSourceSnapshot(
            DocumentId,
            RelativePath,
            message.Revision,
            message.Bytes,
            message.EncodingName);
        if (!string.Equals(snapshot.Sha256, message.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Connected source update bytes do not match their hash.");
        var layout = CreateSidecar();
        _catalog = catalog;
        _model = PxslParser.Parse(snapshot, _catalog);
        RebuildGraph(layout);
        MarkConnected(true);
        NotifyModelChanged();
    }

    public void Reparse(QuestCatalog? catalog)
    {
        var layout = CreateSidecar();
        _catalog = catalog;
        _model = PxslParser.Parse(_model.Source, _catalog);
        RebuildGraph(layout);
        NotifyModelChanged();
    }

    public void SetSourcePath(string path)
    {
        SourcePath = Path.GetFullPath(path);
        OnPropertyChanged(nameof(DisplayName));
        OnPropertyChanged(nameof(TabTitle));
    }

    public void MarkSourceSaved()
    {
        _savedSourceSha256 = _model.Source.Sha256;
        _savedSourceBytes = _model.Source.ToArray();
        _savedWasModified = false;
        NotifyDirty();
        Status = "Saved";
    }

    public void MarkLayoutSaved()
    {
        _savedSidecar = CreateSidecar();
        _savedLayoutFingerprint = LayoutFingerprint();
        NotifyDirty();
        Status = "Saved";
    }

    public async Task<bool> DiscardUnsavedChangesAsync()
    {
        if (!IsDirty)
            return true;
        var saved = new DocumentState((byte[])_savedSourceBytes.Clone(), _savedSidecar);
        (bool Admitted, long? Revision) admission;
        try
        {
            admission = await AdmitSavedBaselineAsync(saved).ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is
            IOException or InvalidDataException or InvalidOperationException)
        {
            Status = $"Discard was cancelled because the saved PXSL source could not be restored ({exception.GetType().Name}).";
            return false;
        }
        if (!admission.Admitted)
        {
            Status = RequiresConnection
                ? "Discard was cancelled because Developer Tools could not restore the saved PXSL source."
                : "Discard was cancelled because the saved PXSL source could not be restored.";
            return false;
        }
        RestoreState(saved, admission.Revision);
        _undo.Clear();
        _redo.Clear();
        RefreshCommandStates();
        NotifyDirty();
        Status = "Discarded unsaved source and layout changes.";
        return true;
    }

    public QuestGraphSidecarV1 CreateSidecar()
    {
        var layouts = Nodes.Select(node => QuestGraphSidecarService.CreateNodeLayout(
            node.SemanticNode,
            node.StableId,
            node.Location.X,
            node.Location.Y,
            260,
            node.IsCollapsed ? 56 : 150,
            node.IsCollapsed,
            FindGroupId(node.StableId),
            node.Note));
        return QuestGraphSidecarService.Create(
            _model,
            layouts.Concat(_orphans),
            new QuestGraphViewport(ViewportLocation.X, ViewportLocation.Y, ViewportZoom),
            CreateGroupLayouts());
    }

    public async Task<bool> InsertAsync(
        QuestPaletteItemViewModel item,
        bool asChild,
        Point? requestedLocation = null,
        QuestNodeViewModel? explicitTarget = null)
    {
        ArgumentNullException.ThrowIfNull(item);
        if (!CanEditSource)
            return false;
        var isQuestDeclaration = item.InsertionText.TrimStart()
            .StartsWith("quest ", StringComparison.OrdinalIgnoreCase);
        var target = explicitTarget ?? SelectedNode;
        if (isQuestDeclaration)
        {
            target = null;
            asChild = false;
        }
        else if (target is null)
        {
            target = Nodes.FirstOrDefault(node => node.SemanticNode.Kind == QuestSemanticKind.Quest);
            asChild = target is not null;
        }
        else if (target.SemanticNode.Kind == QuestSemanticKind.Quest)
        {
            asChild = true;
        }
        if (!isQuestDeclaration && target is null)
        {
            Status = "Add a quest declaration before inserting quest actions on the canvas.";
            return false;
        }
        if (target?.IsOpaque == true)
        {
            Status = "Opaque source nodes are movable for layout only and cannot be edited structurally.";
            return false;
        }
        var source = _model.Source;
        var newline = PreferredNewline(source);
        var insertionText = NormalizeInsertionText(item.InsertionText, newline);
        int offset;
        byte[] replacement;
        if (target is null)
        {
            offset = source.Bytes.Length;
            var prefix = offset > 0 && !EndsWithNewline(source) ? newline : string.Empty;
            replacement = QuestDocumentService.EncodeText(source, prefix + insertionText + newline);
        }
        else if (asChild)
        {
            var span = target.SemanticNode.Span;
            var (lineStart, lineEnd, endingLength) = FindLineBounds(source, span.Start);
            var indentation = source.Bytes.Slice(lineStart, span.Start - lineStart).ToArray();
            var childIndent = QuestDocumentService.EncodeText(source, "    ");
            var indentationText = Encoding.GetEncoding(source.EncodingName).GetString(indentation);
            var content = QuestDocumentService.EncodeText(
                source,
                IndentContinuationLines(insertionText, indentationText + "    ", newline) + newline);
            if (endingLength == 0)
            {
                var lineBreak = QuestDocumentService.EncodeText(source, newline);
                replacement = Join(lineBreak, indentation, childIndent, content);
                offset = lineEnd;
            }
            else
            {
                replacement = Join(indentation, childIndent, content);
                offset = lineEnd + endingLength;
            }
        }
        else
        {
            var last = LastDescendantBySourceEnd(target.SemanticNode);
            var (lineStart, lineEnd, endingLength) = FindLineBounds(source, last.Span.Start);
            var indentation = source.Bytes.Slice(lineStart, last.Span.Start - lineStart).ToArray();
            var indentationText = Encoding.GetEncoding(source.EncodingName).GetString(indentation);
            var content = QuestDocumentService.EncodeText(
                source,
                IndentContinuationLines(insertionText, indentationText, newline) + newline);
            if (endingLength == 0)
            {
                var lineBreak = QuestDocumentService.EncodeText(source, newline);
                replacement = Join(lineBreak, indentation, content);
                offset = lineEnd;
            }
            else
            {
                replacement = Join(indentation, content);
                offset = lineEnd + endingLength;
            }
        }

        var edit = new QuestTextEdit(
            source.Revision,
            source.Sha256,
            offset,
            0,
            replacement);
        var applied = await ApplyEditsAsync(new[] { edit }, $"Inserted {item.Name}.").ConfigureAwait(true);
        if (applied && requestedLocation is Point location)
        {
            var inserted = Nodes
                .Where(node => node.SemanticNode.Span.Start >= offset &&
                    node.SemanticNode.Span.Start < offset + replacement.Length)
                .OrderBy(node => node.SemanticNode.Span.Start)
                .FirstOrDefault();
            if (inserted is not null)
            {
                _isApplyingState = true;
                try
                {
                    inserted.Location = location;
                    SelectedItems.Clear();
                    inserted.IsSelected = true;
                    SelectedItems.Add(inserted);
                    SelectedNode = inserted;
                }
                finally
                {
                    _isApplyingState = false;
                }
                NotifyDirty();
            }
        }
        return applied;
    }

    public void AutoLayout()
    {
        CaptureUndoState();
        _isApplyingState = true;
        try
        {
            var collapsed = Nodes
                .Where(node => node.IsCollapsed)
                .Select(node => node.GraphNode.Id)
                .ToHashSet(StringComparer.Ordinal);
            var arranged = QuestGraphAutoLayout.Compute(_model, null, collapsed).Positions;
            foreach (var node in Nodes)
            {
                if (arranged.TryGetValue(node.GraphNode.Id, out var point))
                    node.Location = new Point(point.X, point.Y);
            }
            RefitGroups();
            var options = QuestGraphAutoLayoutOptions.Default;
            ViewportLocation = new Point(
                options.MarginX - options.FlowGap,
                options.MarginY - options.BandGap);
        }
        finally
        {
            _isApplyingState = false;
        }
        _redo.Clear();
        NotifyDirty();
        RefreshCommandStates();
        Status = "Graph auto-arranged. PXSL source was not changed.";
    }

    /// <summary>
    /// Places nodes the saved layout has never seen. Their container is already
    /// positioned, so they hang beneath it and slide right past anything occupied
    /// rather than landing on top of the author's arrangement.
    /// </summary>
    private void PlaceUnanchoredNodes(
        IReadOnlyList<QuestNodeViewModel> unanchored,
        IReadOnlyDictionary<string, QuestNodeViewModel> graphById)
    {
        if (unanchored.Count == 0)
            return;
        var options = QuestGraphAutoLayoutOptions.Default;
        var containers = QuestGraphAutoLayout.ContainerByGraphNodeId(_model);
        var pending = unanchored.ToHashSet();
        var occupied = Nodes
            .Where(node => !pending.Contains(node))
            .Select(Bounds)
            .ToList();

        foreach (var node in unanchored.OrderBy(node => node.SemanticNode.Span.Start))
        {
            var anchor = containers.TryGetValue(node.GraphNode.Id, out var containerId) &&
                graphById.TryGetValue(containerId, out var container)
                ? new Point(
                    container.Location.X,
                    container.Location.Y + Bounds(container).Height + options.BandGap)
                : new Point(options.MarginX, options.MarginY);
            var height = QuestGraphAutoLayout.EstimateHeight(
                node.GraphNode, options, node.IsCollapsed);
            var candidate = anchor;
            var guard = 0;
            while (guard++ < 512 && occupied.Any(taken => taken.IntersectsWith(
                new Rect(candidate.X, candidate.Y, options.NodeWidth, height))))
            {
                candidate = new Point(
                    candidate.X + options.NodeWidth + options.FlowGap,
                    candidate.Y);
            }
            node.Location = candidate;
            occupied.Add(new Rect(candidate.X, candidate.Y, options.NodeWidth, height));
        }
    }

    private static Rect Bounds(QuestNodeViewModel node) => new(
        node.Location.X,
        node.Location.Y,
        QuestGraphAutoLayoutOptions.Default.NodeWidth,
        QuestGraphAutoLayout.EstimateHeight(
            node.GraphNode,
            QuestGraphAutoLayoutOptions.Default,
            node.IsCollapsed));

    /// <summary>
    /// Re-fits every group rectangle around its members. Auto-layout moves the nodes,
    /// so a group left where it was would sit over unrelated parts of the graph.
    /// </summary>
    private void RefitGroups()
    {
        if (Groups.Count == 0)
            return;
        const double padding = 28;
        var byStableId = Nodes
            .GroupBy(node => node.StableId, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.Ordinal);
        foreach (var group in Groups)
        {
            var members = group.MemberStableIds
                .Select(id => byStableId.GetValueOrDefault(id))
                .Where(node => node is not null)
                .Select(node => Bounds(node!))
                .ToArray();
            if (members.Length == 0)
                continue;
            var left = members.Min(bounds => bounds.Left);
            var top = members.Min(bounds => bounds.Top);
            var right = members.Max(bounds => bounds.Right);
            var bottom = members.Max(bounds => bounds.Bottom);
            group.X = left - padding;
            group.Y = top - padding;
            group.Width = right - left + padding * 2;
            group.Height = bottom - top + padding * 2;
        }
    }

    /// <summary>Translates collapsed sidecar entries, keyed by semantic id, to graph ids.</summary>
    private HashSet<string> CollapsedGraphNodeIds(
        IReadOnlyDictionary<string, QuestGraphNodeLayout> layouts) => _model.GraphNodes
        .Where(node => layouts.TryGetValue(node.SemanticNodeId, out var layout) &&
            layout.IsCollapsed)
        .Select(node => node.Id)
        .ToHashSet(StringComparer.Ordinal);

    private async Task ApplySelectedSourceAsync()
    {
        var node = SelectedNode;
        if (node is null || !CanEditSource)
            return;
        if (node.IsOpaque)
        {
            Status = "Opaque source is preserved exactly and must be edited in the PXSL editor.";
            return;
        }
        byte[] replacement;
        try
        {
            replacement = QuestDocumentService.EncodeText(_model.Source, node.SourcePreview);
        }
        catch (EncoderFallbackException)
        {
            Status = $"The edited text cannot be represented as {_model.Source.EncodingName}. No bytes were changed.";
            return;
        }
        var edit = new QuestTextEdit(
            _model.Source.Revision,
            _model.Source.Sha256,
            node.SemanticNode.Span.Start,
            node.SemanticNode.Span.Length,
            replacement);
        var applied = await ApplyEditsAsync(
            new[] { edit },
            "Updated the selected PXSL source region.").ConfigureAwait(true);
        if (applied)
            RestoreSelectionByStableId(
                Array.Empty<string>(),
                node.StableId,
                node.SemanticNode.Span.Start);
    }

    private async Task DeleteSelectedAsync()
    {
        var selected = SelectedItems
            .DistinctBy(node => node.SemanticNode.Id)
            .ToList();
        if (selected.Count == 0 && SelectedNode is not null)
            selected.Add(SelectedNode);
        if (selected.Count == 0 || !CanEditSource)
            return;
        try
        {
            var mutation = QuestGraphMutationService.DeleteMany(
                _model,
                selected.Select(node => node.SemanticNode.Id));
            await ApplyEditsAsync(mutation.Edits, mutation.Description).ConfigureAwait(true);
        }
        catch (QuestGraphOperationException exception)
        {
            Status = exception.Message;
        }
    }

    private async Task FocusSelectedSourceAsync()
    {
        if (SelectedNode is null || FocusSourceHandler is null)
            return;
        await FocusSourceHandler(this, SelectedNode.SemanticNode.Span.Start).ConfigureAwait(true);
    }

    private async Task<bool> ApplyEditsAsync(
        IReadOnlyList<QuestTextEdit> edits,
        string successStatus)
    {
        if (RequiresConnection)
        {
            if (!CanEditSource || ConnectedEditHandler is null)
                return false;
            if (!await ConnectedEditHandler(this, edits).ConfigureAwait(true))
                return false;
        }

        CaptureUndoState();
        var layout = CreateSidecar();
        try
        {
            var result = QuestSourceEditService.Apply(_model.Source, edits);
            _model = PxslParser.Parse(result.Source, _catalog);
            RebuildGraph(layout);
            _redo.Clear();
            NotifyModelChanged();
            RefreshCommandStates();
            Status = successStatus;
            return true;
        }
        catch (Exception exception) when (
            exception is QuestEditRejectedException or DecoderFallbackException or InvalidDataException)
        {
            Status = exception.Message;
            return false;
        }
    }

    private async Task UndoAsync()
    {
        if (_undo.Count == 0 || !CanRestoreState(_undo.Peek()))
            return;
        var target = _undo.Pop();
        var current = CaptureState();
        if (!await AdmitStateSourceAsync(target).ConfigureAwait(true))
        {
            _undo.Push(target);
            return;
        }
        _redo.Push(current);
        RestoreState(target);
        Status = "Undo";
        RefreshCommandStates();
    }

    private async Task RedoAsync()
    {
        if (_redo.Count == 0 || !CanRestoreState(_redo.Peek()))
            return;
        var target = _redo.Pop();
        var current = CaptureState();
        if (!await AdmitStateSourceAsync(target).ConfigureAwait(true))
        {
            _redo.Push(target);
            return;
        }
        _undo.Push(current);
        RestoreState(target);
        Status = "Redo";
        RefreshCommandStates();
    }

    private async Task<bool> AdmitStateSourceAsync(DocumentState target)
    {
        if (!RequiresConnection || target.Bytes.AsSpan().SequenceEqual(_model.Source.Bytes.Span))
            return true;
        if (ConnectedEditHandler is null)
            return false;
        var currentBytes = _model.Source.Bytes.ToArray();
        var prefix = 0;
        var sharedLength = Math.Min(currentBytes.Length, target.Bytes.Length);
        while (prefix < sharedLength && currentBytes[prefix] == target.Bytes[prefix])
            ++prefix;
        var suffix = 0;
        while (suffix < sharedLength - prefix &&
            currentBytes[currentBytes.Length - 1 - suffix] ==
                target.Bytes[target.Bytes.Length - 1 - suffix])
            ++suffix;
        var replacementLength = target.Bytes.Length - prefix - suffix;
        var replacement = target.Bytes.AsMemory(prefix, replacementLength).ToArray();
        var edit = new QuestTextEdit(
            _model.Source.Revision,
            _model.Source.Sha256,
            prefix,
            currentBytes.Length - prefix - suffix,
            replacement);
        return await ConnectedEditHandler(this, new[] { edit }).ConfigureAwait(true);
    }

    private bool CanRestoreState(DocumentState state) =>
        CanEditSource ||
        state.Bytes.AsSpan().SequenceEqual(_model.Source.Bytes.Span);

    private void RestoreState(DocumentState state, long? admittedRevision = null)
    {
        _isApplyingState = true;
        try
        {
            var current = _model.Source;
            var snapshot = admittedRevision is long revision
                ? new QuestSourceSnapshot(
                    DocumentId,
                    RelativePath,
                    revision,
                    state.Bytes,
                    current.EncodingName)
                : state.Bytes.AsSpan().SequenceEqual(current.Bytes.Span)
                    ? current
                    : new QuestSourceSnapshot(
                        DocumentId,
                        RelativePath,
                        checked(current.Revision + 1),
                        state.Bytes,
                        current.EncodingName);
            _model = PxslParser.Parse(snapshot, _catalog);
            RebuildGraph(state.Sidecar);
            ViewportLocation = new Point(state.Sidecar.Viewport.X, state.Sidecar.Viewport.Y);
            ViewportZoom = state.Sidecar.Viewport.Zoom;
        }
        finally
        {
            _isApplyingState = false;
        }
        NotifyModelChanged();
    }

    private void RebuildGraph(QuestGraphSidecarV1? sidecar)
    {
        _isApplyingState = true;
        try
        {
            Nodes.Clear();
            Connections.Clear();
            SelectedItems.Clear();
            Problems.Clear();
            Variables.Clear();
            _orphans.Clear();
            ReplaceGroups(sidecar?.Groups);
            var inspectorIndex = RebuildInspectorIndex();

            var layouts = new Dictionary<string, QuestGraphNodeLayout>(StringComparer.Ordinal);
            if (sidecar is not null)
            {
                var reconciled = QuestGraphSidecarService.Reconcile(sidecar, _model);
                foreach (var match in reconciled.Matches)
                    layouts[match.SemanticNode.Id] = match.Layout;
                _orphans.AddRange(reconciled.Orphans);
                _viewportLocation = new Point(sidecar.Viewport.X, sidecar.Viewport.Y);
                _viewportZoom = Math.Clamp(sidecar.Viewport.Zoom, 0.1, 3.0);
            }

            var semanticById = _model.SemanticNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
            var collapsed = CollapsedGraphNodeIds(layouts);
            var arranged = QuestGraphAutoLayout.Compute(_model, null, collapsed).Positions;
            var graphById = new Dictionary<string, QuestNodeViewModel>(StringComparer.Ordinal);
            var unanchored = new List<QuestNodeViewModel>();
            foreach (var graphNode in _model.GraphNodes.OrderBy(node => node.Order))
            {
                if (!semanticById.TryGetValue(graphNode.SemanticNodeId, out var semantic))
                    continue;
                QuestGraphNodeLayout? layout = null;
                layouts.TryGetValue(semantic.Id, out layout);
                // A document with no saved layout opens already arranged. Once the
                // author has arranged it, saved positions are authoritative and only
                // the nodes they have never seen get placed, next to their container.
                var location = layout is not null
                    ? new Point(layout.X, layout.Y)
                    : arranged.TryGetValue(graphNode.Id, out var computed)
                        ? new Point(computed.X, computed.Y)
                        : new Point(QuestGraphAutoLayoutOptions.Default.MarginX,
                            QuestGraphAutoLayoutOptions.Default.MarginY);
                var sourcePreview = QuestDocumentService.DecodeSpan(_model.Source, semantic.Span);
                QuestCatalogEntry? catalogEntry = null;
                _catalog?.TryGetEntry(semantic.Name, out catalogEntry);
                var card = inspectorIndex.GetCard(semantic.Id);
                var viewModel = new QuestNodeViewModel(
                    graphNode,
                    semantic,
                    layout?.StableId ?? Guid.NewGuid().ToString("N"),
                    location,
                    sourcePreview,
                    card.Title,
                    card.Summary,
                    layout?.IsCollapsed ?? false,
                    layout?.Note,
                    OnNodeLayoutChanging,
                    QuestPaletteColors.ForEntry(catalogEntry),
                    QuestPaletteColors.TextForEntry(catalogEntry));
                Nodes.Add(viewModel);
                graphById[graphNode.Id] = viewModel;
                if (layout is null && layouts.Count > 0)
                    unanchored.Add(viewModel);
            }

            PlaceUnanchoredNodes(unanchored, graphById);

            foreach (var edge in _model.GraphEdges.OrderBy(edge => edge.Order))
            {
                if (!graphById.TryGetValue(edge.SourceNodeId, out var sourceNode) ||
                    !graphById.TryGetValue(edge.TargetNodeId, out var targetNode))
                    continue;
                var sourcePort = sourceNode.FindPort(edge.SourcePortId);
                var targetPort = targetNode.FindPort(edge.TargetPortId);
                if (sourcePort is not null && targetPort is not null)
                {
                    Connections.Add(new QuestConnectionViewModel(edge, sourcePort, targetPort));
                    continue;
                }
                // A projected edge that names a port its node does not expose used to
                // vanish, leaving the node looking unconnected with nothing to explain it.
                Problems.Add(new QuestProblemViewModel(
                    "Warning",
                    "GRAPH003",
                    $"The connection from '{sourceNode.SemanticNode.Name}' to " +
                    $"'{targetNode.SemanticNode.Name}' references port " +
                    $"'{(sourcePort is null ? edge.SourcePortId : edge.TargetPortId)}', " +
                    "which that node does not expose, so it is not drawn.",
                    targetNode.SemanticNode.Span.Line,
                    targetNode.SemanticNode.Span.Column,
                    targetNode.SemanticNode.Span.Start));
            }

            foreach (var diagnostic in _model.Diagnostics)
            {
                Problems.Add(new QuestProblemViewModel(
                    diagnostic.Severity.ToString(),
                    diagnostic.Code,
                    diagnostic.Message,
                    diagnostic.Span?.Line,
                    diagnostic.Span?.Column,
                    diagnostic.Span?.Start));
            }
            if (_orphans.Count > 0)
            {
                Problems.Add(new QuestProblemViewModel(
                    "Warning",
                    "GRAPH001",
                    $"{_orphans.Count} layout entr{(_orphans.Count == 1 ? "y is" : "ies are")} orphaned and preserved without guessing.",
                    null,
                    null,
                    null));
            }
            if (!string.IsNullOrWhiteSpace(SidecarWarning))
                Problems.Add(new QuestProblemViewModel("Warning", "GRAPH002", SidecarWarning, null, null, null));

            foreach (var semantic in _model.SemanticNodes.Where(node =>
                node.Name.Contains("var", StringComparison.OrdinalIgnoreCase)))
            {
                Variables.Add(new QuestVariableViewModel(
                    semantic.Name,
                    string.Join(' ', semantic.Arguments),
                    semantic.Span.Line,
                    semantic.Id));
            }
        }
        finally
        {
            _isApplyingState = false;
        }
        OnPropertyChanged(nameof(ViewportLocation));
        OnPropertyChanged(nameof(ViewportZoom));
    }

    private QuestSemanticNode LastDescendantBySourceEnd(QuestSemanticNode root)
    {
        var childrenByParent = _model.SemanticNodes
            .Where(node => node.ParentId is not null)
            .GroupBy(node => node.ParentId!, StringComparer.Ordinal)
            .ToDictionary(
                group => group.Key,
                group => group.OrderBy(node => node.Order).ToArray(),
                StringComparer.Ordinal);
        var last = root;
        var pending = new Stack<QuestSemanticNode>();
        var visited = new HashSet<string>(StringComparer.Ordinal);
        pending.Push(root);
        while (pending.Count > 0)
        {
            var node = pending.Pop();
            if (!visited.Add(node.Id))
                continue;
            if (node.Span.End > last.Span.End)
                last = node;
            if (!childrenByParent.TryGetValue(node.Id, out var children))
                continue;
            for (var index = children.Length - 1; index >= 0; --index)
                pending.Push(children[index]);
        }
        return last;
    }

    private static (int Start, int End, int EndingLength) FindLineBounds(
        QuestSourceSnapshot source,
        int byteOffset)
    {
        var bytes = source.Bytes.ToArray();
        var encoding = Encoding.GetEncoding(source.EncodingName);
        var newlineCandidates = new[] { "\r\n", "\n", "\r" }
            .Select(value => encoding.GetBytes(value))
            .OrderByDescending(value => value.Length)
            .ToArray();
        var preambleLength = source.HasBom
            ? encoding.GetPreamble().Length
            : 0;
        var start = preambleLength;
        var index = preambleLength;
        while (index < Math.Min(byteOffset, bytes.Length))
        {
            var matched = newlineCandidates.FirstOrDefault(candidate =>
                index + candidate.Length <= bytes.Length &&
                bytes.AsSpan(index, candidate.Length).SequenceEqual(candidate));
            if (matched is not null)
            {
                start = index + matched.Length;
                index += matched.Length;
            }
            else
            {
                index++;
            }
        }
        index = byteOffset;
        while (index < bytes.Length)
        {
            var matched = newlineCandidates.FirstOrDefault(candidate =>
                index + candidate.Length <= bytes.Length &&
                bytes.AsSpan(index, candidate.Length).SequenceEqual(candidate));
            if (matched is not null)
                return (start, index, matched.Length);
            index++;
        }
        return (start, bytes.Length, 0);
    }

    private static string PreferredNewline(QuestSourceSnapshot source) => source.LineEnding switch
    {
        QuestLineEnding.Cr => "\r",
        QuestLineEnding.Lf => "\n",
        _ => "\r\n"
    };

    private static string NormalizeInsertionText(string text, string newline) =>
        text.Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .TrimEnd('\n')
            .Replace("\n", newline, StringComparison.Ordinal);

    private static string IndentContinuationLines(
        string text,
        string prefix,
        string newline) => string.IsNullOrEmpty(prefix)
        ? text
        : text.Replace(newline, newline + prefix, StringComparison.Ordinal);

    private static bool EndsWithNewline(QuestSourceSnapshot source)
    {
        var bytes = source.Bytes.ToArray();
        var encoding = Encoding.GetEncoding(source.EncodingName);
        return new[] { "\r\n", "\n", "\r" }
            .Select(encoding.GetBytes)
            .Any(ending => bytes.AsSpan().EndsWith(ending));
    }

    private static byte[] Join(params byte[][] parts)
    {
        var length = parts.Sum(part => part.Length);
        var result = new byte[length];
        var offset = 0;
        foreach (var part in parts)
        {
            Buffer.BlockCopy(part, 0, result, offset, part.Length);
            offset += part.Length;
        }
        return result;
    }

    private void OnNodeLayoutChanging(QuestNodeViewModel node)
    {
        RecordLayoutMutation(node.StableId);
        NotifyDirty();
    }

    private void RecordLayoutMutation(string identity)
    {
        if (_isApplyingState)
            return;
        var now = DateTime.UtcNow;
        if (!string.Equals(identity, _lastLayoutMutationNodeId, StringComparison.Ordinal) ||
            now - _lastLayoutMutationUtc > TimeSpan.FromMilliseconds(500))
        {
            CaptureUndoState();
            _redo.Clear();
        }
        _lastLayoutMutationNodeId = identity;
        _lastLayoutMutationUtc = now;
    }

    private void CaptureUndoState()
    {
        if (_isApplyingState)
            return;
        _undo.Push(CaptureState());
        while (_undo.Count > MaximumUndoStates)
        {
            var retained = _undo.Take(MaximumUndoStates).Reverse().ToArray();
            _undo.Clear();
            foreach (var state in retained)
                _undo.Push(state);
        }
        RefreshCommandStates();
    }

    private DocumentState CaptureState() => new(_model.Source.ToArray(), CreateSidecar());

    private string LayoutFingerprint()
    {
        var canonical = new StringBuilder();
        canonical.Append(ViewportLocation.X).Append('|').Append(ViewportLocation.Y).Append('|')
            .Append(ViewportZoom).Append('\n');
        foreach (var node in Nodes.OrderBy(node => node.StableId, StringComparer.Ordinal))
        {
            canonical.Append(node.StableId).Append('|').Append(node.Location.X).Append('|')
                .Append(node.Location.Y).Append('|').Append(node.IsCollapsed).Append('|')
                .Append(node.Note).Append('\n');
        }
        foreach (var orphan in _orphans.OrderBy(node => node.StableId, StringComparer.Ordinal))
            canonical.Append("orphan|").Append(orphan.StableId).Append('\n');
        AppendGroupFingerprint(canonical);
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(canonical.ToString())));
    }

    private void OnSelectedItemsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        SelectedNode = SelectedItems.LastOrDefault();
    }

    private void NotifyModelChanged()
    {
        OnPropertyChanged(nameof(Model));
        OnPropertyChanged(nameof(Revision));
        OnPropertyChanged(nameof(SourceSha256));
        OnPropertyChanged(nameof(EncodingDescription));
        NotifyDirty();
    }

    private void NotifyDirty()
    {
        OnPropertyChanged(nameof(IsSourceDirty));
        OnPropertyChanged(nameof(IsLayoutDirty));
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(TabTitle));
    }

    private void RefreshCommandStates()
    {
        ApplySourceCommand.RaiseCanExecuteChanged();
        DeleteNodeCommand.RaiseCanExecuteChanged();
        FocusSourceCommand.RaiseCanExecuteChanged();
        UndoCommand.RaiseCanExecuteChanged();
        RedoCommand.RaiseCanExecuteChanged();
    }

    private sealed record DocumentState(byte[] Bytes, QuestGraphSidecarV1 Sidecar);
}
