using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Protocol;
using Dluma.QuestGraph.Services;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class QuestBridgeProtocolSecurityTests
{
    private const string SessionToken = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    private const string RequestId = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    private static readonly string DocumentId = new('C', 64);
    private static readonly string Hash = new('D', 64);

    [Fact]
    public void FrameLengthBoundaryFailsClosed()
    {
        QuestBridgeClient.ValidateFrameLength(1);
        QuestBridgeClient.ValidateFrameLength(
            QuestBridgeProtocol.MaximumFrameBytes);

        Assert.Throws<InvalidDataException>(
            () => QuestBridgeClient.ValidateFrameLength(0));
        Assert.Throws<InvalidDataException>(
            () => QuestBridgeClient.ValidateFrameLength(
                QuestBridgeProtocol.MaximumFrameBytes + 1U));
    }

    [Fact]
    public void InvalidUtf8FailsClosed()
    {
        QuestBridgeClient.ValidateUtf8("valid"u8);

        Assert.Throws<DecoderFallbackException>(
            () => QuestBridgeClient.ValidateUtf8([0xC3, 0x28]));
    }

    [Fact]
    public void ValidServerEnvelopeIsAccepted()
    {
        QuestBridgeClient.ValidateEnvelopeForSession(
            Envelope("result", SessionToken, RequestId, new { success = true }),
            SessionToken);
    }

    [Theory]
    [InlineData(1, "result", RequestId, SessionToken, true)]
    [InlineData(2, "unknown", RequestId, SessionToken, true)]
    [InlineData(2, "Result", RequestId, SessionToken, true)]
    [InlineData(2, "result", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", SessionToken, true)]
    [InlineData(2, "result", RequestId, "cccccccccccccccccccccccccccccccc", true)]
    [InlineData(2, "result", RequestId, SessionToken, false)]
    public void HostileEnvelopeIsRejected(
        int schemaVersion,
        string type,
        string requestId,
        string token,
        bool objectPayload)
    {
        var payload = objectPayload
            ? JsonSerializer.SerializeToElement(new { success = true })
            : JsonSerializer.SerializeToElement("not-an-object");
        var envelope = new QuestBridgeEnvelope(
            schemaVersion,
            type,
            requestId,
            token,
            payload);

        Assert.Throws<InvalidDataException>(
            () => QuestBridgeClient.ValidateEnvelopeForSession(
                envelope,
                SessionToken));
    }

    [Fact]
    public void ForgedDocumentHashAndTraversalAreRejected()
    {
        var bytes = Encoding.UTF8.GetBytes("quest synthetic");
        var forged = new SourceChangedMessage(
            DocumentId,
            1,
            "utf-8",
            bytes,
            Hash);
        var actualHash = Convert.ToHexString(SHA256.HashData(bytes));
        var traversal = new OpenDocumentWireMessage(
            DocumentId,
            Path.GetPathRoot(Environment.SystemDirectory)!,
            "../outside.pxsl",
            1,
            "utf-8",
            bytes,
            actualHash,
            false);

        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(forged));
        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(traversal));
    }

    [Fact]
    public void OverlappingOrAscendingEditsAreRejected()
    {
        var request = new ApplyEditsRequest(
            DocumentId,
            1,
            Hash,
            Hash,
            [
                new QuestBridgeTextEdit(5, 1, [0x41]),
                new QuestBridgeTextEdit(5, 1, [0x42])
            ]);

        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(request));
    }

    [Fact]
    public void UnsupportedPayloadTypesAreRejected()
    {
        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(
                new UnsupportedPayload("synthetic")));
    }

    [Fact]
    public void EmptyLocalizedTextValueIsAccepted()
    {
        var context = new QuestContextSnapshot(
            0,
            "synthetic-project",
            string.Empty,
            Array.Empty<QuestContextObject>(),
            [new QuestContextText("TXT_EMPTY", string.Empty)],
            Array.Empty<string>(),
            Array.Empty<string>(),
            Array.Empty<string>());

        QuestBridgePayloadValidator.Validate(new ContextSnapshotMessage(context));
    }

    [Fact]
    public void OversizedLocalizedTextValueIsRejected()
    {
        var context = new QuestContextSnapshot(
            0,
            "synthetic-project",
            string.Empty,
            Array.Empty<QuestContextObject>(),
            [new QuestContextText(
                "TXT_TOO_LARGE",
                new string('X', QuestBridgeProtocol.MaximumStringCharacters + 1))],
            Array.Empty<string>(),
            Array.Empty<string>(),
            Array.Empty<string>());

        Assert.Throws<InvalidDataException>(
            () => QuestBridgePayloadValidator.Validate(
                new ContextSnapshotMessage(context)));
    }

    [Fact]
    public void UnknownPayloadMembersAreRejected()
    {
        var payload = JsonSerializer.SerializeToElement(new
        {
            protocolVersion = QuestBridgeProtocol.SchemaVersion,
            editorProcessId = 42,
            hostBuildId = "synthetic",
            projectRoot = (string?)null,
            installedCatalogRoots = Array.Empty<string>(),
            contextGeneration = 0,
            unrecognized = true
        });
        var envelope = new QuestBridgeEnvelope(
            QuestBridgeProtocol.SchemaVersion,
            "result",
            RequestId,
            SessionToken,
            payload);

        Assert.Throws<JsonException>(
            () => QuestBridgeClient.DeserializePayload<HelloResult>(envelope));
    }

    private static QuestBridgeEnvelope Envelope(
        string type,
        string token,
        string requestId,
        object payload) =>
        new(
            QuestBridgeProtocol.SchemaVersion,
            type,
            requestId,
            token,
            JsonSerializer.SerializeToElement(payload));

    private sealed record UnsupportedPayload(string Value);
}
