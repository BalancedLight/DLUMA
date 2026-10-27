using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows;
using System.Windows.Data;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Services;

namespace Dluma.QuestGraph.ViewModels;

public sealed record QuestDocumentSaveReceipt(
    bool SourceSaved,
    bool SidecarPublished,
    bool ExistingSidecarPreserved,
    string Message)
{
    public bool CanCompile =>
        SourceSaved && SidecarPublished && !ExistingSidecarPreserved;
}

public sealed partial class MainViewModel : ObservableObject, IDisposable
{
    private readonly QuestDocumentService _documentService;
    private readonly IFileDialogService _fileDialogs;
    private readonly QuestBridgeClient _bridge;
    private readonly LaunchOptions _launchOptions;
    private readonly object _bridgeNotificationGate = new();
    private Task _bridgeNotificationTail = Task.CompletedTask;
    private readonly List<QuestPaletteItemViewModel> _allPaletteItems = new();
    private QuestDocumentViewModel? _activeDocument;
    private QuestPaletteItemViewModel? _selectedPaletteItem;
    private QuestCatalog? _catalog;
    private QuestContextSnapshot? _context;
    private string? _projectRoot;
    private string _paletteSearch = string.Empty;
    private string _connectionStatus = "Offline";
    private string _connectionDetails = "No authenticated Developer Tools bridge session is active.";
    private string _globalStatus = "Open a PXSL file or connect from Developer Tools.";
    private bool _isBusy;
    private QuestContextObject? _selectedContextObject;
    private QuestContextText? _selectedContextText;
    private string? _selectedSpeaker;
    private string? _selectedDialogueGroup;
    private string? _selectedQuestReference;

    public MainViewModel(
        QuestDocumentService documentService,
        IFileDialogService fileDialogs,
        QuestBridgeClient bridge,
        LaunchOptions launchOptions)
    {
        _documentService = documentService;
        _fileDialogs = fileDialogs;
        _bridge = bridge;
        _launchOptions = launchOptions;
        _bridge.NotificationReceived += OnBridgeNotification;
        _bridge.Disconnected += OnBridgeDisconnected;

        OpenCommand = new AsyncRelayCommand(OpenAsync, () => !IsBusy);
        SaveCommand = new AsyncRelayCommand(SaveActiveAsync, () => ActiveDocument?.CanSaveOriginal == true && !IsBusy);
        SaveAllCommand = new AsyncRelayCommand(SaveAllAsync, () => Documents.Count > 0 && !IsBusy);
        SaveCopyCommand = new AsyncRelayCommand(SaveCopyAsync, () => ActiveDocument is not null && !IsBusy);
        CompileCommand = new AsyncRelayCommand(CompileActiveAsync, () =>
            ActiveDocument?.IsConnected == true && ActiveDocument.CanSaveOriginal && !IsBusy);
        CloseCommand = new AsyncRelayCommand(CloseActiveAsync, () => ActiveDocument is not null && !IsBusy);
        LoadCatalogCommand = new AsyncRelayCommand(LoadCatalogAsync, () => !IsBusy);
        InsertAfterCommand = new AsyncRelayCommand(
            () => InsertSelectedPaletteAsync(false),
            () => ActiveDocument?.CanEditSource == true &&
                ActiveDocument.SelectedNode?.IsOpaque != true &&
                SelectedPaletteItem is not null && !IsBusy);
        AddChildCommand = new AsyncRelayCommand(
            () => InsertSelectedPaletteAsync(true),
            () => ActiveDocument?.CanEditSource == true &&
                ActiveDocument.SelectedNode is { IsOpaque: false } &&
                ActiveDocument.CanInsertCompatibleChild(SelectedPaletteItem) && !IsBusy);
        RefreshContextCommand = new AsyncRelayCommand(RefreshContextAsync, () => !IsBusy);
        AppendContextValueCommand = new RelayCommand(AppendContextValue, _ =>
            ActiveDocument?.CanEditSource == true &&
            ActiveDocument.SelectedNode is { IsOpaque: false });

        PopulateFallbackPalette();
        ApplyPaletteFilter();
        PaletteItemsView = CollectionViewSource.GetDefaultView(PaletteItems);
        PaletteItemsView.GroupDescriptions.Add(
            new PropertyGroupDescription(nameof(QuestPaletteItemViewModel.Category)));
        PaletteItemsView.SortDescriptions.Add(
            new SortDescription(nameof(QuestPaletteItemViewModel.Category), ListSortDirection.Ascending));
        PaletteItemsView.SortDescriptions.Add(
            new SortDescription(nameof(QuestPaletteItemViewModel.Name), ListSortDirection.Ascending));
    }

