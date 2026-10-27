using System.Collections.ObjectModel;
using System.Text;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class QuestDocumentViewModel
{
    private QuestInspectorIndex? _inspectorIndex;
    private string _inspectorTitle = string.Empty;
    private string _inspectorSummary = string.Empty;
    private Func<QuestParameterEditorKind, string?, IReadOnlyList<string>>?
        _inspectorOptionProvider;
    private bool _isRefreshingInspectorContext;

    public ObservableCollection<QuestInspectorFieldViewModel> InspectorFields { get; } = new();

    public string InspectorTitle
    {
        get => _inspectorTitle;
        private set => SetProperty(ref _inspectorTitle, value);
    }

    public string InspectorSummary
    {
        get => _inspectorSummary;
        private set => SetProperty(ref _inspectorSummary, value);
    }

    public bool HasInspectorSelection => SelectedNode is not null;
    public bool IsSelectedRawReadOnly => !CanEditSource || SelectedNode?.IsOpaque != false;

    public Func<QuestParameterEditorKind, string?, IReadOnlyList<string>>?
        InspectorOptionProvider
    {
        get => _inspectorOptionProvider;
        set
        {
            _inspectorOptionProvider = value;
            RefreshInspector();
        }
    }

    public Func<Task>? InspectorContextRefreshHandler { get; set; }

    public async Task RefreshInspectorContextAsync()
    {
        if (_isRefreshingInspectorContext)
            return;

        _isRefreshingInspectorContext = true;
        try
        {
            if (InspectorContextRefreshHandler is not null)
                await InspectorContextRefreshHandler().ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            Status = $"Context unavailable: {exception.Message}";
        }
        finally
        {
            _isRefreshingInspectorContext = false;
            RefreshInspectorOptions();
        }
    }

    public void RefreshInspectorOptions()
    {
        var selected = SelectedNode;
        if (selected is null)
            return;
        try
        {
            var snapshot = InspectorIndex().Inspect(selected.SemanticNode.Id);
            foreach (var field in InspectorFields)
            {
                var value = snapshot.Fields.FirstOrDefault(candidate =>
                    string.Equals(
                        candidate.Definition.Id,
                        field.Id,
                        StringComparison.OrdinalIgnoreCase));
                if (value is null)
                    continue;
                var pairedClass = value.Definition.PairedFieldId is null
                    ? snapshot.Fields.FirstOrDefault(candidate =>
                        candidate.Definition.EditorKind ==
                            QuestParameterEditorKind.MapObjectClass)?.Value
                    : snapshot.Fields.FirstOrDefault(candidate =>
                        string.Equals(
                            candidate.Definition.Id,
                            value.Definition.PairedFieldId,
                            StringComparison.OrdinalIgnoreCase))?.Value;
                if (string.Equals(pairedClass, "_", StringComparison.Ordinal))
                    pairedClass = null;
                var contextOptions = InspectorOptionProvider?.Invoke(
                    value.Definition.EditorKind,
                    pairedClass) ?? Array.Empty<string>();
                field.ReplaceOptions(value.Definition.AllowedValues.Concat(contextOptions));
            }
        }
        catch (QuestInspectorException exception)
        {
            Status = exception.Message;
        }
    }

    private QuestInspectorIndex RebuildInspectorIndex()
    {
        _inspectorIndex = QuestInspectorService.CreateIndex(_model);
        return _inspectorIndex;
    }

    private QuestInspectorIndex InspectorIndex() =>
        _inspectorIndex ??= QuestInspectorService.CreateIndex(_model);

    private void RefreshInspector()
    {
        InspectorFields.Clear();
        OnPropertyChanged(nameof(HasInspectorSelection));
        OnPropertyChanged(nameof(IsSelectedRawReadOnly));
        var selected = SelectedNode;
        if (selected is null)
        {
            InspectorTitle = string.Empty;
            InspectorSummary = string.Empty;
            return;
        }

        try
        {
            var snapshot = InspectorIndex().Inspect(selected.SemanticNode.Id);
            InspectorTitle = snapshot.Title;
            InspectorSummary = snapshot.Summary;
            foreach (var value in snapshot.Fields)
            {
                var pairedClass = value.Definition.PairedFieldId is null
                    ? snapshot.Fields.FirstOrDefault(candidate =>
                        candidate.Definition.EditorKind ==
                            QuestParameterEditorKind.MapObjectClass)?.Value
                    : snapshot.Fields.FirstOrDefault(candidate =>
                        string.Equals(
                            candidate.Definition.Id,
                            value.Definition.PairedFieldId,
                            StringComparison.OrdinalIgnoreCase))?.Value;
                var contextOptions = InspectorOptionProvider?.Invoke(
                    value.Definition.EditorKind,
                    pairedClass) ?? Array.Empty<string>();
                var options = value.Definition.AllowedValues
                    .Concat(contextOptions)
                    .ToArray();
                InspectorFields.Add(new QuestInspectorFieldViewModel(
                    value.Definition.Id,
                    value.Definition.DisplayName,
                    value.Definition.EditorKind,
                    value.Definition.IsRequired,
                    value.Value,
                    options,
                    value.Definition.Description,
                    CanEditSource && !selected.IsOpaque,
                    CommitInspectorFieldAsync));
            }
        }
        catch (QuestInspectorException exception)
        {
            InspectorTitle = selected.Title;
            InspectorSummary = selected.Summary;
            Status = exception.Message;
        }
    }

    private async Task<bool> CommitInspectorFieldAsync(
        QuestInspectorFieldViewModel field,
        string value)
    {
        var node = SelectedNode;
        if (node is null || !CanEditSource || node.IsOpaque)
            return false;
        try
        {
            var selection = SelectedItems
                .Select(item => item.StableId)
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            var primary = node.StableId;
            var primaryByteOffset = node.SemanticNode.Span.Start;
            var mutation = QuestInspectorMutationService.SetFieldValue(
                _model,
                node.SemanticNode.Id,
                field.Id,
                value);
            if (!await ApplyEditsAsync(mutation.Edits, mutation.Description).ConfigureAwait(true))
                return false;
            RestoreSelectionByStableId(selection, primary, primaryByteOffset);
            return true;
        }
        catch (Exception exception) when (exception is
            QuestInspectorException or QuestGraphOperationException or
            EncoderFallbackException or InvalidDataException)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool BeginHeaderEdit(QuestNodeViewModel node)
    {
        ArgumentNullException.ThrowIfNull(node);
        if (!CanEditSource || node.IsOpaque || !Nodes.Contains(node))
            return false;
        try
        {
            var snapshot = InspectorIndex().Inspect(node.SemanticNode.Id);
            var field = snapshot.Fields.FirstOrDefault(value =>
                value.Definition.Role is
                    QuestInspectorFieldRole.PhaseName or
                    QuestInspectorFieldRole.NodeName);
            if (field is null)
                return false;
            node.BeginHeaderEdit(field.Value);
            return true;
        }
        catch (QuestInspectorException exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public async Task<bool> CommitHeaderEditAsync(QuestNodeViewModel node)
    {
        ArgumentNullException.ThrowIfNull(node);
        try
        {
            if (!CanEditSource || node.IsOpaque || !Nodes.Contains(node))
                return false;
            var snapshot = InspectorIndex().Inspect(node.SemanticNode.Id);
            var field = snapshot.Fields.FirstOrDefault(value =>
                value.Definition.Role is
                    QuestInspectorFieldRole.PhaseName or
                    QuestInspectorFieldRole.NodeName);
            if (field is null)
                return false;
            var selection = SelectedItems
                .Select(item => item.StableId)
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            var primary = node.StableId;
            var primaryByteOffset = node.SemanticNode.Span.Start;
            var mutation = QuestInspectorMutationService.SetFieldValue(
                _model,
                node.SemanticNode.Id,
                field.Definition.Id,
                node.HeaderEditText);
            if (!await ApplyEditsAsync(mutation.Edits, mutation.Description).ConfigureAwait(true))
                return false;
            RestoreSelectionByStableId(selection, primary, primaryByteOffset);
            return true;
        }
        catch (Exception exception) when (exception is
            QuestInspectorException or QuestGraphOperationException or
            EncoderFallbackException or InvalidDataException)
        {
            Status = exception.Message;
            return false;
        }
        finally
        {
            node.CompleteHeaderCommit();
        }
    }

    private void RestoreSelectionByStableId(
        IReadOnlyCollection<string> selectedStableIds,
        string? primaryStableId,
        int? primaryByteOffset = null)
    {
        var selected = selectedStableIds.Count == 0 && primaryStableId is not null
            ? new HashSet<string>(new[] { primaryStableId }, StringComparer.Ordinal)
            : new HashSet<string>(selectedStableIds, StringComparer.Ordinal);
        var primaryNode = primaryStableId is null
            ? null
            : Nodes.FirstOrDefault(node =>
                string.Equals(node.StableId, primaryStableId, StringComparison.Ordinal));
        if (primaryNode is null && primaryByteOffset is int byteOffset)
        {
            primaryNode = Nodes.FirstOrDefault(node =>
                node.SemanticNode.Span.Start == byteOffset);
        }
        if (primaryNode is not null)
            selected.Add(primaryNode.StableId);
        SelectedItems.Clear();
        foreach (var node in Nodes.Where(node => selected.Contains(node.StableId)))
        {
            node.IsSelected = true;
            SelectedItems.Add(node);
        }
        SelectedNode = primaryNode;
    }
}
