using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentLayoutTests
{
    private const string Quest =
        "quest Example\n" +
        "    ,BLOCK first\n" +
        "        ,step alpha\n" +
        "        ,step beta\n" +
        "    ,BLOCK second\n" +
        "        ,step gamma\n";

    [Fact]
    public void ADocumentWithNoSavedLayoutOpensAlreadyArranged()
    {
        var document = Open(Quest);

        Assert.Equal(document.Model.GraphNodes.Count, document.Nodes.Count);
        AssertNoOverlap(document);

        var byName = document.Nodes.ToDictionary(node => Label(node));
        Assert.True(byName["first"].Location.Y > byName["Example"].Location.Y,
            "a block must open below the quest that contains it");
        Assert.True(byName["alpha"].Location.Y > byName["first"].Location.Y,
            "a step must open below the block that contains it");
        Assert.Equal(byName["first"].Location.Y, byName["second"].Location.Y);
        Assert.True(byName["first"].Location.X < byName["second"].Location.X,
            "ordered blocks must open left to right");
    }

    [Fact]
    public void AutoLayoutRearrangesWithoutTouchingTheSource()
    {
        var document = Open(Quest);
        var before = document.Model.Source.ToArray();
        foreach (var node in document.Nodes)
            node.Location = new System.Windows.Point(0, 0);

        document.AutoLayout();

        AssertNoOverlap(document);
        Assert.Equal(before, document.Model.Source.ToArray());
        Assert.False(document.IsSourceDirty);
    }

    [Fact]
    public void SavedPositionsSurviveAndOnlyNewNodesAreParked()
    {
        var placed = Open(Quest);
        // Pin every node somewhere deliberate, then reopen against that sidecar.
        var index = 0;
        foreach (var node in placed.Nodes)
            node.Location = new System.Windows.Point(4000 + index++ * 500, 4000);
        var sidecar = placed.CreateSidecar();
        var expected = placed.Nodes.ToDictionary(Label, node => node.Location);

        var reopened = Open(Quest, sidecar);

        foreach (var node in reopened.Nodes)
            Assert.Equal(expected[Label(node)], node.Location);
    }

    [Fact]
    public void AutoLayoutRefitsGroupsAroundTheirMembers()
    {
        var document = Open(Quest);
        var members = document.Nodes.Take(2).Select(node => node.StableId).ToArray();
        document.Groups.Add(new QuestGroupViewModel(
            "group-1", "Group", -9000, -9000, 10, 10, members, null));

        document.AutoLayout();

        var group = Assert.Single(document.Groups);
        var bounds = document.Nodes
            .Where(node => members.Contains(node.StableId))
            .Select(node => node.Location)
            .ToArray();
        Assert.True(group.X <= bounds.Min(point => point.X),
            "the group must enclose its leftmost member");
        Assert.True(group.Y <= bounds.Min(point => point.Y),
            "the group must enclose its topmost member");
        Assert.True(group.X + group.Width >= bounds.Max(point => point.X),
            "the group must enclose its rightmost member");
    }

    private static void AssertNoOverlap(QuestDocumentViewModel document)
    {
        var options = QuestGraphAutoLayoutOptions.Default;
        var rectangles = document.Nodes
            .Select(node => (
                Label: Label(node),
                Rect: new System.Windows.Rect(
                    node.Location.X,
                    node.Location.Y,
                    options.NodeWidth,
                    QuestGraphAutoLayout.EstimateHeight(node.GraphNode, options, node.IsCollapsed))))
            .ToArray();
        for (var outer = 0; outer < rectangles.Length; ++outer)
        {
            for (var inner = outer + 1; inner < rectangles.Length; ++inner)
            {
                Assert.False(
                    rectangles[outer].Rect.IntersectsWith(rectangles[inner].Rect),
                    $"'{rectangles[outer].Label}' overlaps '{rectangles[inner].Label}'");
            }
        }
    }

    private static string Label(QuestNodeViewModel node) =>
        node.SemanticNode.Arguments.FirstOrDefault() ?? node.SemanticNode.Name;

    private static QuestDocumentViewModel Open(string text, QuestGraphSidecarV1? sidecar = null)
    {
        var source = new QuestSourceSnapshot(
            "layout-document",
            "quests/layout.pxsl",
            0,
            Encoding.UTF8.GetBytes(text));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source),
            null,
            sidecar,
            false,
            null,
            false,
            null);
        return new QuestDocumentViewModel(new QuestDocumentService(), opened);
    }
}
