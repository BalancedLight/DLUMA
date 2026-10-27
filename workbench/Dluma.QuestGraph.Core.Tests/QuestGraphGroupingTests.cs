using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphGroupingTests
{
    [Fact]
    public async Task LayoutGroupRoundTripsWithoutChangingSourceTopology()
    {
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            System.Text.Encoding.UTF8.GetBytes(
                "quest Example\n" +
                "    ,step first\n" +
                "    ,step second\n"));
        var document = PxslParser.Parse(source);
        var steps = document.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro)
            .OrderBy(node => node.Order)
            .ToArray();
        Assert.Equal(2, steps.Length);
        var firstLayout = QuestGraphSidecarService.CreateNodeLayout(
            steps[0], "stable-first", 10, 20, groupId: "group-flow");
        var secondLayout = QuestGraphSidecarService.CreateNodeLayout(
            steps[1], "stable-second", 300, 20, groupId: "group-flow");
        var group = new QuestGraphGroupLayout(
            "group-flow",
            "Flow",
            0,
            0,
            600,
            240,
            new[] { "stable-first", "stable-second" });
        var sidecar = QuestGraphSidecarService.Create(
            document,
            new[] { firstLayout, secondLayout },
            groups: new[] { group });
        await using var stream = new MemoryStream();

        await QuestGraphSidecarService.WriteAsync(stream, sidecar);
        stream.Position = 0;
        var roundTrip = await QuestGraphSidecarService.ReadAsync(stream);

        var roundTripGroup = Assert.Single(roundTrip.Groups);
        Assert.Equal("group-flow", roundTripGroup.Id);
        Assert.Equal(new[] { "stable-first", "stable-second" }, roundTripGroup.MemberStableIds);
        Assert.All(roundTrip.Nodes, node => Assert.Equal("group-flow", node.GroupId));
        Assert.Equal(source.ToArray(), document.Syntax.ToBytes());
        Assert.Equal(2, document.GraphEdges.Count(edge => edge.Kind == QuestGraphEdgeKind.Ordered));
    }
}
