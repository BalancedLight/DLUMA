using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Dluma.EditorTheme;
using Microsoft.Win32;

namespace Dluma.QuestGraph.Services;

internal sealed class EditorThemeService : IDisposable
{
    internal static EditorThemeService? Current { get; private set; }
    public event EventHandler<EventArgs>? ThemeChanged;
    private uint[]? _colors;
    internal string GraphColor(string fallback, ThemeRole highContrastRole) =>
        _kind == 2 && _colors is not null ? $"#{_colors[(int)highContrastRole]:X8}" : fallback;
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct SystemState
    {
        public uint Size, Version;
        public ulong Generation;
        public uint AppsModeAvailable, AppsDark, HighContrast, Reserved;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public uint[] Colors;
        public uint ReservedTail;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct Resolved
    {
        public uint Size, Version;
        public ulong Generation;
        public uint Kind, Preference;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public uint[] Colors;
        public uint Reserved;
    }
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void Changed(IntPtr context, IntPtr state);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Start(Changed callback, IntPtr context, ref SystemState state);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Resolve(uint preference, ref SystemState state, ref Resolved resolved);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Stop();

    private readonly object _gate = new();
    private readonly Dispatcher _dispatcher = Application.Current.Dispatcher;
    private readonly ResourceDictionary _palette = new();
    private readonly HashSet<FrameworkElement> _roots = new();
    private IntPtr _library;
    private Changed? _callback;
    private Resolve? _resolve;
    private Stop? _stop;
    private SystemState _state;
    private FileSystemWatcher? _watcher;
    private CancellationTokenSource? _reload;
    private string? _configPath;
    private uint _preference;
    private bool _disposed;
    private int _queued;
    private readonly HashSet<Window> _windows = new();
    private uint _kind;
    private bool _fallbackEvents;
    internal bool IsHighContrast => _kind == 2;

    public void Initialize(string? themeConfigPath)
    {
        Current = this;
        Application.Current.Resources.MergedDictionaries.Add(_palette);
        try { _configPath = string.IsNullOrWhiteSpace(themeConfigPath) ? null : Path.GetFullPath(themeConfigPath); }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException) { _configPath = null; }
        _preference = ReadPreference(_configPath);
        var path = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "editor", "dluma_editor_theme_system.dll"));
        if (NativeLibrary.TryLoad(path, out _library))
        {
            try
            {
                var start = Marshal.GetDelegateForFunctionPointer<Start>(NativeLibrary.GetExport(_library, "dluma_editor_theme_system_start"));
                _resolve = Marshal.GetDelegateForFunctionPointer<Resolve>(NativeLibrary.GetExport(_library, "dluma_editor_theme_system_resolve"));
                _stop = Marshal.GetDelegateForFunctionPointer<Stop>(NativeLibrary.GetExport(_library, "dluma_editor_theme_system_stop"));
                _callback = OnChanged;
                var initial = NewState();
                if (start(_callback, IntPtr.Zero, ref initial) != 0) _state = initial;
                else ReleaseLibrary();
            }
            catch (Exception ex) when (ex is EntryPointNotFoundException or MarshalDirectiveException or BadImageFormatException) { ReleaseLibrary(); }
        }
        if (_resolve is null)
        {
            try { SystemEvents.UserPreferenceChanged += OnSystemPreferenceChanged; _fallbackEvents = true; }
            catch (InvalidOperationException) { }
        }
        ApplyCurrentTheme();
        if (_configPath is null) return;
        try
        {
            _watcher = new FileSystemWatcher(Path.GetDirectoryName(_configPath)!, Path.GetFileName(_configPath))
            { NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.CreationTime | NotifyFilters.Size };
            _watcher.Changed += OnFileChanged;
            _watcher.Created += OnFileChanged;
            _watcher.Deleted += OnFileChanged;
            _watcher.Renamed += OnFileChanged;
            _watcher.EnableRaisingEvents = true;
            OnFileChanged(_watcher, new FileSystemEventArgs(WatcherChangeTypes.Changed, Path.GetDirectoryName(_configPath)!, Path.GetFileName(_configPath)));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException) { _watcher?.Dispose(); _watcher = null; }
    }

    public void AttachWindow(Window window)
    {
        if (_disposed || !_windows.Add(window)) return;
        window.SourceInitialized += OnSourceInitialized;
        window.Closed += OnWindowClosed;
        UpdateTitleBar(window);
    }

    internal void AttachRoot(FrameworkElement root)
    {
        _dispatcher.VerifyAccess();
        if (_disposed || _roots.Contains(root)) return;
        // HwndSource content is outside Application.Windows. Owning the shared
        // palette here lets resource changes invalidate this visual tree too.
        root.Resources.MergedDictionaries.Add(_palette);
        _roots.Add(root);
    }

    internal void DetachRoot(FrameworkElement root)
    {
        _dispatcher.VerifyAccess();
        if (_roots.Remove(root)) root.Resources.MergedDictionaries.Remove(_palette);
    }

    public void ApplyCurrentTheme()
    {
        if (!_dispatcher.CheckAccess()) { QueueApply(); return; }
        SystemState state; uint preference;
        lock (_gate) { if (_disposed) return; state = _state; preference = _preference; }
        var resolved = new Resolved { Size = (uint)Marshal.SizeOf<Resolved>(), Version = 1, Colors = new uint[17] };
        if (_resolve is null || state.Colors is null || _resolve(preference, ref state, ref resolved) == 0)
            resolved = ResolveFallback(preference);
        if (_colors is not null && _kind == resolved.Kind && _colors.SequenceEqual(resolved.Colors)) return;
        _kind = resolved.Kind;
        _colors = resolved.Colors;
        for (var i = 0; i < (int)ThemeRole.Count; i++)
        {
            var value = resolved.Colors[i];
            _palette[$"EditorTheme.{(ThemeRole)i}"] = new SolidColorBrush(Color.FromArgb((byte)(value >> 24), (byte)(value >> 16), (byte)(value >> 8), (byte)value));
        }
        var inactiveText = resolved.Colors[(int)(resolved.Kind == 2 ? ThemeRole.SelectionText : ThemeRole.Text)];
        _palette["EditorTheme.InactiveSelectionText"] = new SolidColorBrush(Color.FromArgb(
            (byte)(inactiveText >> 24), (byte)(inactiveText >> 16), (byte)(inactiveText >> 8), (byte)inactiveText));
        foreach (var window in _windows.ToArray()) UpdateTitleBar(window);
        ThemeChanged?.Invoke(this, EventArgs.Empty);
    }

    private static Resolved ResolveFallback(uint preference)
    {
        var highContrast = SystemParameters.HighContrast;
        var dark = preference == 2;
        if (preference == 0)
        {
            try { dark = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 1) is int value && value == 0; }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.SecurityException) { }
        }
        var colors = (uint[])(dark ? ThemePalette.Dark : ThemePalette.Light).Clone();
        if (highContrast)
        {
            Color[] system = { SystemColors.WindowColor, SystemColors.ControlColor, SystemColors.WindowColor, SystemColors.WindowColor,
                SystemColors.WindowTextColor, SystemColors.ControlTextColor, SystemColors.WindowTextColor, SystemColors.GrayTextColor,
                SystemColors.WindowFrameColor, SystemColors.HighlightColor, SystemColors.HighlightColor, SystemColors.HighlightColor,
                SystemColors.HighlightTextColor, SystemColors.HighlightColor, SystemColors.WindowTextColor, SystemColors.WindowTextColor, SystemColors.WindowTextColor };
            for (var i = 0; i < colors.Length; i++) colors[i] = 0xff000000u | ((uint)system[i].R << 16) | ((uint)system[i].G << 8) | system[i].B;
        }
        return new Resolved { Kind = highContrast ? 2u : dark ? 1u : 0u, Preference = preference, Colors = colors };
    }
    private void OnSystemPreferenceChanged(object sender, UserPreferenceChangedEventArgs e) { _ = sender; _ = e; QueueApply(); }

    private void OnChanged(IntPtr context, IntPtr pointer)
    {
        _ = context;
        if (pointer == IntPtr.Zero) return;
        var state = Marshal.PtrToStructure<SystemState>(pointer);
        if (state.Size != Marshal.SizeOf<SystemState>() || state.Version != 1 || state.Colors?.Length != 17) return;
        lock (_gate) { if (_disposed) return; _state = state; }
        QueueApply();
    }
    private void QueueApply()
    {
        if (Interlocked.Exchange(ref _queued, 1) != 0) return;
        if (_dispatcher.HasShutdownStarted) { Interlocked.Exchange(ref _queued, 0); return; }
        _dispatcher.BeginInvoke(() => { Interlocked.Exchange(ref _queued, 0); ApplyCurrentTheme(); });
    }
    private void OnFileChanged(object sender, FileSystemEventArgs e)
    {
        _ = sender; _ = e;
        CancellationToken token;
        lock (_gate)
        {
            if (_disposed) return;
            _reload?.Cancel(); _reload?.Dispose(); _reload = new CancellationTokenSource(); token = _reload.Token;
        }
        _ = Task.Run(async () =>
        {
            try
            {
                await Task.Delay(100, token).ConfigureAwait(false);
                var preference = ReadPreference(_configPath);
                lock (_gate) { if (_disposed || token.IsCancellationRequested) return; _preference = preference; }
                QueueApply();
            }
            catch (OperationCanceledException) { }
        });
    }
    private static uint ReadPreference(string? path)
    {
        if (path is null) return 0;
        try
        {
            var section = false;
            foreach (var line in File.ReadLines(path))
            {
                var text = line.Trim();
                if (text.StartsWith('[') && text.EndsWith(']')) { section = string.Equals(text[1..^1].Trim(), "loader", StringComparison.OrdinalIgnoreCase); continue; }
                if (!section || text.StartsWith(';') || text.StartsWith('#')) continue;
                var equals = text.IndexOf('=');
                if (equals < 0 || !string.Equals(text[..equals].Trim(), "editor_theme", StringComparison.OrdinalIgnoreCase)) continue;
                return text[(equals + 1)..].Trim().ToLowerInvariant() switch { "light" => 1, "dark" => 2, _ => 0 };
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException) { }
        return 0;
    }
    private static SystemState NewState() => new() { Size = (uint)Marshal.SizeOf<SystemState>(), Version = 1, Colors = new uint[17] };
    private void OnSourceInitialized(object? sender, EventArgs e) { _ = e; if (sender is Window window) UpdateTitleBar(window); }
    private void OnWindowClosed(object? sender, EventArgs e)
    {
        _ = e;
        if (sender is not Window window || !_windows.Remove(window)) return;
        window.SourceInitialized -= OnSourceInitialized;
        window.Closed -= OnWindowClosed;
    }
    private void UpdateTitleBar(Window window)
    {
        var hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero) return;
        var dark = _kind == 1 ? 1 : 0;
        try { _ = DwmSetWindowAttribute(hwnd, 20, ref dark, sizeof(int)); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { }
    }
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
    private void ReleaseLibrary() { if (_library != IntPtr.Zero) NativeLibrary.Free(_library); _library = IntPtr.Zero; _resolve = null; _stop = null; _callback = null; }
    public void Dispose()
    {
        lock (_gate) { if (_disposed) return; _disposed = true; _reload?.Cancel(); }
        _watcher?.Dispose();
        if (_fallbackEvents) SystemEvents.UserPreferenceChanged -= OnSystemPreferenceChanged;
        _stop?.Invoke();
        foreach (var window in _windows)
        {
            window.SourceInitialized -= OnSourceInitialized;
            window.Closed -= OnWindowClosed;
        }
        _windows.Clear();
        foreach (var root in _roots) root.Resources.MergedDictionaries.Remove(_palette);
        _roots.Clear();
        Application.Current.Resources.MergedDictionaries.Remove(_palette);
        _reload?.Dispose();
        if (Current == this) Current = null;
        ReleaseLibrary();
    }
}
