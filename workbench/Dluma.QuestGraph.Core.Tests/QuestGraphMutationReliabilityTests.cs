using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphMutationReliabilityTests
{
    [Fact]
    public void DeleteManyCoalescesAncestorAndDescendantIntoOneAtomicEdit()
    {
        var document = PxslParser.Parse(Source(
            "quest Example\n" +
            "    ,owner first\n" +
            "        ,record nested\n" +
            "    ,owner second\n"));
        var first = Macro(document, "owner", "first");
        var nested = Macro(document, "record", "nested");

        var mutation = QuestGraphMutationService.DeleteMany(
            document,
            new[] { first.Id, nested.Id, first.Id });
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;

        Assert.Single(mutation.Edits);
        Assert.Equal(document.Source.Revision, mutation.Edits[0].ExpectedRevision);
        Assert.Equal(document.Source.Sha256, mutation.Edits[0].ExpectedSha256);
        Assert.Equal(
            "quest Example\n    ,owner second\n",
            Encoding.UTF8.GetString(result.Bytes.Span));
    }

    [Fact]
    public void DeleteManyUsesDescendingDisjointEditsWithoutPartialApplication()
    {
        var document = PxslParser.Parse(Source(
            "quest Example\n" +
            "    ,step first\n" +
            "    ,step keep\n" +
            "    ,step last\n"));
        var first = Macro(document, "step", "first");
        var last = Macro(document, "step", "last");

        var mutation = QuestGraphMutationService.DeleteMany(document, new[] { first.Id, last.Id });
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;

        Assert.Equal(2, mutation.Edits.Count);
        Assert.True(mutation.Edits[0].ByteOffset > mutation.Edits[1].ByteOffset);
        Assert.All(mutation.Edits, edit =>
        {
            Assert.Equal(document.Source.Revision, edit.ExpectedRevision);
            Assert.Equal(document.Source.Sha256, edit.ExpectedSha256);
        });
        Assert.Equal(
            "quest Example\n    ,step keep\n",
            Encoding.UTF8.GetString(result.Bytes.Span));
    }

    [Fact]
    public void OpaqueTargetCanDisconnectByExactSpanAndStageAtRoot()
    {
        var catalog = Catalog("known");
        var document = PxslParser.Parse(Source(
            "quest Example\n" +
            "    ,known owner\n" +
            "        ,unknown exact\n" +
            "    ,known final\n"), catalog);
        var opaque = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Opaque && node.Name == "unknown");
        var opaqueGraph = Assert.Single(document.GraphNodes,
            node => node.SemanticNodeId == opaque.Id);
        var edge = Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == opaqueGraph.Id);

        var mutation = QuestGraphMutationService.Disconnect(document, edge.Id);
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;
        var reparsed = PxslParser.Parse(result, catalog);
        var detached = Assert.Single(reparsed.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Opaque && node.Name == "unknown");
        var detachedGraph = Assert.Single(reparsed.GraphNodes,
            node => node.SemanticNodeId == detached.Id);

        Assert.Equal(2, mutation.Edits.Count);
        Assert.True(mutation.Edits[0].ByteOffset > mutation.Edits[1].ByteOffset);
        Assert.Null(detached.ParentId);
        Assert.True(detachedGraph.IsDetached);
        Assert.EndsWith(",unknown exact\n", Encoding.UTF8.GetString(result.Bytes.Span), StringComparison.Ordinal);
    }

    [Fact]
    public void ContextualPathConnectMovesAStagedNodeIntoDeclaredBranch()
    {
        var catalog = Catalog("BLOCK", "step", compound: "BLOCK");
        var document = PxslParser.Parse(Source(
            "quest Example\n" +
            "    ,BLOCK sequence\n" +
            ",step staged\n"), catalog);
        var block = Macro(document, "BLOCK", "sequence");
        var staged = Macro(document, "step", "staged");

        var mutation = QuestGraphMutationService.Connect(document, block.Id, "branch:path", staged.Id);
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;
        var reparsed = PxslParser.Parse(result, catalog);
        var blockAfter = Macro(reparsed, "BLOCK", "sequence");
        var stagedAfter = Macro(reparsed, "step", "staged");
        var graph = reparsed.GraphNodes.ToDictionary(node => node.SemanticNodeId);

        var pathAfter = Assert.Single(reparsed.SemanticNodes,
            node => node.Kind == QuestSemanticKind.OrderedFlow && node.Name == "path");
        Assert.Equal(pathAfter.Id, stagedAfter.ParentId);
        Assert.Contains(reparsed.GraphEdges, edge =>
            edge.SourceNodeId == graph[blockAfter.Id].Id &&
            edge.SourcePortId == "branch:path");
        Assert.Contains("    ,BLOCK sequence\n        path\n            ,step staged\n",
            Encoding.UTF8.GetString(result.Bytes.Span), StringComparison.Ordinal);
    }

    private static QuestCatalog Catalog(
        string first,
        string? second = null,
        string? third = null,
        string? compound = null)
    {
        var names = new[] { first, second, third }.Where(name => name is not null).Select(name => name!);
        var text = new StringBuilder();
        foreach (var name in names)
        {
            text.AppendLine(name.Equals(compound, StringComparison.OrdinalIgnoreCase)
                ? "### Compound ###"
                : "### SimplePhase ###");
            text.Append(',').Append(name).AppendLine(" value =");
            if (name.Equals(compound, StringComparison.OrdinalIgnoreCase))
            {
                text.AppendLine("  Phase -type=AND -name=<(,value)>");
                text.AppendLine("    path");
                text.AppendLine("      ,BODY");
            }
            else
            {
                text.Append("  Phase -type=").Append(name).AppendLine(" -name=<(,value)>");
            }
        }
        using var stream = new MemoryStream(Encoding.UTF8.GetBytes(text.ToString()), writable: false);
        return QuestCatalogExtractor.Extract(
            new[] { new QuestCatalogInput("synthetic.edf", QuestCatalogFormat.Edf, stream) });
    }

    private static QuestSemanticNode Macro(
        QuestGraphDocument document,
        string name,
        string argument) => Assert.Single(document.SemanticNodes,
        node => node.Kind == QuestSemanticKind.Macro &&
            node.Name == name &&
            node.Arguments.Contains(argument));

    private static QuestSourceSnapshot Source(string text) => new(
        "synthetic-document",
        "quests/synthetic.pxsl",
        0,
        Encoding.UTF8.GetBytes(text));
}
