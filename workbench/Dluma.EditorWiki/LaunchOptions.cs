using System.Globalization;

namespace Dluma.EditorWiki;

internal sealed record LaunchOptions(
    string Pipe, string Token, int EditorPid, string EditorSha256, long EditorStartTime,
    long PanelParent, ulong PanelToken, string ThemeConfig)
{
    internal static LaunchOptions Parse(string[] args)
    {
        if (args.Length != 16) throw new ArgumentException("The Editor panel launch is incomplete.");
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            if (string.IsNullOrWhiteSpace(args[i]) || string.IsNullOrWhiteSpace(args[i + 1]) ||
                !values.TryAdd(args[i], args[i + 1]))
                throw new ArgumentException("The Editor panel launch is invalid.");
        }
        string[] keys = ["--pipe", "--token", "--editor-pid", "--editor-sha256", "--editor-start-time",
            "--panel-parent", "--panel-token", "--theme-config"];
        if (keys.Any(key => !values.ContainsKey(key)))
            throw new ArgumentException("The Editor panel launch is incomplete.");
        var hash = values["--editor-sha256"];
        if (hash.Length != 64 || !hash.All(Uri.IsHexDigit) ||
            !int.TryParse(values["--editor-pid"], NumberStyles.None, CultureInfo.InvariantCulture, out var pid) || pid <= 0 ||
            !long.TryParse(values["--editor-start-time"], NumberStyles.None, CultureInfo.InvariantCulture, out var start) || start <= 0 ||
            !long.TryParse(values["--panel-parent"], NumberStyles.None, CultureInfo.InvariantCulture, out var parent) || parent <= 0 ||
            !ulong.TryParse(values["--panel-token"], NumberStyles.None, CultureInfo.InvariantCulture, out var token) || token == 0 ||
            !Path.IsPathFullyQualified(values["--theme-config"]))
            throw new ArgumentException("The Editor panel launch is invalid.");
        return new(values["--pipe"], values["--token"], pid, hash.ToUpperInvariant(), start, parent, token,
            Path.GetFullPath(values["--theme-config"]));
    }
}
