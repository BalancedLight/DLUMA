using System.Runtime.InteropServices;

namespace Dluma.Managed.Bootstrap;

internal static class NativeConstants
{
    internal const uint AbiVersion = 0x0001_0000;
    internal const uint StructVersion = 1;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeStringView
{
    internal byte* Data;
    internal ulong Size;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeStringBuffer
{
    internal byte* Data;
    internal ulong Capacity;
    internal ulong Required;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeByteView
{
    internal byte* Data;
    internal ulong Size;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeHudSurfaceCreateRequest
{
    internal uint StructSize, StructVersion, Width, Height;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeHudSurfaceUpdateRequest
{
    internal uint StructSize, StructVersion;
    internal ulong Token;
    internal uint Stride;
    internal NativeByteView Pixels;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeHudSurfaceLayoutRequest
{
    internal uint StructSize, StructVersion;
    internal ulong Token;
    internal float X, Y, Width, Height;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeHudSurfaceVisibilityRequest
{
    internal uint StructSize, StructVersion;
    internal ulong Token;
    internal uint Visible;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeHudSurfaceSuppressRequest
{
    internal uint StructSize, StructVersion;
    internal ulong Token;
    internal uint Mask;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeByteBuffer
{
    internal byte* Data;
    internal ulong Capacity;
    internal ulong Required;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeError
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint Retryable;
    internal uint OperationId;
    internal uint CapabilityId;
    internal uint AccessId;
    internal NativeStringBuffer Code;
    internal NativeStringBuffer Message;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCall
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint Status;
    internal uint Reserved;
    internal NativeError* Error;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeBridgeV1
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint AbiVersion;
    internal uint Reserved;
    internal void* Context;
    internal delegate* unmanaged[Cdecl]<
        void*, uint, void*, uint, void*, uint, int> Invoke;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeArtifactV1
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint Kind;
    internal uint Reserved;
    internal NativeStringView RelativePath;
    internal byte* Bytes;
    internal ulong Size;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeModLoadV1
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong OwnerId;
    internal uint Scope;
    internal uint MapGeneration;
    internal NativeStringView ModId;
    internal NativeStringView EntryType;
    internal NativeStringView EntryPath;
    internal NativeBridgeV1* Bridge;
    internal NativeArtifactV1* Artifacts;
    internal uint ArtifactCount;
    internal uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeActivationV1
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint Scope;
    internal uint MapGeneration;
    internal NativeStringView Project;
    internal NativeStringView Map;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEventV1
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint Kind;
    internal uint MapGeneration;
    internal ulong CallbackId;
    internal ulong Token;
    internal double Number;
    internal int State;
    internal uint Reserved;
    internal NativeStringView Name;
    internal NativeStringView Project;
    internal NativeStringView Map;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeBootstrapApiV1
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint AbiVersion;
    internal uint Reserved;
    internal delegate* unmanaged[Cdecl]<
        NativeModLoadV1*, ulong*, byte*, uint, int> Load;
    internal delegate* unmanaged[Cdecl]<
        ulong, NativeActivationV1*, byte*, uint, int> Activate;
    internal delegate* unmanaged[Cdecl]<
        ulong, uint, byte*, uint, int> Deactivate;
    internal delegate* unmanaged[Cdecl]<
        ulong, NativeEventV1*, byte*, uint, int> Dispatch;
    internal delegate* unmanaged[Cdecl]<
        ulong, uint, byte*, uint, int> Shutdown;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeObjectHandle
{
    internal ulong OwnerId;
    internal ulong HandleId;
    internal uint Generation;
    internal uint Kind;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeVec3
{
    internal float X;
    internal float Y;
    internal float Z;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeScalar
{
    internal uint Kind;
    internal uint Reserved;
    internal long IntegerValue;
    internal double NumberValue;
    internal NativeStringView StringValue;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeNoRequest
{
    internal uint StructSize;
    internal uint StructVersion;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeLogRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Level;
    internal NativeStringView Message;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCapabilityQueryRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint CapabilityId;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCapabilityQueryResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal uint Configured;
    internal uint Ready;
    internal NativeStringBuffer Status;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSimpleResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeBoolResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal uint Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeU64Result
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal ulong Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeFloatResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal double Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeVec3Result
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeVec3 Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeWorldFindRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Name;
    internal NativeStringView ExpectedClass;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeObjectResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeObjectHandle Handle;
    internal NativeStringBuffer Name;
    internal NativeStringBuffer ClassName;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeObjectRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeObjectHandle Handle;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePlayerVariableRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeObjectHandle Player;
    internal NativeStringView Name;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePlayerVariableSetRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeObjectHandle Player;
    internal NativeStringView Name;
    internal double Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePlayerVariableResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeStringBuffer Name;
    internal double Value;
    internal double Minimum;
    internal double Maximum;
    internal uint Overridden;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeStringListResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeStringBuffer* Items;
    internal uint Capacity;
    internal uint Count;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSetEnabledManyRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView* Names;
    internal uint NameCount;
    internal uint Enabled;
    internal uint Atomic;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSetEnabledManyResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal uint RequestedCount;
    internal uint ChangedCount;
    internal uint UnchangedCount;
    internal uint Partial;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeNamedRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Name;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEventSubscribeRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Name;
    internal ulong CallbackId;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeTokenResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal ulong Token;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSubtitleRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Text;
    internal double DurationSeconds;
    internal uint Force;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSubtitleUpdateRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Text;
    internal uint Fade;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeRadioDialogRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Speaker;
    internal NativeStringView Group;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeQuestHudRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringBuffer Json;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeQuestHudResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeStringBuffer Json;
    internal ulong Revision;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeQuestHudOverrideRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView TargetId;
    internal NativeStringView Text;
    internal ulong Token;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeQuestTransitionRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView QuestId;
    internal NativeStringView PhaseId;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeTimerRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal double Seconds;
    internal ulong CallbackId;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTokenRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong Token;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeConfigGetRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Key;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeScalarResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeScalar Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeStorageGetRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Key;
    internal NativeScalar Fallback;
    internal uint HasFallback;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeStorageSetRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView Key;
    internal NativeScalar Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCompanionOpenRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeStringView RelativePath;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCompanionOpenResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal ulong Token;
    internal ulong Size;
    internal NativeStringBuffer Sha256;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeVehicleStateResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal uint PlayerPresent;
    internal uint VehiclePresent;
    internal uint IsLocalDriver;
    internal NativeStringBuffer VehicleType;
    internal NativeStringBuffer ClassName;
    internal NativeVec3 LinearVelocity;
    internal double SpeedMps;
    internal double SpeedMph;
    internal ulong LevelEpoch;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCompanionEntryReadRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong Token;
    internal NativeStringView EntryPath;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeCompanionEntryReadResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal NativeByteBuffer Bytes;
    internal NativeStringBuffer Sha256;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAudioBankOpenRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong CompanionToken;
    internal NativeStringView BankId;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAudioCueRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong BankToken;
    internal NativeStringView Cue;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAudioCuePlayRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong BankToken;
    internal NativeStringView Cue;
    internal ulong StartMs;
    internal float InitialGain;
    internal uint Loop;
    internal uint FadeInMs;
    internal uint FadeOutMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAudioCueStateResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal uint Playing;
    internal ulong PositionMs;
    internal ulong DurationMs;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioCueSeekRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong CueToken;
    internal ulong PositionMs;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioCueGainRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong CueToken;
    internal float Gain;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioFilterRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong Token;
    internal float WetMix;
    internal float CutoffHz;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioCueStopRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong CueToken;
    internal uint FadeOutMs;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioMusicAcquireRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint FadeOutMs;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAudioMusicStateResult
{
    internal uint StructSize;
    internal uint StructVersion;
    internal NativeCall Call;
    internal uint Phase;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioMusicReleaseRequest
{
    internal uint StructSize;
    internal uint StructVersion;
    internal ulong Token;
    internal uint ResumeDelayMs;
}

internal static unsafe class NativeUtf8
{
    internal static string Read(NativeStringView value)
    {
        if (value.Data is null || value.Size == 0)
        {
            return string.Empty;
        }

        if (value.Size > int.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }

        return System.Text.Encoding.UTF8.GetString(
            new ReadOnlySpan<byte>(value.Data, checked((int)value.Size)));
    }

    internal static void WriteDiagnostic(
        byte* destination,
        uint capacity,
        string value)
    {
        if (destination is null || capacity == 0)
        {
            return;
        }

        var bytes = System.Text.Encoding.UTF8.GetBytes(value);
        var count = Math.Min(bytes.Length, checked((int)capacity - 1));
        bytes.AsSpan(0, count).CopyTo(
            new Span<byte>(destination, checked((int)capacity)));
        destination[count] = 0;
    }
}

internal sealed unsafe class Utf8Lease : IDisposable
{
    private byte* _bytes;

    internal Utf8Lease(string? value)
    {
        var encoded = System.Text.Encoding.UTF8.GetBytes(value ?? string.Empty);
        _bytes = (byte*)NativeMemory.Alloc((nuint)Math.Max(1, encoded.Length));
        encoded.CopyTo(new Span<byte>(_bytes, encoded.Length));
        View = new NativeStringView
        {
            Data = _bytes,
            Size = checked((ulong)encoded.Length),
        };
    }

    internal NativeStringView View { get; }

    public void Dispose()
    {
        if (_bytes is not null)
        {
            NativeMemory.Free(_bytes);
            _bytes = null;
        }
    }
}
