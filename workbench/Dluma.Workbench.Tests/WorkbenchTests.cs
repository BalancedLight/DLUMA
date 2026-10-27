using System.Text.Json;
using Dluma.Workbench.Core;
using Xunit;

namespace Dluma.Workbench.Tests;

public sealed class WorkbenchTests : IDisposable
{
    private readonly string _repositoryRoot = RepositoryLocator.Find();
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-workbench-tests-{Guid.NewGuid():N}");

    [Fact]
    public void CatalogLoadsAndExposesAllLanguageAdapters()
    {
        var catalog = ApiCatalogService.Load(Path.Combine(
            _repositoryRoot, "api", "generated", "workbench_api.json"));
        Assert.Equal("1.0", catalog.Version);
        Assert.Equal(64, catalog.CatalogSha256.Length);
        Assert.NotEmpty(catalog.Operations);
        Assert.All(catalog.Operations, operation =>
        {
            Assert.True(operation.Adapters.ContainsKey("lua"));
            Assert.True(operation.Adapters.ContainsKey("csharp"));
            Assert.True(operation.Adapters.ContainsKey("c"));
            Assert.True(operation.Adapters.ContainsKey("cpp"));
        });
    }

    [Fact]
    public void FiltersAndSnippetsUseCanonicalCatalog()
    {
        var catalog = ApiCatalogService.Load(Path.Combine(
            _repositoryRoot, "api", "generated", "workbench_api.json"));
        var filtered = ApiCatalogService.Filter(
            catalog, "world.find", "world", "gated", "map", "world.query");
        var operation = Assert.Single(filtered);
        Assert.Contains("dluma.world.find", SnippetService.Create(operation, ModLanguage.Lua));
        Assert.Contains("context.World.Find", SnippetService.Create(operation, ModLanguage.CSharp));
        Assert.Contains("context.world().find", SnippetService.Create(operation, ModLanguage.Native));
    }

    [Theory]
    [InlineData(ModLanguage.Lua, "data/scripts/lua/maps/test-map/author.parity", "main.lua")]
    [InlineData(ModLanguage.CSharp, "mods/csharp/maps/test-map/author.parity", "Mod.cs")]
    [InlineData(ModLanguage.Native, "mods/native/maps/test-map/author.parity", "mod.cpp")]
    public async Task WizardCreatesCanonicalMapPackages(
        ModLanguage language,
        string relativeDirectory,
        string sourceName)
    {
        Directory.CreateDirectory(_temporaryRoot);
        var request = new ModTemplateRequest(
            _repositoryRoot, _temporaryRoot, "author.parity", "Parity", "1.0.0",
            "Parity example", "Author", language, ModScope.Map, "test-map",
            language == ModLanguage.CSharp ? "Parity.Mod" : null,
            0, new[] { "world.query", "events.sensor" }, true);
        var result = await ModProjectService.CreateAsync(request);
        Assert.Equal(
            Path.GetFullPath(Path.Combine(_temporaryRoot, relativeDirectory)),
            Path.GetFullPath(result.Directory));
        Assert.True(File.Exists(Path.Combine(result.Directory, sourceName)));
        if (language == ModLanguage.Native)
        {
            var nativeSource = await File.ReadAllTextAsync(
                Path.Combine(result.Directory, sourceName));
            Assert.Contains("sdk/c/dluma_api.h", nativeSource.Replace('\\', '/'));
        }
        using var manifest = JsonDocument.Parse(await File.ReadAllTextAsync(result.Manifest));
        Assert.Equal("1.0", manifest.RootElement.GetProperty("api_version").GetString());
        Assert.Equal("map", manifest.RootElement.GetProperty("scope").GetString());
        Assert.Equal("test-map", manifest.RootElement.GetProperty("map").GetString());
        Assert.True(File.Exists(Path.Combine(result.Directory, "config.dluma.json")));

        var build = await WorkbenchCommandService.BuildAsync(language, result.Directory);
        Assert.True(build.Succeeded, build.Combined);
        var entry = manifest.RootElement.GetProperty("entry").GetString();
        Assert.False(string.IsNullOrWhiteSpace(entry));
        Assert.True(File.Exists(Path.Combine(result.Directory, entry!)));
        Assert.Empty(Directory.EnumerateFiles(
            result.Directory, "*.obj", SearchOption.AllDirectories));
        Assert.False(Directory.Exists(Path.Combine(result.Directory, "bin")));
        Assert.False(Directory.Exists(Path.Combine(result.Directory, "obj")));

        var locked = await WorkbenchCommandService.LockAsync(
            _repositoryRoot, result.Directory);
        Assert.True(locked.Succeeded, locked.Combined);
        var validated = await WorkbenchCommandService.ValidateAsync(
            _repositoryRoot, result.Directory, true);
        Assert.True(validated.Succeeded, validated.Combined);
        var package = Path.Combine(
            _temporaryRoot, "packages", $"{language.ToString().ToLowerInvariant()}.dlumamod");
        var packaged = await WorkbenchCommandService.PackageAsync(
            _repositoryRoot, result.Directory, package);
        Assert.True(packaged.Succeeded, packaged.Combined);
        Assert.True(File.Exists(package));
    }

    [Fact]
    public void LuaDiagnosticsRejectLegacyNamespace()
    {
        var diagnostics = LuaDiagnosticService.Analyze("local mod = dlua.mod()\nreturn mod\n");
        Assert.Contains(diagnostics, item => item.Severity == "Error" && item.Message.Contains("dluma", StringComparison.Ordinal));
    }

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
