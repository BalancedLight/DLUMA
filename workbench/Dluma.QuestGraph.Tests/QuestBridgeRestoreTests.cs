using System.Security.Cryptography;
using System.Text;
using Dluma.QuestGraph.Protocol;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestBridgeRestoreTests
{
    private static readonly string DocumentId = new('A', 64);

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void FullBaselineRestoreRequestIsAccepted(bool wasModified)
    {
        var bytes = Encoding.UTF8.GetBytes("quest Synthetic\r\n    ,step baseline\r\n");
        var request = new RestoreDocumentRequest(
            DocumentId,
            bytes,
            Convert.ToHexString(SHA256.HashData(bytes)),
            wasModified);

        QuestBridgePayloadValidator.Validate(request);
    }

    [Fact]
    public void ForgedBaselineHashIsRejected()
    {
        var request = new RestoreDocumentRequest(
            DocumentId,
            Encoding.UTF8.GetBytes("quest Synthetic\n"),
            new string('B', 64),
            false);

        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(request));
    }

    [Fact]
    public void OversizedBaselineIsRejected()
    {
        var bytes = new byte[QuestBridgeProtocol.MaximumDocumentBytes + 1];
        var request = new RestoreDocumentRequest(
            DocumentId,
            bytes,
            Convert.ToHexString(SHA256.HashData(bytes)),
            false);

        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(request));
    }

    [Fact]
    public void SuccessfulRestoreRequiresCurrentSourceIdentity()
    {
        var result = new RestoreDocumentResult(
            true,
            4,
            string.Empty,
            "Restored.");

        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(result));
    }
}