    public ObservableCollection<QuestDocumentViewModel> Documents { get; } = new();
    public ObservableCollection<QuestPaletteItemViewModel> PaletteItems { get; } = new();
    public ICollectionView PaletteItemsView { get; }
    public ObservableCollection<QuestContextObject> ContextObjects { get; } = new();
    public ObservableCollection<QuestContextText> ContextTexts { get; } = new();
    public ObservableCollection<string> Speakers { get; } = new();
    public ObservableCollection<string> DialogueGroups { get; } = new();
    public ObservableCollection<string> QuestReferences { get; } = new();

    public AsyncRelayCommand OpenCommand { get; }
    public AsyncRelayCommand SaveCommand { get; }
    public AsyncRelayCommand SaveAllCommand { get; }
    public AsyncRelayCommand SaveCopyCommand { get; }
    public AsyncRelayCommand CompileCommand { get; }
    public AsyncRelayCommand CloseCommand { get; }
    public AsyncRelayCommand LoadCatalogCommand { get; }
    public AsyncRelayCommand InsertAfterCommand { get; }
    public AsyncRelayCommand AddChildCommand { get; }
    public AsyncRelayCommand RefreshContextCommand { get; }
    public RelayCommand AppendContextValueCommand { get; }

    public QuestDocumentViewModel? ActiveDocument
    {
        get => _activeDocument;
        set
        {
            if (!SetProperty(ref _activeDocument, value))
                return;
            OnPropertyChanged(nameof(HasDocuments));
            RefreshCommandStates();
        }
    }

    public bool HasDocuments => Documents.Count > 0;

    public QuestPaletteItemViewModel? SelectedPaletteItem
    {
        get => _selectedPaletteItem;
        set
        {
            if (SetProperty(ref _selectedPaletteItem, value))
                RefreshCommandStates();
        }
    }

    public string PaletteSearch
    {
        get => _paletteSearch;
        set
        {
            if (SetProperty(ref _paletteSearch, value))
                ApplyPaletteFilter();
        }
    }

    public string ConnectionStatus
    {
        get => _connectionStatus;
        private set => SetProperty(ref _connectionStatus, value);
    }

    public string ConnectionDetails
    {
        get => _connectionDetails;
        private set => SetProperty(ref _connectionDetails, value);
    }

    public string GlobalStatus
    {
        get => _globalStatus;
        set => SetProperty(ref _globalStatus, value);
    }

    public bool IsBusy
    {
        get => _isBusy;
        private set
        {
            if (SetProperty(ref _isBusy, value))
                RefreshCommandStates();
        }
    }

    public QuestContextObject? SelectedContextObject
    {
        get => _selectedContextObject;
        set
        {
            if (SetProperty(ref _selectedContextObject, value) && value is not null)
                ContextObjectInput = value.Name;
        }
    }

    public QuestContextText? SelectedContextText
    {
        get => _selectedContextText;
        set
        {
            if (SetProperty(ref _selectedContextText, value) && value is not null)
                ContextTextInput = value.Id;
        }
    }

    public string? SelectedSpeaker
    {
        get => _selectedSpeaker;
        set
        {
            if (SetProperty(ref _selectedSpeaker, value) && value is not null)
                SpeakerInput = value;
        }
    }

    public string? SelectedDialogueGroup
    {
        get => _selectedDialogueGroup;
        set
        {
            if (SetProperty(ref _selectedDialogueGroup, value) && value is not null)
                DialogueGroupInput = value;
        }
    }

    public string? SelectedQuestReference
    {
        get => _selectedQuestReference;
        set
        {
            if (SetProperty(ref _selectedQuestReference, value) && value is not null)
                QuestReferenceInput = value;
        }
    }

