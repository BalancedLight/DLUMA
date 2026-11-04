using System.Security.Cryptography;

namespace Dluma.EditorGit.Core;

internal static class GitStatusParser
{
    internal static GitStatus Parse(byte[] bytes)
    {
        var text = GitProcess.Text(bytes);
        if (text.Length > 0 && text[^1] != '\0') throw new GitException("Git returned an incomplete status.");
        var records = text.Split('\0');
        string? branch = null, upstream = null;
        var detached = false; var unborn = false; var ahead = 0; var behind = 0;
        var sawOid = false; var sawHead = false;
        var changes = new List<GitChange>();
        for (var i = 0; i < records.Length; i++)
        {
            var record = records[i];
            if (record.Length == 0)
            {
                if (i != records.Length - 1) throw new GitException("Git returned an invalid status record.");
                continue;
            }
            if (record.StartsWith("# branch.oid ", StringComparison.Ordinal))
            {
                var oid = record[13..];
                if (sawOid || (oid != "(initial)" && (oid.Length is not (40 or 64) || !oid.All(Uri.IsHexDigit))))
                    throw new GitException("Git returned an invalid branch status.");
                sawOid = true; unborn = oid == "(initial)";
            }
            else if (record.StartsWith("# branch.head ", StringComparison.Ordinal))
            {
                branch = record[14..];
                if (sawHead || branch.Length == 0) throw new GitException("Git returned an invalid branch status.");
                sawHead = true; detached = branch == "(detached)";
                if (detached) branch = null;
            }
            else if (record.StartsWith("# branch.upstream ", StringComparison.Ordinal)) upstream = record[18..];
            else if (record.StartsWith("# branch.ab ", StringComparison.Ordinal))
            {
                var parts = record[12..].Split(' ');
                if (parts.Length != 2 || parts[0].Length < 2 || parts[1].Length < 2 || parts[0][0] != '+' || parts[1][0] != '-' ||
                    !parts[0][1..].All(char.IsAsciiDigit) || !parts[1][1..].All(char.IsAsciiDigit) ||
                    !int.TryParse(parts[0][1..], out ahead) || !int.TryParse(parts[1][1..], out behind))
                    throw new GitException("Git returned an invalid branch status.");
            }
            else if (record.StartsWith("# ", StringComparison.Ordinal)) continue;
            else if (record.StartsWith("? ", StringComparison.Ordinal) && record.Length > 2) changes.Add(new(record[2..], null, '.', '?', true, false));
            else if (record.StartsWith("1 ", StringComparison.Ordinal) || record.StartsWith("2 ", StringComparison.Ordinal) || record.StartsWith("u ", StringComparison.Ordinal))
            {
                var kind = record[0];
                var fields = record.Split(' ', kind == '1' ? 9 : kind == '2' ? 10 : 11);
                var expected = kind == '1' ? 9 : kind == '2' ? 10 : 11;
                if (fields.Length != expected || fields.Any(string.IsNullOrEmpty) || fields[1].Length != 2 ||
                    fields[1].Any(value => !".MADRCUT".Contains(value)))
                    throw new GitException("Git returned an invalid file status.");
                string? original = null;
                if (kind == '2')
                {
                    if (++i >= records.Length || records[i].Length == 0) throw new GitException("Git returned an incomplete rename.");
                    original = records[i];
                }
                changes.Add(new(fields[^1], original, fields[1][0], fields[1][1], false, kind == 'u'));
            }
            else throw new GitException("Git returned an unsupported status record.");
        }
        if (!sawOid || !sawHead) throw new GitException("Git returned an incomplete branch status.");
        return new(branch, upstream, detached, unborn, ahead, behind, changes, Convert.ToHexString(SHA256.HashData(bytes)));
    }
}
