using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInspectorAcknowledgementTests
{
    [Fact]
    public async Task ConnectedFieldCommitRebuildsOnlyAfterEditAdmission()
    {
        var catalog = CreateCatalog();
        var source = new QuestSourceSnapshot(
            "synthetic-inspector-ack",
            "quests/synthetic-inspector.pxsl",
            0,
            Encoding.UTF8.GetBytes(",semaphore <<Before>>\n"));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source, catalog),
            null,
            null,
            false,
            null,
            true,
            catalog);
        var document = new QuestDocumentViewModel(new QuestDocumentService(), opened);
        document.MarkConnected(true);
        var admit = false;
        document.ConnectedEditHandler = (_, _) => Task.FromResult(admit);
        document.SelectedNode = Assert.Single(document.Nodes);
        var field = Assert.Single(document.InspectorFields);

        field.DraftValue = "Rejected";
        Assert.False(await field.CommitAsync());
        Assert.Equal(",semaphore <<Before>>\n", Decode(document.Model.Source));
        Assert.Equal("Semaphore (Before)", document.InspectorTitle);

        admit = true;
        field.DraftValue = "After";
        Assert.True(await field.CommitAsync());

        Assert.Equal(",semaphore <<After>>\n", Decode(document.Model.Source));
        Assert.Equal("Semaphore (After)", document.InspectorTitle);
        Assert.NotNull(document.SelectedNode);
    }

    private static QuestCatalog CreateCatalog()
    {
        const string edf = """
            ### SimplePhase ###
            ,semaphore name =
              phase -type=semaphore -name=<(,name)>
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="semaphore"/>
            </PhaseTypeDefinitions>
            """;
        var definitions = new[]
        {
            ("synthetic-macros.edf", QuestCatalogFormat.Edf, edf),
            ("synthetic-phases.xml", QuestCatalogFormat.PhaseDefinitionsXml, phases)
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
