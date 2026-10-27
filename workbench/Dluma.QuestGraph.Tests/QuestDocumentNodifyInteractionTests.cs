using System.Text;
using System.Windows;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentNodifyInteractionTests
{
    [Fact]
    public async Task MultiSelectionDeleteAppliesDisjointSubtreeEditsTogether()
    {
        var document = CreateDocument(
            "quest Synthetic\n" +
            "    ,step first\n" +
            "    ,step keep\n" +
            "    ,step third\n");
        Select(
            document,
            document.Nodes.Where(node =>
                node.SourcePreview.Contains("first", StringComparison.Ordinal) ||
                node.SourcePreview.Contains("third", StringComparison.Ordinal)));

        await document.DeleteSelectionAsync();

        var text = Encoding.UTF8.GetString(document.Model.Source.Bytes.Span);
        Assert.DoesNotContain("first", text, StringComparison.Ordinal);
        Assert.DoesNotContain("third", text, StringComparison.Ordinal);
        Assert.Contains(",step keep", text, StringComparison.Ordinal);
        Assert.True(document.IsDirty);
    }

    [Fact]
    public async Task DirectConnectorDisconnectAndReconnectUsesProjectedPorts()
    {
        var document = CreateDocument(
            "quest Synthetic\n" +
            "    ,step first\n" +
            "    ,step second\n");
        var second = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("second", StringComparison.Ordinal));

        await document.DisconnectPortAsync(Assert.Single(second.Inputs));

        second = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("second", StringComparison.Ordinal));
        Assert.True(second.IsDetached);
        var first = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("first", StringComparison.Ordinal));
        await document.ConnectPortsAsync(
            Assert.Single(first.Outputs, port => port.Id == "out"),
            Assert.Single(second.Inputs));

        second = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("second", StringComparison.Ordinal));
        Assert.False(second.IsDetached);
        Assert.Contains(document.Connections, connection =>
            connection.Target == Assert.Single(second.Inputs));
    }

    [Fact]
    public async Task DroppingOutputOnEmptyCanvasInsertsAndConnectsSelectedTemplate()
    {
        var document = CreateDocument(
            "quest Synthetic\n" +
            "    ,step first\n");
        var first = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("first", StringComparison.Ordinal));
        var requestedLocation = new Point(640, 360);

        await document.InsertConnectedAsync(
            Assert.Single(first.Outputs, port => port.Id == "out"),
            new QuestPaletteItemViewModel(
                "Synthetic step",
                "Test",
                ",step second",
                "Synthetic interaction-test node."),
            requestedLocation);

        var second = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("second", StringComparison.Ordinal));
        Assert.Equal(requestedLocation, second.Location);
        Assert.Contains(document.Connections, connection =>
            ReferenceEquals(connection.Target, Assert.Single(second.Inputs)));
    }

    [Fact]
    public void OutputInsertionPaletteExcludesQuestDeclarations()
    {
        var document = CreateDocument(
            "quest Synthetic\n" +
            "    ,step first\n");
        document.ConnectionPaletteProvider = _ =>
        [
            new QuestPaletteItemViewModel(
                "Quest",
                "Structure",
                "quest Another",
                "Quest root."),
            new QuestPaletteItemViewModel(
                "Step",
                "Flow",
                ",step next",
                "Synthetic next step.")
        ];
        var first = Assert.Single(document.Nodes, node =>
            node.SourcePreview.Contains("first", StringComparison.Ordinal));

        var items = document.GetConnectionPaletteItems(
            Assert.Single(first.Outputs, port => port.Id == "out"));

        Assert.Collection(items, item => Assert.Equal("Step", item.Name));
    }

    private static void Select(
        QuestDocumentViewModel document,
        IEnumerable<QuestNodeViewModel> nodes)
    {
        document.SelectedItems.Clear();
        foreach (var node in nodes)
        {
            node.IsSelected = true;
            document.SelectedItems.Add(node);
        }
    }

    private static QuestDocumentViewModel CreateDocument(string text)
    {
        var source = new QuestSourceSnapshot(
            $"synthetic-{Guid.NewGuid():N}",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(text));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source),
            null,
            null,
            false,
            null,
            false,
            null);
        return new QuestDocumentViewModel(new QuestDocumentService(), opened);
    }
}
