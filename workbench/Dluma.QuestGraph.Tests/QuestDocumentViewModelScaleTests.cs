using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentViewModelScaleTests
{
    [Fact]
    public async Task DeepDocumentRebuildLayoutAndSubtreeInsertionAreIterative()
    {
        const int depth = 3_500;
        var text = new StringBuilder();
        for (var index = 0; index < depth; ++index)
        {
            text.Append('\t', index);
            text.Append(",node ");
            text.Append(index);
            text.Append('\n');
        }
        var source = new QuestSourceSnapshot(
            "synthetic-deep-document",
            "quests/synthetic-deep.pxsl",
            0,
            Encoding.UTF8.GetBytes(text.ToString()));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source),
            null,
            null,
            false,
            null,
            false,
            null);
        var document = new QuestDocumentViewModel(new QuestDocumentService(), opened);

        document.AutoLayout();
        document.SelectedNode = Assert.Single(
            document.Nodes,
            node => node.SemanticNode.ParentId is null);
        var inserted = await document.InsertAsync(
            new QuestPaletteItemViewModel(
                "Synthetic node",
                "Test",
                ",node inserted",
                "Generic synthetic scale-test node."),
            asChild: false);

        Assert.True(inserted);
        Assert.Equal(depth + 1, document.Model.SemanticNodes.Count);
        Assert.Equal(depth + 1, document.Nodes.Count);
    }
}
