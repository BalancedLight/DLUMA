using System.Security.Cryptography;
using System.Windows;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;

namespace Dluma.EditorWiki;

public partial class App : Application
{
    private EditorIdentity? _editor;
    private EditorThemeService? _theme;
    private QuestBridgeClient? _bridge;
    private EditorPanelHost? _panel;
    private bool _stopping;

    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        try
        {
            var options = LaunchOptions.Parse(e.Args);
            _editor = new EditorIdentity(options);
            _editor.MonitorExit(OnEditorExited);
            EditorPanelHost.PrepareUiThread(options, _editor);
            _theme = new EditorThemeService();
            _theme.Initialize(options.ThemeConfig);
            _bridge = new QuestBridgeClient();
            _bridge.Disconnected += OnDisconnected;
            _bridge.NotificationReceived += OnNotification;
            await _bridge.ConnectAsync(options.Pipe, options.Token, TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            _editor.RequireCurrent();
            _bridge.RequireServerProcess(options.EditorPid);
            var path = Environment.ProcessPath ?? throw new InvalidOperationException("The wiki process is unavailable.");
            await using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(file).ConfigureAwait(true));
            await _bridge.HelloAsync(options.EditorPid, hash).ConfigureAwait(true);
            _editor.RequireCurrent();
            if (_stopping) return;
            _panel = new EditorPanelHost(options, _editor, _bridge, _theme);
            await _panel.AttachAsync().ConfigureAwait(true);
        }
        catch (Exception)
        {
            if (!_stopping) Shutdown(1);
        }
    }

    private void OnEditorExited(object? sender, EventArgs e) => RequestShutdown();
    private void OnDisconnected(object? sender, string reason) => RequestShutdown();

    private void OnNotification(object? sender, QuestBridgeEnvelope envelope)
    {
        _bridge?.AbortProtocolSession("Unexpected Editor panel notification.");
        RequestShutdown();
    }

    private void RequestShutdown()
    {
        if (Dispatcher.HasShutdownStarted || Dispatcher.HasShutdownFinished) return;
        Dispatcher.BeginInvoke(() => { if (!_stopping) Shutdown(); });
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _stopping = true;
        _panel?.Dispose();
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
