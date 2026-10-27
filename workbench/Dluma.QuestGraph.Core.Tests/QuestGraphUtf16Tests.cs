using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphUtf16Tests
{
    [Fact]
    public void Utf16LeBomAndCrLfSurviveCopyAndOrderedMove()
    {
        var encoding = new UnicodeEncoding(bigEndian: false, byteOrderMark: true, throwOnInvalidBytes: true);
        var body = encoding.GetBytes(
            "quest Example\r\n" +
            "    ,step first\r\n" +
            "    ,step second\r\n");
        var bytes = encoding.GetPreamble().Concat(body).ToArray();
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            bytes,
            encoding.WebName);
        var document = PxslParser.Parse(source);
        var first = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro && node.Arguments.Contains("first"));

        var copied = QuestGraphMutationService.CopySubtree(document, first.Id);
        var mutation = QuestGraphMutationService.MoveSibling(
            document,
            first.Id,
            QuestGraphMoveDirection.Down);
        var result = QuestSourceEditService.Apply(source, mutation.Edits).Source;

        Assert.Equal("    ,step first\r\n", copied.Text);
        Assert.True(result.HasBom);
        Assert.Equal(QuestLineEnding.CrLf, result.LineEnding);
        Assert.True(result.Bytes.Span.StartsWith(encoding.GetPreamble()));
        Assert.Equal(
            "quest Example\r\n" +
            "    ,step second\r\n" +
            "    ,step first\r\n",
            encoding.GetString(result.Bytes.Span[encoding.GetPreamble().Length..]));
    }
}
