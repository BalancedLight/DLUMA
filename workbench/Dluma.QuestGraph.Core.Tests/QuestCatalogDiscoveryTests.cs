using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestCatalogDiscoveryTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-quest-catalog-discovery-{Guid.NewGuid():N}");

    [Fact]
    public void DiscoversProjectAndInstalledDefinitionsFromSuppliedRoots()
    {
        var projectRoot = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "project")).FullName;
        var installedRoot = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "installed")).FullName;
        Write(projectRoot, "private/quest-macros-private.edf", ",project_macro target =\n  Target target\n");
        Write(projectRoot, "private/QuestEditorConfig.scr", "Block(\",project_macro\")\n{\n  Expand(\"post\")\n}\n");
        Write(projectRoot, "private/phases_struct_defs.xml", """
            <?xml version="1.0"?>
            <PhaseTypeDefinitions>
              <PhaseType name="project_phase"><attribute name="value" type="string"/></PhaseType>
            </PhaseTypeDefinitions>
            """);
        Write(installedRoot, "quest-macros-public.edf", ",installed_macro value =\n  Value value\n");
        Write(installedRoot, "quests_object_defs.xml", """
            <?xml version="1.0"?>
            <QuestObjectDefinitions>
              <QuestObject name="SyntheticTarget"><class name="SyntheticClass"/></QuestObject>
            </QuestObjectDefinitions>
            """);
        Write(installedRoot, "unrelated.xml", "<Unrelated/>");
        var service = new QuestCatalogDiscoveryService();

        var result = service.Discover(
        [
            new(installedRoot, QuestCatalogRootScope.Installed),
            new(projectRoot, QuestCatalogRootScope.ProjectPrivate)
        ]);

        Assert.False(result.CacheHit);
        Assert.False(result.WasTruncated);
        Assert.Equal(5, result.Inputs.Count);
        Assert.True(result.Catalog.ContainsMacro("project_macro"));
        Assert.True(result.Catalog.ContainsMacro("project_phase"));
        Assert.True(result.Catalog.ContainsMacro("installed_macro"));
        Assert.Contains(result.Catalog.Entries, entry =>
            entry.Kind == QuestCatalogEntryKind.QuestObject &&
            entry.Name == "SyntheticTarget");
        Assert.Equal(
            QuestCatalogRootScope.ProjectPrivate,
            result.Inputs[0].Scope);
        Assert.DoesNotContain(result.Inputs, input => Path.IsPathRooted(input.RelativePath));
        Assert.All(result.Inputs, input => Assert.Equal(64, input.Sha256.Length));
        Assert.DoesNotContain(result.Inputs, input => input.RelativePath.EndsWith("unrelated.xml", StringComparison.Ordinal));
    }

    [Fact]
    public void ContentHashCacheHitsAndInvalidatesWithoutTimestampTrust()
    {
        var root = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "cache")).FullName;
        var path = Write(root, "macros.edf", ",alpha value =\n  Value value\n");
        var service = new QuestCatalogDiscoveryService();
        var roots = new[] { new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.ProjectPrivate) };

        var first = service.Discover(roots);
        var second = service.Discover(roots);
        File.WriteAllText(path, ",bravo value =\n  Value value\n", new UTF8Encoding(false));
        var changed = service.Discover(roots);

        Assert.False(first.CacheHit);
        Assert.True(second.CacheHit);
        Assert.Same(first.Catalog, second.Catalog);
        Assert.Equal(first.CacheKey, second.CacheKey);
        Assert.False(changed.CacheHit);
        Assert.NotEqual(first.CacheKey, changed.CacheKey);
        Assert.True(changed.Catalog.ContainsMacro("bravo"));
        Assert.False(changed.Catalog.ContainsMacro("alpha"));
    }

    [Fact]
    public void DepthAndFileLimitsBoundTraversalDeterministically()
    {
        var root = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "bounded")).FullName;
        Write(root, "a-root.edf", ",root_macro value =\n  Value value\n");
        Write(root, "one/a-level-one.edf", ",level_one value =\n  Value value\n");
        Write(root, "one/two/a-hidden.edf", ",hidden value =\n  Value value\n");
        var depthService = new QuestCatalogDiscoveryService(new QuestCatalogDiscoveryOptions
        {
            MaximumDepth = 1
        });

        var depthResult = depthService.Discover(
            new[] { new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.ProjectPrivate) });

        Assert.True(depthResult.Catalog.ContainsMacro("root_macro"));
        Assert.True(depthResult.Catalog.ContainsMacro("level_one"));
        Assert.False(depthResult.Catalog.ContainsMacro("hidden"));
        Assert.Contains(depthResult.Diagnostics, diagnostic => diagnostic.Code == "QCAT010");

        var fileService = new QuestCatalogDiscoveryService(new QuestCatalogDiscoveryOptions
        {
            MaximumFiles = 1
        });
        var fileResult = fileService.Discover(
            new[] { new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.ProjectPrivate) });

        Assert.True(fileResult.WasTruncated);
        Assert.Single(fileResult.Inputs);
        Assert.Contains(fileResult.Diagnostics, diagnostic => diagnostic.Code == "QCAT011");
    }

    [Fact]
    public void AggregateAndPerFileByteLimitsFailClosed()
    {
        var root = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "bytes")).FullName;
        Write(root, "a.edf", ",a v =\n");
        Write(root, "b.edf", ",b v =\n");
        Write(root, "c.edf", ",c v =\n");
        var aggregateService = new QuestCatalogDiscoveryService(new QuestCatalogDiscoveryOptions
        {
            MaximumBytesPerFile = 20,
            MaximumTotalBytes = 20
        });

        var aggregate = aggregateService.Discover(
            new[] { new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.ProjectPrivate) });

        Assert.True(aggregate.WasTruncated);
        Assert.Equal(2, aggregate.Inputs.Count);
        Assert.Contains(aggregate.Diagnostics, diagnostic => diagnostic.Code == "QCAT014");

        var perFileRoot = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "per-file")).FullName;
        Write(perFileRoot, "a-large.edf", new string('x', 64));
        Write(perFileRoot, "b-small.edf", ",small v =\n");
        var perFileService = new QuestCatalogDiscoveryService(new QuestCatalogDiscoveryOptions
        {
            MaximumBytesPerFile = 32,
            MaximumTotalBytes = 64
        });
        var perFile = perFileService.Discover(
            new[] { new QuestCatalogDiscoveryRoot(perFileRoot, QuestCatalogRootScope.ProjectPrivate) });

        Assert.Single(perFile.Inputs);
        Assert.True(perFile.Catalog.ContainsMacro("small"));
        Assert.Contains(perFile.Diagnostics, diagnostic => diagnostic.Code == "QCAT013");
    }

    [Fact]
    public void MalformedDefinitionsAreSkippedWithoutPoisoningValidInputs()
    {
        var root = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "malformed")).FullName;
        Write(root, "a-valid.edf", ",valid value =\n  Value value\n");
        Write(root, "b-invalid.xml", "<PhaseTypeDefinitions><PhaseType");
        var service = new QuestCatalogDiscoveryService();

        var result = service.Discover(
            new[] { new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.ProjectPrivate) });

        Assert.True(result.Catalog.ContainsMacro("valid"));
        Assert.Single(result.Inputs);
        Assert.Contains(result.Diagnostics, diagnostic => diagnostic.Code == "QCAT017");
    }

    [Fact]
    public void ReparsePointRootsAndEntriesAreNeverTraversed()
    {
        var root = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "reparse-root")).FullName;
        var outside = Directory.CreateDirectory(Path.Combine(_temporaryRoot, "outside")).FullName;
        Write(outside, "secret.edf", ",must_not_load value =\n  Value value\n");
        var link = Path.Combine(root, "linked");
        try
        {
            Directory.CreateSymbolicLink(link, outside);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or PlatformNotSupportedException)
        {
            return;
        }
        var service = new QuestCatalogDiscoveryService();

        var entryResult = service.Discover(
            new[] { new QuestCatalogDiscoveryRoot(root, QuestCatalogRootScope.ProjectPrivate) });
        var rootResult = service.Discover(
            new[] { new QuestCatalogDiscoveryRoot(link, QuestCatalogRootScope.ProjectPrivate) });

        Assert.False(entryResult.Catalog.ContainsMacro("must_not_load"));
        Assert.Contains(entryResult.Diagnostics, diagnostic => diagnostic.Code == "QCAT009");
        Assert.Empty(rootResult.Inputs);
        Assert.Contains(rootResult.Diagnostics, diagnostic => diagnostic.Code == "QCAT002");
    }

    private static string Write(string root, string relativePath, string text)
    {
        var path = Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text, new UTF8Encoding(false));
        return path;
    }

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
