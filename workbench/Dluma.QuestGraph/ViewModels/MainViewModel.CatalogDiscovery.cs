using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class MainViewModel
{
    private readonly QuestCatalogDiscoveryService _catalogDiscovery = new();
    private IReadOnlyList<string> _installedCatalogRoots = Array.Empty<string>();
    private CancellationTokenSource? _catalogDiscoveryCancellation;
    private string? _manualCatalogRoot;
    private long _catalogDiscoveryGeneration;

    private void SetInstalledCatalogRoots(IEnumerable<string> roots)
    {
        ArgumentNullException.ThrowIfNull(roots);
        _installedCatalogRoots = roots
            .Where(root => !string.IsNullOrWhiteSpace(root))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Take(16)
            .ToArray();
    }

    private void SetManualCatalogRoot(string root)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(root);
        _manualCatalogRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
    }

    private async Task<bool> DiscoverRuntimeCatalogAsync(string? projectRoot)
    {
        var roots = new List<QuestCatalogDiscoveryRoot>();
        if (!string.IsNullOrWhiteSpace(projectRoot))
            roots.Add(new QuestCatalogDiscoveryRoot(
                projectRoot,
                QuestCatalogRootScope.ProjectPrivate));
        if (!string.IsNullOrWhiteSpace(_manualCatalogRoot) &&
            !roots.Any(root => string.Equals(
                root.Path,
                _manualCatalogRoot,
                StringComparison.OrdinalIgnoreCase)))
            roots.Add(new QuestCatalogDiscoveryRoot(
                _manualCatalogRoot,
                QuestCatalogRootScope.ProjectPrivate));
        roots.AddRange(_installedCatalogRoots.Select(root =>
            new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.Installed)));
        if (roots.Count == 0)
            return false;

        _catalogDiscoveryCancellation?.Cancel();
        _catalogDiscoveryCancellation?.Dispose();
        var cancellation = new CancellationTokenSource();
        _catalogDiscoveryCancellation = cancellation;
        var generation = Interlocked.Increment(ref _catalogDiscoveryGeneration);
        QuestCatalogDiscoveryResult result;
        try
        {
            result = await _catalogDiscovery.DiscoverAsync(roots, cancellation.Token)
                .ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
            return false;
        }
        if (generation != Interlocked.Read(ref _catalogDiscoveryGeneration))
            return false;
        if (result.Inputs.Count == 0)
        {
            GlobalStatus = result.Diagnostics.Count == 0
                ? "No persisted quest definitions were discovered; the generic fallback remains active."
                : $"Quest definition discovery found no usable inputs and reported {result.Diagnostics.Count} diagnostic(s).";
            return false;
        }

        _catalog = result.Catalog;
        PopulateCatalogPalette(result.Catalog);
        foreach (var document in Documents)
            document.Reparse(result.Catalog);
        var cacheDescription = result.CacheHit ? "cached" : "parsed";
        var truncation = result.WasTruncated ? " Discovery reached a safety limit." : string.Empty;
        GlobalStatus = $"Runtime quest catalog {cacheDescription}: {result.Catalog.Entries.Count} definitions from {result.Inputs.Count} hashed input(s).{truncation}";
        return true;
    }

    private void DisposeCatalogDiscovery()
    {
        Interlocked.Increment(ref _catalogDiscoveryGeneration);
        _catalogDiscoveryCancellation?.Cancel();
        _catalogDiscoveryCancellation?.Dispose();
        _catalogDiscoveryCancellation = null;
    }
}
