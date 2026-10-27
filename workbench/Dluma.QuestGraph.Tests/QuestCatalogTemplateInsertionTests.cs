using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestCatalogTemplateInsertionTests
{
    [Theory]
    [InlineData("\n")]
    [InlineData("\r\n")]
    public async Task MinimumTemplateKeepsRelativeIndentationAndDocumentEol(
        string newline)
    {
        var source = new QuestSourceSnapshot(
            $"synthetic-{Guid.NewGuid():N}",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes("quest Synthetic" + newline));
        var document = new QuestDocumentViewModel(
            new QuestDocumentService(),
            new OpenedQuestDocument(
                PxslParser.Parse(source),
                null,
                null,
                false,
                null,
                false,
                null));
        document.SelectedNode = Assert.Single(
            document.Nodes,
            node => node.SemanticNode.Kind == QuestSemanticKind.Quest);

        Assert.True(await document.InsertAsync(
            new QuestPaletteItemViewModel(
                "Synthetic container",
                "Test",
                ",container _\n    ,child _",
                "Synthetic multi-line minimum template."),
            asChild: true));

        Assert.Equal(
            "quest Synthetic" + newline +
            "    ,container _" + newline +
            "        ,child _" + newline,
            Encoding.UTF8.GetString(document.Model.Source.Bytes.Span));
    }
}
