using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Data;

namespace Dluma.QuestGraph.Views;

public partial class QuestDocumentView
{
    private Popup? _connectionInsertPopup;
    private TextBox? _connectionPaletteSearch;
    private ListBox? _connectionPaletteList;

    private Popup ConnectionInsertPopup =>
        _connectionInsertPopup ??= CreateConnectionInsertionPopup();

    private TextBox ConnectionPaletteSearch
    {
        get
        {
            _ = ConnectionInsertPopup;
            return _connectionPaletteSearch!;
        }
    }

    private ListBox ConnectionPaletteList
    {
        get
        {
            _ = ConnectionInsertPopup;
            return _connectionPaletteList!;
        }
    }

    private Popup CreateConnectionInsertionPopup()
    {
        var search = new TextBox
        {
            Margin = new Thickness(0, 7, 0, 7),
            ToolTip = "Search insertable quest nodes"
        };
        search.TextChanged += OnConnectionPaletteSearchChanged;
        search.KeyDown += OnConnectionPaletteKeyDown;
        _connectionPaletteSearch = search;

        var list = new ListBox
        {
            MinHeight = 170,
            MaxHeight = 320,
            ItemsSource = _connectionPaletteItems,
            DisplayMemberPath = nameof(ViewModels.QuestPaletteItemViewModel.Name),
            HorizontalContentAlignment = HorizontalAlignment.Stretch
        };
        list.Items.GroupDescriptions.Add(new PropertyGroupDescription(
            nameof(ViewModels.QuestPaletteItemViewModel.Category)));
        var groupHeader = new FrameworkElementFactory(typeof(TextBlock));
        groupHeader.SetBinding(TextBlock.TextProperty, new Binding("Name"));
        groupHeader.SetValue(TextBlock.FontWeightProperty, FontWeights.SemiBold);
        groupHeader.SetValue(TextBlock.MarginProperty, new Thickness(3, 7, 3, 2));
        list.GroupStyle.Add(new GroupStyle
        {
            HeaderTemplate = new DataTemplate { VisualTree = groupHeader }
        });
        list.MouseDoubleClick += OnConnectionPaletteMouseDoubleClick;
        list.KeyDown += OnConnectionPaletteKeyDown;
        _connectionPaletteList = list;

        var insert = new Button
        {
            Content = "Insert and connect",
            IsDefault = true,
            Padding = new Thickness(10, 4, 10, 4),
            Margin = new Thickness(0, 8, 5, 0)
        };
        insert.Click += OnConnectionPaletteInsertClick;
        var cancel = new Button
        {
            Content = "Cancel",
            IsCancel = true,
            Padding = new Thickness(10, 4, 10, 4),
            Margin = new Thickness(5, 8, 0, 0)
        };

        var actions = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        actions.Children.Add(insert);
        actions.Children.Add(cancel);

        var content = new Grid();
        content.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        content.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        content.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        content.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var heading = new TextBlock
        {
            Text = "CREATE CONNECTED NODE",
            FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Color.FromRgb(137, 199, 236))
        };
        Grid.SetRow(heading, 0);
        Grid.SetRow(search, 1);
        Grid.SetRow(list, 2);
        Grid.SetRow(actions, 3);
        content.Children.Add(heading);
        content.Children.Add(search);
        content.Children.Add(list);
        content.Children.Add(actions);

        var popup = new Popup
        {
            AllowsTransparency = true,
            StaysOpen = false,
            Child = new Border
            {
                Width = 340,
                Padding = new Thickness(11),
                CornerRadius = new CornerRadius(5),
                BorderThickness = new Thickness(1),
                BorderBrush = new SolidColorBrush(Color.FromRgb(82, 101, 122)),
                Background = new SolidColorBrush(Color.FromRgb(24, 33, 43)),
                Child = content
            }
        };
        cancel.Click += (_, _) => popup.IsOpen = false;
        popup.Closed += OnConnectionPopupClosed;
        return popup;
    }
}
