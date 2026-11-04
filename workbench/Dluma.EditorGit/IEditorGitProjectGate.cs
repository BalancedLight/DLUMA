namespace Dluma.EditorGit;

internal enum GitProjectOperation
{
    Initialize, Stage, Unstage, Commit, CreateBranch, SwitchBranch,
    Fetch, Pull, PublishBranch, Push, AddRemote, Clone
}

internal interface IEditorGitProjectGate
{
    // The native host verifies this exact project/generation and prevents its
    // retirement or replacement until the lease is disposed. Cancellation
    // must be coordinated with any Git child before releasing the project.
    Task<IAsyncDisposable?> AcquireProjectLeaseAsync(
        string projectRoot, ulong projectGeneration, GitProjectOperation operation,
        CancellationToken cancellationToken, string? destination = null);
}
