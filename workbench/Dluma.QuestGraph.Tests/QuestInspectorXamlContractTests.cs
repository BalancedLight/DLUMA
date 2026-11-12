using System.Xml.Linq;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInspectorXamlContractTests
{
    [Fact]
    public void MainWindowUsesOneSelectedNodeInspectorWithoutLegacyContextTelemetry()
    {
        var source = ReadWorkspaceFile("workbench", "Dluma.QuestGraph", "MainWindow.xaml");

        var inspector = Assert.Single(XDocument.Parse(source).Descendants(),
            element => element.Name.LocalName == "QuestInspectorPanel");
        Assert.Equal("{Binding ActiveDocument}", inspector.Attribute("DataContext")?.Value);
        Assert.DoesNotContain("Header=\"Context\"", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Refresh Context", source, StringComparison.Ordinal);
        Assert.DoesNotContain("EncodingDescription", source, StringComparison.Ordinal);
        var inspectorSource = ReadWorkspaceFile(
            "workbench", "Dluma.QuestGraph", "Views", "QuestInspectorPanel.xaml");
        Assert.DoesNotContain("GlobalStatus", inspectorSource, StringComparison.Ordinal);
    }

    [Fact]
    public void MainWindowKeepsGlobalStatusInScrollableFooterOutsideInspector()
    {
        var document = XDocument.Parse(ReadWorkspaceFile(
            "workbench", "Dluma.QuestGraph", "MainWindow.xaml"));
        XNamespace presentation = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
        var status = Assert.Single(document.Descendants(presentation + "TextBlock"),
            element => (string?)element.Attribute("Text") == "{Binding GlobalStatus}");

        Assert.Equal("Wrap", status.Attribute("TextWrapping")?.Value);
        Assert.Equal(presentation + "ScrollViewer", status.Parent?.Name);
        Assert.Equal("Auto", status.Parent?.Attribute("VerticalScrollBarVisibility")?.Value);
        var footer = Assert.Single(status.Ancestors(presentation + "Border"));
        Assert.Equal("2", footer.Attribute("Grid.Row")?.Value);
    }

    [Fact]
    public void InspectorKeepsRawPxslCollapsedUnderAdvanced()
    {
        var source = ReadWorkspaceFile(
            "workbench",
            "Dluma.QuestGraph",
            "Views",
            "QuestInspectorPanel.xaml");

        Assert.Contains("Header=\"Advanced\" IsExpanded=\"False\"", source, StringComparison.Ordinal);
        Assert.Contains("SelectedNode.SourcePreview", source, StringComparison.Ordinal);
        Assert.Contains("InspectorFields", source, StringComparison.Ordinal);
    }

    [Fact]
    public void CardsUseSummariesAndInlineHeadersInsteadOfRawSource()
    {
        var source = ReadWorkspaceFile(
            "workbench",
            "Dluma.QuestGraph",
            "Views",
            "QuestDocumentView.xaml");

        Assert.Contains("Text=\"{Binding Summary}\"", source, StringComparison.Ordinal);
        Assert.Contains("OnNodeHeaderMouseLeftButtonDown", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"Source location\"", source, StringComparison.Ordinal);
    }

    [Fact]
    public void EditableComboBoxReservesOnlyTheRightArrowAsDropdownHitZone()
    {
        var source = ReadWorkspaceFile("workbench", "Dluma.QuestGraph", "App.xaml");

        Assert.Contains("x:Name=\"EditableDropDownToggle\"", source, StringComparison.Ordinal);
        Assert.Contains("<ColumnDefinition Width=\"30\"", source, StringComparison.Ordinal);
        Assert.Contains("TargetName=\"SelectionToggle\" Property=\"Visibility\" Value=\"Hidden\"", source, StringComparison.Ordinal);
    }

    private static string ReadWorkspaceFile(params string[] segments)
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            var path = Path.Combine(new[] { directory.FullName }.Concat(segments).ToArray());
            if (File.Exists(path))
                return File.ReadAllText(path);
        }
        throw new FileNotFoundException(
            $"Could not locate the workspace file '{Path.Combine(segments)}'.");
    }
}
