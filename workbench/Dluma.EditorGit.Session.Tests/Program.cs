using Dluma.EditorGit;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using System.Text.Json;

if (args.Length == 3 && args[0] == "--diagnostic-readonly")
{
    var repository = new Dluma.EditorGit.Core.GitRepository(args[1], args[2]);
    await repository.VerifyDiagnosticRepositoryAsync();
    var metadata = typeof(Dluma.EditorGit.Core.GitRepository).Assembly.GetType("Dluma.EditorGit.Core.DiagnosticGitMetadata")
        ?? throw new Exception("Missing metadata identity check.");
    var matches = metadata.GetMethod("Matches", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)
        ?? throw new Exception("Missing metadata identity method.");
    var dotGit = Path.Combine(args[2], ".git");
    if (matches.Invoke(null, [args[2], dotGit, dotGit]) is not true ||
        matches.Invoke(null, [args[2], dotGit, args[2]]) is not false)
        throw new Exception("Metadata directory identity mismatch was not rejected.");
    Console.WriteLine("Git standalone metadata identity verified.");
    return;
}

const string arm = "0123456789abcdef0123456789abcdef";
const string lease = "1123456789abcdef0123456789abcdef";
var root = Path.GetFullPath(Path.GetTempPath());
void Require(bool condition, string message) { if (!condition) throw new Exception(message); }

{
    var requests = 0;
    var available = false;
    string? unavailableMessage = "Read only";
    await GitContextRefresh.RequeryAsync("git.exe", (_, _) =>
    {
        ++requests;
        return Task.FromResult(new GitContextMessage(root, 7, true, true, false, "Ready", true));
    }, context =>
    {
        available = context.MutationsAvailable;
        unavailableMessage = GitContextRefresh.UnavailableMessage(context);
    }, CancellationToken.None);
    Require(requests == 1 && available && unavailableMessage is null,
        "Refresh must requery changed native availability and clear the read-only message.");
}

{
    Require(GitContextRefresh.AfterRefreshFailure("Done.", "Status unavailable") ==
        "Git action finished, but refresh failed. Status unavailable", "A failed refresh must not be reported as Done.");
    Require(GitContextRefresh.AfterRefreshFailure("Stage failed.", "Status unavailable") ==
        "Stage failed. Refresh failed. Status unavailable", "A refresh failure must preserve the original action failure.");
    var calls = new List<string>();
    Task Refresh(CancellationToken token) { token.ThrowIfCancellationRequested(); calls.Add("refresh"); return Task.CompletedTask; }
    var complete = await GitContextRefresh.CompleteAsync(new TestLease(calls), false, Refresh);
    Require(complete.ReleaseFailure is null && complete.RefreshFailure is null && calls.SequenceEqual(["release", "refresh"]),
        "Post-action refresh must run only after the project lease is released.");
    calls.Clear();
    complete = await GitContextRefresh.CompleteAsync(new TestLease(calls, fail: true), false, Refresh);
    Require(complete.ReleaseFailure is not null && calls.SequenceEqual(["release"]),
        "An unconfirmed release must not start another Git process.");
    calls.Clear();
    complete = await GitContextRefresh.CompleteAsync(new TestLease(calls), true, Refresh);
    Require(complete.ReleaseFailure is not null && calls.Count == 0,
        "A quarantined lease must remain held and skip refresh.");
    complete = await GitContextRefresh.CompleteAsync(null, false,
        _ => throw new Dluma.EditorGit.Core.GitException("Repository unavailable."));
    Require(complete.RefreshFailure == "Repository unavailable.", "A failed refresh must remain visible.");
    complete = await GitContextRefresh.CompleteAsync(null, false,
        async token => await Task.Delay(Timeout.InfiniteTimeSpan, token), TimeSpan.FromMilliseconds(20));
    Require(complete.RefreshFailure == "Git refresh timed out. Choose Refresh to try again.",
        "An independent refresh timeout must not be reported as action cancellation.");
    complete = await GitContextRefresh.CompleteAsync(null, false, async token =>
    {
        try { await Task.Delay(Timeout.InfiniteTimeSpan, token); }
        catch (OperationCanceledException) { throw new Dluma.EditorGit.Core.GitException("Canceled", Dluma.EditorGit.Core.GitFailureKind.Canceled); }
    }, TimeSpan.FromMilliseconds(20));
    Require(complete.RefreshFailure == "Git refresh timed out. Choose Refresh to try again.",
        "A Git child stopped by the refresh budget must be reported as a refresh timeout.");
    Require(GitContextRefresh.AfterRefreshFailure("That branch already exists.", complete.RefreshFailure!) ==
        "That branch already exists. Refresh failed. Git refresh timed out. Choose Refresh to try again.",
        "The original branch refusal must survive a refresh timeout.");
    Require(GitContextRefresh.AfterRefreshFailure("Canceled. Refresh before retrying.", complete.RefreshFailure!).StartsWith("Canceled.", StringComparison.Ordinal),
        "Explicit action cancellation must remain distinct from a later refresh timeout.");
    using var refreshCanceled = new CancellationTokenSource(TimeSpan.FromMilliseconds(20));
    complete = await GitContextRefresh.CompleteAsync(null, false,
        async token => await Task.Delay(Timeout.InfiniteTimeSpan, token), TimeSpan.FromSeconds(1), refreshCanceled.Token);
    Require(complete.RefreshFailure == "Refresh canceled. Choose Refresh to try again.",
        "Explicit refresh cancellation must not be reported as a timeout.");
}

