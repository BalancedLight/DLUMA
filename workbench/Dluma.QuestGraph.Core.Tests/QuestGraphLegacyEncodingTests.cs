using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphLegacyEncodingTests
{
    [Fact]
    public void OrderedMovePreservesWindows1252BytesAndCrLf()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        var encoding = Encoding.GetEncoding(
            1252,
            EncoderFallback.ExceptionFallback,
            DecoderFallback.ExceptionFallback);
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            encoding.GetBytes(
                "quest Example\r\n" +
                "    ,step café\r\n" +
                "    ,step second\r\n"),
            encoding.WebName);
        var document = PxslParser.Parse(source);
        var first = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro && node.Arguments.Contains("café"));

        var mutation = QuestGraphMutationService.MoveSibling(
            document,
            first.Id,
            QuestGraphMoveDirection.Down);
        var result = QuestSourceEditService.Apply(source, mutation.Edits).Source;

        Assert.Equal(QuestLineEnding.CrLf, result.LineEnding);
        Assert.Equal(encoding.WebName, result.EncodingName);
        Assert.Equal(
            encoding.GetBytes(
                "quest Example\r\n" +
                "    ,step second\r\n" +
                "    ,step café\r\n"),
            result.ToArray());
    }

    [Fact]
    public void PasteRejectsTextOutsideLegacyCodePage()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        var encoding = Encoding.GetEncoding(
            1252,
            EncoderFallback.ExceptionFallback,
            DecoderFallback.ExceptionFallback);
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            encoding.GetBytes("quest Example\r\n    ,step target\r\n"),
            encoding.WebName);
        var document = PxslParser.Parse(source);
        var target = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro);
        var fragment = new QuestGraphClipboardFragment(
            "    ,step emoji-😀\r\n",
            "    ",
            QuestSemanticKind.Macro,
            "step");

        var exception = Assert.Throws<QuestGraphOperationException>(() =>
            QuestGraphMutationService.Paste(
                document,
                target.Id,
                fragment,
                QuestGraphPastePlacement.After));

        Assert.Equal(QuestGraphOperationRejection.UnrepresentableText, exception.Reason);
        Assert.Equal(
            encoding.GetBytes("quest Example\r\n    ,step target\r\n"),
            source.ToArray());
    }
}
