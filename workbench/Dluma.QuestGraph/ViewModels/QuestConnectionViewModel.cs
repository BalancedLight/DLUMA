using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed class QuestConnectionViewModel
{
    public QuestConnectionViewModel(
        QuestGraphEdge edge,
        QuestPortViewModel source,
        QuestPortViewModel target)
    {
        Edge = edge;
        Source = source;
        Target = target;
        Source.IsConnected = true;
        Target.IsConnected = true;
    }

    public QuestGraphEdge Edge { get; }
    public QuestPortViewModel Source { get; }
    public QuestPortViewModel Target { get; }
    public string Label => Edge.Kind switch
    {
        QuestGraphEdgeKind.Branch => "branch",
        QuestGraphEdgeKind.Nested => "child",
        _ => string.Empty
    };
}
