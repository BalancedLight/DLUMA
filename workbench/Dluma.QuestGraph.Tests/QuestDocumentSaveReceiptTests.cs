using Dluma.QuestGraph.Services;
using Dluma.QuestGraph.ViewModels;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentSaveReceiptTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-quest-save-{Guid.NewGuid():N}");

    [Fact]
    public async Task SuccessfulSourceAndSidecarSaveAdmitsCompile()
    {
        var path = await CreateSourceAsync();
        var service = new QuestDocumentService();
        var document = new QuestDocumentViewModel(
            service,
            await service.OpenFileAsync(path, null));
        using var bridge = new QuestBridgeClient();
        using var main = CreateMain(service, bridge);

        var receipt = await main.SaveDocumentAsync(document);

        Assert.True(receipt.SourceSaved);
        Assert.True(receipt.SidecarPublished);
        Assert.False(receipt.ExistingSidecarPreserved);
        Assert.True(receipt.CanCompile);
        Assert.True(File.Exists(QuestDocumentService.SidecarPath(path)));
    }

    [Fact]
    public async Task PreservedInvalidSidecarNeverAdmitsCompileOrChangesItsBytes()
    {
        var path = await CreateSourceAsync();
        var sidecarPath = QuestDocumentService.SidecarPath(path);
        var invalidBytes = "{\"schema\":\"future.sidecar\"}"u8.ToArray();
        await File.WriteAllBytesAsync(sidecarPath, invalidBytes);
        var service = new QuestDocumentService();
        var document = new QuestDocumentViewModel(
            service,
            await service.OpenFileAsync(path, null));
        using var bridge = new QuestBridgeClient();
        using var main = CreateMain(service, bridge);

        var receipt = await main.SaveDocumentAsync(document);

        Assert.True(receipt.SourceSaved);
        Assert.False(receipt.SidecarPublished);
        Assert.True(receipt.ExistingSidecarPreserved);
        Assert.False(receipt.CanCompile);
        Assert.Equal(invalidBytes, await File.ReadAllBytesAsync(sidecarPath));
        Assert.Contains("Compile is blocked", receipt.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task SidecarPublicationFailureNeverAdmitsCompile()
    {
        var path = await CreateSourceAsync();
        var service = new QuestDocumentService();
        var document = new QuestDocumentViewModel(
            service,
            await service.OpenFileAsync(path, null));
        Directory.CreateDirectory(QuestDocumentService.SidecarPath(path));
        using var bridge = new QuestBridgeClient();
        using var main = CreateMain(service, bridge);

        var receipt = await main.SaveDocumentAsync(document);

        Assert.True(receipt.SourceSaved);
        Assert.False(receipt.SidecarPublished);
        Assert.False(receipt.ExistingSidecarPreserved);
        Assert.False(receipt.CanCompile);
        Assert.Contains("could not be updated", receipt.Message, StringComparison.Ordinal);
    }

    private async Task<string> CreateSourceAsync()
    {
        Directory.CreateDirectory(_temporaryRoot);
        var path = Path.Combine(_temporaryRoot, $"quest-{Guid.NewGuid():N}.pxsl");
        await File.WriteAllTextAsync(path, "quest Synthetic\n    ,step first\n");
        return path;
    }

    private static MainViewModel CreateMain(
        QuestDocumentService service,
        QuestBridgeClient bridge) => new(
        service,
        new NullFileDialogs(),
        bridge,
        new LaunchOptions(null, null, null, Array.Empty<string>(), false));

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }

    private sealed class NullFileDialogs : IFileDialogService
    {
        public string? SelectQuestToOpen() => null;
        public string? SelectQuestSavePath(string suggestedName) => null;
        public string? SelectCatalogRoot() => null;
    }
}
