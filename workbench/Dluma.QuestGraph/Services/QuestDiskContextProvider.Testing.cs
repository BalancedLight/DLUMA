namespace Dluma.QuestGraph.Services;

public static partial class QuestDiskContextProvider
{
    internal static Task<byte[]> ReadCandidateForTestsAsync(
        FileInfo file,
        long remainingAggregateBytes,
        CancellationToken cancellationToken = default) =>
        ReadCandidateBoundedAsync(file, remainingAggregateBytes, cancellationToken);
}
