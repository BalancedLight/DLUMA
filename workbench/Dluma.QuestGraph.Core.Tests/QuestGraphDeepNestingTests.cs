using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphDeepNestingTests
{
    [Fact]
    public void DeeplyIndentedSourceParsesProjectsAndWalksSubtreesWithoutCallStackRecursion()
    {
        const int depth = 3_500;
        var text = new StringBuilder();
        for (var index = 0; index < depth; ++index)
        {
            text.Append('\t', index);
            text.Append(",node ");
            text.Append(index);
            text.Append('\n');
        }
        var bytes = Encoding.UTF8.GetBytes(text.ToString());
        Assert.True(bytes.Length < 8 * 1024 * 1024);
        var source = new QuestSourceSnapshot(
            "synthetic-deep-document",
            "quests/synthetic-deep.pxsl",
            0,
            bytes);

        var document = PxslParser.Parse(source);

        Assert.Equal(depth, document.SemanticNodes.Count);
        Assert.Equal(depth, document.GraphNodes.Count);
        Assert.Equal(depth - 1, document.GraphEdges.Count);
        Assert.Equal(bytes, document.Syntax.ToBytes());
        var byId = document.SemanticNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        var deepest = document.SemanticNodes[^1];
        var ancestryLength = 1;
        while (deepest.ParentId is { } parentId)
        {
            deepest = byId[parentId];
            ancestryLength++;
        }
        Assert.Equal(depth, ancestryLength);

        var root = document.SemanticNodes[0];
        var range = QuestSourceBlockService.GetSubtreeRange(document, root);
        Assert.Equal(0, range.Start);
        Assert.Equal(bytes.Length, range.End);
        var copied = QuestGraphMutationService.CopySubtree(document, root.Id);
        Assert.Equal(text.ToString(), copied.Text);

        var layout = QuestGraphSidecarService.CreateNodeLayout(
            document.SemanticNodes[^1],
            "deepest-node",
            10,
            20);
        var sidecar = QuestGraphSidecarService.Create(document, new[] { layout });
        var reconciliation = QuestGraphSidecarService.Reconcile(sidecar, document);
        Assert.Single(reconciliation.Matches);
        Assert.Empty(reconciliation.Orphans);
    }
}
