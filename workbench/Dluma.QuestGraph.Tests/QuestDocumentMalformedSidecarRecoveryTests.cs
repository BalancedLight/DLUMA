using System.Text;
using Dluma.QuestGraph.Services;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestDocumentMalformedSidecarRecoveryTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-sidecar-recovery-{Guid.NewGuid():N}");

    [Theory]
    [MemberData(nameof(MalformedSidecars))]
    public async Task PxslStillOpensAndMalformedSidecarIsPreserved(string json)
    {
        Directory.CreateDirectory(_temporaryRoot);
        var sourcePath = Path.Combine(_temporaryRoot, $"quest-{Guid.NewGuid():N}.pxsl");
        await File.WriteAllTextAsync(
            sourcePath,
            "quest Synthetic\n    ,step first\n",
            new UTF8Encoding(false));
        var sidecarPath = QuestDocumentService.SidecarPath(sourcePath);
        var invalidBytes = Encoding.UTF8.GetBytes(json);
        await File.WriteAllBytesAsync(sidecarPath, invalidBytes);

        var opened = await new QuestDocumentService().OpenFileAsync(sourcePath, null);

        Assert.NotNull(opened.Document);
        Assert.NotEmpty(opened.Document.SemanticNodes);
        Assert.Null(opened.Sidecar);
        Assert.True(opened.PreserveExistingSidecar);
        var warning = Assert.IsType<string>(opened.SidecarWarning);
        Assert.Contains("will be preserved", warning, StringComparison.Ordinal);
        Assert.True(File.Exists(sidecarPath));
        Assert.Equal(invalidBytes, await File.ReadAllBytesAsync(sidecarPath));
    }

    public static TheoryData<string> MalformedSidecars => new()
    {
        Sidecar("null", "[]"),
        Sidecar(
            "{\"relativePath\":\"synthetic.pxsl\",\"sha256\":null,\"length\":1}",
            "[]"),
        Sidecar(
            $"{{\"relativePath\":\"synthetic.pxsl\",\"sha256\":\"{new string('a', 64)}\",\"length\":1}}",
            "[{\"stableId\":\"stable-node\",\"anchor\":null,\"x\":0,\"y\":0,\"width\":260,\"height\":140,\"isCollapsed\":false,\"groupId\":null,\"note\":null}]")
    };

    private static string Sidecar(string source, string nodes) =>
        $"{{\"schema\":\"dluma.quest_graph.v1\",\"documentId\":\"synthetic-document\"," +
        $"\"source\":{source},\"viewport\":{{\"x\":0,\"y\":0,\"zoom\":1}}," +
        $"\"nodes\":{nodes},\"groups\":[]}}";

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
