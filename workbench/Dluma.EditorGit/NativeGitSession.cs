using Dluma.EditorGit.Core;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using System.Diagnostics;
using System.Text.Json;

namespace Dluma.EditorGit;

internal sealed class NativeGitSession : IEditorGitProjectGate, IEditorDocumentGate, IGitProcessObserver, IDisposable
{
    private readonly QuestBridgeClient _bridge;
    private readonly GitLaunchOptions _options;
    private readonly Action _requireHost;
    private readonly object _sync = new();
    private GitContextMessage? _context;
    private string? _projectLease, _worktreeLease;
    private bool _acquiring, _retired, _recovery, _disposed;
    internal event Action<GitContextMessage>? ContextChanged;
    internal event Action? Retired;

    internal NativeGitSession(QuestBridgeClient bridge, GitLaunchOptions options, Action requireHost)
    {
        _bridge = bridge;
        _options = options;
        _requireHost = requireHost;
    }

    private static bool SamePath(string left, string right) => string.Equals(
        Path.TrimEndingDirectorySeparator(Path.GetFullPath(left)),
        Path.TrimEndingDirectorySeparator(Path.GetFullPath(right)), StringComparison.OrdinalIgnoreCase);

    internal void RequireCurrent()
    {
        _requireHost();
        lock (_sync)
        {
            if (_disposed || _retired || _bridge.AuthenticatedEditorProcessId != _options.Host.EditorPid)
                throw new GitException("The Editor project changed. Reopen Git.", GitFailureKind.InvalidInput);
        }
    }

    internal void ApplyContext(GitContextMessage context)
    {
        EditorGitProtocol.Validate(context);
        var retired = !SamePath(context.ProjectRoot, _options.ProjectRoot) || context.ProjectGeneration != _options.ProjectGeneration;
        lock (_sync)
        {
            if (_disposed) return;
            _retired |= retired;
            if (_recovery) context = context with { MutationsAvailable = false, WorktreeAvailable = false, LocalWritesOnly = false,
                BranchCreateAvailable = false, DiagnosticFixtureOnly = false, Message = "Git cleanup needs attention. Reopen Git after checking the operation." };
            _context = context;
        }
        ContextChanged?.Invoke(context);
        if (retired) Retired?.Invoke();
    }

    internal async Task<GitContextMessage> StateAsync(string? executable, CancellationToken token = default)
    {
        RequireCurrent();
        var context = await RequestAsync<GitStateRequest, GitContextMessage>("gitState",
            new(_options.ProjectRoot, _options.ProjectGeneration, executable), token).ConfigureAwait(false);
        ApplyContext(context);
        RequireCurrent();
        return context;
    }

