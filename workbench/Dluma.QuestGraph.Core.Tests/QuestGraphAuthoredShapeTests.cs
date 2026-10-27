using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

/// <summary>
/// Locks in the projection of PXSL shaped the way quests are actually authored:
/// CRLF endings, blank lines, trailing whitespace, &lt;&lt; label &gt;&gt; comments, and
/// -- crucially -- ,BLOCK containers holding ordinary macros directly, with the
/// path branch left implicit. Every earlier projection test either supplied no
/// catalog or only ever nested a path under ,BLOCK, which is why a catalog-backed
/// container silently dropped every one of its children.
/// </summary>
public sealed class QuestGraphAuthoredShapeTests
{
    private const string AuthoredQuest =
        "<<<?xml version=\"1.0\"?>>>\r\n" +
        "\r\n" +
        "QuestsDefinitions -level=synthetic\r\n" +
        "    quest Example -final=true\r\n" +
        "        ,BLOCK << QUEST START >>    \r\n" +
        "            -- setup\r\n" +
        "            ,enable-multi _\r\n" +
        "                ,o SyntheticClass SyntheticObject\r\n" +
        "            ,semaphore first\r\n" +
        "\r\n" +
        "            ,semaphore second\r\n" +
        "        ,BLOCK << INVESTIGATE >>\r\n" +
        "            ,semaphore third\r\n" +
        "            ,semaphore fourth\r\n" +
        "        ,BLOCK << BRANCHING >>\r\n" +
        "            path\r\n" +
        "                ,semaphore fifth\r\n" +
        "            path\r\n" +
        "                ,semaphore sixth\r\n";

    [Fact]
    public void EveryProjectedEdgeNamesAPortItsSourceActuallyExposes()
    {
        var document = PxslParser.Parse(Source(AuthoredQuest), Catalog());
        var graphById = document.GraphNodes.ToDictionary(node => node.Id, StringComparer.Ordinal);

        Assert.NotEmpty(document.GraphEdges);
        foreach (var edge in document.GraphEdges)
        {
            var source = Assert.Contains(edge.SourceNodeId, graphById);
            var target = Assert.Contains(edge.TargetNodeId, graphById);
            Assert.Contains(source.Ports, port =>
                port.Id == edge.SourcePortId &&
                port.Direction == QuestGraphPortDirection.Output);
            Assert.Contains(target.Ports, port =>
                port.Id == edge.TargetPortId &&
                port.Direction == QuestGraphPortDirection.Input);
        }
        Assert.DoesNotContain(document.Diagnostics, diagnostic =>
            diagnostic.Code is "PXSL101" or "PXSL102");
    }

    [Fact]
    public void EveryNodeExceptTheRootsIsReachableFromItsContainer()
    {
        var document = PxslParser.Parse(Source(AuthoredQuest), Catalog());
        var incoming = document.GraphEdges
            .GroupBy(edge => edge.TargetNodeId, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.Count(), StringComparer.Ordinal);

        // The raw XML preamble and the quest are genuine roots; nothing else may float.
        var roots = document.GraphNodes
            .Where(node => !incoming.ContainsKey(node.Id))
            .Select(node => node.Kind.ToString())
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(new[] { "Quest", "RawXml" }, roots);
        Assert.All(incoming.Values, count => Assert.Equal(1, count));
    }

    [Fact]
    public void BlockOwnsItsDirectMacrosAsAnOrderedBody()
    {
        var document = PxslParser.Parse(Source(AuthoredQuest), Catalog());
        var block = Macro(document, "BLOCK", "<< INVESTIGATE >>");
        var third = Macro(document, "semaphore", "third");
        var fourth = Macro(document, "semaphore", "fourth");
        var graph = document.GraphNodes.ToDictionary(node => node.SemanticNodeId, StringComparer.Ordinal);

        var firstStep = Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == graph[third.Id].Id);
        Assert.Equal(QuestGraphEdgeKind.Ordered, firstStep.Kind);
        Assert.Equal(graph[block.Id].Id, firstStep.SourceNodeId);
        Assert.Equal("body", firstStep.SourcePortId);

