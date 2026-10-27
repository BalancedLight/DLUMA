using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphCoreTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-quest-graph-tests-{Guid.NewGuid():N}");

    [Theory]
    [InlineData("\n", QuestLineEnding.Lf)]
    [InlineData("\r\n", QuestLineEnding.CrLf)]
    public void NoOpParsePreservesEverySourceByte(
        string newline,
        QuestLineEnding expectedLineEnding)
    {
        var text = string.Join(newline,
            "<<<root/>>>",
            "QuestsDefinitions -level=synthetic",
            "    quest Example -final=true",
            "        -- preserved comment",
            "        ,BLOCK _",
            "            path",
            "                ,step <<Visible text>> Target value",
            string.Empty);
        var source = Source(text);

        var document = PxslParser.Parse(source, Catalog("BLOCK", "step"));

        Assert.Equal(expectedLineEnding, source.LineEnding);
        Assert.Equal(source.ToArray(), document.Syntax.ToBytes());
        Assert.Equal(source.Sha256, document.Source.Sha256);
    }

    [Fact]
    public void ExcessBlankLinesCollapseIntoOneOpaqueTailWithoutByteLoss()
    {
        var source = Source(new string('\n', 100_001));

        var document = PxslParser.Parse(source);
        var syntaxNodes = FlattenSyntax(document.Syntax.Roots);

        Assert.Equal(100_000, syntaxNodes.Count);
        var tail = Assert.Single(syntaxNodes,
            node => node.Kind == PxslSyntaxKind.Opaque &&
                node.Name == "parser-limit-tail");
        Assert.True(tail.IsMalformed);
        Assert.True(tail.Span.Length > 0);
        Assert.Equal(source.Bytes.Length, tail.Span.End);
        var diagnostic = Assert.Single(document.Diagnostics,
            item => item.Code == "PXSL007");
        Assert.Equal(tail.Span, diagnostic.Span!.Value);
        Assert.Equal(source.ToArray(), document.Syntax.ToBytes());
    }

    [Fact]
    public void Utf8BomAndRawXmlArePreserved()
    {
        var body = Encoding.UTF8.GetBytes("<<<root\n  child/>>>\nquest Example\n");
        var bytes = Encoding.UTF8.GetPreamble().Concat(body).ToArray();
        var source = new QuestSourceSnapshot("doc", "quests/example.pxsl", 0, bytes);

        var document = PxslParser.Parse(source);

        Assert.True(source.HasBom);
        Assert.Equal(bytes, document.Syntax.ToBytes());
        Assert.Contains(document.SemanticNodes, node => node.Kind == QuestSemanticKind.RawXml);
    }

    [Fact]
    public void UnknownAndMalformedRegionsRemainOpaque()
    {
        const string text = "QuestsDefinitions -level=synthetic\n" +
            "    quest Example\n" +
            "        ,known _\n" +
            "        ,mystery value\n" +
            " \t,known mixed\n" +
            "quest\n";

        var document = PxslParser.Parse(Source(text), Catalog("known"));

        Assert.Equal(3, document.SemanticNodes.Count(node => node.Kind == QuestSemanticKind.Opaque));
        Assert.Contains(document.Diagnostics, diagnostic => diagnostic.Code == "PXSL003");
        Assert.Contains(document.Diagnostics, diagnostic => diagnostic.Code == "PXSL004");
        Assert.Contains(document.Diagnostics, diagnostic => diagnostic.Code == "PXSL006");
        Assert.Equal(Encoding.UTF8.GetBytes(text), document.Syntax.ToBytes());
    }

    [Fact]
    public void LocalizedByteEditPreservesUntouchedPrefixAndSuffix()
    {
        const string text = "quest Example\n    ,step one\n    -- suffix stays exact\n";
        var source = Source(text, revision: 7);
        var oldValue = Encoding.UTF8.GetBytes("one");
        var replacement = Encoding.UTF8.GetBytes("replacement");
        var offset = IndexOf(source.Bytes.Span, oldValue);
        var edit = new QuestTextEdit(
            source.Revision,
            source.Sha256,
            offset,
            oldValue.Length,
            replacement);

        var result = QuestSourceEditService.Apply(source, new[] { edit });

        Assert.Equal(8, result.Source.Revision);
        Assert.Equal(
            "quest Example\n    ,step replacement\n    -- suffix stays exact\n",
            Encoding.UTF8.GetString(result.Source.Bytes.Span));
        Assert.Equal(source.Bytes.Span[..offset].ToArray(), result.Source.Bytes.Span[..offset].ToArray());
        var oldSuffix = source.Bytes.Span[(offset + oldValue.Length)..].ToArray();
        var newSuffix = result.Source.Bytes.Span[(offset + replacement.Length)..].ToArray();
        Assert.Equal(oldSuffix, newSuffix);
    }

    [Fact]
    public void LocalizedByteEditRejectsStaleRevisionAndHash()
    {
        var source = Source("quest Example\n", revision: 2);
        var staleRevision = new QuestTextEdit(1, source.Sha256, 0, 0, ReadOnlyMemory<byte>.Empty);
        var staleHash = new QuestTextEdit(2, new string('0', 64), 0, 0, ReadOnlyMemory<byte>.Empty);

        var revisionError = Assert.Throws<QuestEditRejectedException>(() =>
            QuestSourceEditService.Apply(source, new[] { staleRevision }));
        var hashError = Assert.Throws<QuestEditRejectedException>(() =>
            QuestSourceEditService.Apply(source, new[] { staleHash }));

        Assert.Equal(QuestEditRejection.StaleRevision, revisionError.Reason);
        Assert.Equal(QuestEditRejection.StaleHash, hashError.Reason);
    }

    [Fact]
    public void GraphProjectionPreservesOrderedSiblingsAndNamedBranches()
    {
        const string text = "QuestsDefinitions -level=synthetic\n" +
            "    quest Example\n" +
            "        ,BLOCK _\n" +
            "            path\n" +
            "                ,step first\n" +
            "                ,step second\n" +
            "            fallback\n" +
            "                ,step third\n";
        var document = PxslParser.Parse(Source(text), Catalog("BLOCK", "step"));
        var steps = document.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro && node.Name == "step")
            .ToArray();
        var byId = document.SemanticNodes.ToDictionary(node => node.Id);

        Assert.Equal(new[] { "first", "second", "third" },
            steps.Select(node => Assert.Single(node.Arguments)).ToArray());
        Assert.Equal(new[] { 0, 1, 0 }, steps.Select(node => node.Order).ToArray());
        Assert.Equal("path", byId[steps[0].ParentId!].Name);
        Assert.Equal("path", byId[steps[1].ParentId!].Name);
        Assert.Equal("fallback", byId[steps[2].ParentId!].Name);
        var graphBySemantic = document.GraphNodes.ToDictionary(
            node => node.SemanticNodeId);
        var path = byId[steps[0].ParentId!];
        var pathGraphId = graphBySemantic[path.Id].Id;
        var firstGraphId = graphBySemantic[steps[0].Id].Id;
        var secondGraphId = graphBySemantic[steps[1].Id].Id;
        Assert.Contains(document.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Ordered &&
            edge.SourceNodeId == pathGraphId &&
            edge.TargetNodeId == firstGraphId);
        Assert.Contains(document.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Ordered &&
            edge.SourceNodeId == firstGraphId &&
            edge.TargetNodeId == secondGraphId);
        Assert.DoesNotContain(document.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Ordered &&
            edge.SourceNodeId == pathGraphId &&
            edge.TargetNodeId == secondGraphId);
        Assert.Contains(document.GraphEdges, edge =>
            edge.Kind == QuestGraphEdgeKind.Branch && edge.SourcePortId == "branch:fallback");
    }

    [Fact]
    public void NestedRecordsUseChildPortWithoutEnteringTheOrderedChain()
    {
        const string text = "quest Example\n" +
            "    ,step first\n" +
            "        ,record child\n" +
            "    ,step second\n";
        var document = PxslParser.Parse(Source(text));
        var first = Assert.Single(document.SemanticNodes,
            node => node.Name == "step" && node.Arguments.Contains("first"));
        var second = Assert.Single(document.SemanticNodes,
            node => node.Name == "step" && node.Arguments.Contains("second"));
        var record = Assert.Single(document.SemanticNodes,
            node => node.Name == "record");
        var graph = document.GraphNodes.ToDictionary(node => node.SemanticNodeId);
        var firstGraph = graph[first.Id];
        var nested = Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == graph[record.Id].Id);
        var sequence = Assert.Single(document.GraphEdges,
            edge => edge.TargetNodeId == graph[second.Id].Id);

        Assert.Equal(QuestGraphEdgeKind.Nested, nested.Kind);
        Assert.Equal(firstGraph.Id, nested.SourceNodeId);
        Assert.Equal("child", nested.SourcePortId);
        Assert.Equal(QuestGraphEdgeKind.Ordered, sequence.Kind);
        Assert.Equal(firstGraph.Id, sequence.SourceNodeId);
        Assert.Equal("out", sequence.SourcePortId);
        Assert.Contains(firstGraph.Ports, port =>
            port.Id == "child" &&
            port.Type == "child" &&
            port.AllowsMultiple);
    }

    [Fact]
    public void CatalogExtractsGenericEdfConfigAndXmlInputs()
    {
        const string edf = ",step target amount =\n  Phase target amount\n";
        const string config = "Block(\",step\")\n{\n  Expand(\"fallback\")\n}\n";
        const string phases = """
            <?xml version="1.0"?>
            <PhaseTypeDefinitions>
              <PhaseType name="step">
                <attribute name="count" type="int"/>
                <path name="Path" quantity="+"/>
              </PhaseType>
            </PhaseTypeDefinitions>
            """;
        const string objects = """
            <?xml version="1.0"?>
            <QuestObjectDefinitions>
              <QuestObject name="Target"><class name="SyntheticObject"/></QuestObject>
            </QuestObjectDefinitions>
            """;
        using var edfStream = Stream(edf);
        using var configStream = Stream(config);
        using var phaseStream = Stream(phases);
        using var objectStream = Stream(objects);

        var catalog = QuestCatalogExtractor.Extract(
        [
            new("macros.edf", QuestCatalogFormat.Edf, edfStream),
            new("QuestEditorConfig.scr", QuestCatalogFormat.QuestEditorConfig, configStream),
            new("phases.xml", QuestCatalogFormat.PhaseDefinitionsXml, phaseStream),
            new("objects.xml", QuestCatalogFormat.QuestObjectDefinitionsXml, objectStream)
        ]);

        Assert.True(catalog.ContainsMacro("step"));
        Assert.Equal(64, catalog.CatalogSha256.Length);
        Assert.Contains(catalog.Entries, entry =>
            entry.Kind == QuestCatalogEntryKind.Macro &&
            entry.Name == "step" &&
            entry.Parameters.Select(parameter => parameter.Name).SequenceEqual(new[] { "target", "amount" }));
        Assert.Contains(catalog.Entries, entry =>
            entry.Kind == QuestCatalogEntryKind.Phase &&
            entry.Parameters.Any(parameter => parameter.Name == "count") &&
            entry.AllowedChildren.Any(child => child.Name == "Path"));
        Assert.Contains(catalog.Entries, entry =>
            entry.Kind == QuestCatalogEntryKind.QuestObject &&
            entry.AllowedChildren.Any(child => child.Name == "SyntheticObject"));
        Assert.All(catalog.Provenance, item => Assert.False(Path.IsPathRooted(item.Name)));
    }

    [Fact]
    public async Task SidecarRoundTripsLayoutWithoutSemanticTopology()
    {
        var document = PxslParser.Parse(
            Source("quest Example\n    ,step value\n"),
            Catalog("step"));
        var step = Assert.Single(document.SemanticNodes, node => node.Name == "step");
        var layout = QuestGraphSidecarService.CreateNodeLayout(step, "stable-step", 10, 20);
        var sidecar = QuestGraphSidecarService.Create(document, new[] { layout });
        await using var stream = new MemoryStream();

        await QuestGraphSidecarService.WriteAsync(stream, sidecar);
        var json = Encoding.UTF8.GetString(stream.ToArray());
        stream.Position = 0;
        var roundTrip = await QuestGraphSidecarService.ReadAsync(stream);

        Assert.Equal(QuestGraphSidecarV1.SchemaId, roundTrip.Schema);
        Assert.Equal("stable-step", Assert.Single(roundTrip.Nodes).StableId);
        Assert.DoesNotContain("graphEdges", json, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("arguments", json, StringComparison.OrdinalIgnoreCase);
        var reconciliation = QuestGraphSidecarService.Reconcile(roundTrip, document);
        Assert.Equal(QuestGraphAnchorMatchKind.ExactAnchor, Assert.Single(reconciliation.Matches).Kind);
        Assert.Empty(reconciliation.Orphans);
    }

    [Fact]
    public void ReconciliationUsesUniqueFingerprintAndOrphansAmbiguity()
    {
        var catalog = Catalog("step");
        var original = PxslParser.Parse(Source("quest Example\n    ,step value\n"), catalog);
        var originalStep = Assert.Single(original.SemanticNodes, node => node.Name == "step");
        var layout = QuestGraphSidecarService.CreateNodeLayout(originalStep, "stable-step", 10, 20);
        var sidecar = QuestGraphSidecarService.Create(original, new[] { layout });
        var moved = PxslParser.Parse(Source("-- moved\nquest Example\n    ,step value\n"), catalog);
        var ambiguous = PxslParser.Parse(Source(
            "-- moved\nquest Example\n    ,step value\n    ,step value\n"), catalog);

        var movedResult = QuestGraphSidecarService.Reconcile(sidecar, moved);
        var ambiguousResult = QuestGraphSidecarService.Reconcile(sidecar, ambiguous);

        Assert.Equal(
            QuestGraphAnchorMatchKind.UniqueSemanticFingerprint,
            Assert.Single(movedResult.Matches).Kind);
        Assert.Empty(movedResult.Orphans);
        Assert.Empty(ambiguousResult.Matches);
        Assert.Equal("stable-step", Assert.Single(ambiguousResult.Orphans).StableId);
    }

    [Fact]
    public async Task MissingSidecarDoesNotAffectPxslDocument()
    {
        Directory.CreateDirectory(_temporaryRoot);
        var path = Path.Combine(_temporaryRoot, "missing.pxsl.dlumagraph.json");
        var source = Source("quest Example\n");
        var before = source.ToArray();

        var sidecar = await QuestGraphSidecarService.LoadFileAsync(path);

        Assert.Null(sidecar);
        Assert.Equal(before, source.ToArray());
    }

    private static QuestSourceSnapshot Source(string text, long revision = 0) =>
        new("synthetic-document", "quests/synthetic.pxsl", revision, Encoding.UTF8.GetBytes(text));

    private static QuestCatalog Catalog(params string[] macroNames)
    {
        var text = string.Join('\n', macroNames.Select(name => $",{name} value ="));
        using var stream = Stream(text);
        return QuestCatalogExtractor.Extract(
            new[] { new QuestCatalogInput("synthetic.edf", QuestCatalogFormat.Edf, stream) });
    }

    private static MemoryStream Stream(string text) =>
        new(Encoding.UTF8.GetBytes(text), writable: false);

    private static int IndexOf(ReadOnlySpan<byte> haystack, ReadOnlySpan<byte> needle)
    {
        var offset = haystack.IndexOf(needle);
        Assert.True(offset >= 0);
        return offset;
    }

    private static IReadOnlyList<PxslSyntaxNode> FlattenSyntax(
        IReadOnlyList<PxslSyntaxNode> roots)
    {
        var result = new List<PxslSyntaxNode>();
        var pending = new Stack<PxslSyntaxNode>(roots);
        while (pending.Count > 0)
        {
            var node = pending.Pop();
            result.Add(node);
            foreach (var child in node.Children)
                pending.Push(child);
        }
        return result;
    }

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
