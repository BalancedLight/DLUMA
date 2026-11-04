using System.Globalization;
using HostOptions = Dluma.EditorWiki.LaunchOptions;

namespace Dluma.EditorGit;

internal sealed record GitLaunchOptions(HostOptions Host, string ProjectRoot, ulong ProjectGeneration)
{
    internal static GitLaunchOptions Parse(string[] args)
    {
        if (args.Length != 20) throw new ArgumentException("The Git panel launch is incomplete.");
        var hostArgs = new List<string>();
        string? project = null;
        ulong generation = 0;
        var keys = new HashSet<string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            if (!keys.Add(args[i])) throw new ArgumentException("The Git panel launch is invalid.");
            if (args[i] == "--project-root") project = args[i + 1];
            else if (args[i] == "--project-generation")
            {
                if (!ulong.TryParse(args[i + 1], NumberStyles.None, CultureInfo.InvariantCulture, out generation) || generation == 0)
                    throw new ArgumentException("The Editor project is unavailable.");
            }
            else { hostArgs.Add(args[i]); hostArgs.Add(args[i + 1]); }
        }
        if (project is null || !Path.IsPathFullyQualified(project) || !Directory.Exists(project) || generation == 0)
            throw new ArgumentException("The Editor project is unavailable.");
        return new(HostOptions.Parse(hostArgs.ToArray()), Path.GetFullPath(project), generation);
    }
}
