using System.Globalization;

namespace Dluma.QuestGraph;

public sealed record LaunchOptions(
    string? PipeName,
    string? SessionToken,
    int? EditorProcessId,
    IReadOnlyList<string> Files,
    bool SmokeTest)
{
    private const int MaximumArguments = 1_024;
    private const int MaximumCommandLineCharacters = 32_767;

    public bool IsConnectedLaunch =>
        !string.IsNullOrWhiteSpace(PipeName) &&
        !string.IsNullOrWhiteSpace(SessionToken) &&
        EditorProcessId is > 0;

    public static LaunchOptions Parse(IReadOnlyList<string> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        if (arguments.Count > MaximumArguments)
            throw new ArgumentException(
                $"The launch accepts at most {MaximumArguments:N0} arguments.",
                nameof(arguments));

        long totalCharacters = 0;
        foreach (var argument in arguments)
        {
            if (string.IsNullOrWhiteSpace(argument))
                throw new ArgumentException(
                    "Launch arguments cannot be null or blank.",
                    nameof(arguments));
            totalCharacters += argument.Length;
            if (totalCharacters > MaximumCommandLineCharacters)
                throw new ArgumentException(
                    $"The launch arguments exceed {MaximumCommandLineCharacters:N0} characters.",
                    nameof(arguments));
        }

        string? pipeName = null;
        string? token = null;
        int? editorProcessId = null;
        var files = new List<string>();
        var smokeTest = false;

        for (var index = 0; index < arguments.Count; ++index)
        {
            var argument = arguments[index];
            if (string.Equals(argument, "--smoke-test", StringComparison.Ordinal))
            {
                if (smokeTest)
                    throw new ArgumentException("--smoke-test cannot be specified more than once.");
                smokeTest = true;
                continue;
            }
            if (TryReadValue(arguments, ref index, "--pipe", out var pipe))
            {
                if (pipeName is not null)
                    throw new ArgumentException("--pipe cannot be specified more than once.");
                pipeName = pipe;
                continue;
            }
            if (TryReadValue(arguments, ref index, "--session-token", out var sessionToken))
            {
                if (token is not null)
                    throw new ArgumentException("--session-token cannot be specified more than once.");
                token = sessionToken;
                continue;
            }
            if (TryReadValue(arguments, ref index, "--editor-pid", out var pidText))
            {
                if (editorProcessId is not null)
                    throw new ArgumentException("--editor-pid cannot be specified more than once.");
                if (!int.TryParse(
                        pidText,
                        NumberStyles.None,
                        CultureInfo.InvariantCulture,
                        out var parsedPid) || parsedPid <= 0)
                    throw new ArgumentException("--editor-pid must be a positive integer.");
                editorProcessId = parsedPid;
                continue;
            }
            if (TryReadValue(arguments, ref index, "--open", out var file))
            {
                files.Add(file);
                continue;
            }
            if (argument.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Unknown option '{argument}'.");
            files.Add(argument);
        }

        var connectedValueCount = (pipeName is null ? 0 : 1) +
            (token is null ? 0 : 1) +
            (editorProcessId is null ? 0 : 1);
        if (connectedValueCount is > 0 and < 3)
            throw new ArgumentException(
                "--pipe, --session-token, and --editor-pid must be supplied together.");
        if (smokeTest && (connectedValueCount > 0 || files.Count > 0))
            throw new ArgumentException("--smoke-test must be used by itself.");

        return new LaunchOptions(pipeName, token, editorProcessId, files, smokeTest);
    }

    private static bool TryReadValue(
        IReadOnlyList<string> arguments,
        ref int index,
        string option,
        out string value)
    {
        if (!string.Equals(arguments[index], option, StringComparison.Ordinal))
        {
            value = string.Empty;
            return false;
        }
        if (index + 1 >= arguments.Count ||
            arguments[index + 1].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException($"{option} requires a value.");
        value = arguments[++index];
        return true;
    }
}
