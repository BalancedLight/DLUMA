using System.Windows;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed class QuestPortViewModel : ObservableObject
{
    private Point _anchor;
    private bool _isConnected;

    public QuestPortViewModel(QuestGraphPort port)
    {
        Id = port.Id;
        DisplayName = port.DisplayName;
        Direction = port.Direction;
        Type = port.Type;
        AllowsMultiple = port.AllowsMultiple;
    }

    public string Id { get; }
    public string DisplayName { get; }
    public QuestGraphPortDirection Direction { get; }
    public string Type { get; }
    public bool AllowsMultiple { get; }

    public Point Anchor
    {
        get => _anchor;
        set => SetProperty(ref _anchor, value);
    }

    public bool IsConnected
    {
        get => _isConnected;
        set => SetProperty(ref _isConnected, value);
    }
}
