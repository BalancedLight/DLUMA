using Dluma.QuestGraph.Protocol;
using Dluma.EditorGit.Core;

namespace Dluma.EditorGit;

internal static class GitContextRefresh
{
    internal static string? UnavailableMessage(GitContextMessage context) =>
        !context.MutationsAvailable && !string.IsNullOrWhiteSpace(context.Message) ? context.Message : null;

    internal static bool RequiresDiagnosticFixtureCheck(GitContextMessage context) =>
        context.LocalWritesOnly && context.DiagnosticFixtureOnly != false;

    internal static string AfterRefreshFailure(string? actionResult, string refreshFailure) =>
        actionResult is null or "Done."
            ? "Git action finished, but refresh failed. " + refreshFailure
            : actionResult + " Refresh failed. " + refreshFailure;

    internal readonly record struct Completion(string? ReleaseFailure, string? RefreshFailure);

    internal static async Task<Completion> CompleteAsync(IAsyncDisposable? lease, bool retainLease,
        Func<CancellationToken, Task>? refresh, TimeSpan? refreshBudget = null, CancellationToken refreshCancellation = default)
    {
        if (lease is not null)
        {
            if (retainLease) return new("Git cleanup needs attention. Reopen Git after checking the operation.", null);
            try { await lease.DisposeAsync().ConfigureAwait(true); }
            catch (Exception) { return new("The Editor project could not be released.", null); }
        }
        if (refresh is null) return new(null, null);
        using var timeout = new CancellationTokenSource(refreshBudget ?? TimeSpan.FromSeconds(30));
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token, refreshCancellation);
        try { await refresh(linked.Token).ConfigureAwait(true); return new(null, null); }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested)
        { return new(null, "Git refresh timed out. Choose Refresh to try again."); }
        catch (GitException exception) when (timeout.IsCancellationRequested && (exception.Kind is GitFailureKind.Canceled or GitFailureKind.Timeout))
        { return new(null, "Git refresh timed out. Choose Refresh to try again."); }
        catch (OperationCanceledException) when (refreshCancellation.IsCancellationRequested)
        { return new(null, "Refresh canceled. Choose Refresh to try again."); }
        catch (GitException exception) when (refreshCancellation.IsCancellationRequested && exception.Kind == GitFailureKind.Canceled)
        { return new(null, "Refresh canceled. Choose Refresh to try again."); }
        catch (Exception exception)
        {
            return new(null, exception is GitException git ? git.Message : "Could not refresh Git status.");
        }
    }

    internal static async Task RequeryAsync(string executable,
        Func<string, CancellationToken, Task<GitContextMessage>> request,
        Action<GitContextMessage> apply, CancellationToken token)
    {
        var context = await request(executable, token).ConfigureAwait(true);
        apply(context);
    }
}
