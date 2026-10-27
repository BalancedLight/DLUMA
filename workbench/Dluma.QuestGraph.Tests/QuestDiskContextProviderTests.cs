using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDiskContextProviderTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-quest-context-{Guid.NewGuid():N}");

    [Fact]
    public async Task CandidateReaderChecksActualOpenLengthAndRemainingBudget()
    {
        Directory.CreateDirectory(_temporaryRoot);
        var path = Path.Combine(_temporaryRoot, "context.xml");
        var expected = Enumerable.Range(0, 128).Select(value => (byte)value).ToArray();
        await File.WriteAllBytesAsync(path, expected);
        var information = new FileInfo(path);

        var result = await QuestDiskContextProvider.ReadCandidateForTestsAsync(
            information,
            expected.Length);
        var exception = await Assert.ThrowsAsync<InvalidDataException>(() =>
            QuestDiskContextProvider.ReadCandidateForTestsAsync(
                information,
                expected.Length - 1));

        Assert.Equal(expected, result);
        Assert.Contains("post-open byte limit", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task CandidateReaderRejectsPerFileOverflowBeforeAllocation()
    {
        Directory.CreateDirectory(_temporaryRoot);
        var path = Path.Combine(_temporaryRoot, "oversized.xml");
        await using (var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            stream.SetLength(4L * 1024 * 1024 + 1);

        await Assert.ThrowsAsync<InvalidDataException>(() =>
            QuestDiskContextProvider.ReadCandidateForTestsAsync(
                new FileInfo(path),
                64L * 1024 * 1024));
    }

    [Fact]
    public void MergeUsesLivePrecedenceAndOneSharedBudget()
    {
        var diskSpeakers = Enumerable.Range(0, 60_000)
            .Select(index => $"disk-{index:D5}")
            .ToArray();
        var liveSpeakers = Enumerable.Range(0, 60_000)
            .Select(index => $"live-{index:D5}")
            .ToArray();
        var disk = Snapshot(
            new QuestContextObject("same", "PersistedClass", new[] { "persisted" }),
            diskSpeakers);
        var live = Snapshot(
            new QuestContextObject("SAME", "LiveClass", new[] { "live" }),
            liveSpeakers);

        var merged = QuestDiskContextProvider.Merge(disk, live);
        var total = merged.Objects.Count +
            merged.Objects.Sum(item => item.Roles.Count) +
            merged.Texts.Count +
            merged.Speakers.Count +
            merged.DialogueGroups.Count +
            merged.QuestReferences.Count;
        var same = Assert.Single(merged.Objects);

        Assert.Equal("LiveClass", same.ClassName);
        Assert.Equal("live", Assert.Single(same.Roles));
        Assert.Equal(QuestBridgeProtocol.MaximumCollectionItems, total);
        Assert.Equal(QuestBridgeProtocol.MaximumCollectionItems - 2, merged.Speakers.Count);
        QuestBridgePayloadValidator.Validate(new ContextSnapshotMessage(merged));
    }

    private static QuestContextSnapshot Snapshot(
        QuestContextObject contextObject,
        IReadOnlyList<string> speakers) => new(
        1,
        "synthetic-project",
        "synthetic-map",
        new[] { contextObject },
        Array.Empty<QuestContextText>(),
        speakers,
        Array.Empty<string>(),
        Array.Empty<string>());

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
