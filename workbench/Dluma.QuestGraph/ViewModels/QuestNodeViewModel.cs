using System.Collections.ObjectModel;
using System.Text;
using System.Windows;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed class QuestNodeViewModel : ObservableObject
{
    private readonly Action<QuestNodeViewModel>? _layoutChanging;
    private Point _location;
    private bool _isSelected;
    private bool _isCollapsed;
    private string? _note;
    private string _sourcePreview;
    private string _headerEditText = string.Empty;
    private bool _isHeaderEditing;
    private bool _isHeaderCommitting;

    public QuestNodeViewModel(
        QuestGraphNode graphNode,
        QuestSemanticNode semanticNode,
        string stableId,
        Point location,
        string sourcePreview,
        string displayHeader,
        string summary,
        bool isCollapsed,
        string? note,
        Action<QuestNodeViewModel>? layoutChanging,
        string headerColor = QuestPaletteColors.Default,
        string headerForegroundColor = "#FFFFFF")
    {
        GraphNode = graphNode;
        SemanticNode = semanticNode;
        StableId = stableId;
        _location = location;
        _sourcePreview = sourcePreview;
        DisplayHeader = displayHeader;
        Summary = summary;
        _isCollapsed = isCollapsed;
        _note = note;
        _layoutChanging = layoutChanging;
        HeaderColor = headerColor;
        HeaderForegroundColor = headerForegroundColor;
        IEnumerable<QuestGraphPort> visiblePorts = graphNode.Ports;
        Inputs = new ObservableCollection<QuestPortViewModel>(
            visiblePorts
                .Where(port => port.Direction == QuestGraphPortDirection.Input)
                .Select(port => new QuestPortViewModel(port)));
        Outputs = new ObservableCollection<QuestPortViewModel>(
            visiblePorts
                .Where(port => port.Direction == QuestGraphPortDirection.Output)
                .Select(port => new QuestPortViewModel(port)));
    }

    public QuestGraphNode GraphNode { get; }
    public QuestSemanticNode SemanticNode { get; }
    public string StableId { get; }
    public string Title => DisplayHeader;
    public string DisplayHeader { get; }
    public string Summary { get; }
    public string Kind => GraphNode.Kind.ToString();
    public bool IsOpaque => GraphNode.IsOpaque;
    public bool IsDetached => GraphNode.IsDetached;
    public string HeaderColor { get; }
    public string HeaderForegroundColor { get; }
    public string BorderColor => IsDetached ? "#C9825B" : "#52657A";
    public double NodeOpacity => IsDetached ? 0.78 : 1.0;
    public int Line => SemanticNode.Span.Line;
    public int Column => SemanticNode.Span.Column;
    public ObservableCollection<QuestPortViewModel> Inputs { get; }
    public ObservableCollection<QuestPortViewModel> Outputs { get; }

    public string HeaderEditText
    {
        get => _headerEditText;
        set => SetProperty(ref _headerEditText, value ?? string.Empty);
    }

    public bool IsHeaderEditing
    {
        get => _isHeaderEditing;
        private set => SetProperty(ref _isHeaderEditing, value);
    }

    public bool IsHeaderCommitting
    {
        get => _isHeaderCommitting;
        private set => SetProperty(ref _isHeaderCommitting, value);
    }

    public Point Location
    {
        get => _location;
        set
        {
            if (_location == value)
                return;
            _layoutChanging?.Invoke(this);
            SetProperty(ref _location, value);
        }
    }

    public bool IsSelected
    {
        get => _isSelected;
        set => SetProperty(ref _isSelected, value);
    }

    public bool IsCollapsed
    {
        get => _isCollapsed;
        set
        {
            if (_isCollapsed == value)
                return;
            _layoutChanging?.Invoke(this);
            SetProperty(ref _isCollapsed, value);
        }
    }

    public string? Note
    {
        get => _note;
        set
        {
            if (string.Equals(_note, value, StringComparison.Ordinal))
                return;
            _layoutChanging?.Invoke(this);
            SetProperty(ref _note, value);
        }
    }

    public string SourcePreview
    {
        get => _sourcePreview;
        set => SetProperty(ref _sourcePreview, value);
    }

    public QuestPortViewModel? FindPort(string id) =>
        Inputs.Concat(Outputs).FirstOrDefault(port =>
            string.Equals(port.Id, id, StringComparison.Ordinal));

    public void BeginHeaderEdit(string value)
    {
        HeaderEditText = value;
        IsHeaderCommitting = false;
        IsHeaderEditing = true;
    }

    public bool TryBeginHeaderCommit()
    {
        if (!IsHeaderEditing || IsHeaderCommitting)
            return false;
        IsHeaderCommitting = true;
        return true;
    }

    public void CancelHeaderEdit()
    {
        IsHeaderCommitting = false;
        IsHeaderEditing = false;
    }

    public void CompleteHeaderCommit() => CancelHeaderEdit();
}
