using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Dluma.QuestGraph.Protocol;

public abstract record EditorGitPayload;

public sealed record GitContextMessage(
    string ProjectRoot, ulong ProjectGeneration,
    [property: JsonRequired] bool Enabled,
    [property: JsonRequired] bool MutationsAvailable,
    [property: JsonRequired] bool WorktreeAvailable,
    string Message, bool LocalWritesOnly = false, bool BranchCreateAvailable = false,
    bool? DiagnosticFixtureOnly = null) : EditorGitPayload;

public sealed record GitStateRequest(string ProjectRoot, ulong ProjectGeneration,
    string? GitExecutable = null) : EditorGitPayload;

public sealed record GitProjectAcquireRequest(string ProjectRoot, ulong ProjectGeneration,
    string Operation, string? Destination = null, string? ArmId = null) : EditorGitPayload;

public sealed record GitProjectCancelRequest(string ProjectRoot, ulong ProjectGeneration,
    string ArmId) : EditorGitPayload;

public sealed record GitWorktreeAcquireRequest(string ProjectRoot, ulong ProjectGeneration,
    string Operation, string ProjectLeaseId) : EditorGitPayload;

public sealed record GitChildStartedRequest(string? LeaseId, int ProcessId,
    ulong CreationTime, long ProcessHandle,
    [property: JsonRequired] bool Mutating) : EditorGitPayload;

public sealed record GitChildStartedResult([property: JsonRequired] bool Registered,
    [property: JsonRequired] bool RequiresRecovery, string Code, string Message) : EditorGitPayload;

public sealed record GitLeaseReleaseRequest(string LeaseId) : EditorGitPayload;

public sealed record GitLeaseResult([property: JsonRequired] bool Granted,
    string? LeaseId, string Code, string Message, string? ArmId = null) : EditorGitPayload;

public sealed record GitArmCancelResult([property: JsonRequired] bool Cancelled,
    [property: JsonRequired] bool RequiresRecovery, string Code, string Message) : EditorGitPayload;

public sealed record GitLeaseReleaseResult([property: JsonRequired] bool Released,
    [property: JsonRequired] bool RequiresRecovery, string Code, string Message) : EditorGitPayload;

public sealed record GitCommandMessage(string Command,
    IReadOnlyList<string>? Paths = null) : EditorGitPayload;

public static class EditorGitProtocol
{
    public const int MaximumPayloadBytes = 64 * 1024;
    public const int MaximumSelectedPaths = 256;
    public const int MaximumPathCharacters = 32_768;
    private static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    private static readonly HashSet<string> Operations = new(StringComparer.Ordinal)
    {
        "initialize", "stage", "unstage", "commit", "createBranch", "switchBranch",
        "fetch", "pull", "publishBranch", "push", "addRemote", "clone"
    };
    private static readonly HashSet<string> Commands = new(Operations, StringComparer.Ordinal)
    {
        "open", "refresh", "history", "settings"
    };

    public static bool IsOperation(string? operation) => operation is not null && Operations.Contains(operation);

