using System.Windows;
using System.Windows.Controls;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    private void OnNodeContextMenuOpening(
        object sender,
        ContextMenuEventArgs e)
    {
        if (DataContext is not QuestDocumentViewModel document ||
            sender is not FrameworkElement { DataContext: QuestNodeViewModel node })
            return;
        if (document.SelectedItems.Contains(node))
        {
            document.SelectedNode = node;
            return;
        }

        foreach (var selected in document.SelectedItems)
            selected.IsSelected = false;
        document.SelectedItems.Clear();
        node.IsSelected = true;
        document.SelectedItems.Add(node);
        document.SelectedNode = node;
    }
}
