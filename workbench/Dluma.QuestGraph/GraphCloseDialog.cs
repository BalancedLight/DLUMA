using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;

namespace Dluma.QuestGraph;

internal enum GraphCloseChoice { Cancel, Save, Discard }

internal sealed class GraphCloseDialog : Window
{
    public GraphCloseChoice Choice { get; private set; }

    public GraphCloseDialog(IntPtr owner, bool includesSource)
    {
        Title = "Quest Graph";
        Width = 460;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ShowInTaskbar = false;
        SetResourceReference(BackgroundProperty, "EditorTheme.Panel");
        SetResourceReference(ForegroundProperty, "EditorTheme.Text");
        new WindowInteropHelper(this).Owner = owner;
        Services.EditorThemeService.Current?.AttachWindow(this);
        var body = new StackPanel { Margin = new Thickness(20) };
        body.Children.Add(new TextBlock
        {
            Text = "Save graph layout changes before closing?",
            TextWrapping = TextWrapping.Wrap,
            FontSize = 15,
            Margin = new Thickness(0, 0, 0, includesSource ? 10 : 20)
        });
        if (includesSource)
            body.Children.Add(new TextBlock
            {
                Text = "Saving the layout also saves the quest.",
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 0, 0, 20)
            });
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
        AddButton(buttons, "Save", GraphCloseChoice.Save, true, false);
        AddButton(buttons, "Don't save", GraphCloseChoice.Discard, false, false);
        AddButton(buttons, "Cancel", GraphCloseChoice.Cancel, false, true);
        body.Children.Add(buttons);
        Content = body;
    }

    private void AddButton(Panel panel, string text, GraphCloseChoice choice, bool isDefault, bool isCancel)
    {
        var button = new Button { Content = text, MinWidth = 85, Margin = new Thickness(6, 0, 0, 0), IsDefault = isDefault, IsCancel = isCancel };
        button.Click += (_, _) => { Choice = choice; DialogResult = true; };
        panel.Children.Add(button);
    }
}
