using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentRestoreFailureTests
{
    [Fact]
    public async Task RestoreFailureCancelsDiscardAndKeepsUnsavedProjection()
    {
        const string original = "quest Synthetic\n";
        var source = new QuestSourceSnapshot(
            new string('D', 64),
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(original));
        var document = new QuestDocumentViewModel(
            new QuestDocumentService(),
            new OpenedQuestDocument(
                PxslParser.Parse(source),
                null,
                null,
                false,
                null,
                true,
                null,
                false));
        document.MarkConnected(true);
        document.ConnectedEditHandler = static (_, _) => Task.FromResult(true);
        document.ConnectedRestoreHandler = static (_, _) =>
            throw new InvalidDataException("Synthetic restore rejection.");
        document.SelectedNode = Assert.Single(
            document.Nodes,
            node => node.SemanticNode.Kind == QuestSemanticKind.Quest);
        Assert.True(await document.InsertAsync(
            new QuestPaletteItemViewModel(
                "Synthetic step",
                "Test",
                ",step unsaved",
                "Synthetic restore-failure node."),
            asChild: true));
        var unsaved = document.Model.Source.ToArray();

        Assert.False(await document.DiscardUnsavedChangesAsync());
        Assert.True(document.IsDirty);
        Assert.Equal(unsaved, document.Model.Source.ToArray());
        Assert.Contains("restore", document.Status, StringComparison.OrdinalIgnoreCase);
    }
}
