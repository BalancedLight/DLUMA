using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphProjectionRegressionTests
{
    [Fact]
    public void BlankAndCommentLinesDoNotBreakTheSemanticContainer()
    {
        const string text = "quest Example\n" +
            "    ,BLOCK _\n" +
            "        path\n" +
            "            ,step first\n" +
            "\n" +
            "            -- visual separation only\n" +
            "            ,step second\n";

        var document = PxslParser.Parse(Source(text));
        var steps = document.SemanticNodes.Where(node => node.Name == "step").ToArray();
        var byId = document.SemanticNodes.ToDictionary(node => node.Id);

        Assert.Equal(2, steps.Length);
        Assert.All(steps, step => Assert.Equal("path", byId[step.ParentId!].Name));
        Assert.Equal(new[] { 0, 1 }, steps.Select(step => step.Order).ToArray());
    }

    [Fact]
    public void PathUsesItsNamedGraphPort()
    {
        const string text = "quest Example\n" +
            "    ,BLOCK _\n" +
            "        path\n" +
            "            ,step value\n";

        var document = PxslParser.Parse(Source(text));

        Assert.Contains(document.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Branch &&
            edge.SourcePortId == "branch:path");
    }

    [Fact]
    public void ContainerFingerprintIncludesItsFullAncestry()
    {
        const string text = "quest First\n" +
            "    ,BLOCK _\n" +
            "        path\n" +
            "            ,step value\n" +
            "quest Second\n" +
            "    ,BLOCK _\n" +
            "        path\n" +
            "            ,step value\n";

        var document = PxslParser.Parse(Source(text));
        var steps = document.SemanticNodes.Where(node => node.Name == "step").ToArray();

        Assert.Equal(2, steps.Length);
        Assert.Equal(steps[0].SemanticFingerprint, steps[1].SemanticFingerprint);
        Assert.NotEqual(steps[0].ContainerFingerprint, steps[1].ContainerFingerprint);
    }

    private static QuestSourceSnapshot Source(string text) =>
        new("projection-regression", "quests/synthetic.pxsl", 0, Encoding.UTF8.GetBytes(text));
}
