using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestInspectorTests
{
    [Fact]
    public void StockShapedCatalogDerivesCompleteSemaphoreMovieAndTakeFields()
    {
        var catalog = Catalog();

        var semaphore = Entry(catalog, "semaphore");
        var semaphoreName = Assert.Single(semaphore.InspectorFields);
        Assert.Equal(QuestInspectorFieldRole.PhaseName, semaphoreName.Role);
        Assert.Equal(QuestParameterEditorKind.Text, semaphoreName.EditorKind);
        Assert.Equal(0, semaphoreName.Position);

        var movie = Entry(catalog, "interactive-movie");
        Assert.Equal(movie.Parameters.Count, movie.InspectorFields.Count);
        AssertField(movie, "name", QuestParameterEditorKind.Text, QuestInspectorFieldRole.PhaseName);
        var movieClass = AssertField(
            movie,
            "class",
            QuestParameterEditorKind.MapObjectClass,
            QuestInspectorFieldRole.Parameter);
        var scenario = AssertField(
            movie,
            "scenario_name",
            QuestParameterEditorKind.Scenario,
            QuestInspectorFieldRole.Parameter);
        Assert.Equal(scenario.Id, movieClass.PairedFieldId);
        Assert.Equal(movieClass.Id, scenario.PairedFieldId);

        var take = Entry(catalog, "take");
        Assert.Equal(take.Parameters.Count, take.InspectorFields.Count);
        var objectClass = AssertField(
            take,
            "class",
            QuestParameterEditorKind.MapObjectClass,
            QuestInspectorFieldRole.Parameter);
        var objectName = AssertField(
            take,
            "object",
            QuestParameterEditorKind.MapObjectName,
            QuestInspectorFieldRole.Parameter);
        Assert.Equal(objectName.Id, objectClass.PairedFieldId);
        Assert.Equal(objectClass.Id, objectName.PairedFieldId);
        var objective = AssertField(
            take,
            "obj",
            QuestParameterEditorKind.TextId,
            QuestInspectorFieldRole.Parameter);
        Assert.Equal("obj", objective.NamedArgument);
        Assert.False(objective.IsRequired);
        var show = AssertField(
            take,
            "show",
            QuestParameterEditorKind.Boolean,
            QuestInspectorFieldRole.Parameter);
        Assert.Equal(new[] { "true", "false" }, show.AllowedValues);
        Assert.Equal("show", show.NamedArgument);

        Assert.All(
            catalog.Entries.Where(entry =>
                entry.Kind == QuestCatalogEntryKind.Macro &&
                !entry.Name.Equals("quest", StringComparison.OrdinalIgnoreCase)),
            entry =>
            {
                Assert.Equal(entry.Parameters.Count,
                    entry.InspectorFields.Count(field =>
                        field.Role != QuestInspectorFieldRole.NodeName));
                Assert.Equal(entry.InspectorFields.Count,
                    entry.InspectorFields.Select(field => field.Id)
                        .Distinct(StringComparer.OrdinalIgnoreCase).Count());
            });
    }

    [Fact]
    public void InspectorPreservesWrappedNamesAndReportsUnknownArgumentsByExactSpan()
    {
        const string text = ",semaphore <<Old Name>>   -future=<<keep me>>\r\n";
        var document = Parse(text);
        var node = Assert.Single(document.SemanticNodes);
        var snapshot = QuestInspectorService.CreateIndex(document).Inspect(node.Id);

        Assert.Equal("Semaphore (Old Name)", snapshot.Title);
        Assert.Empty(snapshot.Summary);
        var name = Assert.Single(snapshot.Fields);
        Assert.Equal("Old Name", name.Value);
        Assert.True(name.IsWrapped);
        var unknown = Assert.Single(snapshot.UnknownArguments);
        Assert.Equal("-future=<<keep me>>", unknown.RawText);
        Assert.Equal("keep me", unknown.Value);
        Assert.Equal(unknown.RawText, Decode(document.Source, unknown.Span));
        Assert.Equal(unknown.Value, Decode(document.Source, unknown.ValueSpan));

        var mutation = QuestInspectorMutationService.SetFieldValue(
            document,
            node.Id,
            name.Definition.Id,
            "New Name");
        var result = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;

        Assert.Equal(
            ",semaphore <<New Name>>   -future=<<keep me>>\r\n",
            Decode(result));
        var reparsed = PxslParser.Parse(result, document.Catalog);
        Assert.Equal(
            "Semaphore (New Name)",
            QuestInspectorService.CreateIndex(reparsed)
                .GetCard(Assert.Single(reparsed.SemanticNodes).Id).Title);
    }

    [Fact]
    public void BlankPhaseNameDisplaysBlankAndCommitsAsPlaceholder()
    {
        var document = Parse(",semaphore _\n");
        var node = Assert.Single(document.SemanticNodes);
        var index = QuestInspectorService.CreateIndex(document);

        Assert.Equal("Semaphore", index.GetCard(node.Id).Title);
        var field = Assert.Single(index.Inspect(node.Id).Fields);
        Assert.Equal(string.Empty, field.Value);
        Assert.True(field.IsPlaceholder);

        var named = Apply(document, node.Id, field.Definition.Id, "Ready Signal");
        Assert.Equal(",semaphore <<Ready Signal>>\n", Decode(named.Source));
        var blanked = Apply(named, Assert.Single(named.SemanticNodes).Id,
            field.Definition.Id, string.Empty);
        Assert.Equal(",semaphore _\n", Decode(blanked.Source));
    }

    [Fact]
    public void TakeMutationsUsePairedPositionalsNamedValuesAndTextIdWrappers()
    {
        const string text =
            ",take <<Acquire Item>> Infected zombie -obj=&OBJ_OLD& -show=false -future=42\r\n";
        var document = Parse(text);
        var node = Assert.Single(document.SemanticNodes);
        var fields = QuestInspectorService.Inspect(document, node.Id).Fields
            .ToDictionary(field => field.Definition.ParameterName, StringComparer.OrdinalIgnoreCase);

        document = Apply(document, Assert.Single(document.SemanticNodes).Id,
            fields["class"].Definition.Id, "LootClass");
        document = Apply(document, Assert.Single(document.SemanticNodes).Id,
            fields["object"].Definition.Id, "loot_crate");
        document = Apply(document, Assert.Single(document.SemanticNodes).Id,
            fields["obj"].Definition.Id, "OBJ_NEW");
        document = Apply(document, Assert.Single(document.SemanticNodes).Id,
            fields["show"].Definition.Id, "1");

        Assert.Equal(
            ",take <<Acquire Item>> LootClass loot_crate -obj=&OBJ_NEW& -show=true -future=42\r\n",
            Decode(document.Source));
        var unknown = Assert.Single(QuestInspectorService.Inspect(
            document, Assert.Single(document.SemanticNodes).Id).UnknownArguments);
        Assert.Equal("-future=42", unknown.RawText);
    }

    [Fact]
    public void MissingPositionalsInsertPlaceholdersBeforeExistingNamedArguments()
    {
        var document = Parse(",interactive-movie Intro -future=unchanged\n");
        var node = Assert.Single(document.SemanticNodes);
        var scenario = Assert.Single(
            QuestInspectorService.Inspect(document, node.Id).Fields,
            field => field.Definition.ParameterName == "scenario_name");

        var result = Apply(document, node.Id, scenario.Definition.Id, "Scene One");

        Assert.Equal(
            ",interactive-movie Intro _ <<Scene One>> -future=unchanged\n",
            Decode(result.Source));
    }

    [Fact]
    public void MissingNamedTextIdIsInsertedWithoutReformattingTheLine()
    {
        var document = Parse(",take Pickup LootClass loot_crate   \r\n");
        var node = Assert.Single(document.SemanticNodes);
        var objective = Assert.Single(
            QuestInspectorService.Inspect(document, node.Id).Fields,
            field => field.Definition.ParameterName == "obj");

        var result = Apply(document, node.Id, objective.Definition.Id, "OBJECTIVE_NEW");

        Assert.Equal(
            ",take Pickup LootClass loot_crate -obj=&OBJECTIVE_NEW&   \r\n",
            Decode(result.Source));
    }

    [Fact]
    public void QuestNameMutationPreservesWrapperAndNamedArguments()
    {
        var document = Parse("quest <<Old Quest>> -final=true\r\n");
        var node = Assert.Single(document.SemanticNodes);
        var name = Assert.Single(
            QuestInspectorService.Inspect(document, node.Id).Fields,
            field => field.Definition.Role == QuestInspectorFieldRole.NodeName);

        var result = Apply(document, node.Id, name.Definition.Id, "New Quest");

        Assert.Equal("quest <<New Quest>> -final=true\r\n", Decode(result.Source));
    }

    [Fact]
    public void ABareQuestNameStaysBare()
    {
        // Only a phase name is forced into << >>. A quest identifier is a plain token.
        var document = Parse("quest Example -final=true\r\n");
        var node = Assert.Single(document.SemanticNodes);
        var name = Assert.Single(
            QuestInspectorService.Inspect(document, node.Id).Fields,
            field => field.Definition.Role == QuestInspectorFieldRole.NodeName);

        var result = Apply(document, node.Id, name.Definition.Id, "Renamed");

        Assert.Equal("quest Renamed -final=true\r\n", Decode(result.Source));
    }

    [Theory]
    // The reported defect: a single-word name used to commit bare, and DevTools rejects
    // ",AND sdsds" with "Invalid phase name! Should be \"_\" or \"<<name>>\"."
    [InlineData(",semaphore _\n", "sdsds", ",semaphore <<sdsds>>\n")]
    [InlineData(",semaphore _\n", "Ready Signal", ",semaphore <<Ready Signal>>\n")]
    [InlineData(",semaphore <<OLD>>\n", "sdsds", ",semaphore <<sdsds>>\n")]
    // Source already corrupted by the old behaviour repairs itself on the next edit.
    [InlineData(",semaphore sdsds\n", "other", ",semaphore <<other>>\n")]
    // Typing the brackets yourself must not double-wrap them.
    [InlineData(",semaphore _\n", "<<foo>>", ",semaphore <<foo>>\n")]
    [InlineData(",semaphore <<OLD>>\n", "<<foo>>", ",semaphore <<foo>>\n")]
    public void APhaseNameAlwaysCommitsInsideAngleBrackets(
        string text,
        string typed,
        string expected)
    {
        var document = Parse(text);
        var node = Assert.Single(document.SemanticNodes);
        var field = Assert.Single(QuestInspectorService.Inspect(document, node.Id).Fields);

        var result = Apply(document, node.Id, field.Definition.Id, typed);

        Assert.Equal(expected, Decode(result.Source));
    }

    [Fact]
    public void QuotesInsideAPhaseNameAreContentAndSurviveACommit()
    {
        // Both of these shapes appear in authored quests:
        //   ,radio-dialog << "What the hell happened here?" >> voice2d Player_Note
        const string text = ",semaphore << \"What the hell happened here?\" >>\n";
        var document = Parse(text);
        var node = Assert.Single(document.SemanticNodes);
        var field = Assert.Single(QuestInspectorService.Inspect(document, node.Id).Fields);

        // Committing what the inspector displays, unchanged, must not eat the quotes.
        var result = Apply(document, node.Id, field.Definition.Id, field.Value);

        Assert.Equal(",semaphore <<\"What the hell happened here?\">>\n", Decode(result.Source));
    }

    [Fact]
    public void ANameTypedWithQuotesIsWrappedRatherThanLeftQuoted()
    {
        var document = Parse(",semaphore _\n");
        var node = Assert.Single(document.SemanticNodes);
        var field = Assert.Single(QuestInspectorService.Inspect(document, node.Id).Fields);

        var result = Apply(document, node.Id, field.Definition.Id, "\"quoted\"");

        Assert.Equal(",semaphore <<\"quoted\">>\n", Decode(result.Source));
    }

    [Fact]
    public void LegacyEncodingMutationPreservesEncodingAndRejectsUnrepresentableText()
    {
        var encoding = QuestEncoding.Resolve("windows-1252");
        var source = new QuestSourceSnapshot(
            "quest-inspector-legacy",
            "quests/legacy.pxsl",
            7,
            encoding.GetBytes(",semaphore <<Caf\u00E9>>\r\n"),
            "windows-1252");
        var document = PxslParser.Parse(source, Catalog());
        var node = Assert.Single(document.SemanticNodes);
        var field = Assert.Single(QuestInspectorService.Inspect(document, node.Id).Fields);

        var result = Apply(document, node.Id, field.Definition.Id, "Ni\u00F1o");

        Assert.Equal("windows-1252", result.Source.EncodingName);
        Assert.Equal(QuestLineEnding.CrLf, result.Source.LineEnding);
        Assert.Equal(",semaphore <<Ni\u00F1o>>\r\n", Decode(result.Source));
        var exception = Assert.Throws<QuestInspectorException>(() =>
            QuestInspectorMutationService.SetFieldValue(
                result,
                Assert.Single(result.SemanticNodes).Id,
                field.Definition.Id,
                "\u6F22"));
        Assert.Equal(QuestInspectorRejection.UnrepresentableText, exception.Reason);
    }

    private static QuestGraphDocument Parse(string text) => PxslParser.Parse(
        new QuestSourceSnapshot(
            "quest-inspector",
            "quests/inspector.pxsl",
            1,
            Encoding.UTF8.GetBytes(text)),
        Catalog());

    private static QuestGraphDocument Apply(
        QuestGraphDocument document,
        string semanticNodeId,
        string fieldId,
        string? value)
    {
        var mutation = QuestInspectorMutationService.SetFieldValue(
            document,
            semanticNodeId,
            fieldId,
            value);
        var source = QuestSourceEditService.Apply(document.Source, mutation.Edits).Source;
        return PxslParser.Parse(source, document.Catalog);
    }

    private static string Decode(QuestSourceSnapshot source) =>
        QuestEncoding.Resolve(source.EncodingName).GetString(source.Bytes.Span);

    private static string Decode(QuestSourceSnapshot source, PxslTextSpan span) =>
        QuestEncoding.Resolve(source.EncodingName).GetString(
            source.Bytes.Span.Slice(span.Start, span.Length));

    private static QuestInspectorFieldDefinition AssertField(
        QuestCatalogEntry entry,
        string parameterName,
        QuestParameterEditorKind editorKind,
        QuestInspectorFieldRole role)
    {
        var field = Assert.Single(entry.InspectorFields,
            field => field.ParameterName.Equals(parameterName, StringComparison.OrdinalIgnoreCase));
        Assert.Equal(editorKind, field.EditorKind);
        Assert.Equal(role, field.Role);
        return field;
    }

    private static QuestCatalogEntry Entry(QuestCatalog catalog, string name) =>
        Assert.Single(catalog.Entries, entry =>
            entry.Kind == QuestCatalogEntryKind.Macro &&
            entry.Name.Equals(name, StringComparison.OrdinalIgnoreCase));

    private static QuestCatalog Catalog()
    {
        const string edf = """
            ### Forbidden ###
            ,item class object =
              Item -class=<(,class)> -name=<(,object)>

            ,scenario class scenario_name =
              Scenario -class=<(,class)> -name=<(,scenario_name)>

            ### SimplePhase ###
            ,take-multi name obj enc exp show show_list show_items announce_objective questgiver_indicator =
              phase -type=take -name=<(,name)> -objective=<(,obj)> -encounter=<(,enc)> -show=<(,show)>

            ,semaphore name =
              phase -type=semaphore -name=<(,name)>

            ,interactive-movie name class scenario_name =
              phase -type=<<interactive movie>> -name=<(,name)>
                ,scenario <(,class)> <(,scenario_name)>

            ,take name class object obj enc exp show show_list show_items announce_objective questgiver_indicator =
              ,take-multi <(,name)> <(,obj)> <(,enc)> <(,exp)> <(,show)> <(,show_list)> <(,show_items)> <(,announce_objective)> <(,questgiver_indicator)>
                ,item <(,class)> <(,object)>
            """;
        const string config = """
            Descriptions()
                Desc(",take", ",take [phase_name] [object_class] [object_name] -obj=[TEXT_ID] -enc=[true|false] -exp=[true|false] -show=[true|false] -show_list=[true|false] -show_items=[true|false] -announce_objective=[true|false] -questgiver_indicator=[true|false]", "")
                Desc("quest", "quest [quest_name] -final=[true|false]", "")
            """;
        const string phases = """
            <PhaseTypeDefinitions>
              <PhaseType name="semaphore"/>
              <PhaseType name="interactive movie"/>
              <PhaseType name="take">
                <attribute name="objective" type="string" optional="true"/>
                <attribute name="encounter" type="bool" optional="true"/>
                <attribute name="show" type="bool" optional="true"/>
              </PhaseType>
            </PhaseTypeDefinitions>
            """;
        const string objects = """
            <QuestObjectDefinitions>
              <QuestObject name="Item"><class name="SyntheticItemClass"/></QuestObject>
              <QuestObject name="Scenario"><class name="SyntheticScenarioClass"/></QuestObject>
            </QuestObjectDefinitions>
            """;

        var definitions = new[]
        {
            ("quest-macros-public.edf", QuestCatalogFormat.Edf, edf),
            ("QuestEditorConfig.scr", QuestCatalogFormat.QuestEditorConfig, config),
            ("phases.xml", QuestCatalogFormat.PhaseDefinitionsXml, phases),
            ("objects.xml", QuestCatalogFormat.QuestObjectDefinitionsXml, objects)
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
}
