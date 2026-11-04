using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Dluma.QuestGraph.Services;
using Microsoft.Web.WebView2.Core;

namespace Dluma.EditorWiki;

public partial class BrowserView : UserControl, IDisposable
{
    public const string HomeAddress = "https://dldt.miraheze.org/wiki/Main_Page";
    private EditorThemeService? _theme;
    private bool _started;
    private bool _disposed;

    public BrowserView() => InitializeComponent();

    internal void AttachTheme(EditorThemeService theme)
    {
        ArgumentNullException.ThrowIfNull(theme);
        if (_theme is not null) throw new InvalidOperationException("The theme is already attached.");
        _theme = theme;
        _theme.AttachRoot(this);
        _theme.ThemeChanged += OnThemeChanged;
        ApplyBrowserTheme();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_started || _disposed) return;
        _started = true;
        try
        {
            var folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "DLUMA", "EditorWiki", "WebView2");
            var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: folder);
            if (_disposed) return;
            await Browser.EnsureCoreWebView2Async(environment);
            if (_disposed) return;
            var web = Browser.CoreWebView2;
            web.NavigationStarting += OnNavigationStarting;
            web.NavigationCompleted += OnNavigationCompleted;
            web.NewWindowRequested += OnNewWindowRequested;
            web.SourceChanged += OnSourceChanged;
            web.DocumentTitleChanged += OnDocumentTitleChanged;
            web.HistoryChanged += OnHistoryChanged;
            ApplyBrowserTheme();
            ReloadButton.IsEnabled = HomeButton.IsEnabled = true;
            web.Navigate(HomeAddress);
        }
        catch (WebView2RuntimeNotFoundException)
        {
            ShowUnavailable("Microsoft Edge WebView2 Runtime is required to open the wiki.");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                           COMException or InvalidOperationException or ArgumentException)
        {
            ShowUnavailable("The wiki browser could not start.");
        }
    }

    private static bool IsWebAddress(string? value) =>
        Uri.TryCreate(value, UriKind.Absolute, out var uri) &&
        (uri.Scheme == Uri.UriSchemeHttps || uri.Scheme == Uri.UriSchemeHttp);

    private void OnNavigationStarting(object? sender, CoreWebView2NavigationStartingEventArgs e)
    {
        if (!IsWebAddress(e.Uri)) e.Cancel = true;
    }

    private void OnNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
    {
        if (_disposed) return;
        if (!e.IsSuccess || e.HttpStatusCode >= 400)
            PageTitle.Text = "Could not load page";
        UpdateNavigation();
    }

    private void OnNewWindowRequested(object? sender, CoreWebView2NewWindowRequestedEventArgs e)
    {
        e.Handled = true;
        if (!_disposed && IsWebAddress(e.Uri)) Browser.CoreWebView2.Navigate(e.Uri);
    }

    private void OnSourceChanged(object? sender, CoreWebView2SourceChangedEventArgs e)
    {
        if (!_disposed) AddressBox.Text = Browser.CoreWebView2.Source;
    }

    private void OnDocumentTitleChanged(object? sender, object e)
    {
        if (!_disposed) PageTitle.Text = string.IsNullOrWhiteSpace(Browser.CoreWebView2.DocumentTitle)
            ? "Wiki" : Browser.CoreWebView2.DocumentTitle;
    }

    private void OnHistoryChanged(object? sender, object e) => UpdateNavigation();

    private void UpdateNavigation()
    {
        if (_disposed || Browser.CoreWebView2 is not { } web) return;
        BackButton.IsEnabled = web.CanGoBack;
        ForwardButton.IsEnabled = web.CanGoForward;
    }

    private void OnBack(object sender, RoutedEventArgs e) { if (Browser.CoreWebView2?.CanGoBack == true) Browser.CoreWebView2.GoBack(); }
    private void OnForward(object sender, RoutedEventArgs e) { if (Browser.CoreWebView2?.CanGoForward == true) Browser.CoreWebView2.GoForward(); }
    private void OnReload(object sender, RoutedEventArgs e) => Browser.CoreWebView2?.Reload();
    private void OnHome(object sender, RoutedEventArgs e) => Browser.CoreWebView2?.Navigate(HomeAddress);
    private void OnThemeChanged(object? sender, EventArgs e) => ApplyBrowserTheme();

    private void ApplyBrowserTheme()
    {
        if (_disposed) return;
        var background = TryFindResource("EditorTheme.Window") as SolidColorBrush;
        if (background is not null)
        {
            var color = background.Color;
            Browser.DefaultBackgroundColor = System.Drawing.Color.FromArgb(255, color.R, color.G, color.B);
        }
        if (Browser.CoreWebView2 is not { } web) return;
        if (_theme?.IsHighContrast == true)
            web.Profile.PreferredColorScheme = CoreWebView2PreferredColorScheme.Auto;
        else if (background is not null)
        {
            var color = background.Color;
            web.Profile.PreferredColorScheme = color.R * 299 + color.G * 587 + color.B * 114 < 128_000
                ? CoreWebView2PreferredColorScheme.Dark : CoreWebView2PreferredColorScheme.Light;
        }
    }

    private void ShowUnavailable(string message)
    {
        if (_disposed) return;
        Browser.Visibility = Visibility.Collapsed;
        MessageText.Text = message;
        MessagePanel.Visibility = Visibility.Visible;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Loaded -= OnLoaded;
        if (_theme is not null)
        {
            _theme.ThemeChanged -= OnThemeChanged;
            _theme.DetachRoot(this);
            _theme = null;
        }
        if (Browser.CoreWebView2 is { } web)
        {
            web.NavigationStarting -= OnNavigationStarting;
            web.NavigationCompleted -= OnNavigationCompleted;
            web.NewWindowRequested -= OnNewWindowRequested;
            web.SourceChanged -= OnSourceChanged;
            web.DocumentTitleChanged -= OnDocumentTitleChanged;
            web.HistoryChanged -= OnHistoryChanged;
        }
        Browser.Dispose();
    }
}
