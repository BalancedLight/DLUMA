using System.Diagnostics;
using Dluma.EditorGit.Core;
using Dluma.QuestGraph.Protocol;

if (args.Length != 1 || !Path.IsPathFullyQualified(args[0])) throw new ArgumentException("Pass the absolute path to git.exe.");
var git = args[0];
var root = Path.Combine(Path.GetTempPath(), "dluma-git-test-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
// Keep the disposable fixture independent of user/system Git configuration.
Environment.SetEnvironmentVariable("GIT_CONFIG_NOSYSTEM", "1");
Environment.SetEnvironmentVariable("GIT_CONFIG_GLOBAL", Path.Combine(root, "absent-global-config"));
Environment.SetEnvironmentVariable("GIT_ALLOW_PROTOCOL", "disabled");
Environment.SetEnvironmentVariable("GIT_TERMINAL_PROMPT", "0");
foreach (var name in new[] { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_TEMPLATE_DIR", "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS" })
    Environment.SetEnvironmentVariable(name, null);
try
{
    void Git(params string[] arguments)
    {
        var start = new ProcessStartInfo(git) { WorkingDirectory = root, UseShellExecute = false, RedirectStandardError = true, RedirectStandardOutput = true };
        foreach (var arg in arguments) start.ArgumentList.Add(arg);
        using var process = Process.Start(start) ?? throw new Exception("Git failed to start.");
        var error = process.StandardError.ReadToEnd();
        process.StandardOutput.ReadToEnd();
        process.WaitForExit();
        if (process.ExitCode != 0) throw new Exception(error);
    }
    void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
    Git("init", "--initial-branch=main");
    Git("config", "user.name", "Test"); Git("config", "user.email", "test@example.invalid");
    File.WriteAllText(Path.Combine(root, "literal[1].txt"), "original\n");
    File.WriteAllText(Path.Combine(root, "literal1.txt"), "other original\n");
    File.WriteAllText(Path.Combine(root, "before-rename.txt"), "rename source\n");
    Git("add", "--", "literal[1].txt", "literal1.txt", "before-rename.txt"); Git("commit", "-m", "base");
    var repository = new GitRepository(git, root);
    var gate = new FixtureDocumentGate();
    await repository.CreateBranchAsync("feature");
    Require((await repository.GetBranchesAsync()).Contains("feature"), "Created branch was not listed.");
    try { await repository.CreateBranchAsync("feature"); throw new Exception("Duplicate branch was accepted."); }
    catch (GitException ex) when (ex.Kind == GitFailureKind.BranchExists) { }
    try { await repository.SwitchBranchAsync("feature", gate); throw new Exception("Switch without document admission succeeded."); }
    catch (GitException) { }
    Require((await repository.GetStatusAsync()).Branch == "main", "Refused switch changed the branch.");
    gate.Allow = true;
    await repository.SwitchBranchAsync("feature", gate);
    Require((await repository.GetStatusAsync()).Branch == "feature" && gate.Released == 1, "Admitted switch failed or did not release its document gate.");
    Git("mv", "--", "before-rename.txt", "after-rename.txt");
    File.WriteAllText(Path.Combine(root, "literal[1].txt"), "staged content\n");
    Git("--literal-pathspecs", "add", "--", "literal[1].txt");
    File.WriteAllText(Path.Combine(root, "literal[1].txt"), "working content\n");
    File.WriteAllText(Path.Combine(root, "literal1.txt"), "other changed\n");
    const string arm = "0123456789abcdef0123456789abcdef";
    EditorGitProtocol.Validate(new GitProjectAcquireRequest(root, 1, "stage", null, arm));
    EditorGitProtocol.Validate(new GitProjectCancelRequest(root, 1, arm));
    EditorGitProtocol.Validate(new GitLeaseResult(false, null, "pending", "", arm));
    try { EditorGitProtocol.Validate(new GitLeaseResult(false, null, "pending", "", null)); throw new Exception("Pending arm without identity was accepted."); }
    catch (InvalidDataException) { }
    try { EditorGitProtocol.Validate(new GitLeaseResult(true, arm, "ok", "", arm)); throw new Exception("Granted lease retained arm identity."); }
    catch (InvalidDataException) { }
    var staged = await repository.GetDiffAsync("literal[1].txt", GitDiffKind.Staged);
    var working = await repository.GetDiffAsync("literal[1].txt", GitDiffKind.Worktree);
    Require(staged.Contains("+staged content") && !staged.Contains("working content") && !staged.Contains("other changed"), "Staged diff must show only the selected literal path and index content.");
    Require(working.Contains("+working content") && !working.Contains("other changed"), "Working diff must show only the selected literal path and working content.");
    File.WriteAllText(Path.Combine(root, "new.txt"), "new file content\n");
    var untracked = await repository.GetDiffAsync("new.txt", GitDiffKind.Worktree);
    Require(untracked.Contains("+new file content"), "Untracked text must be reviewable before staging.");
    File.WriteAllBytes(Path.Combine(root, "binary.dat"), [0, 1, 2, 3]);
    var binary = await repository.GetDiffAsync("binary.dat", GitDiffKind.Worktree);
    Require(binary.Contains("Binary files"), "Binary files must be identified without displaying raw bytes.");
    try { await repository.GetDiffAsync("missing.txt", GitDiffKind.Worktree); throw new Exception("A stale selection was accepted."); }
    catch (GitException ex) when (ex.Kind == GitFailureKind.InvalidInput) { }
    File.WriteAllText(Path.Combine(root, "-東京.txt"), "unicode leading dash\n");
    var unusual = await repository.GetDiffAsync("-東京.txt", GitDiffKind.Worktree);
    Require(unusual.Contains("+unicode leading dash") && !unusual.Contains("other changed"), "Unicode and leading-dash paths must be treated as literal files.");
    var rename = await repository.GetDiffAsync("after-rename.txt", GitDiffKind.Staged);
    Require(rename.Contains("before-rename.txt") && rename.Contains("after-rename.txt") && !rename.Contains("literal1.txt"), "Staged rename diff must include its old and new paths only.");
    using (var canceled = new CancellationTokenSource())
    {
        canceled.Cancel();
        try { await repository.GetDiffAsync("new.txt", GitDiffKind.Worktree, canceled.Token); throw new Exception("Canceled diff was accepted."); }
        catch (OperationCanceledException) { }
    }
    File.WriteAllText(Path.Combine(root, "preview-limit.txt"), new string('p', 2 * 1024 * 1024) + "\n");
    try { await repository.GetDiffAsync("preview-limit.txt", GitDiffKind.Worktree); throw new Exception("Oversized preview was accepted."); }
    catch (GitException ex) when (ex.Kind == GitFailureKind.CommandFailed && ex.Message.Contains("preview", StringComparison.OrdinalIgnoreCase)) { }
    File.WriteAllText(Path.Combine(root, "large.txt"), new string('x', 17 * 1024 * 1024) + "\n");
    try { await repository.GetDiffAsync("large.txt", GitDiffKind.Worktree); throw new Exception("Oversized output was accepted."); }
    catch (GitException ex) when (ex.Kind == GitFailureKind.CommandFailed) { }
    try { await repository.SwitchBranchAsync("main", gate); throw new Exception("Dirty switch succeeded."); }
    catch (GitException ex) when (ex.Kind == GitFailureKind.DirtyWorktree) { }
    Require((await repository.GetStatusAsync()).Branch == "feature" &&
        File.ReadAllText(Path.Combine(root, "literal1.txt")) == "other changed\n", "Refused dirty switch changed project work.");
    var before = await repository.GetStatusAsync();
    await repository.GetDiffAsync("literal[1].txt", GitDiffKind.Staged);
    var after = await repository.GetStatusAsync();
    Require(before.ReviewToken == after.ReviewToken && before.Changes.SequenceEqual(after.Changes), "Reading a diff must not alter index or status.");
    await repository.StageAsync(["new.txt"]);
    var withNew = await repository.GetStatusAsync();
    Require(withNew.Changes.Any(c => c.Path == "new.txt" && c.Index == 'A') &&
        withNew.Changes.Any(c => c.Path == "literal1.txt" && c.Index == '.'), "Stage must affect only selected files.");
    try { await repository.CommitAsync("stale review", before.ReviewToken); throw new Exception("A stale index review was accepted."); }
    catch (GitException ex) when (ex.Kind == GitFailureKind.InvalidInput) { }
    await repository.UnstageAsync(["new.txt"]);
    var unstaged = await repository.GetStatusAsync();
    Require(unstaged.Changes.Any(c => c.Path == "new.txt" && c.Untracked) &&
        File.ReadAllText(Path.Combine(root, "new.txt")) == "new file content\n", "Unstage must preserve worktree content.");
    await repository.StageAsync(["new.txt"]);
    var reviewed = await repository.GetStatusAsync();
    await repository.CommitAsync("reviewed commit", reviewed.ReviewToken);
    var committed = await repository.GetStatusAsync();
    Require(!committed.Changes.Any(c => c.Path == "new.txt"), "The reviewed index must commit.");
    Require(committed.Changes.Any(c => c.Path == "literal1.txt" && c.WorkTree != '.'), "Commit must preserve unrelated working changes.");
    Console.WriteLine("Git branch, diff and index contracts passed.");
}
finally
{
    foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)) File.SetAttributes(file, FileAttributes.Normal);
    Directory.Delete(root, recursive: true);
}

sealed class FixtureDocumentGate : IEditorDocumentGate
{
    public bool Allow { get; set; }
    public int Released { get; private set; }
    public Task<IAsyncDisposable?> AcquireWorktreeLeaseAsync(string projectRoot, GitWorktreeOperation operation, CancellationToken cancellationToken)
    {
        _ = projectRoot; _ = operation; cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult<IAsyncDisposable?>(Allow ? new Lease(this) : null);
    }
    sealed class Lease(FixtureDocumentGate owner) : IAsyncDisposable
    {
        public ValueTask DisposeAsync() { owner.Released++; return ValueTask.CompletedTask; }
    }
}