    public async Task<IAsyncDisposable?> AcquireProjectLeaseAsync(string projectRoot, ulong projectGeneration,
        GitProjectOperation operation, CancellationToken cancellationToken, string? destination = null)
    {
        RequireCurrent();
        if (!SamePath(projectRoot, _options.ProjectRoot) || projectGeneration != _options.ProjectGeneration)
            throw new GitException("The selected project changed.", GitFailureKind.InvalidInput);
        lock (_sync)
        {
            if (_recovery || _context is not { Enabled: true, MutationsAvailable: true })
                throw new GitException(_context?.Message is { Length: > 0 } message ? message : "Git is unavailable for this project.", GitFailureKind.InvalidInput);
            if (_acquiring || _projectLease is not null)
                throw new GitException("Another Git operation is still finishing.", GitFailureKind.InvalidInput);
            _acquiring = true;
        }
        string? armId = null;
        var started = Stopwatch.GetTimestamp();
        try
        {
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                // Complete each native transition even if the UI cancels, so
                // an issued arm or lease is never lost with its reply.
                var result = await TransitionAsync<GitProjectAcquireRequest, GitLeaseResult>("gitProjectAcquire",
                    new(projectRoot, projectGeneration, OperationName(operation), destination, armId)).ConfigureAwait(false);
                if (result.Granted)
                {
                    armId = null;
                    lock (_sync) _projectLease = result.LeaseId!;
                    var lease = new NativeLease(this, result.LeaseId!, false);
                    try { cancellationToken.ThrowIfCancellationRequested(); RequireCurrent(); }
                    catch { await lease.DisposeAsync().ConfigureAwait(false); throw; }
                    return lease;
                }
                // Before an arm exists, a busy result can be a read child or
                // Job accounting still draining. Retry admission only; no Git
                // mutation or synthetic arm/lease has been issued here.
                if (result.Code == "busy" && armId is null)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    if (Stopwatch.GetElapsedTime(started) >= TimeSpan.FromSeconds(15))
                        throw new GitException("The Editor did not finish preparing Git. Try again.", GitFailureKind.Timeout);
                    await Task.Delay(100, cancellationToken).ConfigureAwait(false);
                    RequireCurrent();
                    continue;
                }
                if (result.Code != "pending") throw Refused(result.Code, result.Message);
                if (armId is not null && !string.Equals(armId, result.ArmId, StringComparison.Ordinal))
                {
                    armId = null;
                    MarkRecovery();
                    _bridge.AbortProtocolSession("Git arm identity changed.");
                    throw new GitException("The Editor connection changed during Git admission. Reopen Git after cleanup.", GitFailureKind.RecoveryRequired);
                }
                armId = result.ArmId!;
                cancellationToken.ThrowIfCancellationRequested();
                if (Stopwatch.GetElapsedTime(started) >= TimeSpan.FromSeconds(15))
                    throw new GitException("The Editor did not finish preparing Git. Try again.", GitFailureKind.Timeout);
                await Task.Delay(100, cancellationToken).ConfigureAwait(false);
                RequireCurrent();
            }
        }
        finally
        {
            try
            {
                if (armId is not null)
                {
                    var canceled = await TransitionAsync<GitProjectCancelRequest, GitArmCancelResult>("gitProjectCancel",
                        new(projectRoot, projectGeneration, armId)).ConfigureAwait(false);
                    if (!canceled.Cancelled) throw Refused(canceled.Code, canceled.Message);
                }
            }
            catch
            {
                MarkRecovery();
                lock (_sync) _retired = true;
                _bridge.AbortProtocolSession("Git arm cancellation could not be confirmed.");
                throw new GitException("Git preparation could not be canceled. Reopen Git after cleanup.", GitFailureKind.RecoveryRequired);
            }
            finally { lock (_sync) _acquiring = false; }
        }
    }

    public async Task<IAsyncDisposable?> AcquireWorktreeLeaseAsync(string projectRoot, GitWorktreeOperation operation,
        CancellationToken cancellationToken)
    {
        RequireCurrent();
        string parent;
        lock (_sync)
        {
            if (!SamePath(projectRoot, _options.ProjectRoot) || _context is not { Enabled: true } ||
                _projectLease is null || _worktreeLease is not null || _recovery)
                throw new GitException("Close all project documents before changing the checkout.", GitFailureKind.InvalidInput);
            parent = _projectLease;
        }
        cancellationToken.ThrowIfCancellationRequested();
        var result = await TransitionAsync<GitWorktreeAcquireRequest, GitLeaseResult>("gitWorktreeAcquire",
            new(projectRoot, _options.ProjectGeneration, operation == GitWorktreeOperation.Pull ? "pull" : "switchBranch", parent)).ConfigureAwait(false);
        if (!result.Granted) throw Refused(result.Code, result.Message);
        lock (_sync) _worktreeLease = result.LeaseId!;
        var lease = new NativeLease(this, result.LeaseId!, true);
        try { cancellationToken.ThrowIfCancellationRequested(); RequireCurrent(); }
        catch { await lease.DisposeAsync().ConfigureAwait(false); throw; }
        return lease;
    }

    public async Task StartedAsync(GitChildProcessIdentity child, CancellationToken cancellationToken)
    {
        RequireCurrent();
        string? lease;
        lock (_sync)
        {
            lease = _worktreeLease ?? _projectLease;
            if (_recovery || _context is not { Enabled: true } || child.Mutating && lease is null)
                throw new GitException("The Editor did not admit this Git operation.", GitFailureKind.InvalidInput);
        }
        cancellationToken.ThrowIfCancellationRequested();
        // The source process handle must remain alive through acknowledgment,
        // including cancellation while the native owner is processing it.
        var result = await TransitionAsync<GitChildStartedRequest, GitChildStartedResult>("gitChildStarted",
            new(lease, child.ProcessId, child.CreationTime, child.ProcessHandle, child.Mutating)).ConfigureAwait(false);
        if (!result.Registered)
        {
            if (result.RequiresRecovery) MarkRecovery();
            throw Refused(result.Code, result.Message);
        }
        cancellationToken.ThrowIfCancellationRequested();
    }

    private async Task ReleaseAsync(string id, bool worktree)
    {
        var started = Stopwatch.GetTimestamp();
        try
        {
            while (true)
            {
                var result = await TransitionAsync<GitLeaseReleaseRequest, GitLeaseReleaseResult>("gitLeaseRelease", new(id)).ConfigureAwait(false);
                if (result.Released)
                {
                    lock (_sync)
                    {
                        if (worktree && _worktreeLease == id) _worktreeLease = null;
                        if (!worktree && _projectLease == id) _projectLease = null;
                    }
                    return;
                }
                if (!result.RequiresRecovery && (result.Code is "busy" or "pending" or "git_pending" or "job_pending") && Stopwatch.GetElapsedTime(started) < TimeSpan.FromSeconds(15))
                {
                    await Task.Delay(100).ConfigureAwait(false);
                    continue;
                }
                MarkRecovery();
                throw Refused(result.Code, result.Message);
            }
        }
        catch { MarkRecovery(); throw; }
    }

    private void MarkRecovery()
    {
        GitContextMessage? context;
        lock (_sync) { _recovery = true; context = _context; }
        if (context is not null) ApplyContext(context);
    }

    private async Task<TReply> RequestAsync<TRequest, TReply>(string type, TRequest payload, CancellationToken token)
        where TRequest : EditorGitPayload
    {
        using var budget = CancellationTokenSource.CreateLinkedTokenSource(token);
        budget.CancelAfter(TimeSpan.FromSeconds(10));
        try { return await _bridge.RequestAsync<TRequest, TReply>(type, payload, TimeSpan.FromSeconds(10), budget.Token).ConfigureAwait(false); }
        catch (QuestBridgeException exception) { throw Refused(exception.Code, exception.Message); }
        catch (OperationCanceledException)
        {
            // Cancellation can interrupt a partial frame or leave a reply in
            // flight. Retire this connection before another request can write.
            _bridge.AbortProtocolSession("Git request interrupted.");
            if (token.IsCancellationRequested) throw;
            throw new GitException("The Editor did not finish the Git request. Refresh before retrying.", GitFailureKind.Timeout);
        }
    }

    private async Task<TReply> TransitionAsync<TRequest, TReply>(string type, TRequest payload)
        where TRequest : EditorGitPayload
    {
        try { return await RequestAsync<TRequest, TReply>(type, payload, CancellationToken.None).ConfigureAwait(false); }
        catch (Exception exception) when (exception is IOException or InvalidOperationException or JsonException ||
                                             exception is GitException { Kind: GitFailureKind.Timeout })
        {
            // A lost transition reply may hide a native grant. Retire this
            // session so native job/lease cleanup, rather than a retry, owns it.
            MarkRecovery();
            lock (_sync) _retired = true;
            _bridge.AbortProtocolSession("Native Git transition outcome unavailable.");
            Retired?.Invoke();
            throw new GitException("The Editor connection changed during Git admission. Reopen Git after cleanup.",
                GitFailureKind.RecoveryRequired);
        }
    }

    private static GitException Refused(string code, string message) => new(
        string.IsNullOrWhiteSpace(message) ? "The Editor did not admit the Git action." : message,
        code.Contains("recovery", StringComparison.Ordinal) ? GitFailureKind.RecoveryRequired : GitFailureKind.InvalidInput);

    private static string OperationName(GitProjectOperation operation) => operation switch
    {
        GitProjectOperation.Initialize => "initialize", GitProjectOperation.Stage => "stage",
        GitProjectOperation.Unstage => "unstage", GitProjectOperation.Commit => "commit",
        GitProjectOperation.CreateBranch => "createBranch", GitProjectOperation.SwitchBranch => "switchBranch",
        GitProjectOperation.Fetch => "fetch", GitProjectOperation.Pull => "pull",
        GitProjectOperation.PublishBranch => "publishBranch", GitProjectOperation.Push => "push",
        GitProjectOperation.AddRemote => "addRemote", GitProjectOperation.Clone => "clone",
        _ => throw new ArgumentOutOfRangeException(nameof(operation))
    };

    private sealed class NativeLease(NativeGitSession session, string id, bool worktree) : IAsyncDisposable
    {
        private int _released;
        public async ValueTask DisposeAsync()
        {
            if (Interlocked.Exchange(ref _released, 1) == 0)
                await session.ReleaseAsync(id, worktree).ConfigureAwait(false);
        }
    }

    public void Dispose()
    {
        // Local disposal cannot release native authority. The native session
        // retains its job/leases until it independently verifies cleanup.
        lock (_sync) { _disposed = true; _retired = true; }
    }
}
