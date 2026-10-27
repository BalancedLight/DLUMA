using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentInteractionTests
{
    [Fact]
    public void ProjectedConnectionsMarkBothConnectorAnchorsActive()
    {
        var document = CreateDocument(
            "quest Synthetic\n    ,step first\n    ,step second\n",
            requiresConnection: false);

        Assert.NotEmpty(document.Connections);
        Assert.All(document.Connections, connection =>
        {
            Assert.True(connection.Source.IsConnected);
            Assert.True(connection.Target.IsConnected);
        });
    }

    [Fact]
    public async Task DiscardRestoresSavedConnectedSourceBeforeClearingDirtyState()
    {
        const string original = "quest Synthetic\n    ,step first\n";
        var document = CreateDocument(original, requiresConnection: true);
        var admitted = new List<IReadOnlyList<QuestTextEdit>>();
        document.MarkConnected(true);
        document.ConnectedEditHandler = (_, edits) =>
        {
            admitted.Add(edits);
            return Task.FromResult(true);
        };
        QuestSourceRestoreBaseline? restoredBaseline = null;
        document.ConnectedRestoreHandler = (_, baseline) =>
        {
            restoredBaseline = baseline;
            return Task.FromResult<RestoreDocumentResult?>(new(
                true,
                document.Revision + 1,
                baseline.Sha256,
                "Restored."));
        };
        document.SelectedNode = Assert.Single(
            document.Nodes,
            node => node.SemanticNode.Kind == QuestSemanticKind.Quest);

        Assert.True(await document.InsertAsync(
            new QuestPaletteItemViewModel(
                "Synthetic step",
                "Test",
                ",step second",
                "Synthetic interaction-test node."),
            asChild: true));
        Assert.True(document.IsDirty);

        Assert.True(await document.DiscardUnsavedChangesAsync());

        Assert.Single(admitted);
        var restore = Assert.IsType<QuestSourceRestoreBaseline>(restoredBaseline);
        Assert.Equal(original, Encoding.UTF8.GetString(restore.Bytes.Span));
        Assert.False(restore.WasModified);
        Assert.Equal(original, Encoding.UTF8.GetString(document.Model.Source.ToArray()));
        Assert.False(document.IsDirty);
    }

    private static QuestDocumentViewModel CreateDocument(
        string text,
        bool requiresConnection)
    {
        var source = new QuestSourceSnapshot(
            $"synthetic-{Guid.NewGuid():N}",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(text));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source),
            null,
            null,
            false,
            null,
            requiresConnection,
            null);
        return new QuestDocumentViewModel(new QuestDocumentService(), opened);
    }
}