    public async Task InitializeAsync()
    {
        if (_launchOptions.IsConnectedLaunch)
        {
            try
            {
                var processPath = Environment.ProcessPath ??
                    throw new InvalidOperationException("The companion executable path is unavailable.");
                var executableHash = Convert.ToHexString(SHA256.HashData(
                    await File.ReadAllBytesAsync(processPath).ConfigureAwait(false)));
                await _bridge.ConnectAsync(
                    _launchOptions.PipeName!,
                    _launchOptions.SessionToken!,
                    TimeSpan.FromSeconds(5)).ConfigureAwait(true);
                var hello = await _bridge.HelloAsync(
                    _launchOptions.EditorProcessId!.Value,
                    executableHash).ConfigureAwait(true);
                if (hello.EditorProcessId != _launchOptions.EditorProcessId.Value)
                    throw new InvalidDataException("The bridge acknowledged a different Editor process.");
                _projectRoot = hello.ProjectRoot;
                SetInstalledCatalogRoots(hello.InstalledCatalogRoots);
                ConnectionStatus = "Connected";
                ConnectionDetails = $"Authenticated against {hello.HostBuildId}.";
                GlobalStatus = "Connected to the Developer Tools quest editor.";
                try
                {
                    await DiscoverRuntimeCatalogAsync(_projectRoot).ConfigureAwait(true);
                }
                catch (Exception exception)
                {
                    ConnectionDetails = $"Authenticated bridge is healthy; quest definitions are incomplete: {exception.Message}";
                    GlobalStatus = ConnectionDetails;
                }
                await RefreshContextAsync().ConfigureAwait(true);
            }
            catch (Exception exception)
            {
                if (_bridge.IsConnected)
                {
                    ConnectionStatus = "Connected";
                    ConnectionDetails = $"The pipe is connected, but setup did not complete: {exception.Message}";
                }
                else
                {
                    ConnectionStatus = "Offline";
                    ConnectionDetails = exception.Message;
                }
                GlobalStatus = ConnectionDetails;
            }
        }

        foreach (var path in _launchOptions.Files)
        {
            try
            {
                await OpenFileAsync(path).ConfigureAwait(true);
            }
            catch (Exception exception)
            {
                GlobalStatus = $"Could not open {Path.GetFileName(path)}: {exception.Message}";
            }
        }
    }

    public void ReportUnhandledError(Exception exception)
    {
        GlobalStatus = exception.Message;
        IsBusy = false;
    }

