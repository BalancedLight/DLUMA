using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Threading;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.ViewModels;
using Nodify.Events;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    private readonly ObservableCollection<QuestPaletteItemViewModel>
        _connectionPaletteItems = new();
    private IReadOnlyList<QuestPaletteItemViewModel> _connectionPaletteSource =
        Array.Empty<QuestPaletteItemViewModel>();
    private QuestPortViewModel? _pendingInsertionPort;
    private Point _pendingInsertionLocation;

    private async void OnPendingConnectionCompleted(
        object sender,
        PendingConnectionEventArgs e)
    {
        if (DataContext is not QuestDocumentViewModel document ||
            e.SourceConnector is not QuestPortViewModel sourcePort)
            return;

        if (e.TargetConnector is QuestPortViewModel targetPort)
        {
            await document.ConnectPortsAsync(sourcePort, targetPort).ConfigureAwait(true);
            e.Handled = true;
            return;
        }
        if (sourcePort.Direction != QuestGraphPortDirection.Output)
        {
            document.Status = "Drop an output connector on empty canvas to create its next node.";
            e.Handled = true;
            return;
        }

        OpenConnectionInsertionPopup(document, sourcePort);
        e.Handled = true;
    }

    private async void OnConnectorDisconnect(
        object sender,
        ConnectorEventArgs e)
    {
        if (DataContext is QuestDocumentViewModel document &&
            e.Connector is QuestPortViewModel port)
        {
            await document.DisconnectPortAsync(port).ConfigureAwait(true);
            e.Handled = true;
        }
    }

    private void OpenConnectionInsertionPopup(
        QuestDocumentViewModel document,
        QuestPortViewModel sourcePort)
    {
        var items = document.GetConnectionPaletteItems(sourcePort);
        if (items.Count == 0)
        {
            document.Status = "No insertable catalog entries are available for this connector.";
            return;
        }

        var point = Mouse.GetPosition(Editor);
        _pendingInsertionLocation = ToGraphLocation(point);
        _pendingInsertionPort = sourcePort;
        _connectionPaletteSource = items;
        ConnectionPaletteSearch.Text = string.Empty;
        ReplaceConnectionPalette(items);
        ConnectionPaletteList.SelectedIndex = 0;
        ConnectionInsertPopup.PlacementTarget = Editor;
        ConnectionInsertPopup.Placement = PlacementMode.RelativePoint;
        ConnectionInsertPopup.HorizontalOffset = point.X;
        ConnectionInsertPopup.VerticalOffset = point.Y;
        ConnectionInsertPopup.IsOpen = true;
        _ = Dispatcher.BeginInvoke(
            new Action(() =>
            {
                ConnectionPaletteSearch.Focus();
                Keyboard.Focus(ConnectionPaletteSearch);
            }),
            DispatcherPriority.Input);
    }

    private void OnConnectionPaletteSearchChanged(
        object sender,
        TextChangedEventArgs e)
    {
        if (!IsInitialized || ConnectionPaletteSearch is null)
            return;
        var search = ConnectionPaletteSearch.Text.Trim();
        ReplaceConnectionPalette(_connectionPaletteSource.Where(item =>
            search.Length == 0 ||
            item.Name.Contains(search, StringComparison.OrdinalIgnoreCase) ||
            item.Category.Contains(search, StringComparison.OrdinalIgnoreCase) ||
            item.Description.Contains(search, StringComparison.OrdinalIgnoreCase)));
        ConnectionPaletteList.SelectedIndex = _connectionPaletteItems.Count > 0
            ? 0
            : -1;
    }

    private async void OnConnectionPaletteMouseDoubleClick(
        object sender,
        MouseButtonEventArgs e)
    {
        await InsertPendingConnectionNodeAsync().ConfigureAwait(true);
        e.Handled = true;
    }

    private async void OnConnectionPaletteKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            await InsertPendingConnectionNodeAsync().ConfigureAwait(true);
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            ConnectionInsertPopup.IsOpen = false;
            e.Handled = true;
        }
    }

    private void OnConnectionPopupClosed(object? sender, EventArgs e)
    {
        _pendingInsertionPort = null;
        _connectionPaletteSource = Array.Empty<QuestPaletteItemViewModel>();
        _connectionPaletteItems.Clear();
    }

    private async Task InsertPendingConnectionNodeAsync()
    {
        if (DataContext is not QuestDocumentViewModel document ||
            _pendingInsertionPort is not QuestPortViewModel port ||
            ConnectionPaletteList.SelectedItem is not QuestPaletteItemViewModel item)
            return;
        var location = _pendingInsertionLocation;
        ConnectionInsertPopup.IsOpen = false;
        await document.InsertConnectedAsync(port, item, location).ConfigureAwait(true);
    }

    private Point ToGraphLocation(Point viewportPoint)
    {
        var zoom = Math.Max(Editor.ViewportZoom, 0.0001);
        return new Point(
            Editor.ViewportLocation.X + viewportPoint.X / zoom,
            Editor.ViewportLocation.Y + viewportPoint.Y / zoom);
    }

    private void ReplaceConnectionPalette(
        IEnumerable<QuestPaletteItemViewModel> items)
    {
        _connectionPaletteItems.Clear();
        foreach (var item in items)
            _connectionPaletteItems.Add(item);
    }
}
