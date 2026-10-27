using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestSourceBlockServiceTests
{
    [Fact]
    public void SyntaxOwnedTrailingTriviaIsIncludedAndDeletedAtomically()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner first\n" +
            "        -- owned trailing comment\n" +
            "        \n" +
            "    ,owner second\n");
        var first = Owner(document, "first");

        var range = QuestSourceBlockService.GetSubtreeRange(document, first);
        var ownedBytes = document.Source.Bytes.Span.Slice(range.Start, range.Length).ToArray();
        var mutation = QuestSourceBlockService.DeleteSubtree(document, first.Id);
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;

        Assert.Equal(
            "    ,owner first\n        -- owned trailing comment\n        \n",
            Encoding.UTF8.GetString(ownedBytes));
        Assert.Equal(
            "quest Example\n    ,owner second\n",
            Encoding.UTF8.GetString(result.Bytes.Span));
    }

    [Fact]
    public void OpaqueNodeCanBeDeletedByItsExactOwnedSpan()
    {
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes("quest Example\n    ,unknown exact\n"));
        using var catalogStream = new MemoryStream(
            Encoding.UTF8.GetBytes(",known value =\n"), writable: false);
        var catalog = QuestCatalogExtractor.Extract(
            new[] { new QuestCatalogInput("synthetic.edf", QuestCatalogFormat.Edf, catalogStream) });
        var document = PxslParser.Parse(source, catalog);
        var opaque = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Opaque);

        var mutation = QuestSourceBlockService.DeleteSubtree(document, opaque.Id);
        var result = QuestSourceEditService.Apply(source, mutation.Edits).Source;

        Assert.Equal("quest Example\n", Encoding.UTF8.GetString(result.Bytes.Span));
        Assert.Equal(source.ToArray(), document.Syntax.ToBytes());
    }

    private static QuestGraphDocument Parse(string text) => PxslParser.Parse(
        new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(text)));

    private static QuestSemanticNode Owner(QuestGraphDocument document, string value) =>
        Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro &&
                node.Name == "owner" &&
                node.Arguments.Contains(value));
}
