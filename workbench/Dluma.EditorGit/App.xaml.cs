using System.Security.Cryptography;
using System.Windows;
using Dluma.EditorWiki;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;

namespace Dluma.EditorGit;

public partial class App : Application
{
    private EditorIdentity? _editor;
    private EditorThemeService? _theme;
    private QuestBridgeClient? _bridge;
    private GitPanelHost? _panel;
    private GitView? _view;
    private NativeGitSession? _session;
    private GitCommandMessage? _pendingCommand;
    private bool _commandReady;
    private bool _stopping;

    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        try
        {
            var options = GitLaunchOptions.Parse(e.Args);
            _editor = new EditorIdentity(options.Host);
            _editor.MonitorExit(OnEditorExited);
            GitPanelHost.PrepareUiThread(options.Host, _editor);
            _theme = new EditorThemeService();
            _theme.Initialize(options.Host.ThemeConfig);
            _bridge = new QuestBridgeClient();
            _bridge.Disconnected += OnDisconnected;
            _bridge.NotificationReceived += OnNotification;
            await _bridge.ConnectAsync(options.Host.Pipe, options.Host.Token, TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            _editor.RequireCurrent();
            _bridge.RequireGitServerProcess(options.Host.EditorPid);
            _session = new NativeGitSession(_bridge, options, _editor.RequireParent);
            _session.ContextChanged += OnContextChanged;
            _session.Retired += RequestShutdown;
            var path = Environment.ProcessPath ?? throw new InvalidOperationException("The Git panel is unavailable.");
            await using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(file).ConfigureAwait(true));
            await _bridge.HelloAsync(options.Host.EditorPid, hash).ConfigureAwait(true);
            _editor.RequireCurrent();
            if (_stopping) return;
            var context = await _session.StateAsync(null).ConfigureAwait(true);
            if (_stopping) return;
            _view = new GitView(options.ProjectRoot, options.ProjectGeneration,
                _session.RequireCurrent, _session, _session, _session, _session.StateAsync);
            _view.CommandReady += OnCommandReady;
            _view.ApplyContext(context);
            _theme.AttachRoot(_view);
            _panel = new GitPanelHost(options.Host, _editor, _bridge, _view);
            _panel.CloseRequested += RequestShutdown;
            await _panel.AttachAsync().ConfigureAwait(true);
            OnCommandReady();
        }
        catch (Exception exception)
        {
            System.Diagnostics.Trace.TraceError("Editor Git startup: {0}", exception.GetType().Name);
            RequestShutdown();
        }
    }

    private void OnEditorExited(object? sender, EventArgs e) => RequestShutdown();
    private void OnDisconnected(object? sender, string reason) => RequestShutdown();
    private void OnNotification(object? sender, QuestBridgeEnvelope envelope)
    {
        try
        {
            if (envelope.Type == "gitContext")
            {
                var context = QuestBridgeClient.DeserializePayload<GitContextMessage>(envelope);
                _session?.ApplyContext(context);
                return;
            }
            if (envelope.Type == "gitCommand")
            {
                var command = QuestBridgeClient.DeserializePayload<GitCommandMessage>(envelope);
                PostUi(() =>
                {
                    if (_view is null || !_commandReady) _pendingCommand = command;
                    else _view.HandleCommand(command);
                });
                return;
            }
        }
        catch (Exception exception) when (exception is System.Text.Json.JsonException or InvalidDataException or ArgumentException)
        {
            _bridge?.AbortProtocolSession("Invalid Git panel notification.");
            RequestShutdown();
            return;
        }
        _bridge?.AbortProtocolSession("Unexpected Git panel notification.");
        RequestShutdown();
    }

    private void OnContextChanged(GitContextMessage context) => PostUi(() => _view?.ApplyContext(context));

    private void OnCommandReady()
    {
        if (!_stopping && _view?.IsReadyForCommands == true)
        {
            _commandReady = true;
            if (_pendingCommand is { } command)
            {
                _pendingCommand = null;
                _view.HandleCommand(command);
            }
        }
    }

    private void PostUi(Action action)
    {
        if (Dispatcher.HasShutdownStarted || Dispatcher.HasShutdownFinished) return;
        Dispatcher.BeginInvoke(() => { if (!_stopping) action(); });
    }

    private void RequestShutdown()
    {
        if (Dispatcher.HasShutdownStarted || Dispatcher.HasShutdownFinished) return;
        Dispatcher.BeginInvoke(async () =>
        {
            if (_stopping) return;
            _stopping = true;
            if (_view is not null) await _view.StopAsync().ConfigureAwait(true);
            Shutdown();
        });
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _stopping = true;
        if (_view is not null) _view.CommandReady -= OnCommandReady;
        _view?.Dispose();
        _panel?.Dispose();
        if (_session is not null)
        {
            _session.ContextChanged -= OnContextChanged;
            _session.Retired -= RequestShutdown;
            _session.Dispose();
        }
        if (_bridge is not null)
        {
            _bridge.Disconnected -= OnDisconnected;
            _bridge.NotificationReceived -= OnNotification;
            _bridge.Dispose();
        }
        _theme?.Dispose();
        _editor?.Dispose();
        base.OnExit(e);
    }
}
