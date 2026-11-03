using System.Windows;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph;

public partial class App : Application
{
    private QuestBridgeClient? _bridge;
    private MainViewModel? _viewModel;
    private EditorThemeService? _theme;
    private EditorPanelHost? _panelHost;

    private async void OnStartup(object sender, StartupEventArgs e)
    {
        LaunchOptions? options = null;
        MainWindow? window = null;
        try
        {
            options = LaunchOptions.Parse(e.Args);
            if (options.SmokeTest) { Shutdown(0); return; }
            EditorPanelHost.PrepareUiThread(options);
            if (options.PanelParent is not null)
                ShutdownMode = ShutdownMode.OnExplicitShutdown;
            _theme = new EditorThemeService();
            _theme.Initialize(options.ThemeConfigPath);
            _bridge = new QuestBridgeClient();
            var fileDialogs = new WpfFileDialogService();
            var documents = new QuestDocumentService();
            _viewModel = new MainViewModel(documents, fileDialogs, _bridge, options);
            window = new MainWindow(_viewModel);
            _theme.AttachWindow(window);
            MainWindow = window;
            if (options.PanelParent is not null)
            {
                _panelHost = new EditorPanelHost(window, _bridge);
                _ = _panelHost.AttachAsync(options.PanelParent.Value, options.PanelToken!.Value);
            }
            else
                window.Show();

            await _viewModel.InitializeAsync().ConfigureAwait(true);
            if (_panelHost is not null)
            {
                if (_bridge.AuthenticatedEditorProcessId != options.EditorProcessId)
                    throw new InvalidOperationException("The Editor panel session could not be authenticated.");
                await _panelHost.WaitForCurrentAttachmentAsync().ConfigureAwait(true);
            }
        }
        catch (Exception exception)
        {
            _viewModel?.ReportUnhandledError(exception);
            if (options?.PanelParent is not null || window is null)
            {
                _bridge?.AbortProtocolSession(exception.Message);
                _panelHost?.Dispose();
                window?.CloseEmbedded();
                System.Diagnostics.Trace.WriteLine(exception);
                Shutdown(1);
            }
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _panelHost?.Dispose();
        _viewModel?.Dispose();
        _bridge?.Dispose();
        _theme?.Dispose();
        base.OnExit(e);
    }
}
