using System.Collections.Concurrent;
using System.Text;

namespace Dluma.EditorGit.Core;

public sealed class GitRepository
{
    private sealed class RepositoryState
    {
        internal readonly SemaphoreSlim Lock = new(1, 1);
        internal volatile bool Quarantined;
        internal IAsyncDisposable? HeldLease;
    }
    private static readonly ConcurrentDictionary<string, RepositoryState> States = new(StringComparer.OrdinalIgnoreCase);
    private readonly string _root;
    private readonly GitProcess _git;
    private readonly SemaphoreSlim _lock;
    private readonly RepositoryState _state;

    public bool RequiresRecovery => _state.Quarantined;

    public static bool RequiresRecoveryAt(string repositoryRoot) =>
        States.TryGetValue(Path.TrimEndingDirectorySeparator(Path.GetFullPath(repositoryRoot)), out var state) && state.Quarantined;

    public GitRepository(string gitExecutable, string projectRoot, IGitProcessObserver? processObserver = null)
    {
        if (!Path.IsPathFullyQualified(projectRoot) || !Directory.Exists(projectRoot))
            throw new ArgumentException("Select an existing project folder.", nameof(projectRoot));
        _root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(projectRoot));
        var state = States.GetOrAdd(_root, _ => new RepositoryState());
        _state = state;
        _git = new GitProcess(gitExecutable, () => state.Quarantined, () => state.Quarantined = true, processObserver);
        _lock = state.Lock;
    }

    // The local-only diagnostic admits only a standalone .git directory in
    // its selected fixture. Normal repository operations do not use this gate.
    public async Task VerifyDiagnosticRepositoryAsync(CancellationToken cancellationToken = default)
    {
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var gitDir = GitProcess.Text(await _git.RunAsync(_root,
                ["rev-parse", "--absolute-git-dir"], false, cancellationToken).ConfigureAwait(false)).TrimEnd('\r', '\n');
            var commonDir = GitProcess.Text(await _git.RunAsync(_root,
                ["rev-parse", "--path-format=absolute", "--git-common-dir"], false, cancellationToken).ConfigureAwait(false)).TrimEnd('\r', '\n');
            if (!DiagnosticGitMetadata.Matches(_root, gitDir, commonDir))
                throw new GitException("The local diagnostic requires a standalone repository in this project.", GitFailureKind.InvalidInput);
        }
        finally { _lock.Release(); }
    }

    public async Task<GitStatus> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try { await ValidateAsync(cancellationToken).ConfigureAwait(false); return await StatusAsync(cancellationToken).ConfigureAwait(false); }
        finally { _lock.Release(); }
    }

    // Restrict diffs to a currently reported literal path.
    public async Task<string> GetDiffAsync(string path, GitDiffKind kind, CancellationToken cancellationToken = default)
    {
        path = ValidatePath(path);
        if (kind is not (GitDiffKind.Staged or GitDiffKind.Worktree)) throw new ArgumentOutOfRangeException(nameof(kind));
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var status = await StatusAsync(cancellationToken).ConfigureAwait(false);
            var change = status.Changes.FirstOrDefault(item => item.Path == path);
            if (change is null) throw new GitException("The selected file changed. Refresh and try again.", GitFailureKind.InvalidInput, "diff");
            if (change.Untracked)
            {
                if (kind == GitDiffKind.Staged) return "This file is not staged.";
                var untracked = await _git.RunAsync(_root,
                    ["--no-optional-locks", "--literal-pathspecs", "diff", "--no-index", "--no-ext-diff", "--no-textconv", "--no-color", "--", "/dev/null", path],
                    false, cancellationToken, allowDiffExitOne: true).ConfigureAwait(false);
                return DiffText(untracked);
            }
            var args = new List<string> { "--no-optional-locks", "--literal-pathspecs", "diff", "--no-ext-diff", "--no-textconv", "--no-color" };
            if (kind == GitDiffKind.Staged) args.Add("--cached");
            args.Add("--");
            if (change.OriginalPath is string original) args.Add(ValidatePath(original));
            args.Add(path);
            var bytes = await _git.RunAsync(_root, args, false, cancellationToken).ConfigureAwait(false);
            return DiffText(bytes);
        }
        finally { _lock.Release(); }
    }

    private static string DiffText(byte[] bytes)
    {
        if (bytes.Length > 1024 * 1024)
            throw new GitException("This diff is too large to preview in the panel.", GitFailureKind.CommandFailed, "diff");
        return Encoding.UTF8.GetString(bytes);
    }

    public async Task InitializeAsync(string initialBranch = "main", CancellationToken cancellationToken = default)
    {
        ValidateName(initialBranch);
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            // Never reinitialize an existing, ancestor, or bare repository.
            for (DirectoryInfo? directory = new DirectoryInfo(_root); directory is not null; directory = directory.Parent)
            {
                if (Path.Exists(Path.Combine(directory.FullName, ".git")) ||
                    (File.Exists(Path.Combine(directory.FullName, "HEAD")) &&
                     Directory.Exists(Path.Combine(directory.FullName, "objects")) &&
                     File.Exists(Path.Combine(directory.FullName, "config"))))
                    throw new GitException("This folder is already in a Git repository.", GitFailureKind.InvalidInput, "init");
            }
            await _git.RunAsync(_root, ["check-ref-format", "--branch", initialBranch], false, cancellationToken).ConfigureAwait(false);
            await _git.RunAsync(_root, ["init", "--initial-branch=" + initialBranch], false, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public async Task<IReadOnlyList<GitRemote>> GetRemotesAsync(CancellationToken cancellationToken = default)
    {
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var names = await RemoteNamesAsync(cancellationToken).ConfigureAwait(false);
            var result = new List<GitRemote>(names.Count);
            foreach (var name in names)
            {
                var raw = GitProcess.Text(await _git.RunAsync(_root, ["remote", "get-url", "--", name], false, cancellationToken).ConfigureAwait(false)).TrimEnd('\r', '\n');
                result.Add(new GitRemote(name, SafeRemoteDisplay(raw)));
            }
            return result;
        }
        finally { _lock.Release(); }
    }

    public async Task AddRemoteAsync(string name, string url, CancellationToken cancellationToken = default)
    {
        ValidateRemoteName(name);
        ValidateRemoteUrl(url);
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            await _git.RunAsync(_root, ["remote", "add", "--", name, url], false, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public async Task FetchAsync(string remote, CancellationToken cancellationToken = default)
    {
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            await RequireRemoteAsync(remote, cancellationToken).ConfigureAwait(false);
            await _git.RunAsync(_root, ["fetch", "--", remote], true, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public async Task PublishBranchAsync(string remote, string branch, CancellationToken cancellationToken = default)
    {
        ValidateName(branch);
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var status = await StatusAsync(cancellationToken).ConfigureAwait(false);
            if (status.Detached || status.Unborn || status.Branch != branch || status.Upstream is not null)
                throw new GitException("Select the current unpublished branch.", GitFailureKind.InvalidInput, "push");
            await RequireRemoteAsync(remote, cancellationToken).ConfigureAwait(false);
            var reference = "refs/heads/" + branch;
            await _git.RunAsync(_root, ["push", "--porcelain", "--set-upstream", "--no-follow-tags", "--recurse-submodules=no", "--", remote, reference + ":" + reference], true, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public async Task<IReadOnlyList<GitHistoryEntry>> GetHistoryAsync(int maxCount = 50, CancellationToken cancellationToken = default)
    {
        if (maxCount is < 1 or > 200) throw new ArgumentOutOfRangeException(nameof(maxCount));
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            if ((await StatusAsync(cancellationToken).ConfigureAwait(false)).Unborn) return Array.Empty<GitHistoryEntry>();
            var output = GitProcess.Text(await _git.RunAsync(_root,
                ["log", "-z", "--max-count=" + maxCount.ToString(System.Globalization.CultureInfo.InvariantCulture), "--format=%H%x00%an%x00%aI%x00%s"], false, cancellationToken).ConfigureAwait(false));
            if (output.Length == 0) return Array.Empty<GitHistoryEntry>();
            if (output[^1] != '\0') throw new GitException("Git returned incomplete history.", GitFailureKind.CommandFailed, "log");
            var fields = output.Split('\0');
            if ((fields.Length - 1) % 4 != 0) throw new GitException("Git returned invalid history.", GitFailureKind.CommandFailed, "log");
            var result = new List<GitHistoryEntry>((fields.Length - 1) / 4);
            for (var i = 0; i < fields.Length - 1; i += 4)
            {
                if (fields[i].Length is not (40 or 64) || !fields[i].All(Uri.IsHexDigit) ||
                    !DateTimeOffset.TryParseExact(fields[i + 2], "yyyy-MM-dd'T'HH:mm:sszzz", System.Globalization.CultureInfo.InvariantCulture,
                        System.Globalization.DateTimeStyles.None, out var time))
                    throw new GitException("Git returned invalid history.", GitFailureKind.CommandFailed, "log");
                result.Add(new GitHistoryEntry(fields[i], fields[i + 1], time, fields[i + 3]));
            }
            return result;
        }
        finally { _lock.Release(); }
    }

    public Task StageAsync(IReadOnlyCollection<string> paths, CancellationToken cancellationToken = default) =>
        ChangeIndexAsync(paths, true, cancellationToken);

    public Task UnstageAsync(IReadOnlyCollection<string> paths, CancellationToken cancellationToken = default) =>
        ChangeIndexAsync(paths, false, cancellationToken);

    public async Task<IReadOnlyList<string>> GetBranchesAsync(CancellationToken cancellationToken = default)
    {
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var output = GitProcess.Text(await _git.RunAsync(_root,
                ["for-each-ref", "--format=%(refname:short)", "refs/heads/"], false, cancellationToken).ConfigureAwait(false));
            return output.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(line => line.TrimEnd('\r')).ToArray();
        }
        finally { _lock.Release(); }
    }

    public async Task CreateBranchAsync(string branch, CancellationToken cancellationToken = default)
    {
        ValidateName(branch);
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            await _git.RunAsync(_root, ["check-ref-format", "--branch", branch], false, cancellationToken).ConfigureAwait(false);
            if (await BranchExistsAsync(branch, cancellationToken).ConfigureAwait(false))
                throw new GitException("That branch already exists.", GitFailureKind.BranchExists, "branch");
            await _git.RunAsync(_root, ["branch", branch], false, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    private async Task ChangeIndexAsync(IReadOnlyCollection<string> paths, bool stage, CancellationToken token)
    {
        ArgumentNullException.ThrowIfNull(paths);
        var selected = paths.Select(ValidatePath).Distinct(StringComparer.Ordinal).ToArray();
        if (selected.Length == 0) throw new ArgumentException("Select at least one file.", nameof(paths));
        if (selected.Sum(path => (long)path.Length + 3) > 8000)
            throw new GitException("Select fewer files at a time.", GitFailureKind.InvalidInput, stage ? "add" : "restore");
        await _lock.WaitAsync(token).ConfigureAwait(false);
        try
        {
            await ValidateAsync(token).ConfigureAwait(false);
            var status = await StatusAsync(token).ConfigureAwait(false);
            var available = status.Changes.SelectMany(c => c.OriginalPath is string original ? new[] { c.Path, original } : new[] { c.Path });
            var known = new HashSet<string>(available, StringComparer.Ordinal);
            if (selected.Any(path => !known.Contains(path))) throw new GitException("The selected files changed. Refresh and try again.");
            if (!stage && status.Unborn)
            {
                // Remove selected index entries only; the worktree bytes are untouched.
                var format = GitProcess.Text(await _git.RunAsync(_root, ["rev-parse", "--show-object-format"], false, token).ConfigureAwait(false)).Trim();
                var length = format switch { "sha1" => 40, "sha256" => 64, _ => throw new GitException("Unsupported repository object format.") };
                var input = Encoding.UTF8.GetBytes(string.Concat(selected.Select(path => "0 " + new string('0', length) + "\t" + path + "\0")));
                await _git.RunAsync(_root, ["update-index", "-z", "--index-info"], false, token, input).ConfigureAwait(false);
            }
            else
            {
                var args = new List<string> { "--literal-pathspecs" };
                if (stage) args.Add("add"); else { args.Add("restore"); args.Add("--staged"); }
                args.Add("--"); args.AddRange(selected);
                await _git.RunAsync(_root, args, false, token).ConfigureAwait(false);
            }
        }
        finally { _lock.Release(); }
    }

    // The caller must display the complete staged index and pass the token from that status.
    public async Task CommitAsync(string message, string reviewedStatusToken, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(message) || message.IndexOf('\0') >= 0) throw new ArgumentException("Enter a commit message.", nameof(message));
        if (string.IsNullOrWhiteSpace(reviewedStatusToken)) throw new ArgumentException("Review staged changes first.", nameof(reviewedStatusToken));
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var status = await StatusAsync(cancellationToken).ConfigureAwait(false);
            if (status.ReviewToken != reviewedStatusToken || status.Changes.Any(c => c.Conflicted) ||
                !status.Changes.Any(c => !c.Untracked && c.Index != '.'))
                throw new GitException("Staged changes changed or need attention. Review them again.", status.Changes.Any(c => c.Conflicted) ? GitFailureKind.Conflict : GitFailureKind.InvalidInput, "commit");
            try
            {
                await _git.RunAsync(_root, ["var", "GIT_AUTHOR_IDENT"], false, cancellationToken).ConfigureAwait(false);
                await _git.RunAsync(_root, ["var", "GIT_COMMITTER_IDENT"], false, cancellationToken).ConfigureAwait(false);
            }
            catch (GitException exception) when (IsCompletedIdentityCommandFailure(exception))
            {
                throw new GitException("Git could not resolve the author or committer identity. Check the Git name, email, and configuration.",
                    GitFailureKind.CommandFailed, "commit", exception.ExitCode);
            }
            await _git.RunAsync(_root, ["commit", "-m", message], false, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    private static bool IsCompletedIdentityCommandFailure(GitException exception) =>
        exception.Kind == GitFailureKind.CommandFailed && exception.Operation == "var" && exception.ExitCode is not null and not 0;

    public async Task SwitchBranchAsync(string branch, IEditorDocumentGate documentGate, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(documentGate);
        ValidateName(branch);
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ChangeWorktreeAsync(documentGate, GitWorktreeOperation.SwitchBranch, async token =>
            {
                await _git.RunAsync(_root, ["check-ref-format", "--branch", branch], false, token).ConfigureAwait(false);
                if (!await BranchExistsAsync(branch, token).ConfigureAwait(false))
                    throw new GitException("That branch no longer exists.", GitFailureKind.BranchMissing, "switch");
                await _git.RunAsync(_root, ["switch", "--no-guess", branch], false, token).ConfigureAwait(false);
            }, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public async Task PullAsync(IEditorDocumentGate documentGate, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(documentGate);
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ChangeWorktreeAsync(documentGate, GitWorktreeOperation.Pull, async token =>
            {
                var (remote, reference) = await UpstreamAsync(token).ConfigureAwait(false);
                await _git.RunAsync(_root, ["-c", "merge.autostash=false", "-c", "rebase.autostash=false", "pull", "--ff-only", "--no-rebase", "--", remote, reference], true, token).ConfigureAwait(false);
            }, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public async Task PushAsync(CancellationToken cancellationToken = default)
    {
        await _lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await ValidateAsync(cancellationToken).ConfigureAwait(false);
            var (remote, reference) = await UpstreamAsync(cancellationToken).ConfigureAwait(false);
            await _git.RunAsync(_root, ["push", "--porcelain", "--no-follow-tags", "--recurse-submodules=no", "--", remote, "HEAD:" + reference], true, cancellationToken).ConfigureAwait(false);
        }
        finally { _lock.Release(); }
    }

    public static async Task CloneAsync(string gitExecutable, string remoteUrl, string destination, CancellationToken cancellationToken = default,
        IGitProcessObserver? processObserver = null)
    {
        ValidateRemoteUrl(remoteUrl);
        if (!Path.IsPathFullyQualified(destination)) throw new ArgumentException("Select a destination folder.", nameof(destination));
        destination = Path.TrimEndingDirectorySeparator(Path.GetFullPath(destination));
        var parent = Path.GetDirectoryName(destination);
        if (parent is null || !Directory.Exists(parent)) throw new ArgumentException("Select an existing parent folder.", nameof(destination));
        var state = States.GetOrAdd(destination, _ => new RepositoryState());
        await state.Lock.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (Path.Exists(destination)) throw new GitException("The destination already exists.");
            var git = new GitProcess(gitExecutable, () => state.Quarantined, () => state.Quarantined = true, processObserver);
            await git.RunAsync(parent, ["clone", "--", remoteUrl, destination], true, cancellationToken).ConfigureAwait(false);
        }
        finally { state.Lock.Release(); }
    }

    private async Task ChangeWorktreeAsync(IEditorDocumentGate gate, GitWorktreeOperation operation, Func<CancellationToken, Task> action, CancellationToken token)
    {
        await ValidateAsync(token).ConfigureAwait(false);
        if (!(await StatusAsync(token).ConfigureAwait(false)).IsClean) throw new GitException("Save or resolve repository changes first.", GitFailureKind.DirtyWorktree, operation.ToString());
        var lease = await gate.AcquireWorktreeLeaseAsync(_root, operation, token).ConfigureAwait(false)
            ?? throw new GitException("Save or close modified Editor documents first.");
        try
        {
            // Admission can take time; validate again while the Editor lease is held.
            await ValidateAsync(token).ConfigureAwait(false);
            if (!(await StatusAsync(token).ConfigureAwait(false)).IsClean) throw new GitException("Repository changes appeared. Refresh and try again.", GitFailureKind.DirtyWorktree, operation.ToString());
            await action(token).ConfigureAwait(false);
        }
        finally
        {
            // Refresh before releasing the Editor lease, even after cancellation or failure.
            // The caller still refreshes its display and handles an unavailable repository.
            try { await ValidateAsync(CancellationToken.None).ConfigureAwait(false); await StatusAsync(CancellationToken.None).ConfigureAwait(false); }
            catch (Exception) { }
            // Keep the Editor frozen if a mutating child may still be running.
            if (_state.Quarantined) _state.HeldLease = lease;
            else await lease.DisposeAsync().ConfigureAwait(false);
        }
    }

    private async Task ValidateAsync(CancellationToken token)
    {
        if (!Path.Exists(Path.Combine(_root, ".git")))
        {
            for (DirectoryInfo? directory = new DirectoryInfo(_root); directory is not null; directory = directory.Parent)
            {
                if (File.Exists(Path.Combine(directory.FullName, "HEAD")) && Directory.Exists(Path.Combine(directory.FullName, "objects")) && File.Exists(Path.Combine(directory.FullName, "config")))
                    throw new GitException("A bare repository is not an Editor checkout.", GitFailureKind.InvalidInput, "status");
                if (directory.FullName != _root && Path.Exists(Path.Combine(directory.FullName, ".git")))
                    throw new GitException("This project is inside another checkout. Open that repository's project root.", GitFailureKind.InvalidInput, "status");
            }
            throw new GitException("This project is not a Git repository.", GitFailureKind.NotRepository, "status");
        }
        var root = GitProcess.Text(await _git.RunAsync(_root, ["rev-parse", "--show-toplevel"], false, token).ConfigureAwait(false)).TrimEnd('\r', '\n');
        var bare = GitProcess.Text(await _git.RunAsync(_root, ["rev-parse", "--is-bare-repository"], false, token).ConfigureAwait(false)).Trim();
        if (bare != "false" || !string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)), _root, StringComparison.OrdinalIgnoreCase))
            throw new GitException("The project folder is not the root of this Git repository.", GitFailureKind.NotRepository, "status");
    }

    private async Task<GitStatus> StatusAsync(CancellationToken token) => GitStatusParser.Parse(await _git.RunAsync(_root,
        ["--no-optional-locks", "status", "--porcelain=v2", "--branch", "--untracked-files=all", "--ignore-submodules=none", "-z"], false, token).ConfigureAwait(false));

    private async Task<(string Remote, string Reference)> UpstreamAsync(CancellationToken token)
    {
        var status = await StatusAsync(token).ConfigureAwait(false);
        if (status.Detached || status.Unborn || status.Branch is null || status.Upstream is null)
            throw new GitException("The current branch needs an upstream branch.", GitFailureKind.MissingUpstream, "upstream");
        var value = GitProcess.Text(await _git.RunAsync(_root, ["for-each-ref", "--format=%(upstream:remotename)%00%(upstream:remoteref)", "refs/heads/" + status.Branch], false, token).ConfigureAwait(false)).TrimEnd('\r', '\n');
        var parts = value.Split('\0');
        if (parts.Length != 2 || string.IsNullOrWhiteSpace(parts[0]) || parts[0] == "." || parts[0].StartsWith('-') ||
            !parts[1].StartsWith("refs/heads/", StringComparison.Ordinal))
            throw new GitException("The current branch needs a remote upstream branch.", GitFailureKind.MissingUpstream, "upstream");
        return (parts[0], parts[1]);
    }

    private async Task<IReadOnlyList<string>> RemoteNamesAsync(CancellationToken token)
    {
        var output = GitProcess.Text(await _git.RunAsync(_root, ["remote"], false, token).ConfigureAwait(false));
        return output.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(line => line.TrimEnd('\r')).ToArray();
    }

    private async Task<bool> BranchExistsAsync(string branch, CancellationToken token)
    {
        var reference = "refs/heads/" + branch;
        var output = GitProcess.Text(await _git.RunAsync(_root,
            ["for-each-ref", "--format=%(refname)", reference], false, token).ConfigureAwait(false));
        return output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Any(line => string.Equals(line.TrimEnd('\r'), reference, StringComparison.Ordinal));
    }

    private async Task RequireRemoteAsync(string name, CancellationToken token)
    {
        ValidateRemoteName(name);
        if (!(await RemoteNamesAsync(token).ConfigureAwait(false)).Contains(name, StringComparer.Ordinal))
            throw new GitException("Select an existing remote.", GitFailureKind.InvalidInput, "remote");
    }

    private static void ValidateRemoteName(string name)
    {
        if (string.IsNullOrEmpty(name) || name is "." or ".." || name.StartsWith('-') || name.Any(char.IsControl) || name.Any(char.IsWhiteSpace) ||
            name.IndexOfAny(['\\', ':']) >= 0)
            throw new ArgumentException("Enter a valid remote name.", nameof(name));
    }

    private static string SafeRemoteDisplay(string url)
    {
        string? host = null;
        string? scheme = null;
        string? path = null;
        if (Uri.TryCreate(url, UriKind.Absolute, out var uri) && (uri.Scheme == "https" || uri.Scheme == "ssh"))
        {
            host = uri.Host;
            scheme = uri.Scheme;
            if ((uri.UserInfo.Length == 0 || uri.Scheme == "ssh" && !Uri.UnescapeDataString(uri.UserInfo).Contains(':')) &&
                uri.Query.Length == 0 && uri.Fragment.Length == 0)
                path = Uri.UnescapeDataString(uri.AbsolutePath);
        }
        else
        {
            var colon = url.IndexOf(':');
            var at = colon > 0 ? url.LastIndexOf('@', colon - 1) : -1;
            if (at >= 0 && at + 1 < colon)
            {
                host = url[(at + 1)..colon]; scheme = "ssh";
                if (!url.Contains('?') && !url.Contains('#')) path = url[(colon + 1)..];
            }
        }
        if (string.IsNullOrEmpty(host) || !host.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '-' or ':' or '[' or ']'))
            return "Configured remote";
        var parts = (path ?? "").Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 2 && parts.All(part => part.Length <= 128 &&
                part.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-') &&
                !part.Contains("token", StringComparison.OrdinalIgnoreCase) &&
                !part.Contains("secret", StringComparison.OrdinalIgnoreCase) &&
                !part.Contains("password", StringComparison.OrdinalIgnoreCase)))
            return scheme + "://" + host + "/" + string.Join('/', parts);
        return scheme + "://" + host;
    }

    private static string ValidatePath(string path)
    {
        if (string.IsNullOrEmpty(path) || path.IndexOf('\0') >= 0 || Path.IsPathRooted(path) ||
            path.Split('/', '\\').Any(part => part is "" or "." or ".."))
            throw new ArgumentException("Select a valid repository file.", nameof(path));
        return path;
    }

    private static void ValidateName(string branch)
    {
        if (string.IsNullOrWhiteSpace(branch) || branch.StartsWith('-') || branch.StartsWith("@{-", StringComparison.Ordinal) || branch.IndexOf('\0') >= 0)
            throw new ArgumentException("Select a valid branch.", nameof(branch));
    }

    private static void ValidateRemoteUrl(string url)
    {
        if (string.IsNullOrWhiteSpace(url) || url.StartsWith('-') || url.Any(char.IsControl) ||
            url.Contains('?') || url.Contains('#'))
            throw new ArgumentException("Enter a valid remote URL.", nameof(url));
        if (Uri.TryCreate(url, UriKind.Absolute, out var uri) && (uri.Scheme == "https" || uri.Scheme == "ssh"))
        {
            if (string.IsNullOrWhiteSpace(uri.Host))
                throw new ArgumentException("Enter a valid remote URL.", nameof(url));
            if (uri.Scheme == "https" && uri.UserInfo.Length != 0 ||
                uri.Scheme == "ssh" && Uri.UnescapeDataString(uri.UserInfo).Contains(':'))
                throw new ArgumentException("Remove credentials from the remote URL.", nameof(url));
            return;
        }
        // Git's SSH shorthand permits a user name but no password.
        var colon = url.IndexOf(':');
        var at = colon > 0 ? url[..colon].IndexOf('@') : -1;
        if (colon <= 0 || at <= 0 || at == colon - 1 || url[(colon + 1)..].Length == 0 ||
            url[..colon].Contains('/') || url[..colon].Any(char.IsWhiteSpace) || url[..colon].LastIndexOf('@') != at)
            throw new ArgumentException("Enter an HTTPS or SSH remote URL.", nameof(url));
    }
}
