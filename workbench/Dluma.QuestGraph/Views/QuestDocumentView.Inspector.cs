using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    private void OnNodeHeaderMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount != 2 ||
            sender is not TextBlock { DataContext: QuestNodeViewModel node } label ||
            DataContext is not QuestDocumentViewModel document ||
            !document.BeginHeaderEdit(node))
            return;

        e.Handled = true;
        _ = Dispatcher.BeginInvoke(
            new Action(() =>
            {
                if (label.Parent is not Grid grid)
                    return;
                var editor = grid.Children.OfType<TextBox>().FirstOrDefault();
                if (editor is null)
                    return;
                editor.Focus();
                editor.SelectAll();
            }),
            DispatcherPriority.Input);
    }

    private async void OnNodeHeaderEditKeyDown(object sender, KeyEventArgs e)
    {
        if (sender is not TextBox { DataContext: QuestNodeViewModel node } ||
            DataContext is not QuestDocumentViewModel document)
            return;
        if (e.Key == Key.Escape)
        {
            node.CancelHeaderEdit();
            e.Handled = true;
            return;
        }
        if (e.Key is not (Key.Enter or Key.Return) ||
            Keyboard.Modifiers != ModifierKeys.None ||
            !node.TryBeginHeaderCommit())
            return;

        e.Handled = true;
        await document.CommitHeaderEditAsync(node).ConfigureAwait(true);
    }

    private async void OnNodeHeaderEditLostKeyboardFocus(
        object sender,
        KeyboardFocusChangedEventArgs e)
    {
        if (sender is TextBox { DataContext: QuestNodeViewModel node } &&
            DataContext is QuestDocumentViewModel document &&
            node.IsHeaderEditing &&
            node.TryBeginHeaderCommit())
            await document.CommitHeaderEditAsync(node).ConfigureAwait(true);
    }
}
