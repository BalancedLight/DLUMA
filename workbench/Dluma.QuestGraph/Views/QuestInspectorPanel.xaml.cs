using System.Windows.Controls;
using System.Windows.Input;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph.Views;

public partial class QuestInspectorPanel : UserControl
{
    public QuestInspectorPanel() => InitializeComponent();

    private async void OnTextEditorKeyDown(object sender, KeyEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is not QuestInspectorFieldViewModel field)
            return;
        if (e.Key == Key.Escape)
        {
            field.Revert();
            e.Handled = true;
            return;
        }
        if (e.Key is not (Key.Enter or Key.Return) ||
            Keyboard.Modifiers != ModifierKeys.None)
            return;
        e.Handled = true;
        await field.CommitAsync().ConfigureAwait(true);
        (sender as UIElement)?.MoveFocus(new TraversalRequest(FocusNavigationDirection.Next));
    }

    private async void OnTextEditorLostKeyboardFocus(
        object sender,
        KeyboardFocusChangedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is QuestInspectorFieldViewModel field &&
            field.HasPendingValue)
            await field.CommitAsync().ConfigureAwait(true);
    }

    private async void OnPickerSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (sender is not ComboBox { DataContext: QuestInspectorFieldViewModel field } ||
            e.AddedItems.Count == 0 ||
            e.AddedItems[0] is not string selected)
            return;
        await field.CommitAsync(selected).ConfigureAwait(true);
    }

    private async void OnPickerDropDownOpened(object sender, EventArgs e)
    {
        if (DataContext is QuestDocumentViewModel document)
            await document.RefreshInspectorContextAsync().ConfigureAwait(true);
    }

    private async void OnPickerDropDownClosed(object sender, EventArgs e)
    {
        if (sender is ComboBox { DataContext: QuestInspectorFieldViewModel field } &&
            field.HasPendingValue)
            await field.CommitAsync().ConfigureAwait(true);
    }

    private async void OnBooleanClick(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is QuestInspectorFieldViewModel field)
            await field.CommitAsync().ConfigureAwait(true);
    }
}
