using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphAutoLayoutTests
{
    private const string Quest =
        "quest Example\n" +
        "    ,BLOCK first\n" +
        "        ,step alpha\n" +
        "            ,record alphaChild\n" +
        "        ,step beta\n" +
        "    ,BLOCK second\n" +
        "        ,step gamma\n" +
        "        ,step delta\n";

    [Fact]
    public void NestingDescendsAndOrderedFlowRunsLeftToRight()
    {
        var (document, layout) = Layout();
        var quest = At(document, layout, "Example");
        var first = At(document, layout, "first");
        var second = At(document, layout, "second");
        var alpha = At(document, layout, "alpha");
        var beta = At(document, layout, "beta");

        // A container sits one band above its contents.
        Assert.True(quest.Y < first.Y, "the quest must sit above its blocks");
        Assert.True(first.Y < alpha.Y, "a block must sit above its steps");

        // Ordered siblings share a band and advance rightward in source order.
        Assert.Equal(first.Y, second.Y);
        Assert.True(first.X < second.X, "the second block must follow the first");
        Assert.Equal(alpha.Y, beta.Y);
        Assert.True(alpha.X < beta.X, "beta must follow alpha");

        // A container is left-aligned with its first child, so the body wire is short.
        Assert.Equal(first.X, alpha.X);
        Assert.Equal(quest.X, first.X);
    }

    [Fact]
    public void SiblingSubtreesNeverOverlap()
    {
        var (document, layout) = Layout();
        var options = QuestGraphAutoLayoutOptions.Default;
        var byId = document.GraphNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);

        var rectangles = layout.Positions
            .Select(entry => (
                entry.Key,
                Left: entry.Value.X,
                Top: entry.Value.Y,
                Right: entry.Value.X + options.NodeWidth,
                Bottom: entry.Value.Y + QuestGraphAutoLayout.EstimateHeight(
                    byId[entry.Key], options)))
            .ToArray();

        Assert.NotEmpty(rectangles);
        for (var outer = 0; outer < rectangles.Length; ++outer)
        {
            for (var inner = outer + 1; inner < rectangles.Length; ++inner)
            {
                var a = rectangles[outer];
                var b = rectangles[inner];
                var overlaps = a.Left < b.Right && b.Left < a.Right &&
                    a.Top < b.Bottom && b.Top < a.Bottom;
                Assert.False(overlaps, $"'{a.Key}' overlaps '{b.Key}'");
            }
        }
    }

    [Fact]
    public void ASubtreeReservesItsOwnHorizontalExtent()
    {
        var (document, layout) = Layout();
        // alpha owns a nested record, so beta must clear alpha's whole subtree
        // rather than sitting one node-width away.
        var alpha = At(document, layout, "alpha");
        var beta = At(document, layout, "beta");
        var alphaChild = At(document, layout, "alphaChild");

        Assert.True(alphaChild.X < beta.X,
            "the nested record must stay left of the next ordered step");
        Assert.True(alpha.Y < alphaChild.Y);
    }

    [Fact]
    public void EveryProjectedNodeIsPlacedExactlyOnce()
    {
        var (document, layout) = Layout();

        Assert.Equal(document.GraphNodes.Count, layout.Positions.Count);
        Assert.All(document.GraphNodes, node => Assert.Contains(node.Id, layout.Positions));
    }

    [Fact]
    public void LayoutIsDeterministic()
    {
        var document = PxslParser.Parse(Source(Quest));
        var first = QuestGraphAutoLayout.Compute(document);
        var second = QuestGraphAutoLayout.Compute(document);

        Assert.Equal(first.Positions, second.Positions);
        Assert.Equal(first.Width, second.Width);
        Assert.Equal(first.Height, second.Height);
    }

    [Fact]
    public void DetachedRootsArePackedBesideTheQuestInsteadOfOnTopOfIt()
    {
        var document = PxslParser.Parse(Source(
            "<<<?xml version=\"1.0\"?>>>\n" + Quest));
        var layout = QuestGraphAutoLayout.Compute(document);
        var preamble = Assert.Single(document.GraphNodes,
            node => node.Kind == QuestSemanticKind.RawXml);
        var quest = Assert.Single(document.GraphNodes,
            node => node.Kind == QuestSemanticKind.Quest);

        Assert.Equal(layout.Positions[preamble.Id].Y, layout.Positions[quest.Id].Y);
        Assert.NotEqual(layout.Positions[preamble.Id].X, layout.Positions[quest.Id].X);
    }

    [Fact]
    public void TheTransposedOrientationStacksTheFlowAndNestsRightward()
    {
        var document = PxslParser.Parse(Source(Quest));
        var layout = QuestGraphAutoLayout.Compute(document, new QuestGraphAutoLayoutOptions
        {
            Orientation = QuestGraphFlowOrientation.FlowDownNestRight
        });
        var quest = At(document, layout, "Example");
        var first = At(document, layout, "first");
        var second = At(document, layout, "second");

        // Nesting now extends rightward...
        Assert.True(quest.X < first.X, "a block must sit right of the quest");
        // ...and ordered siblings stack downward in one column.
        Assert.Equal(first.X, second.X);
        Assert.True(first.Y < second.Y, "the second block must follow the first");
    }

    [Fact]
    public void TheTransposedOrientationIsNarrowerThanTheDefaultForAWideQuest()
    {
        // A long ordered flow with shallow nesting -- the shape real quests take.
        var text = "quest Example\n" + string.Concat(
            Enumerable.Range(0, 20).Select(index =>
                $"    ,BLOCK block{index}\n        ,step step{index}\n"));
        var document = PxslParser.Parse(Source(text));
        var wide = QuestGraphAutoLayout.Compute(document);
        var tall = QuestGraphAutoLayout.Compute(document, new QuestGraphAutoLayoutOptions
        {
            Orientation = QuestGraphFlowOrientation.FlowDownNestRight
        });

        Assert.True(tall.Width < wide.Width, "transposing must trade width for height");
        Assert.True(tall.Height > wide.Height);
    }

    [Fact]
    public void AnEmptyDocumentPlacesNothing()
    {
        var document = PxslParser.Parse(Source(string.Empty));
        var layout = QuestGraphAutoLayout.Compute(document);

        Assert.Empty(layout.Positions);
        Assert.Equal(0, layout.Width);
        Assert.Equal(0, layout.Height);
    }

    private static (QuestGraphDocument Document, QuestGraphAutoLayoutResult Layout) Layout()
    {
        var document = PxslParser.Parse(Source(Quest));
        return (document, QuestGraphAutoLayout.Compute(document));
    }

    private static QuestGraphLayoutPoint At(
        QuestGraphDocument document,
        QuestGraphAutoLayoutResult layout,
        string argument)
    {
        var semantic = Assert.Single(document.SemanticNodes, node =>
            node.Name == argument || node.Arguments.Contains(argument));
        var graph = Assert.Single(document.GraphNodes,
            node => node.SemanticNodeId == semantic.Id);
        return layout.Positions[graph.Id];
    }

    private static QuestSourceSnapshot Source(string text) => new(
        "layout-document",
        "quests/layout.pxsl",
        0,
        Encoding.UTF8.GetBytes(text));
}