        var secondStep = Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == graph[fourth.Id].Id);
        Assert.Equal(QuestGraphEdgeKind.Ordered, secondStep.Kind);
        Assert.Equal(graph[third.Id].Id, secondStep.SourceNodeId);
        Assert.Equal("out", secondStep.SourcePortId);
    }

    [Fact]
    public void ADeclaredChildSlotStillWinsOverTheOrderedBody()
    {
        var document = PxslParser.Parse(Source(AuthoredQuest), Catalog());
        var owner = Macro(document, "enable-multi", "_");
        var record = Macro(document, "o", "SyntheticObject");
        var graph = document.GraphNodes.ToDictionary(node => node.SemanticNodeId, StringComparer.Ordinal);

        var nested = Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == graph[record.Id].Id);

        Assert.Equal(QuestGraphEdgeKind.Nested, nested.Kind);
        Assert.Equal(graph[owner.Id].Id, nested.SourceNodeId);
        Assert.Equal("child:o", nested.SourcePortId);
    }

    [Fact]
    public void AContainersFirstChildNeverChainsFromTheContainersPreviousSibling()
    {
        var document = PxslParser.Parse(Source(AuthoredQuest), Catalog());
        var graph = document.GraphNodes.ToDictionary(node => node.SemanticNodeId, StringComparer.Ordinal);
        var firstBlock = graph[Macro(document, "BLOCK", "<< QUEST START >>").Id].Id;
        var secondBlock = graph[Macro(document, "BLOCK", "<< INVESTIGATE >>").Id].Id;
        var third = graph[Macro(document, "semaphore", "third").Id].Id;
        var fifth = graph[Macro(document, "semaphore", "fifth").Id].Id;

        // The blocks themselves chain to each other...
        Assert.Contains(document.GraphEdges, edge =>
            edge.SourceNodeId == firstBlock && edge.TargetNodeId == secondBlock);

        // ...but their contents belong to their own container, not to the block before it.
        Assert.DoesNotContain(document.GraphEdges, edge =>
            edge.SourceNodeId == firstBlock && edge.TargetNodeId == third);
        Assert.DoesNotContain(document.GraphEdges, edge =>
            edge.SourceNodeId == secondBlock && edge.TargetNodeId == fifth);
    }

    [Fact]
    public void AnExplicitPathBranchStillProjectsAsABranch()
    {
        var document = PxslParser.Parse(Source(AuthoredQuest), Catalog());
        var block = Macro(document, "BLOCK", "<< BRANCHING >>");
        var graph = document.GraphNodes.ToDictionary(node => node.SemanticNodeId, StringComparer.Ordinal);

        var branches = document.GraphEdges
            .Where(edge => edge.SourceNodeId == graph[block.Id].Id)
            .ToArray();

        Assert.Equal(2, branches.Length);
        Assert.All(branches, edge =>
        {
            Assert.Equal(QuestGraphEdgeKind.Branch, edge.Kind);
            Assert.Equal("branch:path", edge.SourcePortId);
        });
    }

    [Fact]
    public void ProjectionLeavesTheSourceBytesUntouched()
    {
        var source = Source(AuthoredQuest);
        var before = source.ToArray();

        var document = PxslParser.Parse(source, Catalog());

        Assert.Equal(before, document.Source.ToArray());
        Assert.Equal(AuthoredQuest, Encoding.UTF8.GetString(document.Source.ToArray()));
    }

    private static QuestSemanticNode Macro(
        QuestGraphDocument document,
        string name,
        string argument) => Assert.Single(document.SemanticNodes,
        node => node.Kind == QuestSemanticKind.Macro &&
            node.Name == name &&
            node.Arguments.Contains(argument));

    private static QuestSourceSnapshot Source(string text) => new(
        "authored-document",
        "quests/authored.pxsl",
        0,
        Encoding.UTF8.GetBytes(text));

    private static QuestCatalog Catalog()
    {
        const string edf = """
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
            Descriptions()
                Desc(",enable-multi", ",enable-multi [phase_name]", "    ,o [objects_class] [object_name]", "")
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="AND"><path name="Path" quantity="+"/></PhaseType>
              <PhaseType name="enable"><QuestObject name="Object" quantity="+"/></PhaseType>
              <PhaseType name="semaphore"/>
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
