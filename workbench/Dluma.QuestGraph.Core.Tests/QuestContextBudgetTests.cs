using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestContextBudgetTests
{
    [Fact]
    public void SharedBudgetCountsObjectsRolesAndEveryOtherCategory()
    {
        var snapshot = new QuestContextSnapshot(
            7,
            "synthetic-project",
            "synthetic-map",
            new[]
            {
                new QuestContextObject("object-b", "ClassB", new[] { "role-2", "role-1" }),
                new QuestContextObject("object-a", "ClassA", new[] { "role-3" })
            },
            new[]
            {
                new QuestContextText("text-b", "B"),
                new QuestContextText("text-a", "A")
            },
            new[] { "speaker-b", "speaker-a" },
            new[] { "dialogue-b", "dialogue-a" },
            new[] { "quest-b", "quest-a" });

        var bounded = QuestContextBudget.Apply(snapshot, maximumItems: 8);

        Assert.Equal(8, CountItems(bounded));
        Assert.Equal(new[] { "object-a", "object-b" }, bounded.Objects.Select(item => item.Name));
        Assert.Equal(new[] { "role-1", "role-2" }, bounded.Objects[1].Roles);
        Assert.Equal(new[] { "text-a", "text-b" }, bounded.Texts.Select(item => item.Id));
        Assert.Equal("speaker-a", Assert.Single(bounded.Speakers));
        Assert.Empty(bounded.DialogueGroups);
        Assert.Empty(bounded.QuestReferences);
    }

    [Fact]
    public void DuplicateKeysUseLastValueBeforeDeterministicSorting()
    {
        var snapshot = new QuestContextSnapshot(
            1,
            "synthetic-project",
            string.Empty,
            new[]
            {
                new QuestContextObject("same", "PersistedClass", new[] { "persisted" }),
                new QuestContextObject("other", "OtherClass", Array.Empty<string>()),
                new QuestContextObject("SAME", "LiveClass", new[] { "live", "LIVE" })
            },
            new[]
            {
                new QuestContextText("same", "persisted"),
                new QuestContextText("SAME", "live")
            },
            Array.Empty<string>(),
            Array.Empty<string>(),
            Array.Empty<string>());

        var bounded = QuestContextBudget.Apply(snapshot, maximumItems: 10);
        var sameObject = Assert.Single(bounded.Objects,
            item => item.Name.Equals("same", StringComparison.OrdinalIgnoreCase));
        var sameText = Assert.Single(bounded.Texts,
            item => item.Id.Equals("same", StringComparison.OrdinalIgnoreCase));

        Assert.Equal("LiveClass", sameObject.ClassName);
        Assert.Equal("live", Assert.Single(sameObject.Roles));
        Assert.Equal("live", sameText.Value);
        Assert.Equal(new[] { "other", "SAME" }, bounded.Objects.Select(item => item.Name));
    }

    [Fact]
    public void InvalidBudgetIsRejected()
    {
        var snapshot = new QuestContextSnapshot(
            0,
            "synthetic-project",
            string.Empty,
            Array.Empty<QuestContextObject>(),
            Array.Empty<QuestContextText>(),
            Array.Empty<string>(),
            Array.Empty<string>(),
            Array.Empty<string>());

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            QuestContextBudget.Apply(snapshot, 0));
    }

    private static int CountItems(QuestContextSnapshot snapshot) =>
        snapshot.Objects.Count +
        snapshot.Objects.Sum(item => item.Roles.Count) +
        snapshot.Texts.Count +
        snapshot.Speakers.Count +
        snapshot.DialogueGroups.Count +
        snapshot.QuestReferences.Count;
}
