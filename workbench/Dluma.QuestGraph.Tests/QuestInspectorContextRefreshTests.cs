using System.Text;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInspectorContextRefreshTests
{
    [Fact]
    public async Task RefreshRebindsCapturedPickerOptionsWithoutDiscardingFreeFormDraft()
    {
        var catalog = CreateCatalog();
        var source = new QuestSourceSnapshot(
            "synthetic-context-refresh",
            "quests/synthetic-context-refresh.pxsl",
            0,
            Encoding.UTF8.GetBytes(
                ",interactive-movie Intro SyntheticScenarioClass scenario_old\n"));
        var opened = new OpenedQuestDocument(
            PxslParser.Parse(source, catalog),
            null,
            null,
            false,
            null,
            false,
            catalog);
        var document = new QuestDocumentViewModel(new QuestDocumentService(), opened);
        var options = new List<string> { "scenario_old" };
        document.InspectorOptionProvider = (kind, _) =>
            kind == QuestParameterEditorKind.Scenario
                ? options.ToArray()
                : Array.Empty<string>();
        document.SelectedNode = Assert.Single(document.Nodes);
        var field = Assert.Single(document.InspectorFields,
            item => item.EditorKind == QuestParameterEditorKind.Scenario);
        field.DraftValue = "free_form";

        var refreshCount = 0;
        document.InspectorContextRefreshHandler = () =>
        {
            refreshCount++;
            options.Clear();
            options.Add("scenario_captured");
            return Task.CompletedTask;
        };

        await document.RefreshInspectorContextAsync();

        Assert.Equal(1, refreshCount);
        Assert.Same(field, Assert.Single(document.InspectorFields,
            item => item.EditorKind == QuestParameterEditorKind.Scenario));
        Assert.Equal("free_form", field.DraftValue);
        Assert.Equal(["scenario_captured"], field.Options);
        Assert.True(field.CanEdit);
    }

    [Fact]
    public void EditablePickerRefreshesContextWhenItsDropdownOpens()
    {
        var source = ReadWorkspaceFile(
            "workbench",
            "Dluma.QuestGraph",
            "Views",
            "QuestInspectorPanel.xaml");

        Assert.Contains(
            "DropDownOpened=\"OnPickerDropDownOpened\"",
            source,
            StringComparison.Ordinal);
    }

    private static QuestCatalog CreateCatalog()
    {
        const string edf = """
            ### Forbidden ###
            ,scenario class scenario_name =
              Scenario -class=<(,class)> -name=<(,scenario_name)>

            ### SimplePhase ###
            ,interactive-movie name class scenario_name =
              phase -type=<<interactive movie>> -name=<(,name)>
                ,scenario <(,class)> <(,scenario_name)>
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="interactive movie"/>
            </PhaseTypeDefinitions>
            """;
        const string objects = """
            <QuestObjectDefinitions>
              <QuestObject name="Scenario"><class name="SyntheticScenarioClass"/></QuestObject>
            </QuestObjectDefinitions>
            """;
        var definitions = new[]
        {
            ("synthetic-macros.edf", QuestCatalogFormat.Edf, edf),
            ("synthetic-phases.xml", QuestCatalogFormat.PhaseDefinitionsXml, phases),
            ("synthetic-objects.xml", QuestCatalogFormat.QuestObjectDefinitionsXml, objects)
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

    private static string ReadWorkspaceFile(params string[] segments)
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            var path = Path.Combine(new[] { directory.FullName }.Concat(segments).ToArray());
            if (File.Exists(path))
                return File.ReadAllText(path);
        }
        throw new FileNotFoundException(
            $"Could not locate the workspace file '{Path.Combine(segments)}'.");
    }
}
