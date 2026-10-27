using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphTriviaOwnershipTests
{
    [Fact]
    public void CopySubtreeIncludesTrailingNestedComment()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner first\n" +
            "        -- owned trailing comment\n" +
            "        \n" +
            "    ,owner second\n");
        var first = Owner(document, "first");

        var fragment = QuestGraphMutationService.CopySubtree(document, first.Id);

        Assert.Equal(
            "    ,owner first\n        -- owned trailing comment\n        \n",
            fragment.Text);
    }

    [Fact]
    public void MoveSiblingCarriesTrailingNestedCommentWithItsOwner()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner first\n" +
            "        -- owned trailing comment\n" +
            "        \n" +
            "    ,owner second\n");
        var first = Owner(document, "first");

        var mutation = QuestGraphMutationService.MoveSibling(
            document,
            first.Id,
            QuestGraphMoveDirection.Down);
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;

        Assert.Equal(
            "quest Example\n" +
            "    ,owner second\n" +
            "    ,owner first\n" +
            "        -- owned trailing comment\n" +
            "        \n",
            Encoding.UTF8.GetString(result.Bytes.Span));
    }

    [Fact]
    public void DeleteSubtreeRemovesTrailingOwnedBlankAndCommentTrivia()
    {
        var document = Parse(
            "quest Example\n" +
            "    ,owner first\n" +
            "        -- owned trailing comment\n" +
            "        \n" +
            "    ,owner second\n");
        var first = Owner(document, "first");

        var mutation = QuestGraphMutationService.DeleteSubtree(document, first.Id);
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;

        Assert.Equal(
            "quest Example\n" +
            "    ,owner second\n",
            Encoding.UTF8.GetString(result.Bytes.Span));
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
