using System.Runtime.CompilerServices;
using Dluma.Managed.Sdk;

namespace Dluma.Managed.Bootstrap;

internal sealed unsafe class NativeBridge
{
    private readonly NativeBridgeV1 _bridge;

    internal NativeBridge(NativeBridgeV1* bridge)
    {
        if (bridge is null ||
            bridge->StructSize < (uint)sizeof(NativeBridgeV1) ||
            bridge->StructVersion != NativeConstants.StructVersion ||
            bridge->AbiVersion != NativeConstants.AbiVersion ||
            bridge->Context is null ||
            bridge->Invoke is null)
        {
            throw new ArgumentException(
                "The native DLUMA managed bridge is malformed.",
                nameof(bridge));
        }

        _bridge = *bridge;
    }

    internal DlumaCallResult Invoke<TRequest, TResult>(
        uint operationId,
        ref TRequest request,
        ref TResult result)
        where TRequest : unmanaged
        where TResult : unmanaged
    {
        if (Unsafe.SizeOf<TResult>() <
            sizeof(uint) * 2 + sizeof(NativeCall))
        {
            throw new ArgumentException(
                "A DLUMA native result must contain the common call prefix.",
                nameof(result));
        }

        byte* code = stackalloc byte[96];
        byte* message = stackalloc byte[384];
        NativeError error = new()
        {
            StructSize = (uint)sizeof(NativeError),
            StructVersion = NativeConstants.StructVersion,
            Code = new NativeStringBuffer
            {
                Data = code,
                Capacity = 96,
            },
            Message = new NativeStringBuffer
            {
                Data = message,
                Capacity = 384,
            },
        };
        var requestPointer =
            (TRequest*)Unsafe.AsPointer(ref request);
        var resultPointer =
            (TResult*)Unsafe.AsPointer(ref result);
        var call = (NativeCall*)((byte*)resultPointer +
            sizeof(uint) * 2);
        call->Error = &error;
        _ = _bridge.Invoke(
            _bridge.Context,
            operationId,
            requestPointer,
            checked((uint)sizeof(TRequest)),
            resultPointer,
            checked((uint)sizeof(TResult)));
        return ConvertCall(call, &error);
    }

    internal static DlumaCallResult Unavailable(
        uint operationId,
        string message = "The requested DLUMA capability is not ready.") =>
        DlumaCallResult.Fail(
            DlumaStatus.Unavailable,
            new DlumaError(
                "unsupported_capability",
                message,
                true,
                operationId));

    private static DlumaCallResult ConvertCall(
        NativeCall* call,
        NativeError* error)
    {
        var status = call->Status <= (uint)DlumaStatus.InternalError
            ? (DlumaStatus)call->Status
            : DlumaStatus.InternalError;
        if (status is DlumaStatus.Ok)
        {
            return DlumaCallResult.Ok();
        }

        var code = ReadBuffer(error->Code);
        var message = ReadBuffer(error->Message);
        DlumaCapabilityId? capability =
            Enum.IsDefined(typeof(DlumaCapabilityId), error->CapabilityId)
                ? (DlumaCapabilityId)error->CapabilityId
                : null;
        return DlumaCallResult.Fail(
            status,
            new DlumaError(
                string.IsNullOrEmpty(code) ? "internal_error" : code,
                string.IsNullOrEmpty(message)
                    ? "The shared DLUMA API operation failed."
                    : message,
                error->Retryable != 0,
                error->OperationId,
                capability,
                AccessName(error->AccessId)));
    }

    private static string ReadBuffer(NativeStringBuffer buffer)
    {
        if (buffer.Data is null || buffer.Capacity == 0)
        {
            return string.Empty;
        }

        var length = 0;
        var maximum = checked((int)Math.Min(buffer.Capacity, int.MaxValue));
        while (length < maximum && buffer.Data[length] != 0)
        {
            ++length;
        }

        return System.Text.Encoding.UTF8.GetString(
            new ReadOnlySpan<byte>(buffer.Data, length));
    }

    private static string? AccessName(uint accessId) => accessId switch
    {
        1 => "game.read",
        2 => "player.read",
        3 => "world.query",
        4 => "world.write",
        5 => "events.sensor",
        6 => "events.interact",
        7 => "events.enemy_killed",
        8 => "ui.overlay",
        9 => "audio.play",
        10 => "quest.write",
        11 => "configuration.read",
        12 => "storage.mod",
        13 => "companion.read",
        _ => null,
    };
}
