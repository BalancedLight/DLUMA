using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph;

public partial class MainWindow : Window
{
    private bool _closingConfirmed;
    private Point _paletteDragStart;
    private QuestPaletteItemViewModel? _paletteDragItem;

    public MainWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }

    private async void OnClosing(object? sender, CancelEventArgs e)
    {
        if (_closingConfirmed || DataContext is not MainViewModel viewModel)
            return;
        var dirty = viewModel.Documents.Where(document => document.IsDirty).ToArray();
        if (dirty.Length == 0)
            return;

        e.Cancel = true;
        var result = MessageBox.Show(
            this,
            $"{dirty.Length} quest document(s) contain unsaved source or layout changes. Save documents that can safely update their original files before closing?",
            "Unsaved quest documents",
            MessageBoxButton.YesNoCancel,
            MessageBoxImage.Warning);
        if (result == MessageBoxResult.Cancel)
            return;
        if (result == MessageBoxResult.Yes)
        {
            foreach (var document in dirty)
            {
                if (!document.CanSaveOriginal)
                    return;
                var receipt = await viewModel.SaveDocumentAsync(document).ConfigureAwait(true);
                if (!receipt.SourceSaved || document.IsDirty)
                    return;
            }
        }
        else
        {
            foreach (var document in dirty)
            {
                if (!await document.DiscardUnsavedChangesAsync().ConfigureAwait(true))
                    return;
            }
        }
        _closingConfirmed = true;
        _ = Dispatcher.BeginInvoke(new Action(Close), DispatcherPriority.Background);
    }

    private void OnPaletteMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _paletteDragStart = e.GetPosition(PaletteList);
        _paletteDragItem = ItemsControl.ContainerFromElement(
            PaletteList,
            e.OriginalSource as DependencyObject) is ListBoxItem item
                ? item.DataContext as QuestPaletteItemViewModel
                : null;
    }

    private void OnPaletteMouseMove(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed ||
            _paletteDragItem is not QuestPaletteItemViewModel item)
            return;
        var current = e.GetPosition(PaletteList);
        if (Math.Abs(current.X - _paletteDragStart.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(current.Y - _paletteDragStart.Y) < SystemParameters.MinimumVerticalDragDistance)
            return;
        var data = new DataObject(typeof(QuestPaletteItemViewModel), item);
        DragDrop.DoDragDrop(PaletteList, data, DragDropEffects.Copy);
        _paletteDragItem = null;
    }
}
