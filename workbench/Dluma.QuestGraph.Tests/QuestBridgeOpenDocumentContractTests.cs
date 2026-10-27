using System.Security.Cryptography;
using System.Text.Json;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestBridgeOpenDocumentContractTests
{
    [Fact]
    public void ProtocolV2RequiresExplicitOpeningModifiedState()
    {
        var bytes = Array.Empty<byte>();
        var payload = JsonSerializer.SerializeToElement(new
        {
            documentId = new string('A', 64),
            projectRoot = Path.GetPathRoot(Environment.SystemDirectory)!,
            relativePath = "quests/synthetic.pxsl",
            revision = 0,
            encodingName = "utf-8",
            bytes,
            sha256 = Convert.ToHexString(SHA256.HashData(bytes))
        });
        var envelope = new QuestBridgeEnvelope(
            QuestBridgeProtocol.SchemaVersion,
            "openDocument",
            new string('B', 32),
            new string('C', 32),
            payload);

        Assert.Throws<JsonException>(() =>
            QuestBridgeClient.DeserializePayload<OpenDocumentWireMessage>(envelope));
    }
}
