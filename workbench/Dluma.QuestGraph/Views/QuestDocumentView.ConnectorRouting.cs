using Nodify;
using Nodify.Events;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    static QuestDocumentView()
    {
        EventManager.RegisterClassHandler(
            typeof(QuestDocumentView),
            Connector.PendingConnectionCompletedEvent,
            new PendingConnectionEventHandler(OnPendingConnectionCompletedRouted),
            handledEventsToo: true);
        EventManager.RegisterClassHandler(
            typeof(QuestDocumentView),
            Connector.DisconnectEvent,
            new ConnectorEventHandler(OnConnectorDisconnectRouted),
            handledEventsToo: true);
    }

    private static void OnPendingConnectionCompletedRouted(
        object sender,
        PendingConnectionEventArgs e)
    {
        if (sender is QuestDocumentView view)
            view.OnPendingConnectionCompleted(sender, e);
    }

    private static void OnConnectorDisconnectRouted(
        object sender,
        ConnectorEventArgs e)
    {
        if (sender is QuestDocumentView view)
            view.OnConnectorDisconnect(sender, e);
    }
}
