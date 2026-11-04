using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;

namespace Dluma.EditorWiki;

internal sealed class EditorPanelHost : IDisposable
{
    private readonly LaunchOptions _options;
    private readonly EditorIdentity _editor;
    private readonly QuestBridgeClient _bridge;
    private readonly BrowserView _view;
    private readonly uint _focusMessage = RegisterWindowMessage("DLUMA.EditorPanel.Focus.1");
    private HwndSource? _source;
    private ulong _generation;
    private bool _disposed;

    internal EditorPanelHost(LaunchOptions options, EditorIdentity editor,
        QuestBridgeClient bridge, EditorThemeService theme)
    {
        _options = options;
        _editor = editor;
        _bridge = bridge;
        _view = new BrowserView();
        _view.AttachTheme(theme);
    }

    // Called on the UI thread before any WPF window or HwndSource is created.
    internal static void PrepareUiThread(LaunchOptions options, EditorIdentity editor)
    {
        editor.RequireParent();
        var context = GetWindowDpiAwarenessContext(new IntPtr(options.PanelParent));
        if (context == IntPtr.Zero || SetThreadDpiAwarenessContext(context) == IntPtr.Zero ||
            !AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), context))
            throw new InvalidOperationException("The Editor panel display settings could not be matched.");
    }

    internal async Task AttachAsync()
    {
        if (_bridge.AuthenticatedEditorProcessId != _options.EditorPid)
            throw new InvalidDataException("The Editor panel session is not authenticated.");
        _editor.RequireParent();
        var parent = new IntPtr(_options.PanelParent);
        var context = GetWindowDpiAwarenessContext(parent);
        if (context == IntPtr.Zero || !AreDpiAwarenessContextsEqual(context, GetThreadDpiAwarenessContext()))
            throw new InvalidOperationException("The Editor panel display settings changed.");
        var parameters = new HwndSourceParameters("DLUMA Editor Wiki")
        {
            ParentWindow = parent,
            WindowStyle = 0x40000000 | 0x10000000,
            ExtendedWindowStyle = 0x00000004, // WS_EX_NOPARENTNOTIFY
            Width = 1,
            Height = 1
        };
        var source = new HwndSource(parameters);
        _source = source;
        source.Disposed += OnSourceDisposed;
        source.AddHook(WindowProc);
        if (!AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(source.Handle), context))
            throw new InvalidOperationException("The Editor panel display settings do not match.");
        source.RootVisual = _view;
        var result = await _bridge.PanelReadyAsync(new PanelReadyRequest(
            _options.PanelParent, source.Handle.ToInt64(), _options.PanelToken)).ConfigureAwait(true);
        if (_disposed || source.IsDisposed) return;
        _editor.RequireParent();
        if (!result.Bound || result.BindingGeneration == 0)
            throw new InvalidOperationException("The Editor could not open the wiki panel.");
        _generation = result.BindingGeneration;
        FocusRoot();
    }

    private IntPtr WindowProc(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == 0x0082 && !_disposed)
            Application.Current.Dispatcher.BeginInvoke(() => Application.Current.Shutdown());
        if ((uint)message == _focusMessage && _generation != 0 &&
            unchecked((ulong)wParam.ToInt64()) == _options.PanelToken &&
            unchecked((ulong)lParam.ToInt64()) == _generation)
        {
            FocusRoot();
            handled = true;
        }
        return IntPtr.Zero;
    }

    private void FocusRoot()
    {
        if (_source is null || _source.IsDisposed || !IsWindowVisible(_source.Handle)) return;
        _view.Focusable = true;
        _view.Focus();
        Keyboard.Focus(_view);
    }

    private void OnSourceDisposed(object? sender, EventArgs e)
    {
        if (!_disposed) Application.Current.Dispatcher.BeginInvoke(() => Application.Current.Shutdown());
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _generation = 0;
        var source = _source;
        _source = null;
        if (source is not null)
        {
            source.Disposed -= OnSourceDisposed;
            if (!source.IsDisposed)
            {
                source.RootVisual = null;
                source.RemoveHook(WindowProc);
                source.Dispose();
            }
        }
        _view.Dispose();
    }

    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetWindowDpiAwarenessContext(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetThreadDpiAwarenessContext();
    [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] private static extern bool AreDpiAwarenessContextsEqual(IntPtr first, IntPtr second);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "RegisterWindowMessageW")]
    private static extern uint RegisterWindowMessage(string name);
}
