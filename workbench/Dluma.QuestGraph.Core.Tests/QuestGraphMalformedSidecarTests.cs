using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphMalformedSidecarTests
{
    [Theory]
    [MemberData(nameof(MalformedSidecars))]
    public async Task NullPayloadMembersAreNormalizedToInvalidData(string json)
    {
        await using var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));

        await Assert.ThrowsAsync<InvalidDataException>(() =>
            QuestGraphSidecarService.ReadAsync(stream));
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
}
