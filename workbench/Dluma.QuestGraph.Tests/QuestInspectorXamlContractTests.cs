using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInspectorXamlContractTests
{
    [Fact]
    public void MainWindowUsesOneSelectedNodeInspectorWithoutLegacyContextTelemetry()
    {
        var source = ReadWorkspaceFile("workbench", "Dluma.QuestGraph", "MainWindow.xaml");

        Assert.Contains("QuestInspectorPanel", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Header=\"Context\"", source, StringComparison.Ordinal);
        Assert.DoesNotContain("Refresh Context", source, StringComparison.Ordinal);
        Assert.DoesNotContain("EncodingDescription", source, StringComparison.Ordinal);
        Assert.DoesNotContain("GlobalStatus", source, StringComparison.Ordinal);
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
