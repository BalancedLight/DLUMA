using System.Collections.ObjectModel;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed class QuestInspectorFieldViewModel : ObservableObject
{
    private readonly Func<QuestInspectorFieldViewModel, string, Task<bool>> _commit;
    private string _committedValue;
    private string _draftValue;
    private bool? _booleanValue;
    private bool _canEdit;
    private bool _isCommitting;
    private string? _validationMessage;
    private bool _isSynchronizingBoolean;

    public QuestInspectorFieldViewModel(
        string id,
        string displayName,
        QuestParameterEditorKind editorKind,
        bool isRequired,
        string value,
        IReadOnlyList<string> options,
        string? description,
        bool canEdit,
        Func<QuestInspectorFieldViewModel, string, Task<bool>> commit)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentException.ThrowIfNullOrWhiteSpace(displayName);
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(commit);

        Id = id;
        DisplayName = displayName;
        EditorKind = editorKind;
        IsRequired = isRequired;
        Description = description;
        _committedValue = NormalizePresentationValue(value);
        _draftValue = _committedValue;
        _booleanValue = ParseBoolean(_draftValue);
        _canEdit = canEdit;
        _commit = commit;
        ReplaceOptions(options);
    }

    public string Id { get; }
    public string DisplayName { get; }
    public QuestParameterEditorKind EditorKind { get; }
    public bool IsRequired { get; }
    public string? Description { get; }
    public ObservableCollection<string> Options { get; } = new();

    public bool IsTextEditor => EditorKind is
        QuestParameterEditorKind.Text or
        QuestParameterEditorKind.Number;

    public bool IsPickerEditor => EditorKind is
        QuestParameterEditorKind.Enum or
        QuestParameterEditorKind.TextId or
        QuestParameterEditorKind.Speaker or
        QuestParameterEditorKind.DialogueGroup or
        QuestParameterEditorKind.QuestReference or
        QuestParameterEditorKind.MapObjectClass or
        QuestParameterEditorKind.MapObjectName or
        QuestParameterEditorKind.Scenario;

    public bool IsBooleanEditor => EditorKind == QuestParameterEditorKind.Boolean;
    public bool IsLiteralEditor => EditorKind == QuestParameterEditorKind.Literal;
    public bool IsOptional => !IsRequired;
    public bool IsThreeStateBoolean => IsBooleanEditor && IsOptional;
    public bool HasPendingValue => !string.Equals(_draftValue, _committedValue, StringComparison.Ordinal);

    public string DraftValue
    {
        get => _draftValue;
        set
        {
            value ??= string.Empty;
            if (!SetProperty(ref _draftValue, value))
                return;
            OnPropertyChanged(nameof(HasPendingValue));
            if (IsBooleanEditor && !_isSynchronizingBoolean)
            {
                _isSynchronizingBoolean = true;
                try
                {
                    BooleanValue = ParseBoolean(value);
                }
                finally
                {
                    _isSynchronizingBoolean = false;
                }
            }
        }
    }

    public bool? BooleanValue
    {
        get => _booleanValue;
        set
        {
            if (!SetProperty(ref _booleanValue, value) || _isSynchronizingBoolean)
                return;
            _isSynchronizingBoolean = true;
            try
            {
                DraftValue = value switch
                {
                    true => "true",
                    false => "false",
                    null => string.Empty
                };
            }
            finally
            {
                _isSynchronizingBoolean = false;
            }
        }
    }

    public bool CanEdit
    {
        get => _canEdit && !IsLiteralEditor;
        private set
        {
            if (SetProperty(ref _canEdit, value))
                OnPropertyChanged();
        }
    }

    public bool IsCommitting
    {
        get => _isCommitting;
        private set => SetProperty(ref _isCommitting, value);
    }

    public string? ValidationMessage
    {
        get => _validationMessage;
        private set => SetProperty(ref _validationMessage, value);
    }

    public void ReplaceOptions(IEnumerable<string> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        Options.Clear();
        foreach (var value in values
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(value => value, StringComparer.OrdinalIgnoreCase))
            Options.Add(value);
    }

    public void SetCanEdit(bool canEdit) => CanEdit = canEdit;

    public void Revert()
    {
        DraftValue = _committedValue;
        ValidationMessage = null;
    }

    public async Task<bool> CommitAsync(string? explicitValue = null)
    {
        if (!CanEdit || IsCommitting)
            return false;
        if (explicitValue is not null)
            DraftValue = explicitValue;
        if (!HasPendingValue)
            return true;

        var requested = DraftValue;
        IsCommitting = true;
        ValidationMessage = null;
        try
        {
            if (!await _commit(this, requested).ConfigureAwait(true))
            {
                Revert();
                return false;
            }
            _committedValue = requested;
            OnPropertyChanged(nameof(HasPendingValue));
            return true;
        }
        catch (Exception exception)
        {
            Revert();
            ValidationMessage = exception.Message;
            return false;
        }
        finally
        {
            IsCommitting = false;
        }
    }

    private static string NormalizePresentationValue(string? value) =>
        string.Equals(value, "_", StringComparison.Ordinal) ? string.Empty : value ?? string.Empty;

    private static bool? ParseBoolean(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "_")
            return null;
        if (bool.TryParse(value, out var parsed))
            return parsed;
        return value switch
        {
            "1" => true,
            "0" => false,
            _ => null
        };
    }
}
