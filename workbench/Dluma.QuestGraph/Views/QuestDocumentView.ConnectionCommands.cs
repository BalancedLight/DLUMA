using System.Windows;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    private async void OnConnectionPaletteInsertClick(
        object sender,
        RoutedEventArgs e)
    {
        await InsertPendingConnectionNodeAsync().ConfigureAwait(true);
        e.Handled = true;
    }
}
