using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView : UserControl
{
    public QuestDocumentView() => InitializeComponent();

    private void OnEditorDragOver(object sender, DragEventArgs e)
    {
        e.Effects = DataContext is QuestDocumentViewModel { CanEditSource: true } &&
            e.Data.GetDataPresent(typeof(QuestPaletteItemViewModel))
            ? DragDropEffects.Copy
            : DragDropEffects.None;
        e.Handled = true;
    }

    private async void OnEditorDrop(object sender, DragEventArgs e)
    {
        if (DataContext is not QuestDocumentViewModel document ||
            e.Data.GetData(typeof(QuestPaletteItemViewModel)) is not QuestPaletteItemViewModel item)
            return;
        var target = FindNode(e.OriginalSource as DependencyObject);
        var point = e.GetPosition(Editor);
        var zoom = Math.Max(Editor.ViewportZoom, 0.0001);
        var graphLocation = new Point(
            Editor.ViewportLocation.X + point.X / zoom,
            Editor.ViewportLocation.Y + point.Y / zoom);
        try
        {
            await document.DropPaletteItemAsync(
                item,
                target,
                graphLocation).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            document.Status = $"Node drop failed: {exception.Message}";
        }
        e.Handled = true;
    }

    private void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is not (Key.Delete or Key.Back) ||
            Keyboard.FocusedElement is TextBoxBase or PasswordBox ||
            Keyboard.FocusedElement is ComboBox { IsEditable: true } ||
            DataContext is not QuestDocumentViewModel document ||
            !document.DeleteNodeCommand.CanExecute(null))
            return;
        document.DeleteNodeCommand.Execute(null);
        e.Handled = true;
    }

    private static QuestNodeViewModel? FindNode(DependencyObject? current)
    {
        while (current is not null)
        {
            if (current is FrameworkElement { DataContext: QuestNodeViewModel node })
                return node;
            current = current is Visual
                ? VisualTreeHelper.GetParent(current)
                : LogicalTreeHelper.GetParent(current);
        }
        return null;
    }
}
