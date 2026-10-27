using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Dluma.QuestGraph.Protocol;

namespace Dluma.QuestGraph.Services;

public sealed partial class QuestBridgeClient : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = false,
        MaxDepth = 32,
        UnmappedMemberHandling = System.Text.Json.Serialization.JsonUnmappedMemberHandling.Disallow
    };

    private readonly ConcurrentDictionary<string, TaskCompletionSource<QuestBridgeEnvelope>> _pending = new();
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private readonly CancellationTokenSource _lifetime = new();
    private NamedPipeClientStream? _pipe;
    private Task? _reader;
    private string? _sessionToken;
    private bool _disposed;

    public bool IsConnected => _pipe?.IsConnected == true && !_lifetime.IsCancellationRequested;

    public event EventHandler<QuestBridgeEnvelope>? NotificationReceived;
    public event EventHandler<string>? Disconnected;

    public void AbortProtocolSession(string reason)
    {
        if (_disposed)
            return;
        var exception = new InvalidDataException(reason);
        foreach (var completion in _pending.Values)
            completion.TrySetException(exception);
        _lifetime.Cancel();
        _pipe?.Dispose();
    }

    public async Task ConnectAsync(
        string pipeName,
        string sessionToken,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (string.IsNullOrEmpty(pipeName) || !PipeNamePattern().IsMatch(pipeName))
            throw new ArgumentException("The bridge pipe name is invalid.", nameof(pipeName));
        if (string.IsNullOrEmpty(sessionToken) ||
            sessionToken.Length is < 32 or > 512 ||
            !TokenPattern().IsMatch(sessionToken))
            throw new ArgumentException("The bridge session token is invalid.", nameof(sessionToken));
        if (timeout <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(timeout));
        if (_pipe is not null)
            throw new InvalidOperationException("The bridge client is already initialized.");

        _sessionToken = sessionToken;
        _pipe = new NamedPipeClientStream(
            ".",
            pipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            _lifetime.Token);
        linked.CancelAfter(timeout);
        await _pipe.ConnectAsync(linked.Token).ConfigureAwait(false);
        _pipe.ReadMode = PipeTransmissionMode.Byte;
        _reader = ReadLoopAsync(_lifetime.Token);
    }

    public async Task<TResponse> RequestAsync<TRequest, TResponse>(
        string type,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!IsConnected || _pipe is null || _sessionToken is null)
            throw new InvalidOperationException("The Developer Tools bridge is not connected.");
        if (!MessageTypePattern().IsMatch(type))
            throw new ArgumentException("The bridge message type is invalid.", nameof(type));

        var requestId = Guid.NewGuid().ToString("N");
        var completion = new TaskCompletionSource<QuestBridgeEnvelope>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_pending.TryAdd(requestId, completion))
            throw new InvalidOperationException("A duplicate bridge request ID was generated.");

        try
        {
            QuestBridgePayloadValidator.Validate(request);
            var payload = JsonSerializer.SerializeToElement(request, JsonOptions);
            var envelope = new QuestBridgeEnvelope(
                QuestBridgeProtocol.SchemaVersion,
                type,
                requestId,
                _sessionToken,
                payload);
            await WriteEnvelopeAsync(envelope, cancellationToken).ConfigureAwait(false);
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                _lifetime.Token);
            linked.CancelAfter(timeout);
            var response = await completion.Task.WaitAsync(linked.Token).ConfigureAwait(false);
            if (string.Equals(response.Type, "error", StringComparison.Ordinal))
            {
                var error = DeserializePayload<QuestBridgeError>(response);
                throw new QuestBridgeException(error.Code, error.Message, error.Retryable);
            }
            if (!string.Equals(response.Type, "result", StringComparison.Ordinal))
                throw new InvalidDataException($"Unexpected bridge response type '{response.Type}'.");
            return DeserializePayload<TResponse>(response);
        }
        finally
        {
            _pending.TryRemove(requestId, out _);
        }
    }

    public Task<HelloResult> HelloAsync(
        int editorProcessId,
        string executableSha256,
        CancellationToken cancellationToken = default) =>
        RequestAsync<HelloRequest, HelloResult>(
            "hello",
            new HelloRequest(
                QuestBridgeProtocol.SchemaVersion,
                Environment.ProcessId,
                editorProcessId,
                executableSha256),
            TimeSpan.FromSeconds(5),
            cancellationToken);

    public Task<SaveDocumentResult> SaveDocumentAsync(
        SaveDocumentRequest request,
        CancellationToken cancellationToken = default) =>
        RequestAsync<SaveDocumentRequest, SaveDocumentResult>(
            "saveDocument",
            request,
            TimeSpan.FromSeconds(15),
            cancellationToken);

    public Task<CompileDocumentResult> CompileDocumentAsync(
        CompileDocumentRequest request,
        CancellationToken cancellationToken = default) =>
        RequestAsync<CompileDocumentRequest, CompileDocumentResult>(
            "compileDocument",
            request,
            TimeSpan.FromMinutes(2),
            cancellationToken);

    public Task<ApplyEditsResult> ApplyEditsAsync(
        ApplyEditsRequest request,
        CancellationToken cancellationToken = default) =>
        RequestAsync<ApplyEditsRequest, ApplyEditsResult>(
            "applyEdits",
            request,
            TimeSpan.FromSeconds(15),
            cancellationToken);

    public Task<ContextSnapshotMessage> GetContextAsync(
        long? expectedGeneration,
        CancellationToken cancellationToken = default) =>
        RequestAsync<GetContextRequest, ContextSnapshotMessage>(
            "getContext",
            new GetContextRequest(expectedGeneration),
            TimeSpan.FromSeconds(15),
            cancellationToken);

    public Task<QuestBridgeResult> ActivateDocumentAsync(
        string documentId,
        int? byteOffset,
        CancellationToken cancellationToken = default) =>
        RequestAsync<ActivateDocumentRequest, QuestBridgeResult>(
            "activateDocument",
            new ActivateDocumentRequest(documentId, byteOffset),
            TimeSpan.FromSeconds(5),
            cancellationToken);

    public Task<QuestBridgeResult> CloseDocumentAsync(
        string documentId,
        CancellationToken cancellationToken = default) =>
        RequestAsync<CloseDocumentRequest, QuestBridgeResult>(
            "closeDocument",
            new CloseDocumentRequest(documentId),
            TimeSpan.FromSeconds(5),
            cancellationToken);

    public static T DeserializePayload<T>(QuestBridgeEnvelope envelope)
    {
        var materialized = envelope.Payload.Deserialize<T>(JsonOptions) ??
            throw new InvalidDataException(
                $"Bridge message '{envelope.Type}' has an empty payload.");
        QuestBridgePayloadValidator.Validate(materialized);
        return materialized;
    }

    private async Task WriteEnvelopeAsync(
        QuestBridgeEnvelope envelope,
        CancellationToken cancellationToken)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(envelope, JsonOptions);
        if (bytes.Length == 0 || bytes.Length > QuestBridgeProtocol.MaximumFrameBytes)
            throw new InvalidDataException("The bridge message exceeds the 16 MiB frame cap.");
        var header = new byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32LittleEndian(header, checked((uint)bytes.Length));

        await _writeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var pipe = _pipe ?? throw new InvalidOperationException("The bridge pipe is unavailable.");
            await pipe.WriteAsync(header, cancellationToken).ConfigureAwait(false);
            await pipe.WriteAsync(bytes, cancellationToken).ConfigureAwait(false);
            await pipe.FlushAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _writeGate.Release();
        }
    }

    private async Task ReadLoopAsync(CancellationToken cancellationToken)
    {
        var disconnectReason = "Developer Tools disconnected.";
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var header = new byte[sizeof(uint)];
                await ReadExactlyAsync(header, cancellationToken).ConfigureAwait(false);
                var length = BinaryPrimitives.ReadUInt32LittleEndian(header);
                ValidateFrameLength(length);
                var payload = new byte[checked((int)length)];
                await ReadExactlyAsync(payload, cancellationToken).ConfigureAwait(false);
                ValidateUtf8(payload);
                var envelope = JsonSerializer.Deserialize<QuestBridgeEnvelope>(payload, JsonOptions) ??
                    throw new InvalidDataException("The bridge sent an empty envelope.");
                ValidateEnvelopeForSession(envelope, _sessionToken);

                if (string.Equals(envelope.Type, "result", StringComparison.Ordinal) ||
                    string.Equals(envelope.Type, "error", StringComparison.Ordinal))
                {
                    if (!_pending.TryGetValue(envelope.RequestId, out var completion))
                        throw new InvalidDataException("The bridge returned an unknown request ID.");
                    completion.TrySetResult(envelope);
                    continue;
                }
                NotificationReceived?.Invoke(this, envelope);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            disconnectReason = "Developer Tools bridge stopped.";
        }
        catch (Exception exception)
        {
            disconnectReason = exception.Message;
            foreach (var completion in _pending.Values)
                completion.TrySetException(exception);
        }
        finally
        {
            Disconnected?.Invoke(this, disconnectReason);
        }
    }

    private async Task ReadExactlyAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        var pipe = _pipe ?? throw new EndOfStreamException("The bridge pipe is unavailable.");
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = await pipe.ReadAsync(buffer[offset..], cancellationToken).ConfigureAwait(false);
            if (read == 0)
                throw new EndOfStreamException("The Developer Tools bridge closed the pipe.");
            offset += read;
        }
    }

    internal static void ValidateEnvelopeForSession(
        QuestBridgeEnvelope envelope,
        string? sessionToken)
    {
        ArgumentNullException.ThrowIfNull(envelope);
        if (envelope.SchemaVersion != QuestBridgeProtocol.SchemaVersion)
            throw new InvalidDataException("The bridge protocol version does not match this companion.");
        if (string.IsNullOrEmpty(envelope.Type) ||
            !MessageTypePattern().IsMatch(envelope.Type))
            throw new InvalidDataException("The bridge sent an invalid message type.");
        if (envelope.Type is not (
                "result" or
                "error" or
                "openDocument" or
                "sourceChanged" or
                "activateDocument" or
                "contextSnapshot" or
                "disconnected"))
            throw new InvalidDataException("The bridge sent an unsupported message type.");
        if (string.IsNullOrEmpty(envelope.RequestId) ||
            !RequestIdPattern().IsMatch(envelope.RequestId))
            throw new InvalidDataException("The bridge sent an invalid request ID.");
        if (envelope.Payload.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("The bridge payload must be a JSON object.");
        if (string.IsNullOrEmpty(sessionToken) ||
            string.IsNullOrEmpty(envelope.SessionToken) ||
            envelope.SessionToken.Length is < 32 or > 512 ||
            !TokenPattern().IsMatch(envelope.SessionToken) ||
            !FixedTimeEquals(envelope.SessionToken, sessionToken))
            throw new InvalidDataException("The bridge session token did not match this launch.");
    }

    internal static void ValidateFrameLength(uint length)
    {
        if (length == 0 || length > QuestBridgeProtocol.MaximumFrameBytes)
            throw new InvalidDataException("The bridge sent an invalid frame length.");
    }

    internal static void ValidateUtf8(ReadOnlySpan<byte> bytes)
    {
        _ = new UTF8Encoding(false, true).GetCharCount(bytes);
    }

    private static bool FixedTimeEquals(string left, string right)
    {
        var leftBytes = Encoding.UTF8.GetBytes(left);
        var rightBytes = Encoding.UTF8.GetBytes(right);
        return leftBytes.Length == rightBytes.Length &&
            CryptographicOperations.FixedTimeEquals(leftBytes, rightBytes);
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        _lifetime.Cancel();
        _pipe?.Dispose();
        _writeGate.Dispose();
        _lifetime.Dispose();
    }

    [GeneratedRegex("^[A-Za-z0-9._-]{1,128}$", RegexOptions.CultureInvariant)]
    private static partial Regex PipeNamePattern();

    [GeneratedRegex("^[A-Za-z0-9+/=_-]{32,512}$", RegexOptions.CultureInvariant)]
    private static partial Regex TokenPattern();

    [GeneratedRegex("^[a-z][A-Za-z0-9]{0,63}$", RegexOptions.CultureInvariant)]
    private static partial Regex MessageTypePattern();

    [GeneratedRegex("^[a-f0-9]{32}$", RegexOptions.CultureInvariant)]
    private static partial Regex RequestIdPattern();
}
