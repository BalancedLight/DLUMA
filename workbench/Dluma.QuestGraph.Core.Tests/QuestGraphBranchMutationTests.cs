using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphBranchMutationTests
{
    [Fact]
    public void TypedBranchCanBeAttachedToQuestContainer()
    {
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(
                "quest First\n" +
                "    ,BLOCK container\n" +
                "    ,step one\n" +
                "quest Second\n" +
                "    path\n" +
                "        ,step two\n"));
        var catalog = Catalog();
        var document = PxslParser.Parse(source, catalog);
        var block = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro && node.Name == "BLOCK");
        var path = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.OrderedFlow && node.Name == "path");

        var mutation = QuestGraphMutationService.Connect(
            document,
            block.Id,
            "branch:path",
            path.Id);
        var result = QuestSourceEditService.Apply(source, mutation.Edits).Source;
        var reparsed = PxslParser.Parse(result, catalog);
        var blockAfter = Assert.Single(reparsed.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro && node.Name == "BLOCK");
        var pathAfter = Assert.Single(reparsed.SemanticNodes,
            node => node.Kind == QuestSemanticKind.OrderedFlow && node.Name == "path");
        var graphBySemantic = reparsed.GraphNodes.ToDictionary(node => node.SemanticNodeId);

        Assert.Equal(blockAfter.Id, pathAfter.ParentId);
        Assert.Single(reparsed.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Branch &&
            edge.SourceNodeId == graphBySemantic[blockAfter.Id].Id &&
            edge.TargetNodeId == graphBySemantic[pathAfter.Id].Id &&
            edge.SourcePortId == "branch:path");
        Assert.Contains(
            "quest First\n    ,BLOCK container\n        path\n            ,step two\n    ,step one\nquest Second\n",
            Encoding.UTF8.GetString(result.Bytes.Span),
            StringComparison.Ordinal);
    }

    private static QuestCatalog Catalog()
    {
        const string edf = """
            ### Compound ###
            ,BLOCK name =
              Phase -type=AND -name=<(,name)>
                path
                  ,BODY
            """;
        const string config = """
            AutoExpand()
                Block(",BLOCK")
                    Expand("path")
            """;
        using var edfStream = new MemoryStream(Encoding.UTF8.GetBytes(edf), writable: false);
        using var configStream = new MemoryStream(Encoding.UTF8.GetBytes(config), writable: false);
        return QuestCatalogExtractor.Extract(new[]
        {
            new QuestCatalogInput("macros.edf", QuestCatalogFormat.Edf, edfStream),
            new QuestCatalogInput("QuestEditorConfig.scr", QuestCatalogFormat.QuestEditorConfig, configStream)
        });
    }
}