    public static void Validate(EditorGitPayload payload)
    {
        ArgumentNullException.ThrowIfNull(payload);
        switch (payload)
        {
            case GitContextMessage value:
                Project(value.ProjectRoot, value.ProjectGeneration);
                Text(value.Message, 1024, true);
                if ((value.MutationsAvailable && !value.Enabled) ||
                    (value.WorktreeAvailable && !value.MutationsAvailable) ||
                    (value.LocalWritesOnly && (!value.MutationsAvailable || value.WorktreeAvailable)) ||
                    (value.BranchCreateAvailable && (!value.MutationsAvailable || !value.LocalWritesOnly || value.DiagnosticFixtureOnly != false)) ||
                    (value.DiagnosticFixtureOnly == true && (!value.LocalWritesOnly || value.BranchCreateAvailable)))
                    throw new InvalidDataException("Invalid Git admission state.");
                break;
            case GitStateRequest value:
                Project(value.ProjectRoot, value.ProjectGeneration);
                if (value.GitExecutable is not null) AbsolutePath(value.GitExecutable);
                break;
            case GitProjectAcquireRequest value:
                Project(value.ProjectRoot, value.ProjectGeneration);
                if (!IsOperation(value.Operation)) throw new InvalidDataException("Unknown Git operation.");
                if (value.Operation == "clone") AbsolutePath(value.Destination);
                else if (value.Destination is not null) throw new InvalidDataException("Unexpected Git destination.");
                if (value.ArmId is not null) { Lease(value.ArmId); if (value.Operation == "clone") throw new InvalidDataException("Clone cannot reuse an arm."); }
                break;
            case GitProjectCancelRequest value:
                Project(value.ProjectRoot, value.ProjectGeneration);
                Lease(value.ArmId);
                break;
            case GitWorktreeAcquireRequest value:
                Project(value.ProjectRoot, value.ProjectGeneration);
                Lease(value.ProjectLeaseId);
                if (value.Operation is not ("pull" or "switchBranch"))
                    throw new InvalidDataException("Invalid worktree operation.");
                break;
            case GitChildStartedRequest value:
                if (value.LeaseId is not null) Lease(value.LeaseId);
                if ((value.Mutating && value.LeaseId is null) || value.ProcessId <= 0 ||
                    value.CreationTime == 0 || value.ProcessHandle <= 0)
                    throw new InvalidDataException("Invalid Git child identity.");
                break;
            case GitChildStartedResult value:
                Code(value.Code); Text(value.Message, 1024, true);
                if (value.Registered && (value.RequiresRecovery || value.Code != "ok"))
                    throw new InvalidDataException("Invalid Git child registration.");
                break;
            case GitLeaseReleaseRequest value:
                Lease(value.LeaseId);
                break;
            case GitLeaseResult value:
                Code(value.Code); Text(value.Message, 1024, true);
                if (value.Granted) {
                    Lease(value.LeaseId);
                    if (value.Code != "ok" || value.ArmId is not null) throw new InvalidDataException("Invalid lease result.");
                } else {
                    if (value.LeaseId is not null) throw new InvalidDataException("Unexpected lease identity.");
                    if (value.Code == "pending") Lease(value.ArmId);
                    else if (value.ArmId is not null) throw new InvalidDataException("Unexpected arm identity.");
                }
                break;
            case GitArmCancelResult value:
                Code(value.Code); Text(value.Message, 1024, true);
                if (value.Cancelled && (value.RequiresRecovery || value.Code != "ok"))
                    throw new InvalidDataException("Invalid arm cancellation.");
                break;
            case GitLeaseReleaseResult value:
                Code(value.Code); Text(value.Message, 1024, true);
                if (value.Released && (value.RequiresRecovery || value.Code != "ok"))
                    throw new InvalidDataException("Invalid lease release.");
                break;
            case GitCommandMessage value:
                if (value.Command is null || !Commands.Contains(value.Command))
                    throw new InvalidDataException("Unknown Git command.");
                if (value.Paths is { } paths)
                {
                    if (paths.Count > MaximumSelectedPaths) throw new InvalidDataException("Too many Git paths.");
                    var bytes = 0;
                    foreach (var path in paths)
                    {
                        RelativePath(path);
                        bytes += Encoding.UTF8.GetByteCount(path);
                        if (bytes > 48 * 1024) throw new InvalidDataException("Git paths exceed the message limit.");
                    }
                }
                break;
            default:
                throw new InvalidDataException("Unknown Git payload.");
        }
        if (JsonSerializer.SerializeToUtf8Bytes(payload, payload.GetType(), Json).Length > MaximumPayloadBytes)
            throw new InvalidDataException("Git payload exceeds the message limit.");
    }

    private static void Project(string? root, ulong generation)
    {
        AbsolutePath(root);
        if (generation == 0) throw new InvalidDataException("Invalid Editor project generation.");
    }

    private static void AbsolutePath(string? value)
    {
        Text(value, MaximumPathCharacters);
        if (!Path.IsPathFullyQualified(value!)) throw new InvalidDataException("An absolute runtime path is required.");
    }

    private static void RelativePath(string? value)
    {
        Text(value, MaximumPathCharacters);
        if (Path.IsPathRooted(value!) || value!.Contains(':')) throw new InvalidDataException("A relative project path is required.");
        foreach (var part in value.Replace('\\', '/').Split('/'))
            if (part.Length == 0 || part is "." or ".." || part.Equals(".git", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Invalid selected Git path.");
    }

    private static void Lease(string? value)
    {
        if (value is null || value.Length != 32 || value.Any(c => !char.IsAsciiHexDigit(c)))
            throw new InvalidDataException("Invalid Git lease identity.");
    }

    private static void Code(string? value)
    {
        Text(value, 64);
        if (value!.Any(c => !(char.IsAsciiLetterOrDigit(c) || c == '_')))
            throw new InvalidDataException("Invalid Git result code.");
    }

    private static void Text(string? value, int maximum, bool allowEmpty = false)
    {
        if (value is null || (!allowEmpty && value.Length == 0) || value.Length > maximum || value.Contains('\0'))
            throw new InvalidDataException("Invalid Git payload text.");
    }
}