{
    var options = new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    var oldContext = JsonSerializer.Serialize(new { projectRoot = root, projectGeneration = 7UL,
        enabled = true, mutationsAvailable = false, worktreeAvailable = false, message = "" });
    var parsed = JsonSerializer.Deserialize<GitContextMessage>(oldContext, options)
        ?? throw new Exception("Missing old context.");
    Require(!parsed.LocalWritesOnly, "Older contexts must default to unrestricted mode.");
    EditorGitProtocol.Validate(parsed);
    EditorGitProtocol.Validate(new GitContextMessage(root, 7, true, true, false, "", true));
    var legacyDiagnostic = new GitContextMessage(root, 7, true, true, false, "", true);
    Require(GitContextRefresh.RequiresDiagnosticFixtureCheck(legacyDiagnostic), "Old diagnostic contexts must keep fixture confinement.");
    var local = new GitContextMessage(root, 7, true, true, false, "", true, true, false);
    EditorGitProtocol.Validate(local);
    Require(!GitContextRefresh.RequiresDiagnosticFixtureCheck(local) && local.BranchCreateAvailable,
        "Ordinary local mode may offer branch creation without diagnostic fixture confinement.");
    var localWire = JsonSerializer.Deserialize<GitContextMessage>(JsonSerializer.Serialize(local, options), options)
        ?? throw new Exception("Missing ordinary context.");
    EditorGitProtocol.Validate(localWire);
    Require(localWire.BranchCreateAvailable && localWire.DiagnosticFixtureOnly == false,
        "Ordinary local capabilities must survive the protocol round trip.");
    try { EditorGitProtocol.Validate(new GitContextMessage(root, 7, true, false, false, "", true));
        throw new Exception("Invalid diagnostic context accepted."); }
    catch (InvalidDataException) { }
    try { EditorGitProtocol.Validate(new GitContextMessage(root, 7, true, true, false, "", true, true, true));
        throw new Exception("Diagnostic context advertised branch creation."); }
    catch (InvalidDataException) { }
}
{
    var method = typeof(Dluma.EditorGit.Core.GitRepository).GetMethod("IsCompletedIdentityCommandFailure",
        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)
        ?? throw new Exception("Missing identity failure boundary.");
    bool Matches(Dluma.EditorGit.Core.GitException value) => method.Invoke(null, [value]) is true;
    Require(Matches(new("Git failed", Dluma.EditorGit.Core.GitFailureKind.CommandFailed, "var", 128)),
        "A completed failing Git identity command should get identity guidance.");
    Require(!Matches(new("Admission failed", Dluma.EditorGit.Core.GitFailureKind.CommandFailed, "var")) &&
            !Matches(new("Too much output", Dluma.EditorGit.Core.GitFailureKind.CommandFailed, "var", 0)) &&
            !Matches(new("Timed out", Dluma.EditorGit.Core.GitFailureKind.Timeout, "var")) &&
            !Matches(new("Other command", Dluma.EditorGit.Core.GitFailureKind.CommandFailed, "status", 128)),
        "Admission, overflow, timeout and other-command failures must not be relabeled as identity errors.");
}

