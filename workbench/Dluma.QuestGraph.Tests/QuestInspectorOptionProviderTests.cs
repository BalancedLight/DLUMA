using System.Collections.ObjectModel;
using System.Reflection;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInspectorOptionProviderTests
{
    [Fact]
    public void MapObjectsAreFilteredByTheSelectedStockClass()
    {
        var model = CreateMainViewModel();
        model.ContextObjects.Add(new QuestContextObject("gate_a", "Door", Array.Empty<string>()));
        model.ContextObjects.Add(new QuestContextObject("gate_b", "Door", Array.Empty<string>()));
        model.ContextObjects.Add(new QuestContextObject("switch_a", "Switch", Array.Empty<string>()));

        var options = model.ResolveInspectorOptions(
            QuestParameterEditorKind.MapObjectName,
            "Door");

        Assert.Equal(["gate_a", "gate_b"], options);
    }

    [Fact]
    public void ContextOptionsAreCaseInsensitiveSortedAndDoNotRemoveFreeFormInput()
    {
        var model = CreateMainViewModel();
        model.Speakers.Add("Zara");
        model.Speakers.Add("alex");
        model.Speakers.Add("Alex");

        Assert.Equal(
            ["alex", "Zara"],
            model.ResolveInspectorOptions(QuestParameterEditorKind.Speaker, null));
    }

    [Fact]
    public void ScenarioOptionsPreferInteractiveMovieClassesAndHonorPairedClass()
    {
        var model = CreateMainViewModel();
        model.ContextObjects.Add(new QuestContextObject(
            "scenario_primary",
            "InteractiveMovieScenario",
            Array.Empty<string>()));
        model.ContextObjects.Add(new QuestContextObject(
            "scenario_variant",
            "CustomInteractiveMovieScenarioVariant",
            Array.Empty<string>()));
        model.ContextObjects.Add(new QuestContextObject(
            "scenario_custom",
            "ProjectScenario",
            Array.Empty<string>()));
        model.ContextObjects.Add(new QuestContextObject(
            "unrelated",
            "Door",
            Array.Empty<string>()));

        Assert.Equal(
            ["scenario_primary", "scenario_variant"],
            model.ResolveInspectorOptions(QuestParameterEditorKind.Scenario, null));
        Assert.Equal(
            ["scenario_custom"],
            model.ResolveInspectorOptions(
                QuestParameterEditorKind.Scenario,
                "ProjectScenario"));
    }

    private static MainViewModel CreateMainViewModel()
    {
        var constructor = typeof(MainViewModel).GetConstructors(
                BindingFlags.Instance | BindingFlags.Public)
            .Single();
        return (MainViewModel)constructor.Invoke(
        [
            new QuestDocumentService(),
            new NullFileDialogs(),
            new QuestBridgeClient(),
            new LaunchOptions(null, null, null, Array.Empty<string>(), false)
        ]);
    }

    private sealed class NullFileDialogs : IFileDialogService
    {
        public string? SelectQuestToOpen() => null;
        public string? SelectQuestSavePath(string suggestedName) => null;
        public string? SelectCatalogRoot() => null;
    }
}
