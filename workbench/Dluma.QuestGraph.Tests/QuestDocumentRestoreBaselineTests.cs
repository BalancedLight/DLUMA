using System.Security.Cryptography;
using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentRestoreBaselineTests
{
    private static readonly string SyntheticProjectRoot = Path.Combine(
        Path.GetTempPath(),
        "dluma-quest-graph-synthetic-project");
    [Fact]
    public async Task DiscardRestoresExactOpeningBytesAndModifiedState()
    {
        const string openingText = "quest Synthetic\r\n    ,step opening\r\n";
        var document = CreateConnectedDocument(openingText, wasModified: true);
        QuestSourceRestoreBaseline? observed = null;
        document.ConnectedEditHandler = static (_, _) => Task.FromResult(true);
        document.ConnectedRestoreHandler = (_, baseline) =>
        {
            observed = baseline;
            return Task.FromResult<RestoreDocumentResult?>(new(
                true,
                document.Revision + 1,
                baseline.Sha256,
                "Restored."));
        };

        Assert.True(await InsertChildAsync(document, ",step visual"));
        Assert.True(await document.DiscardUnsavedChangesAsync());

        var baseline = Assert.IsType<QuestSourceRestoreBaseline>(observed);
        Assert.Equal(openingText, Encoding.UTF8.GetString(baseline.Bytes.Span));
        Assert.Equal(
            Convert.ToHexString(SHA256.HashData(baseline.Bytes.Span)),
            baseline.Sha256);
        Assert.True(baseline.WasModified);
        Assert.Equal(openingText, Encoding.UTF8.GetString(document.Model.Source.Bytes.Span));
        Assert.False(document.IsDirty);
    }

    [Fact]
    public async Task SuccessfulSaveAdvancesRestoreBaselineAndClearsModifiedState()
    {
        const string openingText = "quest Synthetic\n";
        var document = CreateConnectedDocument(openingText, wasModified: true);
        document.ConnectedEditHandler = static (_, _) => Task.FromResult(true);

        Assert.True(await InsertChildAsync(document, ",step saved"));
        var savedText = Encoding.UTF8.GetString(document.Model.Source.Bytes.Span);
        document.MarkSourceSaved();
        document.MarkLayoutSaved();

        QuestSourceRestoreBaseline? observed = null;
        document.ConnectedRestoreHandler = (_, baseline) =>
        {
            observed = baseline;
            return Task.FromResult<RestoreDocumentResult?>(new(
                true,
                document.Revision + 1,
                baseline.Sha256,
                "Restored."));
        };
        Assert.True(await InsertChildAsync(document, ",step later"));

        Assert.True(await document.DiscardUnsavedChangesAsync());

        var baseline = Assert.IsType<QuestSourceRestoreBaseline>(observed);
        Assert.Equal(savedText, Encoding.UTF8.GetString(baseline.Bytes.Span));
        Assert.False(baseline.WasModified);
        Assert.Equal(savedText, Encoding.UTF8.GetString(document.Model.Source.Bytes.Span));
    }

    [Fact]
    public async Task MissingHostAllowsLocalCloseRecoveryWithoutTouchingAnotherBuffer()
    {
        const string openingText = "quest Synthetic\n";
        var document = CreateConnectedDocument(openingText, wasModified: false);
        document.ConnectedEditHandler = static (_, _) => Task.FromResult(true);
        document.ConnectedRestoreHandler = static (_, _) =>
            Task.FromResult<RestoreDocumentResult?>(null);

        Assert.True(await InsertChildAsync(document, ",step visual"));

        Assert.True(await document.DiscardUnsavedChangesAsync());
        Assert.Equal(openingText, Encoding.UTF8.GetString(document.Model.Source.Bytes.Span));
        Assert.False(document.IsDirty);
    }

    [Fact]
    public async Task RefreshedDocumentIdentityRetainsItsOpeningBaseline()
    {
        const string openingText = "quest Synthetic\n";
        var document = CreateConnectedDocument(openingText, wasModified: false);
        var refreshedBytes = Encoding.UTF8.GetBytes("quest Synthetic\n    ,step refreshed\n");
        var refreshedId = new string('E', 64);
        document.ReacquireConnectedSnapshot(
            new OpenDocumentMessage(
                refreshedId,
                document.RelativePath,
                7,
                "utf-8",
                refreshedBytes,
                Convert.ToHexString(SHA256.HashData(refreshedBytes)),
                true),
            null,
            SyntheticProjectRoot);

        QuestSourceRestoreBaseline? observed = null;
        document.ConnectedRestoreHandler = (_, baseline) =>
        {
            observed = baseline;
            return Task.FromResult<RestoreDocumentResult?>(new(
                true,
                8,
                baseline.Sha256,
                "Restored."));
        };

        Assert.True(await document.DiscardUnsavedChangesAsync());

        Assert.Equal(refreshedId, document.DocumentId);
        Assert.Equal(openingText, Encoding.UTF8.GetString(
            Assert.IsType<QuestSourceRestoreBaseline>(observed).Bytes.Span));
    }

    [Fact]
    public void ReacquireRejectsSameRelativePathFromDifferentProject()
    {
        var document = CreateConnectedDocument("quest Synthetic\n", wasModified: false);
        var bytes = Encoding.UTF8.GetBytes("quest Other\n");

        Assert.Throws<InvalidDataException>(() => document.ReacquireConnectedSnapshot(
            new OpenDocumentMessage(
                new string('F', 64),
                document.RelativePath,
                1,
                "utf-8",
                bytes,
                Convert.ToHexString(SHA256.HashData(bytes)),
                false),
            null,
            Path.Combine(Path.GetTempPath(), "dluma-quest-graph-other-project")));
    }

    private static Task<bool> InsertChildAsync(
        QuestDocumentViewModel document,
        string insertionText)
    {
        document.SelectedNode = Assert.Single(
            document.Nodes,
            node => node.SemanticNode.Kind == QuestSemanticKind.Quest);
        return document.InsertAsync(
            new QuestPaletteItemViewModel(
                "Synthetic step",
                "Test",
                insertionText,
                "Synthetic baseline-test node."),
            asChild: true);
    }

    private static QuestDocumentViewModel CreateConnectedDocument(
        string text,
        bool wasModified)
    {
        var source = new QuestSourceSnapshot(
            new string('D', 64),
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(text));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source),
            null,
            null,
            false,
            null,
            true,
            null,
            wasModified,
            SyntheticProjectRoot);
        var document = new QuestDocumentViewModel(new QuestDocumentService(), opened);
        document.MarkConnected(true);
        return document;
    }
}