using (var canceled = new CancellationTokenSource())
{
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, payload) => type switch
    {
        "gitProjectAcquire" => CancelAndPending(),
        "gitProjectCancel" when payload is GitProjectCancelRequest c && c.ArmId == arm && c.ProjectGeneration == 7 => new GitArmCancelResult(true, false, "ok", ""),
        _ => throw new Exception("Unexpected request: " + type)
    };
    object CancelAndPending() { canceled.Cancel(); return new GitLeaseResult(false, null, "pending", "", arm); }
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    try { await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, canceled.Token); throw new Exception("Canceled arm granted a lease."); }
    catch (OperationCanceledException) { }
    Require(bridge.Requests.SequenceEqual(["gitProjectAcquire", "gitProjectCancel"]), "Cancellation must cancel the arm, not release a nonexistent lease.");
}
{
    var calls = 0;
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, payload) => type switch
    {
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: null } && ++calls == 1 =>
            new GitLeaseResult(false, null, "busy", "Finishing earlier Git reads."),
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: null } =>
            new GitLeaseResult(false, null, "pending", "", arm),
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: arm } => new GitLeaseResult(true, lease, "ok", ""),
        "gitLeaseRelease" => new GitLeaseReleaseResult(true, false, "ok", ""),
        _ => throw new Exception("Unexpected request: " + type)
    };
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    await using (var held = await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, CancellationToken.None))
        Require(held is not null, "Drained reads should allow a later arm and lease.");
    Require(bridge.Requests.SequenceEqual(["gitProjectAcquire", "gitProjectAcquire", "gitProjectAcquire", "gitLeaseRelease"]),
        "Pre-arm busy retry must not invent an arm or release a nonexistent lease.");
}
{
    using var canceled = new CancellationTokenSource();
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, _) => type == "gitProjectAcquire"
        ? CancelAndBusy() : throw new Exception("Unexpected request: " + type);
    object CancelAndBusy() { canceled.Cancel(); return new GitLeaseResult(false, null, "busy", "Busy"); }
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    try { await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, canceled.Token); throw new Exception("Canceled busy acquisition succeeded."); }
    catch (OperationCanceledException) { }
    Require(bridge.Requests.SequenceEqual(["gitProjectAcquire"]), "Cancel before an arm must not cancel or release one.");
}
{
    using var canceled = new CancellationTokenSource(TimeSpan.FromMilliseconds(250));
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, _) => type == "gitProjectAcquire"
        ? new GitLeaseResult(false, null, "busy", "Busy") : throw new Exception("Unexpected request: " + type);
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    try { await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, canceled.Token); throw new Exception("Canceled busy polling succeeded."); }
    catch (OperationCanceledException) { }
    Require(bridge.Requests.Count >= 2 && bridge.Requests.All(type => type == "gitProjectAcquire"),
        "Cancellation during pre-arm polling must not cancel or release a nonexistent arm or lease.");
}
{
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, payload) => type switch
    {
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: null } => new GitLeaseResult(false, null, "pending", "", arm),
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: arm } => new GitLeaseResult(true, lease, "ok", ""),
        "gitLeaseRelease" when payload is GitLeaseReleaseRequest r && r.LeaseId == lease => new GitLeaseReleaseResult(true, false, "ok", ""),
        _ => throw new Exception("Unexpected request: " + type)
    };
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    await using (var held = await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, CancellationToken.None))
        Require(held is not null, "Retry should receive its lease.");
    Require(bridge.Requests.SequenceEqual(["gitProjectAcquire", "gitProjectAcquire", "gitLeaseRelease"]), "Retry must use the same arm and release only the granted lease.");
}
{
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, payload) => type switch
    {
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: null } => new GitLeaseResult(false, null, "pending", "", arm),
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: arm } => new GitLeaseResult(false, null, "busy", "Interrupted"),
        "gitProjectCancel" when payload is GitProjectCancelRequest { ArmId: arm } => new GitArmCancelResult(true, false, "ok", ""),
        _ => throw new Exception("Unexpected request: " + type)
    };
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    try { await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, CancellationToken.None); throw new Exception("Interrupted arm was accepted."); }
    catch (Dluma.EditorGit.Core.GitException) { }
    Require(bridge.Requests.SequenceEqual(["gitProjectAcquire", "gitProjectAcquire", "gitProjectCancel"]),
        "Busy after arming must cancel the exact arm rather than retrying as unarmed.");
}
{
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, payload) => type switch
    {
        "gitProjectAcquire" when payload is GitProjectAcquireRequest { ArmId: null } => new GitLeaseResult(false, null, "pending", "", arm),
        "gitProjectAcquire" => new GitLeaseResult(false, null, "pending", "", lease),
        _ => throw new Exception("Unexpected request: " + type)
    };
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    session.ApplyContext(new(root, 7, true, true, false, ""));
    try { await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, CancellationToken.None); throw new Exception("Changed arm identity was accepted."); }
    catch (Dluma.EditorGit.Core.GitException ex) when (ex.Kind == Dluma.EditorGit.Core.GitFailureKind.RecoveryRequired) { }
    Require(bridge.Aborted && bridge.Requests.SequenceEqual(["gitProjectAcquire", "gitProjectAcquire"]), "Changed arm identity must retire the connection.");
}
{
    using var canceled = new CancellationTokenSource();
    var bridge = new QuestBridgeClient();
    bridge.Respond = (type, _) => type switch
    {
        "gitProjectAcquire" => CancelAndPending(),
        "gitProjectCancel" => new GitArmCancelResult(false, true, "recovery", ""),
        _ => throw new Exception("Unexpected request: " + type)
    };
    object CancelAndPending() { canceled.Cancel(); return new GitLeaseResult(false, null, "pending", "", arm); }
    using var session = new NativeGitSession(bridge, new(new(42), root, 7), () => { });
    GitContextMessage? last = null;
    session.ContextChanged += context => last = context;
    session.ApplyContext(new(root, 7, true, true, false, "", true, true, false));
    try { await session.AcquireProjectLeaseAsync(root, 7, GitProjectOperation.Stage, canceled.Token); throw new Exception("Unconfirmed arm cancellation was accepted."); }
    catch (Dluma.EditorGit.Core.GitException ex) when (ex.Kind == Dluma.EditorGit.Core.GitFailureKind.RecoveryRequired) { }
    Require(bridge.Aborted, "Unconfirmed arm cancellation must retire the connection.");
    Require(last is { MutationsAvailable: false, BranchCreateAvailable: false, LocalWritesOnly: false },
        "Recovery must clear ordinary local mutation and branch capabilities.");
}
Console.WriteLine("Editor Git session arm contract passed.");

sealed class TestLease(List<string> calls, bool fail = false) : IAsyncDisposable
{
    public ValueTask DisposeAsync()
    {
        calls.Add("release");
        return fail ? ValueTask.FromException(new InvalidOperationException()) : ValueTask.CompletedTask;
    }
}
