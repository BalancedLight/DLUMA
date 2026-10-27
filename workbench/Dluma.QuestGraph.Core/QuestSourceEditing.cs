namespace Dluma.QuestGraph.Core;

public static class QuestSourceEditService
{
    public static QuestEditResult Apply(
        QuestSourceSnapshot source,
        IEnumerable<QuestTextEdit> edits)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(edits);

        var ordered = edits.OrderBy(edit => edit.ByteOffset).ToArray();
        foreach (var edit in ordered)
        {
            if (edit.ExpectedRevision != source.Revision)
            {
                throw new QuestEditRejectedException(
                    QuestEditRejection.StaleRevision,
                    $"Edit revision {edit.ExpectedRevision} does not match source revision {source.Revision}.");
            }
            if (!string.Equals(edit.ExpectedSha256, source.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new QuestEditRejectedException(
                    QuestEditRejection.StaleHash,
                    "Edit source hash does not match the current source snapshot.");
            }
            if (edit.ByteOffset < 0 || edit.RemovedLength < 0 ||
                edit.ByteOffset > source.Bytes.Length - edit.RemovedLength)
            {
                throw new QuestEditRejectedException(
                    QuestEditRejection.InvalidRange,
                    "Edit byte range is outside the current source snapshot.");
            }
        }

        for (var index = 1; index < ordered.Length; ++index)
        {
            if (ordered[index - 1].ByteOffset + ordered[index - 1].RemovedLength >
                ordered[index].ByteOffset)
            {
                throw new QuestEditRejectedException(
                    QuestEditRejection.OverlappingRanges,
                    "Quest text edits must not overlap.");
            }
        }

        var bytes = source.ToArray();
        foreach (var edit in ordered.Reverse())
        {
            var replacement = edit.ReplacementBytes.ToArray();
            var updated = new byte[checked(bytes.Length - edit.RemovedLength + replacement.Length)];
            Buffer.BlockCopy(bytes, 0, updated, 0, edit.ByteOffset);
            replacement.CopyTo(updated, edit.ByteOffset);
            var suffixOffset = edit.ByteOffset + edit.RemovedLength;
            Buffer.BlockCopy(
                bytes,
                suffixOffset,
                updated,
                edit.ByteOffset + replacement.Length,
                bytes.Length - suffixOffset);
            bytes = updated;
        }

        return new QuestEditResult(source.Next(bytes), ordered);
    }
}
