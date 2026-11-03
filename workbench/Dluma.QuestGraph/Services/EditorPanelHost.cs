using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.ViewModels;

namespace Dluma.QuestGraph.Services;

/// <summary>Hosts the graph in a child window owned by the authenticated Editor.</summary>
public sealed class EditorPanelHost : IDisposable
{
    private const int WsChild = 0x40000000;
    private const int WsVisible = 0x10000000;
    private readonly MainWindow _window;
    private readonly FrameworkElement _root;
    private readonly EditorThemeService? _theme;
    private readonly QuestBridgeClient _bridge;
    private readonly SemaphoreSlim _attachGate = new(1, 1);
    private readonly CancellationTokenSource _lifetime = new();
    private readonly uint _focusMessage = RegisterWindowMessage("DLUMA.EditorPanel.Focus.1");
    private HwndSource? _source;
    private ulong _token;
    private ulong _generation;
    private long _requestVersion;
    private bool _disposed;
    private long _requestedParent;
    private ulong _requestedToken;
    private Task? _attachment;

    public EditorPanelHost(MainWindow window, QuestBridgeClient bridge)
    {
        _window = window;
        _bridge = bridge;
        _root = window.ExtractPanelContent();
        _theme = EditorThemeService.Current;
        _theme?.AttachRoot(_root);
        _bridge.NotificationReceived += OnNotification;
        _bridge.Disconnected += OnDisconnected;
    }

