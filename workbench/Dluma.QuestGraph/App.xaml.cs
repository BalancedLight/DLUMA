using System.Windows;
using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph;

public partial class App : Application
{
    private QuestBridgeClient? _bridge;
    private MainViewModel? _viewModel;

    private async void OnStartup(object sender, StartupEventArgs e)
    {
        var options = LaunchOptions.Parse(e.Args);
        if (options.SmokeTest)
        {
            Shutdown(0);
            return;
        }
        _bridge = new QuestBridgeClient();
        var fileDialogs = new WpfFileDialogService();
        var documents = new QuestDocumentService();
        _viewModel = new MainViewModel(documents, fileDialogs, _bridge, options);
        var window = new MainWindow(_viewModel);
        MainWindow = window;
        window.Show();

        try
        {
            await _viewModel.InitializeAsync().ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            _viewModel.ReportUnhandledError(exception);
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _viewModel?.Dispose();
        _bridge?.Dispose();
        base.OnExit(e);
    }
}