    public async Task<QuestDocumentSaveReceipt> SaveDocumentAsync(
        QuestDocumentViewModel document)
    {
        if (document.SourcePath is null || document.IsStaleSession)
            return new(false, false, false, document.Status);
        IsBusy = true;
        try
        {
            if (document.RequiresConnection)
            {
                if (string.IsNullOrWhiteSpace(_projectRoot))
                    throw new InvalidDataException("The active Developer Tools project root is unavailable.");
                SafeProjectPath.RequireContainedNonReparsePath(
                    _projectRoot,
                    document.SourcePath);
                var request = new SaveDocumentWireRequest(
                    document.DocumentId,
                    document.Revision,
                    document.SourceSha256,
                    document.SourceSha256);
                var result = await _bridge.RequestAsync<SaveDocumentWireRequest, SaveDocumentResult>(
                    "saveDocument",
                    request,
                    TimeSpan.FromSeconds(15)).ConfigureAwait(true);
                if (!result.SaveAttempted || !result.SaveSucceeded)
                {
                    document.Status = result.Cancelled ? "Save was cancelled in Developer Tools." : result.Message;
                    return new(false, false, false, document.Status);
                }
                if (result.Revision != document.Revision ||
                    !string.Equals(result.Sha256, document.SourceSha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Developer Tools saved a different source revision.");
                if (result.DiskPath is not null &&
                    !string.Equals(
                        Path.GetFullPath(result.DiskPath),
                        Path.GetFullPath(document.SourcePath),
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Developer Tools saved a different PXSL path.");
            }
            else
            {
                await _documentService.SaveSourceAsync(document.SourcePath, document.Model.Source)
                    .ConfigureAwait(true);
            }
            document.MarkSourceSaved();

            if (document.PreserveExistingSidecar)
            {
                document.Status = "PXSL saved. The invalid or future layout sidecar was preserved, so the current layout was not published and Compile is blocked. Use Save Copy to publish a new layout safely.";
                GlobalStatus = document.Status;
                return new(true, false, true, document.Status);
            }
            try
            {
                await _documentService.SaveSidecarAsync(document.SourcePath, document.CreateSidecar())
                    .ConfigureAwait(true);
                document.MarkLayoutSaved();
            }
            catch (Exception exception) when (
                exception is IOException or UnauthorizedAccessException or InvalidDataException)
            {
                document.Status = $"PXSL saved, but its layout sidecar could not be updated: {exception.Message}";
                GlobalStatus = document.Status;
                return new(true, false, false, document.Status);
            }
            GlobalStatus = document.Status;
            return new(true, true, false, document.Status);
        }
        catch (Exception exception)
        {
            document.Status = exception.Message;
            GlobalStatus = exception.Message;
            return new(false, false, false, document.Status);
        }
        finally
        {
            IsBusy = false;
        }
    }

    public async Task<bool> TryCloseDocumentAsync(QuestDocumentViewModel document, bool force = false)
    {
        if (!force && document.IsDirty)
            return false;
        if (document.RequiresConnection && document.IsConnected)
        {
            try
            {
                await _bridge.CloseDocumentAsync(document.DocumentId).ConfigureAwait(true);
            }
            catch (Exception exception)
            {
                GlobalStatus = $"Document closed locally; bridge notification failed: {exception.Message}";
            }
        }
        Documents.Remove(document);
        ActiveDocument = Documents.LastOrDefault();
        OnPropertyChanged(nameof(HasDocuments));
        return true;
    }

    private async Task OpenAsync()
    {
        var path = _fileDialogs.SelectQuestToOpen();
        if (path is not null)
            await OpenFileAsync(path).ConfigureAwait(true);
    }

    private async Task OpenFileAsync(string path)
    {
        IsBusy = true;
        try
        {
            var fullPath = Path.GetFullPath(path);
            var existing = Documents.FirstOrDefault(document =>
                document.SourcePath is not null &&
                string.Equals(document.SourcePath, fullPath, StringComparison.OrdinalIgnoreCase));
            if (existing is not null)
            {
                ActiveDocument = existing;
                return;
            }
            if (_projectRoot is null)
                await DiscoverRuntimeCatalogAsync(Path.GetDirectoryName(fullPath)).ConfigureAwait(true);
            var opened = await _documentService.OpenFileAsync(fullPath, _catalog).ConfigureAwait(true);
            AddDocument(new QuestDocumentViewModel(_documentService, opened));
            GlobalStatus = $"Opened {Path.GetFileName(fullPath)} in offline mode.";
            if (_projectRoot is null)
                await LoadDiskContextForPathAsync(fullPath).ConfigureAwait(true);
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task SaveActiveAsync()
    {
        if (ActiveDocument is not null)
            await SaveDocumentAsync(ActiveDocument).ConfigureAwait(true);
    }

    private async Task SaveAllAsync()
    {
        foreach (var document in Documents.ToArray())
        {
            if (document.IsDirty && document.CanSaveOriginal)
                await SaveDocumentAsync(document).ConfigureAwait(true);
        }
    }

    private async Task SaveCopyAsync()
    {
        var document = ActiveDocument;
        if (document is null)
            return;
        var path = _fileDialogs.SelectQuestSavePath(document.DisplayName);
        if (path is null)
            return;
        IsBusy = true;
        try
        {
            var fullPath = Path.GetFullPath(path);
            if (document.SourcePath is not null &&
                string.Equals(fullPath, document.SourcePath, StringComparison.OrdinalIgnoreCase))
            {
                GlobalStatus = "Save Copy requires a different PXSL path.";
                return;
            }
            var copySource = new QuestSourceSnapshot(
                Guid.NewGuid().ToString("N"),
                Path.GetFileName(fullPath),
                0,
                document.Model.Source.Bytes,
                document.Model.Source.EncodingName);
            var copySidecar = document.CreateSidecar() with
            {
                DocumentId = copySource.DocumentId,
                Source = new QuestGraphSourceIdentity(
                    copySource.RelativePath,
                    copySource.Sha256,
                    copySource.Bytes.Length)
            };
            await _documentService.SaveSourceAsync(fullPath, copySource).ConfigureAwait(true);
            await _documentService.SaveSidecarAsync(fullPath, copySidecar).ConfigureAwait(true);
            var opened = await _documentService.OpenFileAsync(fullPath, _catalog).ConfigureAwait(true);
            AddDocument(new QuestDocumentViewModel(_documentService, opened));
            GlobalStatus = $"Saved and opened a recoverable copy at {Path.GetFileName(fullPath)}.";
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task CompileActiveAsync()
    {
        var document = ActiveDocument;
        if (document is null || !document.IsConnected)
            return;
        var saveReceipt = await SaveDocumentAsync(document).ConfigureAwait(true);
        if (!saveReceipt.CanCompile)
        {
            document.Status = saveReceipt.Message;
            GlobalStatus = saveReceipt.Message;
            return;
        }
        IsBusy = true;
        try
        {
            var request = new CompileDocumentWireRequest(
                document.DocumentId,
                document.Revision,
                document.SourceSha256,
                document.SourceSha256);
            var result = await _bridge.RequestAsync<CompileDocumentWireRequest, CompileDocumentResult>(
                "compileDocument",
                request,
                TimeSpan.FromMinutes(2)).ConfigureAwait(true);
            var receipt = result.Receipt;
            var compileStatus = receipt.StockCommandDispatched
                ? "Compile sent"
                : receipt.Message;
            document.Status = compileStatus;
            GlobalStatus = compileStatus;
        }
        catch (Exception exception)
        {
            document.Status = exception.Message;
            GlobalStatus = exception.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task CloseActiveAsync()
    {
        if (ActiveDocument is not null &&
            !await TryCloseDocumentAsync(ActiveDocument).ConfigureAwait(true))
            GlobalStatus = "Save, Save Copy, or use the window-close prompt to discard this dirty tab.";
    }

    private async Task LoadCatalogAsync()
    {
        var root = _fileDialogs.SelectCatalogRoot();
        if (root is null)
            return;
        IsBusy = true;
        try
        {
            SetManualCatalogRoot(root);
            await DiscoverRuntimeCatalogAsync(_projectRoot).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            GlobalStatus = $"Quest definitions were not loaded: {exception.Message}";
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task InsertSelectedPaletteAsync(bool asChild)
    {
        if (ActiveDocument is null || SelectedPaletteItem is null)
            return;
        if (asChild)
            await ActiveDocument.InsertCompatibleChildAsync(SelectedPaletteItem).ConfigureAwait(true);
        else
            await ActiveDocument.InsertAfterCompatibleAsync(SelectedPaletteItem).ConfigureAwait(true);
    }

    private async Task<bool> ApplyConnectedEditsAsync(
        QuestDocumentViewModel document,
        IReadOnlyList<QuestTextEdit> edits)
    {
        try
        {
            var projected = QuestSourceEditService.Apply(document.Model.Source, edits).Source;
            var request = new ApplyEditsRequest(
                document.DocumentId,
                document.Revision,
                document.SourceSha256,
                projected.Sha256,
                edits.OrderByDescending(edit => edit.ByteOffset)
                    .Select(edit => new QuestBridgeTextEdit(
                        edit.ByteOffset,
                        edit.RemovedLength,
                        edit.ReplacementBytes.ToArray()))
                    .ToArray());
            var result = await _bridge.ApplyEditsAsync(request).ConfigureAwait(true);
            if (!result.Applied)
            {
                document.Status = result.Message;
                return false;
            }
            if (result.Revision != projected.Revision ||
                !string.Equals(result.Sha256, projected.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Developer Tools admitted a different PXSL revision.");
            return true;
        }
        catch (Exception exception)
        {
            document.Status = exception.Message;
            return false;
        }
    }

    private Task FocusSourceAsync(QuestDocumentViewModel document, int? byteOffset) =>
        _bridge.ActivateDocumentAsync(document.DocumentId, byteOffset);

    private async Task RefreshContextAsync()
    {
        IsBusy = true;
        try
        {
            QuestContextSnapshot? disk = null;
            if (!string.IsNullOrWhiteSpace(_projectRoot))
                disk = await QuestDiskContextProvider.LoadAsync(_projectRoot, _context?.Generation ?? 0)
                    .ConfigureAwait(true);
            if (_bridge.IsConnected)
            {
                var live = (await _bridge.GetContextAsync(_context?.Generation).ConfigureAwait(true)).Context;
                SetContext(disk is null ? live : QuestDiskContextProvider.Merge(disk, live));
                GlobalStatus = "Context updated";
            }
            else if (disk is not null)
            {
                SetContext(disk);
                GlobalStatus = "Context updated";
            }
        }
        catch (Exception exception)
        {
            GlobalStatus = $"Context refresh was incomplete: {exception.Message}";
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task LoadDiskContextForPathAsync(string sourcePath)
    {
        var directory = Path.GetDirectoryName(sourcePath);
        if (directory is null)
            return;
        try
        {
            SetContext(await QuestDiskContextProvider.LoadAsync(directory, 0).ConfigureAwait(true));
        }
        catch (Exception exception)
        {
            GlobalStatus = $"PXSL opened; optional disk context scan was skipped: {exception.Message}";
        }
    }

    private static bool HasNativeEditorContext(QuestContextSnapshot context) =>
        context.Generation > 0 ||
        context.Objects.Count > 0 ||
        context.Texts.Count > 0 ||
        context.Speakers.Count > 0 ||
        context.DialogueGroups.Count > 0 ||
        context.QuestReferences.Count > 0;

    private void SetContext(QuestContextSnapshot context)
    {
        QuestBridgePayloadValidator.Validate(new ContextSnapshotMessage(context));
        _context = context;
        Replace(ContextObjects, context.Objects);
        Replace(ContextTexts, context.Texts);
        Replace(Speakers, context.Speakers);
        Replace(DialogueGroups, context.DialogueGroups);
        Replace(QuestReferences, context.QuestReferences);
        foreach (var document in Documents)
            document.RefreshInspectorOptions();
    }

    private static void Replace<T>(ObservableCollection<T> target, IEnumerable<T> values)
    {
        target.Clear();
        foreach (var value in values)
            target.Add(value);
    }

    private void AppendContextValue(object? parameter)
    {
        var node = ActiveDocument?.SelectedNode;
        if (node is null)
            return;
        var value = parameter as string;
        if (string.IsNullOrWhiteSpace(value))
            return;
        var escaped = value.Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("\"", "\\\"", StringComparison.Ordinal);
        node.SourcePreview = node.SourcePreview.TrimEnd() + $" \"{escaped}\"";
    }

    private void AddDocument(QuestDocumentViewModel document)
    {
        var existing = Documents.FirstOrDefault(item =>
            string.Equals(item.DocumentId, document.DocumentId, StringComparison.Ordinal) &&
            string.Equals(
                item.ConnectedProjectRoot,
                document.ConnectedProjectRoot,
                StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            ActiveDocument = existing;
            return;
        }
        document.ConnectedEditHandler = ApplyConnectedEditsAsync;
        document.ConnectedRestoreHandler = RestoreConnectedBaselineAsync;
        document.FocusSourceHandler = FocusSourceAsync;
        document.PaletteProvider = () => _allPaletteItems
            .Where(item => item.CatalogEntry?.IsInsertable != false)
            .ToArray();
        document.ConnectionPaletteProvider = sourcePort => _allPaletteItems
            .Where(item => item.CatalogEntry?.IsInsertable != false)
            .Where(item => sourcePort.Type != "child" ||
                item.CatalogEntry?.Category == QuestCatalogFunctionalCategory.ChildRecords)
            .Where(item => sourcePort.Type == "child" ||
                item.CatalogEntry?.Category != QuestCatalogFunctionalCategory.ChildRecords)
            .ToArray();
        document.InspectorOptionProvider = ResolveInspectorOptions;
        document.InspectorContextRefreshHandler = RefreshContextAsync;
        document.PropertyChanged += (_, args) =>
        {
            RefreshCommandStates();
            if (args.PropertyName == nameof(QuestDocumentViewModel.Status) &&
                ReferenceEquals(ActiveDocument, document))
                GlobalStatus = document.Status;
        };
        Documents.Add(document);
        ActiveDocument = document;
        OnPropertyChanged(nameof(HasDocuments));
    }

    private void PopulateFallbackPalette()
    {
        _allPaletteItems.Clear();
        _allPaletteItems.AddRange(new[]
        {
            new QuestPaletteItemViewModel("Quest", "Flow & Containers", "quest NewQuest", "Create a quest declaration.", HeaderColor: "#3E745B"),
            new QuestPaletteItemViewModel("Path", "Flow & Containers", "path", "Add a typed path branch.", HeaderColor: "#3E745B"),
            new QuestPaletteItemViewModel("Fallback", "Flow & Containers", "fallback", "Add a fallback branch.", HeaderColor: "#3E745B"),
            new QuestPaletteItemViewModel("Pre", "Flow & Containers", "pre", "Add a precondition branch.", HeaderColor: "#3E745B"),
            new QuestPaletteItemViewModel("Post", "Flow & Containers", "post", "Add a postcondition branch.", HeaderColor: "#3E745B"),
            new QuestPaletteItemViewModel("Generic macro", "Custom / Other", ",MacroName", "Insert a clean-room generic macro placeholder.")
        });
    }

    private void PopulateCatalogPalette(QuestCatalog catalog)
    {
        _allPaletteItems.Clear();
        foreach (var entry in catalog.InsertableEntries.OrderBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase))
        {
            var prefix = entry.Kind is QuestCatalogEntryKind.Macro or QuestCatalogEntryKind.Phase ? "," : string.Empty;
            var requiredArguments = entry.Parameters
                .Where(parameter => parameter.IsRequired)
                .OrderBy(parameter => parameter.Position ?? int.MaxValue)
                .Select(parameter => parameter.Values.FirstOrDefault() ?? "_");
            var insertion = entry.BuildMinimumTemplate();
            if (string.IsNullOrWhiteSpace(insertion))
            {
                insertion = prefix + entry.Name;
                var arguments = string.Join(' ', requiredArguments);
                if (arguments.Length > 0)
                    insertion += " " + arguments;
            }
            _allPaletteItems.Add(new QuestPaletteItemViewModel(
                entry.Name,
                QuestPaletteColors.DisplayName(entry.Category),
                insertion,
                entry.DescriptionLines.Count > 0
                    ? string.Join(" ", entry.DescriptionLines.Where(line => !string.IsNullOrWhiteSpace(line)))
                    : $"{entry.Parameters.Count} parameter(s), {entry.AllowedChildren.Count} allowed child record(s)",
                entry,
                QuestPaletteColors.ForEntry(entry)));
        }
        ApplyPaletteFilter();
    }

    private void ApplyPaletteFilter()
    {
        var search = PaletteSearch.Trim();
        var filtered = _allPaletteItems.Where(item =>
            search.Length == 0 ||
            item.Name.Contains(search, StringComparison.OrdinalIgnoreCase) ||
            item.Category.Contains(search, StringComparison.OrdinalIgnoreCase) ||
            item.Description.Contains(search, StringComparison.OrdinalIgnoreCase));
        Replace(PaletteItems, filtered.Take(2_000));
        SelectedPaletteItem = PaletteItems.FirstOrDefault();
    }

    private void OnBridgeNotification(object? sender, QuestBridgeEnvelope envelope)
    {
        EnqueueBridgeAction(() => HandleBridgeNotificationAsync(envelope));
    }

    private void EnqueueBridgeAction(Func<Task> action)
    {
        lock (_bridgeNotificationGate)
        {
            _bridgeNotificationTail = _bridgeNotificationTail.ContinueWith(
                async _ =>
                {
                    var operation = Application.Current.Dispatcher.InvokeAsync(action);
                    await operation.Task.Unwrap().ConfigureAwait(false);
                },
                CancellationToken.None,
                TaskContinuationOptions.None,
                TaskScheduler.Default).Unwrap();
        }
    }

    private async Task HandleBridgeNotificationAsync(QuestBridgeEnvelope envelope)
    {
        try
        {
            switch (envelope.Type)
            {
                case "openDocument":
                {
                    var message = QuestBridgeClient.DeserializePayload<OpenDocumentWireMessage>(envelope);
                    var projectChanged = !string.Equals(
                        _projectRoot,
                        message.ProjectRoot,
                        StringComparison.OrdinalIgnoreCase);
                    if (projectChanged && _projectRoot is not null)
                    {
                        foreach (var stale in Documents.Where(document =>
                            document.RequiresConnection &&
                            string.Equals(
                                document.ConnectedProjectRoot,
                                _projectRoot,
                                StringComparison.OrdinalIgnoreCase)))
                            stale.MarkConnected(false);
                    }
                    _projectRoot = message.ProjectRoot;
                    if (projectChanged || _catalog is null)
                        await DiscoverRuntimeCatalogAsync(_projectRoot).ConfigureAwait(true);
                    var openMessage = new OpenDocumentMessage(
                        message.DocumentId,
                        message.RelativePath,
                        message.Revision,
                        message.EncodingName,
                        message.Bytes,
                        message.Sha256,
                        message.WasModified);
                    var normalizedRelativePath = message.RelativePath
                        .Replace('\\', '/')
                        .TrimStart('/');
                    var existing = Documents.FirstOrDefault(item =>
                        item.RequiresConnection &&
                        string.Equals(
                            item.ConnectedProjectRoot,
                            message.ProjectRoot,
                            StringComparison.OrdinalIgnoreCase) &&
                        (string.Equals(
                            item.DocumentId,
                            message.DocumentId,
                            StringComparison.Ordinal) ||
                         (!projectChanged && string.Equals(
                            item.RelativePath.Replace('\\', '/').TrimStart('/'),
                            normalizedRelativePath,
                            StringComparison.OrdinalIgnoreCase))));
                    if (existing is not null)
                    {
                        existing.ReacquireConnectedSnapshot(openMessage, _catalog, message.ProjectRoot);
                        ActiveDocument = existing;
                        await RefreshContextAsync().ConfigureAwait(true);
                        break;
                    }
                    var opened = await _documentService.OpenConnectedAsync(
                        openMessage,
                        message.ProjectRoot,
                        _catalog).ConfigureAwait(true);
                    var document = new QuestDocumentViewModel(_documentService, opened);
                    document.MarkConnected(true);
                    AddDocument(document);
                    await RefreshContextAsync().ConfigureAwait(true);
                    break;
                }
                case "sourceChanged":
                {
                    var message = QuestBridgeClient.DeserializePayload<SourceChangedMessage>(envelope);
                    var document = Documents.FirstOrDefault(item =>
                        string.Equals(item.DocumentId, message.DocumentId, StringComparison.Ordinal) &&
                        string.Equals(
                            item.ConnectedProjectRoot,
                            _projectRoot,
                            StringComparison.OrdinalIgnoreCase));
                    if (document is null)
                        throw new InvalidDataException("The bridge sent sourceChanged before its admitted openDocument.");
                    document.ReplaceConnectedSnapshot(message, _catalog);
                    break;
                }
                case "activateDocument":
                {
                    var message = QuestBridgeClient.DeserializePayload<ActivateDocumentRequest>(envelope);
                    ActiveDocument = Documents.FirstOrDefault(item =>
                        string.Equals(item.DocumentId, message.DocumentId, StringComparison.Ordinal) &&
                        string.Equals(
                            item.ConnectedProjectRoot,
                            _projectRoot,
                            StringComparison.OrdinalIgnoreCase));
                    break;
                }
                case "contextSnapshot":
                {
                    var message = QuestBridgeClient.DeserializePayload<ContextSnapshotMessage>(envelope);
                    var disk = !string.IsNullOrWhiteSpace(_projectRoot)
                        ? await QuestDiskContextProvider.LoadAsync(_projectRoot, message.Context.Generation).ConfigureAwait(true)
                        : null;
                    SetContext(disk is null ? message.Context : QuestDiskContextProvider.Merge(disk, message.Context));
                    break;
                }
                case "disconnected":
                    MarkDisconnected("Developer Tools ended the quest bridge session.");
                    break;
            }
        }
        catch (Exception exception) when (exception is InvalidDataException or JsonException)
        {
            GlobalStatus = $"Bridge protocol rejected: {exception.Message}";
            MarkDisconnected(GlobalStatus);
            _bridge.AbortProtocolSession(GlobalStatus);
        }
        catch (Exception exception)
        {
            GlobalStatus = $"Bridge message failed: {exception.Message}";
        }
    }

    private void OnBridgeDisconnected(object? sender, string reason)
    {
        EnqueueBridgeAction(() =>
        {
            MarkDisconnected(reason);
            return Task.CompletedTask;
        });
    }

    private void MarkDisconnected(string reason)
    {
        ConnectionStatus = "Offline";
        ConnectionDetails = reason;
        GlobalStatus = reason;
        foreach (var document in Documents.Where(document => document.RequiresConnection))
            document.MarkConnected(false);
        RefreshCommandStates();
    }

    private void RefreshCommandStates()
    {
        OpenCommand.RaiseCanExecuteChanged();
        SaveCommand.RaiseCanExecuteChanged();
        SaveAllCommand.RaiseCanExecuteChanged();
        SaveCopyCommand.RaiseCanExecuteChanged();
        CompileCommand.RaiseCanExecuteChanged();
        CloseCommand.RaiseCanExecuteChanged();
        LoadCatalogCommand.RaiseCanExecuteChanged();
        InsertAfterCommand.RaiseCanExecuteChanged();
        AddChildCommand.RaiseCanExecuteChanged();
        RefreshContextCommand.RaiseCanExecuteChanged();
    }

    public void Dispose()
    {
        _bridge.NotificationReceived -= OnBridgeNotification;
        _bridge.Disconnected -= OnBridgeDisconnected;
        DisposeCatalogDiscovery();
    }
}
