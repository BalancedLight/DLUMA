using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestSourceEditRegressionTests
{
    [Fact]
    public void MultipleEditsUseOriginalByteOffsetsAndApplyAtomically()
    {
        var source = Source("one two three");
        var one = Encoding.UTF8.GetBytes("one");
        var three = Encoding.UTF8.GetBytes("three");
        var edits = new[]
        {
            new QuestTextEdit(
                source.Revision,
                source.Sha256,
                source.Bytes.Span.IndexOf(one),
                one.Length,
                Encoding.UTF8.GetBytes("first-value")),
            new QuestTextEdit(
                source.Revision,
                source.Sha256,
                source.Bytes.Span.IndexOf(three),
                three.Length,
                Encoding.UTF8.GetBytes("last"))
        };

        var result = QuestSourceEditService.Apply(source, edits);

        Assert.Equal("first-value two last", Encoding.UTF8.GetString(result.Source.Bytes.Span));
        Assert.Equal(source.Revision + 1, result.Source.Revision);
    }

    [Fact]
    public void OverlappingEditsAreRejectedBeforeAnyBytesChange()
    {
        var source = Source("abcdef");
        var edits = new[]
        {
            new QuestTextEdit(source.Revision, source.Sha256, 1, 3, Encoding.UTF8.GetBytes("x")),
            new QuestTextEdit(source.Revision, source.Sha256, 2, 2, Encoding.UTF8.GetBytes("y"))
        };

        var exception = Assert.Throws<QuestEditRejectedException>(() =>
            QuestSourceEditService.Apply(source, edits));

        Assert.Equal(QuestEditRejection.OverlappingRanges, exception.Reason);
        Assert.Equal("abcdef", Encoding.UTF8.GetString(source.Bytes.Span));
    }

    private static QuestSourceSnapshot Source(string text) =>
        new("edit-regression", "quests/synthetic.pxsl", 4, Encoding.UTF8.GetBytes(text));
}
