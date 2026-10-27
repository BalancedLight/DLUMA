namespace Dluma.QuestGraph.Services;

public static partial class QuestDiskContextProvider
{
    private static async Task<byte[]> ReadCandidateBoundedAsync(
        FileInfo file,
        long remainingAggregateBytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(file);
        if (remainingAggregateBytes <= 0)
            throw new InvalidDataException("The context aggregate byte limit was reached.");
        var maximum = Math.Min((long)MaximumCandidateBytes, remainingAggregateBytes);
        await using var stream = new FileStream(
            file.FullName,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            81920,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        var length = stream.Length;
        if (length <= 0 || length > maximum || length > int.MaxValue)
            throw new InvalidDataException("A context candidate exceeds its post-open byte limit.");

        var bytes = new byte[checked((int)length)];
        var offset = 0;
        while (offset < bytes.Length)
        {
            var read = await stream.ReadAsync(bytes.AsMemory(offset), cancellationToken)
                .ConfigureAwait(false);
            if (read == 0)
                throw new EndOfStreamException("A context candidate shrank while it was being read.");
            offset += read;
        }

        var probe = new byte[1];
        if (await stream.ReadAsync(probe, cancellationToken).ConfigureAwait(false) != 0 ||
            stream.Length != length)
            throw new InvalidDataException("A context candidate changed length while it was being read.");
        return bytes;
    }
}
