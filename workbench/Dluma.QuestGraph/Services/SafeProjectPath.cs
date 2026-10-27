namespace Dluma.QuestGraph.Services;

public static class SafeProjectPath
{
    public static void RequireContainedNonReparsePath(
        string projectRoot,
        string candidatePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(candidatePath);
        var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(projectRoot));
        var candidate = Path.GetFullPath(candidatePath);
        var rootPrefix = Path.EndsInDirectorySeparator(root)
            ? root
            : root + Path.DirectorySeparatorChar;
        if (!candidate.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Connected quest path escapes the active project root.");
        if (!Directory.Exists(root))
            throw new DirectoryNotFoundException("The active quest project root does not exist.");
        if ((File.GetAttributes(root) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("The active project root cannot be a reparse point.");

        var relative = Path.GetRelativePath(root, candidate);
        var current = root;
        foreach (var component in relative.Split(
            new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar },
            StringSplitOptions.RemoveEmptyEntries))
        {
            current = Path.Combine(current, component);
            if (!File.Exists(current) && !Directory.Exists(current))
                continue;
            if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
            {
                throw new InvalidDataException(
                    "Connected quest paths cannot traverse filesystem reparse points.");
            }
        }
    }
}
