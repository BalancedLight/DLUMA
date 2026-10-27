using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestInspectorFieldViewModelTests
{
    [Fact]
    public async Task TextCommitPublishesOnlyAfterAdmission()
    {
        var admitted = new List<string>();
        var field = CreateField(
            QuestParameterEditorKind.Text,
            "before",
            (_, value) =>
            {
                admitted.Add(value);
                return Task.FromResult(value != "rejected");
            });

        field.DraftValue = "rejected";
        Assert.True(field.HasPendingValue);
        Assert.False(await field.CommitAsync());
        Assert.Equal("before", field.DraftValue);

        field.DraftValue = "after";
        Assert.True(await field.CommitAsync());
        Assert.Equal(["rejected", "after"], admitted);
        Assert.False(field.HasPendingValue);
    }

    [Fact]
    public void PlaceholderIsBlankAndOptionalBooleanIsThreeState()
    {
        var field = new QuestInspectorFieldViewModel(
            "wait",
            "Wait",
            QuestParameterEditorKind.Boolean,
            false,
            "_",
            Array.Empty<string>(),
            null,
            true,
            (_, _) => Task.FromResult(true));

        Assert.Equal(string.Empty, field.DraftValue);
        Assert.Null(field.BooleanValue);
        Assert.True(field.IsThreeStateBoolean);
    }

    [Fact]
    public void PickerOptionsAreSortedAndDeduplicatedWithoutBlockingFreeForm()
    {
        var field = new QuestInspectorFieldViewModel(
            "object",
            "Object",
            QuestParameterEditorKind.MapObjectName,
            true,
            "custom_object",
            ["zebra", "Alpha", "alpha", ""],
            null,
            true,
            (_, _) => Task.FromResult(true));

        Assert.Equal(["Alpha", "zebra"], field.Options);
        Assert.Equal("custom_object", field.DraftValue);
        Assert.True(field.IsPickerEditor);
    }

    private static QuestInspectorFieldViewModel CreateField(
        QuestParameterEditorKind kind,
        string value,
        Func<QuestInspectorFieldViewModel, string, Task<bool>> commit) =>
        new(
            "synthetic",
            "Synthetic",
            kind,
            true,
            value,
            Array.Empty<string>(),
            null,
            true,
            commit);
}
