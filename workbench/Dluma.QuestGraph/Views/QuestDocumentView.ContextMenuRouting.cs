using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    private static readonly bool s_contextMenuHandlerRegistered =
        RegisterContextMenuHandler();

    private static bool RegisterContextMenuHandler()
    {
        EventManager.RegisterClassHandler(
            typeof(QuestDocumentView),
            ContextMenuService.ContextMenuOpeningEvent,
            new ContextMenuEventHandler(OnContextMenuOpeningRouted),
            handledEventsToo: true);
        return true;
    }

    private static void OnContextMenuOpeningRouted(
        object sender,
        ContextMenuEventArgs e)
    {
        if (sender is not QuestDocumentView view)
            return;
        if (view.DataContext is not QuestDocumentViewModel document ||
            FindNode(e.OriginalSource as DependencyObject) is not { } node)
        {
            view.ContextMenu = null;
            e.Handled = true;
            return;
        }

        view.SelectContextNode(document, node);
        var menu = CreateNodeContextMenu(document);
        menu.Placement = PlacementMode.MousePoint;
        menu.PlacementTarget = e.OriginalSource as UIElement ?? view;
        menu.Closed += (_, _) =>
        {
            if (ReferenceEquals(view.ContextMenu, menu))
                view.ContextMenu = null;
        };
        view.ContextMenu = menu;
        menu.IsOpen = true;
        e.Handled = true;
    }

    private void SelectContextNode(
        QuestDocumentViewModel document,
        QuestNodeViewModel node)
    {
        if (!document.SelectedItems.Contains(node))
        {
            foreach (var selected in document.SelectedItems)
                selected.IsSelected = false;
            document.SelectedItems.Clear();
            node.IsSelected = true;
            document.SelectedItems.Add(node);
        }
        document.SelectedNode = node;
    }

    private static ContextMenu CreateNodeContextMenu(
        QuestDocumentViewModel document)
    {
        var menu = new ContextMenu();
        menu.Items.Add(PaletteMenu("Add after", document, asChild: false));
        menu.Items.Add(PaletteMenu("Add child", document, asChild: true));
        menu.Items.Add(Item("Connect / reconnect selection", document.ConnectSelectedCommand));
        menu.Items.Add(DisconnectMenu("Disconnect incoming", document, incoming: true));
        menu.Items.Add(DisconnectMenu("Disconnect outgoing", document, incoming: false));
        menu.Items.Add(new Separator());
        var duplicate = new MenuItem { Header = "Duplicate" };
        duplicate.Click += async (_, _) => await document.DuplicateSelectedAsync().ConfigureAwait(true);
        menu.Items.Add(duplicate);
        menu.Items.Add(new Separator());
        menu.Items.Add(Item("Move up", document.MoveUpCommand));
        menu.Items.Add(Item("Move down", document.MoveDownCommand));
        menu.Items.Add(Item("Collapse / expand", document.ToggleCollapseCommand));
        menu.Items.Add(new Separator());
        menu.Items.Add(Item("Focus in PXSL Editor", document.FocusSourceCommand));
        menu.Items.Add(Item("Delete selection", document.DeleteNodeCommand));
        return menu;
    }

    private static MenuItem PaletteMenu(
        string header,
        QuestDocumentViewModel document,
        bool asChild)
    {
        var root = new MenuItem { Header = header };
        foreach (var group in document.GetPaletteItems(asChild).GroupBy(item => item.Category))
        {
            var category = new MenuItem { Header = $"{group.Key} ({group.Count()})" };
            foreach (var paletteItem in group)
            {
                var item = new MenuItem
                {
                    Header = paletteItem.Name,
                    ToolTip = paletteItem.Description
                };
                item.Click += async (_, _) =>
                    await document.InsertFromPaletteAsync(paletteItem, asChild).ConfigureAwait(true);
                category.Items.Add(item);
            }
            root.Items.Add(category);
        }
        root.IsEnabled = root.Items.Count > 0;
        return root;
    }

    private static MenuItem DisconnectMenu(
        string header,
        QuestDocumentViewModel document,
        bool incoming)
    {
        var root = new MenuItem { Header = header };
        var selected = document.SelectedNode;
        if (selected is null)
        {
            root.IsEnabled = false;
            return root;
        }
        var edges = document.Model.GraphEdges.Where(edge => incoming
            ? edge.TargetNodeId == selected.GraphNode.Id
            : edge.SourceNodeId == selected.GraphNode.Id);
        foreach (var edge in edges)
        {
            var otherId = incoming ? edge.SourceNodeId : edge.TargetNodeId;
            var other = document.Nodes.FirstOrDefault(node => node.GraphNode.Id == otherId);
            var item = new MenuItem
            {
                Header = $"{edge.SourcePortId} → {other?.Title ?? otherId}"
            };
            item.Click += async (_, _) =>
                await document.DisconnectEdgeAsync(edge.Id).ConfigureAwait(true);
            root.Items.Add(item);
        }
        root.IsEnabled = root.Items.Count > 0;
        return root;
    }

    private static MenuItem Item(string header, ICommand command) =>
        new()
        {
            Header = header,
            Command = command
        };
}
