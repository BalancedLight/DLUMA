namespace Dluma.EditorGit.Core;

public sealed record GitChange(string Path, string? OriginalPath, char Index, char WorkTree, bool Untracked, bool Conflicted);

public sealed record GitStatus(string? Branch, string? Upstream, bool Detached, bool Unborn,
    int Ahead, int Behind, IReadOnlyList<GitChange> Changes, string ReviewToken)
{
    public bool IsClean => Changes.Count == 0;
}

public sealed record GitRemote(string Name, string DisplayUrl);
public sealed record GitHistoryEntry(string Oid, string Author, DateTimeOffset Time, string Subject);

public enum GitFailureKind
{
    Unknown, NotRepository, GitUnavailable, InvalidInput, MissingUpstream,
    DirtyWorktree, Conflict, IdentityMissing, BranchExists, BranchMissing,
    PushRejected, CommandFailed, Timeout, Canceled, RecoveryRequired
}

public enum GitWorktreeOperation { SwitchBranch, Pull }

public sealed record GitChildProcessIdentity(
    int ProcessId, ulong CreationTime, long ProcessHandle, bool Mutating);

public interface IGitProcessObserver
{
    Task StartedAsync(GitChildProcessIdentity child, CancellationToken cancellationToken);
}

// The lease must prevent project and document mutations until it is disposed.
public interface IEditorDocumentGate
{
    Task<IAsyncDisposable?> AcquireWorktreeLeaseAsync(string projectRoot, GitWorktreeOperation operation, CancellationToken cancellationToken);
}

public sealed class GitException : Exception
{
    public GitFailureKind Kind { get; }
    public string? Operation { get; }
    public int? ExitCode { get; }
    public bool OutcomeUncertain { get; }

    public GitException(string message, GitFailureKind kind = GitFailureKind.Unknown,
        string? operation = null, int? exitCode = null, bool outcomeUncertain = false) : base(message)
    {
        Kind = kind;
        Operation = operation;
        ExitCode = exitCode;
        OutcomeUncertain = outcomeUncertain;
    }
}
