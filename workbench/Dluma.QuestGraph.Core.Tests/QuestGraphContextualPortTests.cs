using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphContextualPortTests
{
    [Fact]
    public void CatalogBackedNodesExposeOnlyAuthoredTopologyPorts()
    {
        var catalog = Catalog();
        var document = PxslParser.Parse(Source(
            "quest Example\n" +
            "    ,BLOCK block\n" +
            "        path\n" +
            "            ,enable-multi enabled\n" +
            "                ,o SyntheticClass SyntheticObject\n" +
            "    ,interactive-movie scene\n" +
            "        pre\n" +
            "            ,enable-multi prepare\n" +
            "        post\n" +
            "            ,enable-multi cleanup\n" +
            "    ,semaphore never\n"), catalog);
        var block = Macro(document, "BLOCK", "block");
        var enabled = Macro(document, "enable-multi", "enabled");
        var objectReference = Macro(document, "o", "SyntheticObject");
        var movie = Macro(document, "interactive-movie", "scene");
        var semaphore = Macro(document, "semaphore", "never");
        var graph = document.GraphNodes.ToDictionary(node => node.SemanticNodeId);

        // ,BLOCK owns both an explicit path branch and the ordered body that PXSL lets
        // that path leave implicit, so nested macros have a port to hang from.
        Assert.Equal(new[] { "body", "branch:path", "in", "out" }, PortIds(graph[block.Id]));
        Assert.Equal(new[] { "child:o", "in", "out" }, PortIds(graph[enabled.Id]));
        Assert.Equal(new[] { "in" }, PortIds(graph[objectReference.Id]));
        // ,interactive-movie declares ,BODY alongside its pre/post branches.
        Assert.Equal(
            new[] { "body", "branch:post", "branch:pre", "in", "out" },
            PortIds(graph[movie.Id]));
        Assert.Equal(new[] { "in", "out" }, PortIds(graph[semaphore.Id]));
        Assert.All(new[] { block, enabled, objectReference, movie, semaphore }, semantic =>
            Assert.False(graph[semantic.Id].IsDetached));

        Assert.Contains(document.GraphEdges, edge =>
            edge.SourceNodeId == graph[block.Id].Id &&
            edge.SourcePortId == "branch:path" &&
            edge.Kind == QuestGraphEdgeKind.Branch);
        Assert.Contains(document.GraphEdges, edge =>
            edge.SourceNodeId == graph[enabled.Id].Id &&
            edge.SourcePortId == "child:o" &&
            edge.TargetNodeId == graph[objectReference.Id].Id &&
            edge.Kind == QuestGraphEdgeKind.Nested);
        Assert.Contains(document.GraphEdges, edge =>
            edge.SourceNodeId == graph[movie.Id].Id &&
            edge.SourcePortId == "branch:pre" &&
            edge.Kind == QuestGraphEdgeKind.Branch);
        Assert.Contains(document.GraphEdges, edge =>
            edge.SourceNodeId == graph[movie.Id].Id &&
            edge.SourcePortId == "branch:post" &&
            edge.Kind == QuestGraphEdgeKind.Branch);
    }

    [Fact]
    public void RootStagedMacroIsMarkedDetached()
    {
        var document = PxslParser.Parse(Source(",enable-multi staged\n"), Catalog());
        var staged = Macro(document, "enable-multi", "staged");
        var graph = Assert.Single(document.GraphNodes, node => node.SemanticNodeId == staged.Id);

        Assert.True(graph.IsDetached);
    }

    [Fact]
    public void DeclaredChildSlotRejectsAnIncompatibleMacro()
    {
        var document = PxslParser.Parse(Source(
            "quest Example\n" +
            "    ,enable-multi enabled\n" +
            "    ,semaphore wrongChild\n"), Catalog());
        var owner = Macro(document, "enable-multi", "enabled");
        var wrongChild = Macro(document, "semaphore", "wrongChild");

        var rejection = Assert.Throws<QuestGraphOperationException>(() =>
            QuestGraphMutationService.Connect(document, owner.Id, "child:o", wrongChild.Id));

        Assert.Equal(QuestGraphOperationRejection.IncompatiblePort, rejection.Reason);
    }

    private static string[] PortIds(QuestGraphNode node) => node.Ports
        .Select(port => port.Id)
        .Order(StringComparer.Ordinal)
        .ToArray();

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

    private static QuestCatalog Catalog()
    {
        const string edf = """
            ### Forbidden ###
            ,qo class object =
              QuestObject -class=<(,class)> -name=<(,object)>
            ### SubPhase ###
            ,o class object =
              Object
                ,qo <(,class)> <(,object)>
            ### SimplePhase ###
            ,enable-multi name =
              Phase -type=enable -name=<(,name)>
                ,BODY
            ,semaphore name =
              Phase -type=semaphore -name=<(,name)>
            ,interactive-movie name =
              Phase -type=<<interactive movie>> -name=<(,name)>
                ,BODY
            ### Compound ###
            ,BLOCK name =
              Phase -type=AND -name=<(,name)>
                path
                  ,BODY
            """;
        const string config = """
            AutoExpand()
                Block(",interactive-movie")
                    Expand("pre")
                    Expand("post")
                Block(",BLOCK")
                    Expand("path")
            Descriptions()
                Desc(",enable-multi", ",enable-multi [phase_name]", "    ,o [objects_class] [object_name]", "")
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="AND"><path name="Path" quantity="+"/></PhaseType>
              <PhaseType name="enable"><QuestObject name="Object" quantity="+"/></PhaseType>
              <PhaseType name="semaphore"/>
              <PhaseType name="interactive movie">
                <path name="PreMovie" quantity="?"/>
                <path name="PostMovie" quantity="?"/>
              </PhaseType>
            </PhaseTypeDefinitions>
            """;
        var definitions = new[]
        {
            ("macros.edf", QuestCatalogFormat.Edf, edf),
            ("QuestEditorConfig.scr", QuestCatalogFormat.QuestEditorConfig, config),
            ("phases.xml", QuestCatalogFormat.PhaseDefinitionsXml, phases)
        };
        var streams = definitions.Select(definition => new MemoryStream(
            Encoding.UTF8.GetBytes(definition.Item3), writable: false)).ToArray();
        try
        {
            return QuestCatalogExtractor.Extract(definitions.Select((definition, index) =>
                new QuestCatalogInput(definition.Item1, definition.Item2, streams[index])));
        }
        finally
        {
            foreach (var stream in streams)
                stream.Dispose();
        }
    }
}
