using System.Collections.ObjectModel;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using Dluma.Workbench.Core;
using Microsoft.Win32;

namespace Dluma.Workbench;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<ApiOperationInfo> _visibleOperations = [];
    private readonly string _repositoryRoot;
    private readonly ApiCatalogInfo _catalog;
    private string? _createdDirectory;
    private string? _luaPath;
    private string? _playerPath = Environment.GetEnvironmentVariable("DLUMA_PLAYER_PATH");

    public MainWindow()
    {
        InitializeComponent();
        _repositoryRoot = RepositoryLocator.Find();
        var catalogPath = Path.Combine(_repositoryRoot, "api", "generated", "workbench_api.json");
        _catalog = ApiCatalogService.Load(catalogPath);
        ApiGrid.ItemsSource = _visibleOperations;
        CatalogSummary.Text = $"{_catalog.Name} {_catalog.Version}  •  {_catalog.Operations.Count} operations  •  catalog {_catalog.CatalogSha256[..12]}";
        NewOutputRoot.Text = Path.Combine(_repositoryRoot, "workbench", "projects");
        StageDestination.Text = Path.Combine(_repositoryRoot, "workbench", "stage");
        PopulateFilters();
        foreach (var access in _catalog.AccessDeclarations)
            AccessList.Items.Add(access);
        RefreshApi();
        Loaded += OnLoaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            var status = await WorkbenchCommandService.DetectPrerequisitesAsync();
            PrerequisiteSummary.Text = status.Summary;
        }
        catch (Exception exception)
        {
            PrerequisiteSummary.Text = $"Prerequisite check failed: {exception.Message}";
        }
    }

    private void PopulateFilters()
    {
        SetFilter(ApiDomain, _catalog.Operations.Select(item => item.Domain));
        SetFilter(ApiStatus, new[] { "implemented", "gated", "planned", "removed" });
        SetFilter(ApiScope, new[] { "global", "project", "map" });
        SetFilter(ApiAccess, _catalog.AccessDeclarations);
    }

    private static void SetFilter(ComboBox combo, IEnumerable<string> values)
    {
        combo.Items.Add("All");
        foreach (var value in values.Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase))
            combo.Items.Add(value);
        combo.SelectedIndex = 0;
    }

    private static string SelectedText(ComboBox combo) => combo.SelectedItem switch
    {
        ComboBoxItem item => item.Tag?.ToString() ?? item.Content?.ToString() ?? string.Empty,
        object value => value.ToString() ?? string.Empty,
        _ => string.Empty
    };

    private static ModLanguage SelectedLanguage(ComboBox combo) =>
        Enum.Parse<ModLanguage>(SelectedText(combo));

    private void NewLanguageChanged(object sender, SelectionChangedEventArgs e)
    {
        if (EntryTypeLabel is null) return;
        var managed = SelectedText(NewLanguage) == nameof(ModLanguage.CSharp);
        EntryTypeLabel.Visibility = managed ? Visibility.Visible : Visibility.Collapsed;
        NewEntryType.Visibility = managed ? Visibility.Visible : Visibility.Collapsed;
    }

    private void NewScopeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (MapLabel is null) return;
        var map = SelectedText(NewScope) == nameof(ModScope.Map);
        MapLabel.Visibility = map ? Visibility.Visible : Visibility.Collapsed;
        NewMap.Visibility = map ? Visibility.Visible : Visibility.Collapsed;
    }

    private void BrowseNewOutput(object sender, RoutedEventArgs e)
    {
        var selected = PickFolder(NewOutputRoot.Text);
        if (selected is not null) NewOutputRoot.Text = selected;
    }

    private async void CreateMod(object sender, RoutedEventArgs e)
    {
        try
        {
            CreateStatus.Text = "Creating...";
            var request = new ModTemplateRequest(
                _repositoryRoot,
                NewOutputRoot.Text,
                NewId.Text.Trim(),
                NewName.Text.Trim(),
                NewVersion.Text.Trim(),
                NewDescription.Text.Trim(),
                NewAuthor.Text.Trim(),
                SelectedLanguage(NewLanguage),
                Enum.Parse<ModScope>(SelectedText(NewScope)),
                NewMap.Text.Trim(),
                NewEntryType.Text.Trim(),
                0,
                AccessList.SelectedItems.Cast<string>().ToArray(),
                NewConfiguration.IsChecked == true);
            var result = await ModProjectService.CreateAsync(request);
            _createdDirectory = result.Directory;
            BuildModDirectory.Text = result.Directory;
            BuildLanguage.SelectedIndex = (int)request.Language;
            PackageOutput.Text = Path.Combine(Path.GetDirectoryName(result.Directory)!, $"{request.Id}-{request.Version}.dlumamod");
            CreateStatus.Text = $"Created {result.Directory}";
            if (request.Language == ModLanguage.Lua)
            {
                _luaPath = result.Entry;
                LuaPath.Text = result.Entry;
                LuaEditor.Text = await File.ReadAllTextAsync(result.Entry);
            }
        }
        catch (Exception exception)
        {
            CreateStatus.Text = exception.Message;
        }
    }

    private void OpenCreatedFolder(object sender, RoutedEventArgs e)
    {
        if (_createdDirectory is null || !Directory.Exists(_createdDirectory)) return;
        ProcessService.Open("explorer.exe", new[] { _createdDirectory }, _createdDirectory);
    }

    private void ApiFilterChanged(object sender, EventArgs e) => RefreshApi();

    private void RefreshApi()
    {
        if (ApiDomain is null) return;
        var filtered = ApiCatalogService.Filter(
            _catalog,
            ApiSearch.Text,
            SelectedText(ApiDomain),
            SelectedText(ApiStatus),
            SelectedText(ApiScope),
            SelectedText(ApiAccess));
        _visibleOperations.Clear();
        foreach (var operation in filtered) _visibleOperations.Add(operation);
        if (_visibleOperations.Count > 0) ApiGrid.SelectedIndex = 0;
    }

    private void ApiSelectionChanged(object sender, EventArgs e)
    {
        if (ApiGrid?.SelectedItem is not ApiOperationInfo operation || SnippetLanguage is null) return;
        ApiTitle.Text = $"{operation.Id}  [{operation.Status}]";
        ApiDocumentation.Text = operation.Documentation;
        ApiContracts.Text = $"Capability: {operation.Capability}  •  Access: {JoinOrNone(operation.Access)}  •  Scopes: {string.Join(", ", operation.Scopes)}  •  Thread: {operation.Thread}  •  Errors: {JoinOrNone(operation.Errors)}";
        ApiSnippet.Text = SnippetService.Create(operation, SelectedLanguage(SnippetLanguage));
    }

    private static string JoinOrNone(IReadOnlyList<string> values) => values.Count == 0 ? "none" : string.Join(", ", values);

    private void CopySnippet(object sender, RoutedEventArgs e)
    {
        if (!string.IsNullOrEmpty(ApiSnippet.Text)) Clipboard.SetText(ApiSnippet.Text);
    }

    private async void OpenLua(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Lua files (*.lua)|*.lua|All files (*.*)|*.*" };
        if (dialog.ShowDialog(this) != true) return;
        _luaPath = dialog.FileName;
        LuaPath.Text = dialog.FileName;
        LuaEditor.Text = await File.ReadAllTextAsync(dialog.FileName);
    }

    private async void SaveLua(object sender, RoutedEventArgs e)
    {
        if (_luaPath is null) return;
        await File.WriteAllTextAsync(_luaPath, LuaEditor.Text, new UTF8Encoding(false));
        AnalyzeLua(sender, e);
    }

    private void AnalyzeLua(object sender, RoutedEventArgs e)
    {
        LuaDiagnostics.Items.Clear();
        var diagnostics = LuaDiagnosticService.Analyze(LuaEditor.Text);
        if (diagnostics.Count == 0)
            LuaDiagnostics.Items.Add("No diagnostics.");
        else
            foreach (var diagnostic in diagnostics)
                LuaDiagnostics.Items.Add($"{diagnostic.Severity} L{diagnostic.Line}: {diagnostic.Message}");
    }

    private void BrowseBuildMod(object sender, RoutedEventArgs e)
    {
        var selected = PickFolder(BuildModDirectory.Text);
        if (selected is null) return;
        BuildModDirectory.Text = selected;
        var manifest = Path.Combine(selected, "mod.dluma.json");
        if (File.Exists(manifest))
            PackageOutput.Text = Path.Combine(Path.GetDirectoryName(selected)!, Path.GetFileName(selected) + ".dlumamod");
    }

    private void BrowseStage(object sender, RoutedEventArgs e)
    {
        var selected = PickFolder(StageDestination.Text);
        if (selected is not null) StageDestination.Text = selected;
    }

    private async void BuildMod(object sender, RoutedEventArgs e) =>
        await RunAsync(() => WorkbenchCommandService.BuildAsync(SelectedLanguage(BuildLanguage), BuildModDirectory.Text));

    private async void ValidateMod(object sender, RoutedEventArgs e) =>
        await RunAsync(() => WorkbenchCommandService.ValidateAsync(_repositoryRoot, BuildModDirectory.Text, false));

    private async void LockMod(object sender, RoutedEventArgs e) =>
        await RunAsync(() => WorkbenchCommandService.LockAsync(_repositoryRoot, BuildModDirectory.Text));

    private async void PackageMod(object sender, RoutedEventArgs e)
    {
        await RunAsync(async () =>
        {
            var lockResult = await WorkbenchCommandService.LockAsync(_repositoryRoot, BuildModDirectory.Text);
            if (!lockResult.Succeeded) return lockResult;
            return await WorkbenchCommandService.PackageAsync(_repositoryRoot, BuildModDirectory.Text, PackageOutput.Text);
        });
    }

    private async void StageMod(object sender, RoutedEventArgs e)
    {
        try
        {
            await WorkbenchCommandService.StageAsync(PackageOutput.Text, StageDestination.Text);
            BuildOutput.Text = $"Staged {PackageOutput.Text} to {StageDestination.Text}";
        }
        catch (Exception exception)
        {
            BuildOutput.Text = exception.ToString();
        }
    }

    private void OpenLogs(object sender, RoutedEventArgs e)
    {
        var configuredLog = Environment.GetEnvironmentVariable("DLUMA_RUNTIME_LOG");
        var log = string.IsNullOrWhiteSpace(configuredLog)
            ? Path.Combine(StageDestination.Text, "dluma_runtime", "dluma.log")
            : configuredLog;
        if (!File.Exists(log))
        {
            var dialog = new OpenFileDialog
            {
                Title = "Select dluma.log",
                Filter = "Loader log (dluma.log)|dluma.log|Log files (*.log)|*.log|All files (*.*)|*.*",
                InitialDirectory = Directory.Exists(StageDestination.Text)
                    ? StageDestination.Text
                    : _repositoryRoot
            };
            if (dialog.ShowDialog(this) != true) return;
            log = dialog.FileName;
        }
        ProcessService.Open("notepad.exe", new[] { log }, Path.GetDirectoryName(log)!);
    }

    private void LaunchPlayer(object sender, RoutedEventArgs e)
    {
        var player = _playerPath;
        if (!File.Exists(player))
        {
            var dialog = new OpenFileDialog
            {
                Title = "Select DyingLightPlayer.exe",
                Filter = "Dying Light Player (DyingLightPlayer.exe)|DyingLightPlayer.exe|Applications (*.exe)|*.exe"
            };
            if (dialog.ShowDialog(this) != true) return;
            player = dialog.FileName;
            _playerPath = player;
        }
        var result = MessageBox.Show(
            this,
            "Launch Dying Light Player now? This is an explicit live runtime action and is never part of Build, Validate, Package, or Stage.",
            "Launch Player",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning,
            MessageBoxResult.No);
        if (result != MessageBoxResult.Yes) return;
        ProcessService.Open(player, new[] { "-nologos", "-debugconf=debugconf.scr" }, Path.GetDirectoryName(player)!);
    }

    private async Task RunAsync(Func<Task<CommandResult>> action)
    {
        try
        {
            BuildOutput.Text = "Working...";
            var result = await action();
            BuildOutput.Text = $"Exit {result.ExitCode}{Environment.NewLine}{result.Combined}";
        }
        catch (Exception exception)
        {
            BuildOutput.Text = exception.ToString();
        }
    }

    private string? PickFolder(string initial)
    {
        var dialog = new OpenFolderDialog
        {
            InitialDirectory = Directory.Exists(initial) ? initial : _repositoryRoot,
            Multiselect = false
        };
        return dialog.ShowDialog(this) == true ? dialog.FolderName : null;
    }
}
