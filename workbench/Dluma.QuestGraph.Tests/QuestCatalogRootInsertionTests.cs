using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestCatalogRootInsertionTests
{
    [Fact]
    public async Task RuntimeCatalogQuestTemplateCanCreateAnEmptyDocumentRoot()
    {
        var source = new QuestSourceSnapshot(
            $"synthetic-{Guid.NewGuid():N}",
            "quests/empty.pxsl",
            0,
            Array.Empty<byte>());
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
        var catalogEntry = new QuestCatalogEntry(
            "quest",
            QuestCatalogEntryKind.Macro,
            Array.Empty<QuestCatalogParameter>(),
            Array.Empty<QuestCatalogChild>(),
            ["synthetic-catalog"])
        {
            IsInsertable = true,
            IsCompileEligible = true,
            Category = QuestCatalogFunctionalCategory.FlowAndContainers,
            Template = "quest Synthetic"
        };

        Assert.True(await document.InsertAsync(
            new QuestPaletteItemViewModel(
                "quest",
                "Flow & Containers",
                catalogEntry.BuildMinimumTemplate(),
                "Synthetic root declaration.",
                catalogEntry),
            asChild: false));

        Assert.Equal("quest Synthetic\r\n", Encoding.UTF8.GetString(
            document.Model.Source.Bytes.Span));
        Assert.Single(document.Nodes, node =>
            node.SemanticNode.Kind == QuestSemanticKind.Quest);
    }
}