    // Must run on the WPF UI thread before creating any WPF windows or helpers.
    public static void PrepareUiThread(LaunchOptions options)
    {
        if (options.PanelParent is not long parent)
            return;
        ValidateParent(parent, options.EditorProcessId ?? 0);
        var context = GetWindowDpiAwarenessContext(new IntPtr(parent));
        if (context == IntPtr.Zero || SetThreadDpiAwarenessContext(context) == IntPtr.Zero ||
            !AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), context))
            throw new InvalidOperationException("Could not match the Editor's DPI awareness.");
    }

    public Task AttachAsync(long parentWindow, ulong panelToken)
    {
        _window.Dispatcher.VerifyAccess();
        if (_attachment is not null && _requestedParent == parentWindow && _requestedToken == panelToken &&
            (!_attachment.IsCompleted || (_attachment.IsCompletedSuccessfully && _source is { IsDisposed: false })))
            return _attachment;
        _requestedParent = parentWindow;
        _requestedToken = panelToken;
        return _attachment = AttachCoreAsync(parentWindow, panelToken, Interlocked.Increment(ref _requestVersion));
    }

    public async Task WaitForCurrentAttachmentAsync()
    {
        _window.Dispatcher.VerifyAccess();
        while (true)
        {
            var current = _attachment ?? throw new InvalidOperationException("No panel attachment was requested.");
            try { await current.ConfigureAwait(true); }
            catch when (!ReferenceEquals(current, _attachment)) { continue; }
            if (ReferenceEquals(current, _attachment)) return;
        }
    }

    private async Task AttachCoreAsync(long parentWindow, ulong panelToken, long version)
    {
        await _attachGate.WaitAsync().ConfigureAwait(true);
        try
        {
            if (_disposed || version != Volatile.Read(ref _requestVersion)) return;
            var editorPid = await _bridge.WaitForAuthenticatedEditorAsync().WaitAsync(_lifetime.Token).ConfigureAwait(true);
            if (_disposed || version != Volatile.Read(ref _requestVersion)) return;
            if (_bridge.AuthenticatedEditorProcessId != editorPid)
                throw new InvalidOperationException("The Editor connection is not authenticated.");
            ValidateParent(parentWindow, editorPid);
            var parent = new IntPtr(parentWindow);
            var owner = GetAncestor(parent, 2);
            if (owner == IntPtr.Zero || GetWindowThreadProcessId(owner, out var ownerPid) == 0 || ownerPid != (uint)editorPid)
                throw new InvalidOperationException("The Editor window is unavailable.");
            _window.SetEditorOwner(owner);
            var context = GetWindowDpiAwarenessContext(parent);
            if (context == IntPtr.Zero || !AreDpiAwarenessContextsEqual(context, GetThreadDpiAwarenessContext()))
                throw new InvalidOperationException("The Editor panel DPI awareness does not match the graph.");
            Detach();
            var parameters = new HwndSourceParameters("DLUMA Quest Graph")
            {
                ParentWindow = parent,
                WindowStyle = WsChild | WsVisible,
                Width = 1,
                Height = 1
            };
            _source = new HwndSource(parameters);
            if (!AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(_source.Handle), context))
                throw new InvalidOperationException("The graph child window did not inherit the Editor's DPI awareness.");
            _source.AddHook(WindowProc);
            _source.RootVisual = _root;
            _token = panelToken;
            var child = _source.Handle.ToInt64();
            var result = await _bridge.PanelReadyAsync(new PanelReadyRequest(parentWindow, child, panelToken)).ConfigureAwait(true);
            if (_disposed || version != Volatile.Read(ref _requestVersion)) return;
            if (!result.Bound)
                throw new InvalidOperationException(result.Message.Length == 0 ? "The Editor could not bind the graph panel." : result.Message);
            _generation = result.BindingGeneration;
            FocusRoot();
        }
        catch
        {
            if (version == Volatile.Read(ref _requestVersion)) Detach();
            throw;
        }
        finally { _attachGate.Release(); }
    }

    private void OnNotification(object? sender, QuestBridgeEnvelope envelope)
    {
        if (envelope.Type != "attachPanel") return;
        try
        {
            var message = QuestBridgeClient.DeserializePayload<AttachPanelMessage>(envelope);
            _ = _window.Dispatcher.BeginInvoke(new Action(async () =>
            {
                try { await AttachAsync(message.ParentWindow, message.PanelToken); }
                catch (Exception exception) { if (!_disposed) Report(exception); }
            }));
        }
        catch (Exception exception) { _bridge.AbortProtocolSession(exception.Message); }
    }

    private void OnDisconnected(object? sender, string reason)
    {
        Interlocked.Increment(ref _requestVersion);
        _ = _window.Dispatcher.BeginInvoke(new Action(() =>
        {
            if (_disposed) return;
            Dispose();
            _window.CloseEmbedded();
            Application.Current.Shutdown();
        }));
    }

    private IntPtr WindowProc(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if ((uint)message == _focusMessage && _generation != 0 &&
            unchecked((ulong)wParam.ToInt64()) == _token && unchecked((ulong)lParam.ToInt64()) == _generation)
        {
            FocusRoot();
            handled = true;
        }
        return IntPtr.Zero;
    }

    private void FocusRoot()
    {
        if (_source is null || _source.IsDisposed || !IsWindowVisible(_source.Handle)) return;
        _root.Focusable = true;
        _root.Focus();
        Keyboard.Focus(_root);
    }

    private void Report(Exception exception)
    {
        if (_window.DataContext is MainViewModel viewModel) viewModel.ReportUnhandledError(exception);
    }

    private void Detach()
    {
        _token = 0;
        _generation = 0;
        var source = _source;
        _source = null;
        if (source is null || source.IsDisposed) return;
        source.RootVisual = null;
        source.RemoveHook(WindowProc);
        source.Dispose();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _lifetime.Cancel();
        Interlocked.Increment(ref _requestVersion);
        _bridge.NotificationReceived -= OnNotification;
        _bridge.Disconnected -= OnDisconnected;
        Detach();
        _theme?.DetachRoot(_root);
    }

    private static void ValidateParent(long handle, int editorPid)
    {
        if (handle <= 0 || editorPid <= 0 || !IsWindow(new IntPtr(handle)) ||
            GetWindowThreadProcessId(new IntPtr(handle), out var owner) == 0 || owner != (uint)editorPid)
            throw new InvalidOperationException("The Editor panel window is unavailable or belongs to another process.");
    }

    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] private static extern IntPtr GetWindowDpiAwarenessContext(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern IntPtr GetThreadDpiAwarenessContext();
    [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] private static extern bool AreDpiAwarenessContextsEqual(IntPtr first, IntPtr second);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "RegisterWindowMessageW")]
    private static extern uint RegisterWindowMessage(string name);
}
