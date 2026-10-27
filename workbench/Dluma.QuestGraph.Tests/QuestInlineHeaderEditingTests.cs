using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInlineHeaderEditingTests
{
    [Fact]
    public async Task UppercasePhaseHeaderEditsItsPhaseNameWithoutChangingTheType()
    {
        var catalog = CreateCatalog();
        var source = new QuestSourceSnapshot(
            "synthetic-inline-header",
            "quests/synthetic-header.pxsl",
            0,
            Encoding.UTF8.GetBytes(",BLOCK <<OLD>>\n"));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source, catalog),
            null,
            null,
            false,
            null,
            false,
            catalog);
        var document = new QuestDocumentViewModel(new QuestDocumentService(), opened);
        var node = Assert.Single(document.Nodes);
        document.SelectedNode = node;

        Assert.Equal("BLOCK (OLD)", node.DisplayHeader);
        Assert.True(document.BeginHeaderEdit(node));
        node.HeaderEditText = "NEXT";
        Assert.True(node.TryBeginHeaderCommit());
        Assert.True(await document.CommitHeaderEditAsync(node));

        Assert.Equal(",BLOCK <<NEXT>>\n", Decode(document.Model.Source));
        Assert.Equal("BLOCK (NEXT)", Assert.Single(document.Nodes).DisplayHeader);
    }

    [Fact]
    public async Task NamingABlankPhaseFromTheHeaderWrapsItInAngleBrackets()
    {
        // The header edit reaches SetFieldValue by a different route than the inspector
        // field, so it gets its own coverage of the "_" -> <<name>> transition.
        var document = OpenDocument(",BLOCK _\n");
        var node = Assert.Single(document.Nodes);
        document.SelectedNode = node;

        Assert.Equal("BLOCK", node.DisplayHeader);
        Assert.True(document.BeginHeaderEdit(node));
        node.HeaderEditText = "sdsds";
        Assert.True(node.TryBeginHeaderCommit());
        Assert.True(await document.CommitHeaderEditAsync(node));

        Assert.Equal(",BLOCK <<sdsds>>\n", Decode(document.Model.Source));
        Assert.Equal("BLOCK (sdsds)", Assert.Single(document.Nodes).DisplayHeader);
    }

    [Fact]
    public async Task ClearingThePhaseHeaderRestoresThePlaceholder()
    {
        var document = OpenDocument(",BLOCK <<sdsds>>\n");
        var node = Assert.Single(document.Nodes);
        document.SelectedNode = node;

        Assert.True(document.BeginHeaderEdit(node));
        node.HeaderEditText = string.Empty;
        Assert.True(node.TryBeginHeaderCommit());
        Assert.True(await document.CommitHeaderEditAsync(node));

        Assert.Equal(",BLOCK _\n", Decode(document.Model.Source));
    }

    private static QuestDocumentViewModel OpenDocument(string text)
    {
        var catalog = CreateCatalog();
        var source = new QuestSourceSnapshot(
            "synthetic-inline-header",
            "quests/synthetic-header.pxsl",
            0,
            Encoding.UTF8.GetBytes(text));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source, catalog),
            null,
            null,
            false,
            null,
            false,
            catalog);
        return new QuestDocumentViewModel(new QuestDocumentService(), opened);
    }

    private static QuestCatalog CreateCatalog()
    {
        const string edf = """
            ### Compound ###
            ,BLOCK name =
              phase -type=AND -name=<(,name)>
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="AND"/>
            </PhaseTypeDefinitions>
            """;
        var definitions = new[]
        {
            ("synthetic-block.edf", QuestCatalogFormat.Edf, edf),
            ("synthetic-and.xml", QuestCatalogFormat.PhaseDefinitionsXml, phases)
        };
        var streams = definitions.Select(definition => new MemoryStream(
            Encoding.UTF8.GetBytes(definition.Item3), writable: false)).ToArray();
        try
        {
            return QuestCatalogExtractor.Extract(definitions.Select((definition, index) =>
                new QuestCatalogInput(definition.Item1, definition.Item2, streams[index])));
        }
        finally
        {
            foreach (var stream in streams)
                stream.Dispose();
        }
    }

    private static string Decode(QuestSourceSnapshot source) =>
        QuestEncoding.Resolve(source.EncodingName).GetString(source.Bytes.Span);
}
