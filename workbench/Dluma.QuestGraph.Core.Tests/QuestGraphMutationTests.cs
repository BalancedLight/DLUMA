using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphMutationTests
{
    [Fact]
    public void MoveSiblingDownChangesSourceAndOrderedEndpoints()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,step first\n" +
            "    ,step second\n");
        var first = Macro(document, "step", "first");

        var mutation = QuestGraphMutationService.MoveSibling(
            document,
            first.Id,
            QuestGraphMoveDirection.Down);
        var reparsed = ApplyAndParse(document, mutation);
        var steps = reparsed.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro && node.Name == "step")
            .OrderBy(node => node.Order)
            .ToArray();

        Assert.Equal(new[] { "second", "first" },
            steps.Select(node => Assert.Single(node.Arguments)).ToArray());
        AssertOrderedEdge(reparsed, steps[0], steps[1]);
    }

    [Fact]
    public void CopyPasteChildPreservesSubtreeAndReindentsIt()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner first\n" +
            "        ,record copied\n" +
            "    ,owner second\n");
        var copied = Macro(document, "record", "copied");
        var target = Macro(document, "owner", "second");

        var fragment = QuestGraphMutationService.CopySubtree(document, copied.Id);
        var mutation = QuestGraphMutationService.Paste(
            document,
            target.Id,
            fragment,
            QuestGraphPastePlacement.Child);
        var reparsed = ApplyAndParse(document, mutation);
        var targetAfter = Macro(reparsed, "owner", "second");
        var records = reparsed.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro && node.Name == "record")
            .ToArray();

        Assert.Equal(2, records.Length);
        Assert.Single(records, node => node.ParentId == targetAfter.Id);
        Assert.Contains("        ,record copied\n", SourceText(reparsed), StringComparison.Ordinal);
    }

    [Fact]
    public void ConnectOutMovesTargetImmediatelyAfterSource()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,step first\n" +
            "    ,step second\n" +
            "    ,step third\n");
        var first = Macro(document, "step", "first");
        var third = Macro(document, "step", "third");

        var mutation = QuestGraphMutationService.Connect(document, first.Id, "out", third.Id);
        var reparsed = ApplyAndParse(document, mutation);
        var steps = reparsed.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro && node.Name == "step")
            .OrderBy(node => node.Order)
            .ToArray();

        Assert.Equal(new[] { "first", "third", "second" },
            steps.Select(node => Assert.Single(node.Arguments)).ToArray());
        AssertOrderedEdge(reparsed, steps[0], steps[1]);
    }

    [Fact]
    public void ConnectChildNestsAnAdjacentSiblingWithoutOverlappingEdits()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner value\n" +
            "        ,record existing\n" +
            "    ,record child\n" +
            "    ,step final\n");
        var owner = Macro(document, "owner", "value");
        var record = Macro(document, "record", "child");

        var mutation = QuestGraphMutationService.Connect(document, owner.Id, "child", record.Id);
        var reparsed = ApplyAndParse(document, mutation);
        var ownerAfter = Macro(reparsed, "owner", "value");
        var recordAfter = Macro(reparsed, "record", "child");

        Assert.Equal(ownerAfter.Id, recordAfter.ParentId);
        Assert.Contains("        ,record child\n", SourceText(reparsed), StringComparison.Ordinal);
    }

    [Fact]
    public void ConnectTypedBranchWrapsAnAdjacentExecutableNode()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner value\n" +
            "        fallback\n" +
            "            ,step existing\n" +
            "    ,step fallbackTarget\n" +
            "    ,step final\n");
        var owner = Macro(document, "owner", "value");
        var target = Macro(document, "step", "fallbackTarget");

        var mutation = QuestGraphMutationService.Connect(
            document,
            owner.Id,
            "branch:fallback",
            target.Id);
        var reparsed = ApplyAndParse(document, mutation);
        var ownerAfter = Macro(reparsed, "owner", "value");
        var targetAfter = Macro(reparsed, "step", "fallbackTarget");
        var branch = Assert.Single(reparsed.SemanticNodes,
            node => node.Id == targetAfter.ParentId &&
                node.Kind == QuestSemanticKind.Branch &&
                node.Name == "fallback");

        Assert.Equal(ownerAfter.Id, branch.ParentId);
        Assert.Equal(branch.Id, targetAfter.ParentId);
        Assert.Contains("        fallback\n            ,step fallbackTarget\n",
            SourceText(reparsed), StringComparison.Ordinal);
    }

    [Fact]
    public void ConnectRejectsContainmentCycle()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner value\n" +
            "        ,record child\n");
        var owner = Macro(document, "owner", "value");
        var record = Macro(document, "record", "child");

        var exception = Assert.Throws<QuestGraphOperationException>(() =>
            QuestGraphMutationService.Connect(document, record.Id, "child", owner.Id));

        Assert.Equal(QuestGraphOperationRejection.Cycle, exception.Reason);
    }

    [Fact]
    public void DisconnectNestedEdgeStagesTargetAtRoot()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner value\n" +
            "        ,record child\n" +
            "    ,step final\n");
        var owner = Macro(document, "owner", "value");
        var record = Macro(document, "record", "child");
        var edge = EdgeTo(document, record, QuestGraphEdgeKind.Nested);

        var mutation = QuestGraphMutationService.Disconnect(document, edge.Id);
        var reparsed = ApplyAndParse(document, mutation);
        var recordAfter = Macro(reparsed, "record", "child");

        Assert.Null(recordAfter.ParentId);
        Assert.DoesNotContain(reparsed.GraphEdges,
            candidate => candidate.TargetNodeId == GraphNode(reparsed, recordAfter).Id);
        Assert.EndsWith(",record child\n", SourceText(reparsed), StringComparison.Ordinal);
    }

    [Fact]
    public void DisconnectOrderedEdgeStagesTargetAndReconnectRestoresFlow()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,step first\n" +
            "    ,step second\n");
        var second = Macro(document, "step", "second");
        var edge = EdgeTo(document, second, QuestGraphEdgeKind.Ordered);

        var disconnected = ApplyAndParse(
            document,
            QuestGraphMutationService.Disconnect(document, edge.Id));
        var detached = Macro(disconnected, "step", "second");

        Assert.Null(detached.ParentId);
        Assert.DoesNotContain(disconnected.GraphEdges,
            candidate => candidate.TargetNodeId == GraphNode(disconnected, detached).Id);

        var first = Macro(disconnected, "step", "first");
        var reconnected = ApplyAndParse(
            disconnected,
            QuestGraphMutationService.Connect(disconnected, first.Id, "out", detached.Id));
        var steps = reconnected.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro && node.Name == "step")
            .OrderBy(node => node.Order)
            .ToArray();

        Assert.Equal(new[] { "first", "second" },
            steps.Select(node => Assert.Single(node.Arguments)).ToArray());
        AssertOrderedEdge(reconnected, steps[0], steps[1]);
    }

    [Fact]
    public void StructuralMutationRejectsOpaqueNode()
    {
        var document = PxslParser.Parse(
            Source(
                "quest Example\n" +
                "    ,known value\n" +
                "    ,mystery preserved\n"),
            Catalog("known"));
        var opaque = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Opaque && node.Name == "mystery");

        var exception = Assert.Throws<QuestGraphOperationException>(() =>
            QuestGraphMutationService.CopySubtree(document, opaque.Id));

        Assert.Equal(QuestGraphOperationRejection.OpaqueNode, exception.Reason);
    }

    [Fact]
    public void Utf8BomSurvivesOrderedMove()
    {
        const string text = "quest Example\r\n    ,step first\r\n    ,step second\r\n";
        var source = Source(text, includeBom: true);
        var document = PxslParser.Parse(source);
        var first = Macro(document, "step", "first");

        var mutation = QuestGraphMutationService.MoveSibling(
            document,
            first.Id,
            QuestGraphMoveDirection.Down);
        var result = QuestSourceEditService.Apply(source, mutation.Edits).Source;

        Assert.True(result.HasBom);
        Assert.True(result.Bytes.Span.StartsWith(Encoding.UTF8.GetPreamble()));
        Assert.Equal("quest Example\r\n    ,step second\r\n    ,step first\r\n",
            Encoding.UTF8.GetString(result.Bytes.Span[Encoding.UTF8.GetPreamble().Length..]));
    }

    private static QuestGraphDocument Parse(string text) => PxslParser.Parse(Source(text));

    private static QuestGraphDocument ApplyAndParse(
        QuestGraphDocument document,
        QuestGraphMutation mutation) =>
        PxslParser.Parse(QuestSourceEditService.Apply(document.Source, mutation.Edits).Source);

    private static QuestSourceSnapshot Source(string text, bool includeBom = false)
    {
        var body = Encoding.UTF8.GetBytes(text);
        var bytes = includeBom ? Encoding.UTF8.GetPreamble().Concat(body).ToArray() : body;
        return new QuestSourceSnapshot("synthetic-document", "quests/synthetic.pxsl", 0, bytes);
    }

    private static QuestCatalog Catalog(params string[] macroNames)
    {
        var definitions = string.Join('\n', macroNames.Select(name => $",{name} value ="));
        using var stream = new MemoryStream(Encoding.UTF8.GetBytes(definitions), writable: false);
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

    private static QuestGraphNode GraphNode(
        QuestGraphDocument document,
        QuestSemanticNode semantic) => Assert.Single(document.GraphNodes,
        node => node.SemanticNodeId == semantic.Id);

    private static QuestGraphEdge EdgeTo(
        QuestGraphDocument document,
        QuestSemanticNode target,
        QuestGraphEdgeKind kind)
    {
        var targetGraph = GraphNode(document, target);
        return Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == targetGraph.Id && edge.Kind == kind);
    }

    private static void AssertOrderedEdge(
        QuestGraphDocument document,
        QuestSemanticNode source,
        QuestSemanticNode target)
    {
        var sourceGraph = GraphNode(document, source);
        var targetGraph = GraphNode(document, target);
        Assert.Single(document.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Ordered &&
            edge.SourceNodeId == sourceGraph.Id &&
            edge.TargetNodeId == targetGraph.Id);
    }

    private static string SourceText(QuestGraphDocument document) =>
        Encoding.UTF8.GetString(document.Source.Bytes.Span);
}
