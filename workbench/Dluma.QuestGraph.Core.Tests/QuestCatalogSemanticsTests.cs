using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestCatalogSemanticsTests
{
    [Fact]
    public void StockShapedInputsProduceInsertableMacrosWithResolvedSemantics()
    {
        const string edf = """
            ### Forbidden ###
            ,qo class object =
              QuestObject -class=<(,class)> -name=<(,object)>

            ### SubPhase ###
            ,o class object =
              Object
                ,qo <(,class)> <(,object)>

            ### SimplePhase ###
            ,enable-multi name state =
              phase enable <(,name)> -state=<(,state)>
                ,BODY

            ,disable name =
              ,enable-multi <(,name)> -state=disabled
                ,o SyntheticClass SyntheticObject
                ,BODY

            ,semaphore name =
              Phase -type=semaphore -name=<(,name)>

            ,interactive-movie name =
              Phase -type=<<interactive movie>> -name=<(,name)>
                Scenario
                  ,o Scenario SyntheticScenario
                ,BODY

            ### Compound ###
            ,BLOCK name =
              Phase -type=AND -name=<(,name)>
                path
                  ,BODY
            """;
        const string config = """
            AutoExpand()
                Block(",interactive-movie")
                    Expand("pre")
                    Expand("post")
                Block(",BLOCK")
                    Expand("path")
            Descriptions()
                Desc(",enable-multi", ",enable-multi [phase_name]", "    ,o [objects_class] [object_name]", "")
                Desc(",interactive-movie", ",interactive-movie [phase_name]", "", "")
                Desc(",BLOCK", ",BLOCK [phase_name]", "", "")
                Desc("quest", "quest [quest_name] -final=[true|false]", "", "")
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="AND"><path name="Path" quantity="+"/></PhaseType>
              <PhaseType name="interactive movie">
                <path name="PreMovie" quantity="?"/>
                <path name="PostMovie" quantity="?"/>
              </PhaseType>
              <PhaseType name="enable"><QuestObject name="Object" quantity="+"/></PhaseType>
              <PhaseType name="semaphore"/>
            </PhaseTypeDefinitions>
            """;
        const string objects = """
            <QuestObjectDefinitions>
              <QuestObject name="Object"><class name="SyntheticClass"/></QuestObject>
            </QuestObjectDefinitions>
            """;
        const string colors = """
            <ColorDefs>
              <PhaseType name="AND" color="102030" font-color="FFFFFF" short="block"/>
              <PhaseType name="interactive movie" color="0.1 0.2 0.3 0.5" short="imovie"/>
              <PhaseType name="enable" color="AABBCC"/>
              <PhaseType name="semaphore" color="112233"/>
            </ColorDefs>
            """;

        var catalog = Extract(
            ("quest-macros.edf", QuestCatalogFormat.Edf, edf),
            ("QuestEditorConfig.scr", QuestCatalogFormat.QuestEditorConfig, config),
            ("phases.xml", QuestCatalogFormat.PhaseDefinitionsXml, phases),
            ("objects.xml", QuestCatalogFormat.QuestObjectDefinitionsXml, objects),
            ("colors.xml", QuestCatalogFormat.QuestColorsXml, colors));

        var block = Entry(catalog, "BLOCK", QuestCatalogEntryKind.Macro);
        Assert.True(block.IsInsertable);
        Assert.True(block.IsCompileEligible);
        Assert.Equal(QuestCatalogFunctionalCategory.FlowAndContainers, block.Category);
        Assert.Equal("AND", block.ResolvedPhaseType);
        Assert.Equal("#102030", block.Style!.HeaderColor);
        Assert.Equal("#FFFFFF", block.Style.FontColor);
        Assert.Equal("block", block.Style.ShortName);
        var path = Assert.Single(block.ChildSlots,
            slot => slot.Kind == QuestCatalogChildSlotKind.Branch);
        Assert.Equal("branch:path", path.PortId);
        // ,BLOCK declares ,BODY under its path, and PXSL lets that path stay implicit,
        // so the container also owns an ordered body of nested macros.
        var body = Assert.Single(block.ChildSlots,
            slot => slot.Kind == QuestCatalogChildSlotKind.OrderedBody);
        Assert.Equal("body", body.PortId);

        var movie = Entry(catalog, "interactive-movie", QuestCatalogEntryKind.Macro);
        Assert.Equal(QuestCatalogFunctionalCategory.DialogueAndCinematics, movie.Category);
        Assert.Equal("interactive movie", movie.ResolvedPhaseType);
        Assert.Equal(new[] { "post", "pre" }, movie.PathSlotNames.Order().ToArray());
        Assert.Equal("#801A334C", movie.Style!.HeaderColor);

        var semaphore = Entry(catalog, "semaphore", QuestCatalogEntryKind.Macro);
        Assert.Equal(QuestCatalogFunctionalCategory.FlowAndContainers, semaphore.Category);

        var enable = Entry(catalog, "enable-multi", QuestCatalogEntryKind.Macro);
        Assert.Equal(QuestCatalogFunctionalCategory.WorldAndObjects, enable.Category);
        var objectSlot = Assert.Single(enable.ChildSlots);
        Assert.Equal("o", objectSlot.Id);
        Assert.Equal("+", objectSlot.Quantity);
        Assert.Equal(",o [objects_class] [object_name]", objectSlot.Template);
        Assert.Equal(",enable-multi _\n    ,o _ _",
            enable.BuildMinimumTemplate());

        var objectReference = Entry(catalog, "o", QuestCatalogEntryKind.Macro);
        Assert.Equal(QuestCatalogFunctionalCategory.ChildRecords, objectReference.Category);
        Assert.True(objectReference.IsInsertable);
        Assert.False(Entry(catalog, "qo", QuestCatalogEntryKind.Macro).IsInsertable);

        var disable = Entry(catalog, "disable", QuestCatalogEntryKind.Macro);
        Assert.Equal("enable-multi", disable.AliasTarget);
        Assert.Equal("enable", disable.ResolvedPhaseType);
        Assert.Contains("disable", enable.Aliases);

        Assert.True(Entry(catalog, "quest", QuestCatalogEntryKind.Macro).IsInsertable);
        Assert.All(catalog.Entries.Where(entry => entry.Kind is
            QuestCatalogEntryKind.Phase or QuestCatalogEntryKind.QuestObject or QuestCatalogEntryKind.Branch),
            entry => Assert.False(entry.IsInsertable));
        Assert.DoesNotContain(catalog.Entries, entry => entry.Name == "BODY");
        Assert.Equal(catalog.InsertableEntries.Count,
            catalog.InsertableEntries.Select(entry => entry.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count());
    }

    [Fact]
    public void FirstDefinitionWinsSoProjectInputsOverrideInstalledInputs()
    {
        const string project = """
            ### SimplePhase ###
            ,shared project_value =
              Phase -type=<<project phase>> -name=<(,project_value)>
            """;
        const string installed = """
            ### SimplePhase ###
            ,shared installed_value =
              Phase -type=<<installed phase>> -name=<(,installed_value)>
            """;

        var catalog = Extract(
            ("project-private.edf", QuestCatalogFormat.Edf, project),
            ("installed-public.edf", QuestCatalogFormat.Edf, installed));
        var shared = Entry(catalog, "shared", QuestCatalogEntryKind.Macro);

        Assert.Equal("project phase", shared.ResolvedPhaseType);
        Assert.Equal(new[] { "project_value" }, shared.ParameterNames);
        Assert.Contains("project-private.edf", shared.Sources);
        Assert.DoesNotContain("installed-public.edf", shared.Sources);
    }

    private static QuestCatalog Extract(
        params (string Name, QuestCatalogFormat Format, string Text)[] definitions)
    {
        var streams = definitions
            .Select(definition => new MemoryStream(
                Encoding.UTF8.GetBytes(definition.Text),
                writable: false))
            .ToArray();
        try
        {
            return QuestCatalogExtractor.Extract(definitions.Select((definition, index) =>
                new QuestCatalogInput(definition.Name, definition.Format, streams[index])));
        }
        finally
        {
            foreach (var stream in streams)
                stream.Dispose();
        }
    }

    private static QuestCatalogEntry Entry(
        QuestCatalog catalog,
        string name,
        QuestCatalogEntryKind kind) => Assert.Single(catalog.Entries,
        entry => entry.Kind == kind && entry.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
}
