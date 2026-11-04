using System.Windows;
using System.Windows.Controls;
using Dluma.EditorGit.Core;
using Dluma.QuestGraph.Protocol;
using Microsoft.Win32;

namespace Dluma.EditorGit;

internal partial class GitView : UserControl, IDisposable
{
    private static readonly List<IAsyncDisposable> RetainedProjectLeases = [];
    private readonly string _projectRoot;
    private readonly ulong _projectGeneration;
    private readonly Action _requireProjectHost;
    private readonly IEditorGitProjectGate? _projectGate;
    private readonly IEditorDocumentGate? _documentGate;
    private readonly IGitProcessObserver? _processObserver;
    private readonly Func<string, CancellationToken, Task<GitContextMessage>>? _registerGit;
    private GitRepository? _repository;
    private GitStatus? _status;
    private string? _reviewToken;
    private CancellationTokenSource? _operation;
    private Task _running = Task.CompletedTask;
    private string? _cloneTarget;
    private bool _loaded, _disposed, _admitted, _enabled, _mutationsAvailable, _worktreeAvailable, _notRepository;
    internal event Action? CommandReady;
    internal bool IsReadyForCommands => _admitted && _enabled && !_disposed && _repository is not null && _running.IsCompleted;

    internal GitView(string projectRoot, ulong projectGeneration, Action requireProjectHost,
        IEditorGitProjectGate? projectGate, IEditorDocumentGate? documentGate,
        IGitProcessObserver? processObserver = null,
        Func<string, CancellationToken, Task<GitContextMessage>>? registerGit = null)
    {
        _projectRoot = projectRoot;
        _projectGeneration = projectGeneration;
        _requireProjectHost = requireProjectHost;
        _projectGate = projectGate;
        _documentGate = documentGate;
        _processObserver = processObserver;
        _registerGit = registerGit;
        InitializeComponent();
        RemotePicker.SelectionChanged += (_, _) => UpdateActions(!_running.IsCompleted);
        BranchName.SelectionChanged += (_, _) => UpdateActions(!_running.IsCompleted);
        IsEnabled = false;
        UpdateActions(false);
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_loaded || !_admitted) return;
        _loaded = true;
        var executable = FindGit();
        if (executable is null) { Status.Text = "Choose Git to continue."; return; }
        UseGit(executable);
    }

    internal void AdmitBoundPanel()
    {
        if (_disposed) return;
        _admitted = true;
        IsEnabled = true;
        if (IsLoaded) OnLoaded(this, new RoutedEventArgs());
    }

    internal void ApplyContext(GitContextMessage context)
    {
        EditorGitProtocol.Validate(context);
        if (!string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(context.ProjectRoot)),
                Path.TrimEndingDirectorySeparator(Path.GetFullPath(_projectRoot)), StringComparison.OrdinalIgnoreCase) ||
            context.ProjectGeneration != _projectGeneration)
        {
            _enabled = _mutationsAvailable = _worktreeAvailable = false;
            _operation?.Cancel();
            Status.Text = "The Editor project changed. Reopen Git.";
        }
        else
        {
            _enabled = context.Enabled;
            _mutationsAvailable = context.MutationsAvailable;
            _worktreeAvailable = context.WorktreeAvailable;
            if (!_enabled) { _operation?.Cancel(); Status.Text = context.Message; }
        }
        UpdateActions(!_running.IsCompleted);
    }

    private static string? FindGit()
    {
        foreach (var directory in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
        {
            try
            {
                if (!Path.IsPathFullyQualified(directory)) continue;
                var candidate = Path.Combine(directory, "git.exe");
                if (File.Exists(candidate)) return Path.GetFullPath(candidate);
            }
            catch (Exception exception) when (exception is ArgumentException or IOException or UnauthorizedAccessException) { }
        }
        return null;
    }

    private void OnChooseGit(object sender, RoutedEventArgs e)
    {
        if (!_admitted || !_running.IsCompleted) return;
        var dialog = new OpenFileDialog { Title = "Choose Git", Filter = "Git (git.exe)|git.exe", CheckFileExists = true };
        if (dialog.ShowDialog() == true) UseGit(dialog.FileName);
    }

    private void UseGit(string executable)
    {
        if (!_admitted || _disposed || !_running.IsCompleted) return;
        _running = UseGitAsync(executable);
    }

    private async Task UseGitAsync(string executable)
    {
        using var operation = new CancellationTokenSource();
        _operation = operation;
        UpdateActions(true);
        try
        {
            _requireProjectHost();
            GitExecutable.Text = executable;
            _enabled = _mutationsAvailable = _worktreeAvailable = false;
            _status = null;
            _repository = null;
            if (_registerGit is null || _processObserver is null)
                throw new GitException("Git is waiting for the Editor connection.");
            ApplyContext(await _registerGit(executable, operation.Token).ConfigureAwait(true));
            if (!_enabled) throw new GitException("Git is unavailable for this project.");
            _requireProjectHost();
            _repository = new GitRepository(executable, _projectRoot, _processObserver);
            GitExecutable.Text = executable;
            await RefreshAsync(operation.Token).ConfigureAwait(true);
            Status.Text = _notRepository ? "Initialize this project or clone a repository." : "Ready.";
        }
        catch (Exception exception) when (exception is GitException or ArgumentException or IOException or InvalidOperationException or OperationCanceledException or UnauthorizedAccessException)
        {
            if (!_disposed) Status.Text = exception is OperationCanceledException ? "Canceled." : exception.Message;
        }
        finally
        {
            _operation = null;
            if (!_disposed)
            {
                UpdateActions(false);
                if (_enabled && (_status is not null || _notRepository))
                    _ = Dispatcher.BeginInvoke(() => { if (IsReadyForCommands) CommandReady?.Invoke(); });
            }
        }
    }

    private void Run(Func<CancellationToken, Task> operation, GitProjectOperation? kind = null, string? destination = null)
    {
        if (!_admitted || !_enabled || _disposed || !_running.IsCompleted || _repository is null) return;
        if (kind is not null && (_projectGate is null || !_mutationsAvailable)) return;
        if (kind is GitProjectOperation.Pull or GitProjectOperation.SwitchBranch && (!_worktreeAvailable || _documentGate is null)) return;
        _running = RunAsync(operation, kind, destination);
    }

    private async Task RunAsync(Func<CancellationToken, Task> action, GitProjectOperation? kind, string? destination)
    {
        using var operation = new CancellationTokenSource();
        _operation = operation;
        UpdateActions(true);
        Status.Text = "Working…";
        string? result = null;
        IAsyncDisposable? projectLease = null;
        try
        {
            _requireProjectHost();
            if (kind is { } mutation)
            {
                projectLease = await _projectGate!.AcquireProjectLeaseAsync(
                    _projectRoot, _projectGeneration, mutation, operation.Token, destination).ConfigureAwait(true)
                    ?? throw new GitException("The Editor project changed. Reopen Git.");
                _requireProjectHost();
            }
            await action(operation.Token).ConfigureAwait(true);
            result = "Done.";
        }
        catch (OperationCanceledException) { result = "Canceled. Refresh before retrying."; }
        catch (Exception exception) when (exception is GitException or ArgumentException or IOException or
                                                     InvalidOperationException or UnauthorizedAccessException)
        {
            result = exception is GitException { OutcomeUncertain: true } git
                ? git.Message + " Check the result before retrying." : exception.Message;
        }
        catch (Exception) { result = "Git could not complete the action."; }
        finally
        {
            if (!_disposed && _admitted && _enabled)
            {
                try
                {
                    _requireProjectHost();
                    using var refresh = new CancellationTokenSource(TimeSpan.FromSeconds(10));
                    await RefreshAsync(refresh.Token).ConfigureAwait(true);
                }
                catch (Exception exception)
                {
                    _status = null;
                    Changes.ItemsSource = null;
                    result ??= exception is GitException git ? git.Message : "Could not refresh Git status.";
                }
                Status.Text = result ?? "Refresh to continue.";
            }
            _operation = null;
            if (projectLease is not null)
            {
                if (_repository?.RequiresRecovery == true ||
                    (kind == GitProjectOperation.Clone && _cloneTarget is not null && GitRepository.RequiresRecoveryAt(_cloneTarget)))
                    RetainedProjectLeases.Add(projectLease);
                else
                {
                    try { await projectLease.DisposeAsync().ConfigureAwait(true); }
                    catch (Exception)
                    {
                        if (!_disposed) Status.Text = "The Editor project could not be released.";
                    }
                }
            }
            if (!_disposed) UpdateActions(false);
        }
    }

    private async Task RefreshAsync(CancellationToken token)
    {
        if (_repository is null) return;
        try { _status = await _repository.GetStatusAsync(token).ConfigureAwait(true); }
        catch (GitException exception) when (exception.Kind == GitFailureKind.NotRepository)
        {
            _notRepository = true;
            _status = null;
            _reviewToken = null;
            Changes.ItemsSource = null;
            HistoryList.ItemsSource = null;
            HistoryList.Visibility = Visibility.Collapsed;
            Changes.Visibility = Visibility.Visible;
            BranchName.ItemsSource = null;
            RemotePicker.ItemsSource = null;
            BranchStatus.Text = "No repository";
            Commit.Content = "Commit…";
            return;
        }
        _notRepository = false;
        _requireProjectHost();
        _reviewToken = null;
        Commit.Content = "Commit…";
        Changes.ItemsSource = _status.Changes;
        BranchStatus.Text = _status.Detached ? "Detached HEAD" : _status.Branch ?? "New repository";
        if (_status.Upstream is not null) BranchStatus.Text += $"  ↑{_status.Ahead} ↓{_status.Behind}";
        BranchStatus.ToolTip = _projectRoot;
        var oldBranch = BranchName.Text;
        var oldRemote = (RemotePicker.SelectedItem as GitRemote)?.Name;
        BranchName.ItemsSource = await _repository.GetBranchesAsync(token).ConfigureAwait(true);
        BranchName.Text = string.IsNullOrEmpty(oldBranch) ? _status.Branch ?? "" : oldBranch;
        var remotes = await _repository.GetRemotesAsync(token).ConfigureAwait(true);
        RemotePicker.ItemsSource = remotes;
        RemotePicker.SelectedItem = remotes.FirstOrDefault(remote => remote.Name == oldRemote) ?? remotes.FirstOrDefault();
    }

    private void UpdateActions(bool busy)
    {
        Actions.IsEnabled = !busy && _enabled && _repository is not null;
        Cancel.IsEnabled = busy;
        var canMutate = !busy && _enabled && _mutationsAvailable && _projectGate is not null && _repository is not null && !_repository.RequiresRecovery;
        CloneOptions.IsEnabled = canMutate;
        var repo = _status is not null && !_notRepository;
        var namedBranch = repo && !_status!.Detached && !_status.Unborn && _status.Branch is not null;
        var hasRemote = RemotePicker.SelectedItem is GitRemote;
        Initialize.IsEnabled = canMutate && _notRepository;
        Clone.IsEnabled = canMutate;
        Stage.IsEnabled = Unstage.IsEnabled = canMutate && repo && _status!.Changes.Count != 0;
        AddRemote.IsEnabled = canMutate && repo;
        NewBranch.IsEnabled = canMutate && repo && !_status!.Unborn;
        Commit.IsEnabled = canMutate && repo && _status!.Changes.Any(change => !change.Untracked && change.Index != '.') && !_status.Changes.Any(change => change.Conflicted);
        Fetch.IsEnabled = canMutate && repo && hasRemote;
        Publish.IsEnabled = canMutate && namedBranch && _status!.Upstream is null && hasRemote;
        Push.IsEnabled = canMutate && namedBranch && _status!.Upstream is not null;
        Pull.IsEnabled = canMutate && namedBranch && _status!.Upstream is not null && _status.IsClean && _worktreeAvailable && _documentGate is not null;
        SwitchBranch.IsEnabled = canMutate && repo && !_status!.Unborn && _status.IsClean && _worktreeAvailable && _documentGate is not null;
        History.IsEnabled = !busy && _enabled && repo && !_status!.Unborn;
    }

    private string[] SelectedPaths() => Changes.SelectedItems.Cast<GitChange>()
        .SelectMany(change => change.OriginalPath is { } original ? new[] { change.Path, original } : new[] { change.Path })
        .Distinct(StringComparer.Ordinal).ToArray();

    private void OnRefresh(object sender, RoutedEventArgs e)
    {
        Changes.Visibility = Visibility.Visible;
        HistoryList.Visibility = Visibility.Collapsed;
        Run(RefreshAsync);
    }
    private void OnInitialize(object sender, RoutedEventArgs e) =>
        Run(token => _repository!.InitializeAsync(cancellationToken: token), GitProjectOperation.Initialize);
    private void OnStage(object sender, RoutedEventArgs e)
    {
        var paths = SelectedPaths();
        if (paths.Length == 0) { Status.Text = "Select changed files first."; return; }
        Run(token => _repository!.StageAsync(paths, token), GitProjectOperation.Stage);
    }
    private void OnUnstage(object sender, RoutedEventArgs e)
    {
        var paths = SelectedPaths();
        if (paths.Length == 0) { Status.Text = "Select changed files first."; return; }
        Run(token => _repository!.UnstageAsync(paths, token), GitProjectOperation.Unstage);
    }
    private void OnPush(object sender, RoutedEventArgs e) => Run(token => _repository!.PushAsync(token), GitProjectOperation.Push);
    private void OnFetch(object sender, RoutedEventArgs e)
    {
        if (RemotePicker.SelectedItem is not GitRemote remote) { Status.Text = "Select a remote."; return; }
        Run(token => _repository!.FetchAsync(remote.Name, token), GitProjectOperation.Fetch);
    }
    private void OnPublish(object sender, RoutedEventArgs e)
    {
        if (RemotePicker.SelectedItem is not GitRemote remote || _status?.Branch is not string branch)
        { Status.Text = "Select a remote and branch."; return; }
        Run(token => _repository!.PublishBranchAsync(remote.Name, branch, token), GitProjectOperation.PublishBranch);
    }
    private void OnAddRemote(object sender, RoutedEventArgs e)
    {
        var name = RemoteName.Text;
        var url = AddRemoteUrl.Text;
        AddRemoteUrl.Clear();
        Run(token => _repository!.AddRemoteAsync(name, url, token), GitProjectOperation.AddRemote);
    }
    private void OnHistory(object sender, RoutedEventArgs e)
    {
        Run(async token =>
        {
            var entries = await _repository!.GetHistoryAsync(cancellationToken: token).ConfigureAwait(true);
            HistoryList.ItemsSource = entries;
            HistoryList.Visibility = Visibility.Visible;
            Changes.Visibility = Visibility.Collapsed;
        });
    }
    private void OnPull(object sender, RoutedEventArgs e)
    {
        if (_documentGate is not null) Run(token => _repository!.PullAsync(_documentGate, token), GitProjectOperation.Pull);
    }
    private void OnCreateBranch(object sender, RoutedEventArgs e)
    {
        var branch = BranchName.Text;
        Run(token => _repository!.CreateBranchAsync(branch, token), GitProjectOperation.CreateBranch);
    }
    private void OnSwitchBranch(object sender, RoutedEventArgs e)
    {
        var branch = BranchName.Text;
        if (_documentGate is not null) Run(token => _repository!.SwitchBranchAsync(branch, _documentGate, token), GitProjectOperation.SwitchBranch);
    }
    private async void OnCommit(object sender, RoutedEventArgs e)
    {
        if (!_admitted || _projectGate is null || _repository is null || !_running.IsCompleted || _disposed) return;
        if (_reviewToken is not null)
        {
            var token = _reviewToken;
            var message = CommitMessage.Text;
            Run(cancellation => _repository.CommitAsync(message, token, cancellation), GitProjectOperation.Commit);
            return;
        }
        using var operation = new CancellationTokenSource();
        _operation = operation;
        try
        {
            // Review the entire index, including changes staged outside this panel.
            _running = RefreshAsync(operation.Token);
            UpdateActions(true);
            await _running.ConfigureAwait(true);
            if (_disposed || _status is null) return;
            var staged = _status.Changes.Where(change => !change.Untracked && change.Index != '.').ToArray();
            if (staged.Length == 0) { Status.Text = "Stage files first."; return; }
            _reviewToken = _status.ReviewToken;
            Changes.ItemsSource = staged;
            Changes.Visibility = Visibility.Visible;
            HistoryList.Visibility = Visibility.Collapsed;
            Commit.Content = "Commit staged";
            Status.Text = $"Review {staged.Length} staged file(s).";
        }
        catch (Exception exception) when (exception is GitException or IOException or InvalidOperationException or OperationCanceledException)
        {
            Status.Text = exception.Message;
        }
        finally { _operation = null; if (!_disposed) UpdateActions(false); }
    }

    internal void HandleCommand(GitCommandMessage command)
    {
        try { EditorGitProtocol.Validate(command); }
        catch (InvalidDataException) { Status.Text = "The Git command is invalid."; return; }
        if (!_admitted || !_enabled || _disposed || !_running.IsCompleted)
        { Status.Text = "Git is not ready."; return; }
        if (command.Command is "stage" or "unstage")
        {
            var paths = command.Paths;
            if (paths is null || paths.Count == 0 || _status is null)
            { Status.Text = "Select changed files first."; return; }
            var known = new HashSet<string>(_status.Changes.SelectMany(change => change.OriginalPath is string original
                ? new[] { change.Path, original } : new[] { change.Path }), StringComparer.Ordinal);
            if (paths.Any(path => !known.Contains(path)))
            { Status.Text = "Selected files changed. Refresh and try again."; return; }
            if (command.Command == "stage" && Stage.IsEnabled)
                Run(token => _repository!.StageAsync(paths, token), GitProjectOperation.Stage);
            else if (command.Command == "unstage" && Unstage.IsEnabled)
                Run(token => _repository!.UnstageAsync(paths, token), GitProjectOperation.Unstage);
            else Status.Text = "This action is unavailable.";
            return;
        }
        switch (command.Command)
        {
            case "open": break;
            case "refresh": OnRefresh(this, new RoutedEventArgs()); break;
            case "history" when History.IsEnabled: OnHistory(this, new RoutedEventArgs()); break;
            case "initialize" when Initialize.IsEnabled: OnInitialize(this, new RoutedEventArgs()); break;
            case "commit" when Commit.IsEnabled:
                // A native command always enters review; only the panel button confirms it.
                _reviewToken = null;
                OnCommit(this, new RoutedEventArgs());
                break;
            case "clone" when Clone.IsEnabled: OnClone(this, new RoutedEventArgs()); break;
            case "addRemote" when AddRemote.IsEnabled: OnAddRemote(this, new RoutedEventArgs()); break;
            case "createBranch" when NewBranch.IsEnabled: OnCreateBranch(this, new RoutedEventArgs()); break;
            case "switchBranch" when SwitchBranch.IsEnabled: OnSwitchBranch(this, new RoutedEventArgs()); break;
            case "fetch" when Fetch.IsEnabled: OnFetch(this, new RoutedEventArgs()); break;
            case "publishBranch" when Publish.IsEnabled: OnPublish(this, new RoutedEventArgs()); break;
            case "pull" when Pull.IsEnabled: OnPull(this, new RoutedEventArgs()); break;
            case "push" when Push.IsEnabled: OnPush(this, new RoutedEventArgs()); break;
            case "settings": GitExecutable.Focus(); break;
            default: Status.Text = "This action is unavailable."; break;
        }
    }
    private void OnClone(object sender, RoutedEventArgs e) => CloneOptions.Visibility = Visibility.Visible;
    private void OnCloneClose(object sender, RoutedEventArgs e) => CloneOptions.Visibility = Visibility.Collapsed;
    private void OnCloneStart(object sender, RoutedEventArgs e)
    {
        var executable = GitExecutable.Text;
        var remote = RemoteUrl.Text;
        RemoteUrl.Clear();
        var destination = CloneDestination.Text;
        try
        {
            var relative = Path.GetRelativePath(_projectRoot, Path.GetFullPath(destination));
            if (relative != ".." && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) && !Path.IsPathRooted(relative))
                throw new ArgumentException("Choose a checkout folder outside the open project.");
            _cloneTarget = Path.GetFullPath(destination);
            Run(async token =>
            {
                await GitRepository.CloneAsync(executable, remote, destination, token, _processObserver).ConfigureAwait(true);
            }, GitProjectOperation.Clone, _cloneTarget);
        }
        catch (ArgumentException exception) { Status.Text = exception.Message; }
    }
    private void OnCancel(object sender, RoutedEventArgs e) => _operation?.Cancel();

    internal async Task StopAsync()
    {
        _admitted = false;
        IsEnabled = false;
        _operation?.Cancel();
        try { await _running.ConfigureAwait(true); }
        catch (Exception exception) when (exception is OperationCanceledException or GitException or IOException) { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _operation?.Cancel();
    }
}
