using Dluma.Managed.Sdk;

namespace Dluma.Managed.Bootstrap;

internal sealed class NativeModContext : IModContext
{
    private bool _active;

    internal NativeModContext(string modId, NativeBridge bridge)
    {
        ModId = modId;
        Bridge = bridge;
        Core = new CoreApi(this);
        Log = new ModLogger(Core);
        World = new WorldApi(this);
        Player = new PlayerApi(this);
        Events = new EventsApi(this);
        Ui = new UiApi(this);
        UiSurface = new UiSurfaceApi(this);
        Audio = new AudioApi(this);
        Quests = new QuestsApi(this);
        Actions = new ActionsApi(this);
        Configuration = new ConfigurationApi(this);
        Storage = new StorageApi(this);
        CompanionContent = new CompanionContentApi(this);
    }

    public string ModId { get; }

    public string ApiVersion => DlumaApiCatalog.Version;

    public IDlumaLogger Log { get; }

    public ICoreApi Core { get; }

    public IWorldApi World { get; }

    public IPlayerApi Player { get; }

    public IEventsApi Events { get; }

    public IUiApi Ui { get; }

    public IUiSurfaceApi UiSurface { get; }

    public IAudioApi Audio { get; }

    public IQuestsApi Quests { get; }

    public IActionsApi Actions { get; }

    public IConfigurationApi Configuration { get; }

    public IStorageApi Storage { get; }

    public ICompanionContentApi CompanionContent { get; }

    internal NativeBridge Bridge { get; }

    internal void Activate() => _active = true;

    internal void Revoke() => _active = false;

    internal void Dispatch(NativeEventV1 value)
    {
        ((CoreApi)Core).Dispatch(value);
        ((EventsApi)Events).Dispatch(value);
        ((ConfigurationApi)Configuration).Dispatch(value);
    }

    internal DlumaCallResult RequireActive(uint operationId) =>
        _active
            ? DlumaCallResult.Ok()
            : DlumaCallResult.Fail(
                DlumaStatus.OwnerRevoked,
                new DlumaError(
                    "owner_revoked",
                    "The managed mod scope is no longer active.",
                    false,
                    operationId));
}

internal sealed class ModLogger(ICoreApi core) : IDlumaLogger
{
    public DlumaCallResult Trace(string message) => Write("TRACE", message);

    public DlumaCallResult Debug(string message) => Write("DEBUG", message);

    public DlumaCallResult Info(string message) => Write("INFO", message);

    public DlumaCallResult Warn(string message) => Write("WARN", message);

    public DlumaCallResult Error(string message) => Write("ERROR", message);

    private DlumaCallResult Write(string level, string message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return core.Log(new DlumaLogRequest(level, message));
    }
}

internal static class NativeConversions
{
    internal static NativeObjectHandle ToNative(DlumaObjectHandle value) => new()
    {
        OwnerId = value.OwnerId,
        HandleId = value.HandleId,
        Generation = value.Generation,
        Kind = value.Kind,
    };

    internal static DlumaObjectHandle ToManaged(NativeObjectHandle value) =>
        new(
            value.OwnerId,
            value.HandleId,
            value.Generation,
            value.Kind);

    internal static DlumaCallResult InactiveOr(
        NativeModContext context,
        uint operationId,
        Func<DlumaCallResult> invoke)
    {
        var active = context.RequireActive(operationId);
        return active.Succeeded ? invoke() : active;
    }

    internal static DlumaCallResult ActiveUnavailable(
        NativeModContext context,
        uint operationId)
    {
        var active = context.RequireActive(operationId);
        return active.Succeeded
            ? NativeBridge.Unavailable(operationId)
            : active;
    }
}

internal sealed unsafe class CoreApi(NativeModContext context) : ICoreApi
{
    public event EventHandler<DlumaTimerEventArgs>? TimerFired;

    public DlumaCallResult Log(DlumaLogRequest request)
    {
        ArgumentNullException.ThrowIfNull(request.Level);
        ArgumentNullException.ThrowIfNull(request.Message);
        return NativeConversions.InactiveOr(context, 1001, () =>
        {
            using var level = new Utf8Lease(request.Level);
            using var message = new Utf8Lease(request.Message);
            var nativeRequest = new NativeLogRequest
            {
                StructSize = (uint)sizeof(NativeLogRequest),
                StructVersion = NativeConstants.StructVersion,
                Level = level.View,
                Message = message.View,
            };
            var nativeResult = new NativeSimpleResult
            {
                StructSize = (uint)sizeof(NativeSimpleResult),
                StructVersion = NativeConstants.StructVersion,
            };
            return context.Bridge.Invoke(
                1001, ref nativeRequest, ref nativeResult);
        });
    }

    public DlumaCapabilityQueryResult CapabilityQuery(
        DlumaCapabilityQueryRequest request)
    {
        var active = context.RequireActive(1002);
        if (!active.Succeeded)
        {
            return new(active, false, false, string.Empty);
        }

        byte* statusText = stackalloc byte[256];
        var nativeRequest = new NativeCapabilityQueryRequest
        {
            StructSize = (uint)sizeof(NativeCapabilityQueryRequest),
            StructVersion = NativeConstants.StructVersion,
            CapabilityId = request.CapabilityId,
        };
        var nativeResult = new NativeCapabilityQueryResult
        {
            StructSize = (uint)sizeof(NativeCapabilityQueryResult),
            StructVersion = NativeConstants.StructVersion,
            Status = new NativeStringBuffer
            {
                Data = statusText,
                Capacity = 256,
            },
        };
        var call = context.Bridge.Invoke(
            1002, ref nativeRequest, ref nativeResult);
        var length = 0;
        while (length < 256 && statusText[length] != 0)
        {
            ++length;
        }

        return new(
            call,
            nativeResult.Configured != 0,
            nativeResult.Ready != 0,
            System.Text.Encoding.UTF8.GetString(
                new ReadOnlySpan<byte>(statusText, length)));
    }

    public DlumaU64Result ClockMonotonicMs(DlumaNoRequest request)
    {
        var active = context.RequireActive(1003);
        if (!active.Succeeded)
        {
            return new(active, 0);
        }

        var nativeRequest = new NativeNoRequest
        {
            StructSize = (uint)sizeof(NativeNoRequest),
            StructVersion = NativeConstants.StructVersion,
        };
        var nativeResult = new NativeU64Result
        {
            StructSize = (uint)sizeof(NativeU64Result),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            1003, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value);
    }

    public DlumaTokenResult TimerAfter(DlumaTimerRequest request) =>
        Timer(1004, request);

    public DlumaTokenResult TimerEvery(DlumaTimerRequest request) =>
        Timer(1005, request);

    public DlumaBoolResult TokenCancel(DlumaTokenRequest request)
    {
        var active = context.RequireActive(1006);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        var nativeRequest = new NativeTokenRequest
        {
            StructSize = (uint)sizeof(NativeTokenRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            1006, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    private DlumaTokenResult Timer(
        uint operationId,
        DlumaTimerRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, 0);
        }

        var nativeRequest = new NativeTimerRequest
        {
            StructSize = (uint)sizeof(NativeTimerRequest),
            StructVersion = NativeConstants.StructVersion,
            Seconds = request.Seconds,
            CallbackId = request.CallbackId,
        };
        var nativeResult = new NativeTokenResult
        {
            StructSize = (uint)sizeof(NativeTokenResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Token);
    }

    internal void Dispatch(NativeEventV1 value)
    {
        if (value.Kind != 6)
        {
            return;
        }

        TimerFired?.Invoke(
            context,
            new DlumaTimerEventArgs(
                value.CallbackId,
                value.Token,
                value.Number));
    }
}

internal sealed unsafe class WorldApi(NativeModContext context) : IWorldApi
{
    public DlumaObjectResult Find(DlumaWorldFindRequest request)
    {
        var active = context.RequireActive(2001);
        if (!active.Succeeded)
        {
            return new(active, default, string.Empty, string.Empty);
        }

        using var name = new Utf8Lease(request.Name);
        using var expectedClass = new Utf8Lease(request.ExpectedClass);
        byte* resultName = stackalloc byte[97];
        byte* resultClass = stackalloc byte[97];
        var nativeRequest = new NativeWorldFindRequest
        {
            StructSize = (uint)sizeof(NativeWorldFindRequest),
            StructVersion = NativeConstants.StructVersion,
            Name = name.View,
            ExpectedClass = expectedClass.View,
        };
        var nativeResult = new NativeObjectResult
        {
            StructSize = (uint)sizeof(NativeObjectResult),
            StructVersion = NativeConstants.StructVersion,
            Name = new NativeStringBuffer
            {
                Data = resultName,
                Capacity = 97,
            },
            ClassName = new NativeStringBuffer
            {
                Data = resultClass,
                Capacity = 97,
            },
        };
        var call = context.Bridge.Invoke(
            2001, ref nativeRequest, ref nativeResult);
        return new(
            call,
            NativeConversions.ToManaged(nativeResult.Handle),
            Read(resultName, 97),
            Read(resultClass, 97));
    }

    public DlumaVec3Result Position(DlumaObjectRequest request) =>
        Vector(2002, request);

    public DlumaVec3Result ForwardDirection(DlumaObjectRequest request) =>
        Vector(2003, request);

    public DlumaBoolResult LocalEnabled(DlumaObjectRequest request)
    {
        var active = context.RequireActive(2004);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        var nativeRequest = ObjectRequest(request);
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            2004, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaSetEnabledManyResult SetEnabledMany(
        DlumaSetEnabledManyRequest request)
    {
        var active = context.RequireActive(2005);
        if (!active.Succeeded)
        {
            return new(active, 0, 0, 0, false);
        }

        ArgumentNullException.ThrowIfNull(request.Names);
        if (request.Names.Count != request.NameCount ||
            request.Names.Count is < 1 or > 32)
        {
            var invalid = DlumaCallResult.Fail(
                DlumaStatus.InvalidArgument,
                new DlumaError(
                    "invalid_argument",
                    "Names and NameCount must describe 1..32 entries.",
                    false,
                    2005));
            return new(invalid, 0, 0, 0, false);
        }

        var leases = new Utf8Lease[request.Names.Count];
        NativeStringView* names =
            stackalloc NativeStringView[request.Names.Count];
        try
        {
            for (var index = 0; index < request.Names.Count; ++index)
            {
                leases[index] = new Utf8Lease(request.Names[index]);
                names[index] = leases[index].View;
            }

            var nativeRequest = new NativeSetEnabledManyRequest
            {
                StructSize = (uint)sizeof(NativeSetEnabledManyRequest),
                StructVersion = NativeConstants.StructVersion,
                Names = names,
                NameCount = request.NameCount,
                Enabled = request.Enabled ? 1u : 0u,
                Atomic = request.Atomic ? 1u : 0u,
            };
            var nativeResult = new NativeSetEnabledManyResult
            {
                StructSize = (uint)sizeof(NativeSetEnabledManyResult),
                StructVersion = NativeConstants.StructVersion,
            };
            var call = context.Bridge.Invoke(
                2005, ref nativeRequest, ref nativeResult);
            return new(
                call,
                nativeResult.RequestedCount,
                nativeResult.ChangedCount,
                nativeResult.UnchangedCount,
                nativeResult.Partial != 0);
        }
        finally
        {
            foreach (var lease in leases)
            {
                lease?.Dispose();
            }
        }
    }

    private DlumaVec3Result Vector(
        uint operationId,
        DlumaObjectRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, default);
        }

        var nativeRequest = ObjectRequest(request);
        var nativeResult = new NativeVec3Result
        {
            StructSize = (uint)sizeof(NativeVec3Result),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(
            call,
            new DlumaVec3(
                nativeResult.Value.X,
                nativeResult.Value.Y,
                nativeResult.Value.Z));
    }

    private static NativeObjectRequest ObjectRequest(
        DlumaObjectRequest request) => new()
    {
        StructSize = (uint)sizeof(NativeObjectRequest),
        StructVersion = NativeConstants.StructVersion,
        Handle = NativeConversions.ToNative(request.Handle),
    };

    private static string Read(byte* value, int capacity)
    {
        var length = 0;
        while (length < capacity && value[length] != 0)
        {
            ++length;
        }

        return System.Text.Encoding.UTF8.GetString(
            new ReadOnlySpan<byte>(value, length));
    }
}

internal sealed unsafe class EventsApi(NativeModContext context) : IEventsApi
{
    public event EventHandler<DlumaUpdateEventArgs>? Updated;

    public event EventHandler<DlumaNamedEventArgs>? SensorEntered;

    public event EventHandler<DlumaNamedEventArgs>? SensorLeft;

    public event EventHandler<DlumaNamedEventArgs>? InteractionBegan;

    public DlumaTokenResult UpdateSubscribe(
        DlumaEventSubscribeRequest request) =>
        Subscribe(4001, request);

    public DlumaBoolResult SensorIsInside(DlumaNamedRequest request)
    {
        var active = context.RequireActive(4002);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var name = new Utf8Lease(request.Name);
        var nativeRequest = new NativeNamedRequest
        {
            StructSize = (uint)sizeof(NativeNamedRequest),
            StructVersion = NativeConstants.StructVersion,
            Name = name.View,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            4002, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaTokenResult SensorEnter(
        DlumaEventSubscribeRequest request) =>
        Subscribe(4003, request);

    public DlumaTokenResult SensorLeave(
        DlumaEventSubscribeRequest request) =>
        Subscribe(4004, request);

    public DlumaTokenResult InteractionBegin(
        DlumaEventSubscribeRequest request) =>
        Subscribe(4005, request);

    public DlumaTokenResult EnemyKilled(
        DlumaEventSubscribeRequest request) =>
        Subscribe(4006, request);

    internal void Dispatch(NativeEventV1 value)
    {
        var name = NativeUtf8.Read(value.Name);
        var args = new DlumaNamedEventArgs(
            name,
            value.CallbackId,
            value.State != 0);
        switch (value.Kind)
        {
            case 5:
                Updated?.Invoke(
                    context,
                    new DlumaUpdateEventArgs(value.Number));
                break;
            case 7:
                SensorEntered?.Invoke(context, args);
                break;
            case 8:
                SensorLeft?.Invoke(context, args);
                break;
            case 9:
                InteractionBegan?.Invoke(context, args);
                break;
        }
    }

    private DlumaTokenResult Subscribe(
        uint operationId,
        DlumaEventSubscribeRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, 0);
        }

        using var name = new Utf8Lease(request.Name);
        var nativeRequest = new NativeEventSubscribeRequest
        {
            StructSize = (uint)sizeof(NativeEventSubscribeRequest),
            StructVersion = NativeConstants.StructVersion,
            Name = name.View,
            CallbackId = request.CallbackId,
        };
        var nativeResult = new NativeTokenResult
        {
            StructSize = (uint)sizeof(NativeTokenResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Token);
    }
}

internal sealed unsafe class UiApi(NativeModContext context) : IUiApi
{
    public DlumaBoolResult SubtitleShow(DlumaSubtitleRequest request)
    {
        var active = context.RequireActive(5001);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var text = new Utf8Lease(request.Text);
        var nativeRequest = new NativeSubtitleRequest
        {
            StructSize = (uint)sizeof(NativeSubtitleRequest),
            StructVersion = NativeConstants.StructVersion,
            Text = text.View,
            DurationSeconds = request.DurationSeconds,
            Force = request.Force ? 1u : 0u,
        };
        var nativeResult = BoolResult();
        var call = context.Bridge.Invoke(
            5001, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaBoolResult SubtitleUpdate(DlumaSubtitleUpdateRequest request)
    {
        var active = context.RequireActive(5002);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var text = new Utf8Lease(request.Text);
        var nativeRequest = new NativeSubtitleUpdateRequest
        {
            StructSize = (uint)sizeof(NativeSubtitleUpdateRequest),
            StructVersion = NativeConstants.StructVersion,
            Text = text.View,
            Fade = request.Fade ? 1u : 0u,
        };
        var nativeResult = BoolResult();
        var call = context.Bridge.Invoke(
            5002, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaQuestHudResult QuestHudSnapshot(
        DlumaQuestHudRequest request)
    {
        var active = context.RequireActive(5003);
        if (!active.Succeeded) return new(active, string.Empty, 0);
        byte* json = stackalloc byte[8192];
        var nativeRequest = new NativeQuestHudRequest
        {
            StructSize = (uint)sizeof(NativeQuestHudRequest),
            StructVersion = NativeConstants.StructVersion,
        };
        var nativeResult = new NativeQuestHudResult
        {
            StructSize = (uint)sizeof(NativeQuestHudResult),
            StructVersion = NativeConstants.StructVersion,
            Json = new NativeStringBuffer { Data = json, Capacity = 8192 },
        };
        var call = context.Bridge.Invoke(5003, ref nativeRequest, ref nativeResult);
        var text = call.Succeeded && nativeResult.Json.Required is > 0 and <= 8192
            ? System.Text.Encoding.UTF8.GetString(new ReadOnlySpan<byte>(
                json, checked((int)nativeResult.Json.Required - 1)))
            : string.Empty;
        return new(call, text, nativeResult.Revision);
    }

    public DlumaTokenResult QuestHudOverride(
        DlumaQuestHudOverrideRequest request)
    {
        var active = context.RequireActive(5004);
        if (!active.Succeeded) return new(active, 0);
        using var target = new Utf8Lease(request.TargetId);
        using var text = new Utf8Lease(request.Text);
        var nativeRequest = new NativeQuestHudOverrideRequest
        {
            StructSize = (uint)sizeof(NativeQuestHudOverrideRequest),
            StructVersion = NativeConstants.StructVersion,
            TargetId = target.View,
            Text = text.View,
            Token = request.Token,
        };
        var nativeResult = new NativeTokenResult
        {
            StructSize = (uint)sizeof(NativeTokenResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(5004, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Token);
    }

    public DlumaTokenResult Toast(DlumaSubtitleUpdateRequest request) =>
        new(NativeBridge.Unavailable(5005), 0);

    private static NativeBoolResult BoolResult() => new()
    {
        StructSize = (uint)sizeof(NativeBoolResult),
        StructVersion = NativeConstants.StructVersion,
    };
}

internal sealed unsafe class UiSurfaceApi(NativeModContext context) : IUiSurfaceApi
{
    public DlumaTokenResult Create(DlumaHudSurfaceCreateRequest request)
    {
        var active = context.RequireActive(13001);
        if (!active.Succeeded) return new(active, 0);
        var nativeRequest = new NativeHudSurfaceCreateRequest
        {
            StructSize = (uint)sizeof(NativeHudSurfaceCreateRequest),
            StructVersion = NativeConstants.StructVersion,
            Width = request.Width, Height = request.Height,
        };
        var result = new NativeTokenResult
        {
            StructSize = (uint)sizeof(NativeTokenResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(13001, ref nativeRequest, ref result);
        return new(call, result.Token);
    }

    public DlumaBoolResult Update(DlumaHudSurfaceUpdateRequest request)
    {
        var active = context.RequireActive(13002);
        if (!active.Succeeded) return new(active, false);
        ArgumentNullException.ThrowIfNull(request.Pixels);
        fixed (byte* pixels = request.Pixels)
        {
            var nativeRequest = new NativeHudSurfaceUpdateRequest
            {
                StructSize = (uint)sizeof(NativeHudSurfaceUpdateRequest),
                StructVersion = NativeConstants.StructVersion,
                Token = request.Token, Stride = request.Stride,
                Pixels = new NativeByteView { Data = pixels, Size = (ulong)request.Pixels.Length },
            };
            return Invoke(13002, ref nativeRequest);
        }
    }

    public DlumaBoolResult Layout(DlumaHudSurfaceLayoutRequest request)
    {
        var value = new NativeHudSurfaceLayoutRequest
        {
            StructSize = (uint)sizeof(NativeHudSurfaceLayoutRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token, X = request.X, Y = request.Y,
            Width = request.Width, Height = request.Height,
        };
        return Invoke(13003, ref value);
    }

    public DlumaBoolResult Visibility(DlumaHudSurfaceVisibilityRequest request)
    {
        var value = new NativeHudSurfaceVisibilityRequest
        {
            StructSize = (uint)sizeof(NativeHudSurfaceVisibilityRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token, Visible = request.Visible ? 1u : 0u,
        };
        return Invoke(13004, ref value);
    }

    public DlumaBoolResult Suppress(DlumaHudSurfaceSuppressRequest request)
    {
        var value = new NativeHudSurfaceSuppressRequest
        {
            StructSize = (uint)sizeof(NativeHudSurfaceSuppressRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token, Mask = request.Mask,
        };
        return Invoke(13005, ref value);
    }

    public DlumaBoolResult Release(DlumaTokenRequest request)
    {
        var value = new NativeTokenRequest
        {
            StructSize = (uint)sizeof(NativeTokenRequest),
            StructVersion = NativeConstants.StructVersion, Token = request.Token,
        };
        return Invoke(13006, ref value);
    }

    private DlumaBoolResult Invoke<T>(uint operation, ref T request) where T : unmanaged
    {
        var active = context.RequireActive(operation);
        if (!active.Succeeded) return new(active, false);
        var result = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(operation, ref request, ref result);
        return new(call, result.Value != 0);
    }
}

internal sealed unsafe class AudioApi(NativeModContext context) : IAudioApi
{
    public DlumaBoolResult RadioDialogPlay(DlumaRadioDialogRequest request)
    {
        var active = context.RequireActive(6001);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var speaker = new Utf8Lease(request.Speaker);
        using var group = new Utf8Lease(request.Group);
        var nativeRequest = new NativeRadioDialogRequest
        {
            StructSize = (uint)sizeof(NativeRadioDialogRequest),
            StructVersion = NativeConstants.StructVersion,
            Speaker = speaker.View,
            Group = group.View,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            6001, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaTokenResult CuePlay(DlumaAudioCuePlayRequest request)
    {
        var active = context.RequireActive(6002);
        if (!active.Succeeded) return new(active, 0);
        using var cue = new Utf8Lease(request.Cue);
        var nativeRequest = new NativeAudioCuePlayRequest
        {
            StructSize = (uint)sizeof(NativeAudioCuePlayRequest),
            StructVersion = NativeConstants.StructVersion,
            BankToken = request.BankToken,
            Cue = cue.View,
            StartMs = request.StartMs,
            InitialGain = request.InitialGain,
            Loop = request.Loop ? 1u : 0u,
            FadeInMs = request.FadeInMs,
            FadeOutMs = request.FadeOutMs,
        };
        return InvokeToken(6002, ref nativeRequest);
    }

    public DlumaBoolResult CueStop(DlumaAudioCueStopRequest request)
    {
        var nativeRequest = new NativeAudioCueStopRequest
        {
            StructSize = (uint)sizeof(NativeAudioCueStopRequest),
            StructVersion = NativeConstants.StructVersion,
            CueToken = request.CueToken,
            FadeOutMs = request.FadeOutMs,
        };
        return InvokeBool(6003, ref nativeRequest);
    }

    public DlumaTokenResult BankOpen(DlumaAudioBankOpenRequest request)
    {
        var active = context.RequireActive(6004);
        if (!active.Succeeded) return new(active, 0);
        using var bankId = new Utf8Lease(request.BankId);
        var nativeRequest = new NativeAudioBankOpenRequest
        {
            StructSize = (uint)sizeof(NativeAudioBankOpenRequest),
            StructVersion = NativeConstants.StructVersion,
            CompanionToken = request.CompanionToken,
            BankId = bankId.View,
        };
        return InvokeToken(6004, ref nativeRequest);
    }

    public DlumaU64Result CueDuration(DlumaAudioCueRequest request)
    {
        var active = context.RequireActive(6005);
        if (!active.Succeeded) return new(active, 0);
        using var cue = new Utf8Lease(request.Cue);
        var nativeRequest = new NativeAudioCueRequest
        {
            StructSize = (uint)sizeof(NativeAudioCueRequest),
            StructVersion = NativeConstants.StructVersion,
            BankToken = request.BankToken,
            Cue = cue.View,
        };
        var nativeResult = new NativeU64Result
        {
            StructSize = (uint)sizeof(NativeU64Result),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            6005, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value);
    }

    public DlumaAudioCueStateResult CueState(DlumaTokenRequest request)
    {
        var active = context.RequireActive(6006);
        if (!active.Succeeded) return new(active, false, 0, 0);
        var nativeRequest = new NativeTokenRequest
        {
            StructSize = (uint)sizeof(NativeTokenRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token,
        };
        var nativeResult = new NativeAudioCueStateResult
        {
            StructSize = (uint)sizeof(NativeAudioCueStateResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            6006, ref nativeRequest, ref nativeResult);
        return new(
            call, nativeResult.Playing != 0,
            nativeResult.PositionMs, nativeResult.DurationMs);
    }

    public DlumaBoolResult CueSeek(DlumaAudioCueSeekRequest request)
    {
        var nativeRequest = new NativeAudioCueSeekRequest
        {
            StructSize = (uint)sizeof(NativeAudioCueSeekRequest),
            StructVersion = NativeConstants.StructVersion,
            CueToken = request.CueToken,
            PositionMs = request.PositionMs,
        };
        return InvokeBool(6007, ref nativeRequest);
    }

    public DlumaBoolResult CueSetGain(DlumaAudioCueGainRequest request)
    {
        var nativeRequest = new NativeAudioCueGainRequest
        {
            StructSize = (uint)sizeof(NativeAudioCueGainRequest),
            StructVersion = NativeConstants.StructVersion,
            CueToken = request.CueToken,
            Gain = request.Gain,
        };
        return InvokeBool(6008, ref nativeRequest);
    }

    public DlumaBoolResult CueSetFilter(DlumaAudioFilterRequest request) =>
        Filter(6009, request);

    public DlumaTokenResult MusicAcquire(DlumaAudioMusicAcquireRequest request)
    {
        var nativeRequest = new NativeAudioMusicAcquireRequest
        {
            StructSize = (uint)sizeof(NativeAudioMusicAcquireRequest),
            StructVersion = NativeConstants.StructVersion,
            FadeOutMs = request.FadeOutMs,
        };
        return InvokeToken(6010, ref nativeRequest);
    }

    public DlumaAudioMusicStateResult MusicState(DlumaTokenRequest request)
    {
        var active = context.RequireActive(6011);
        if (!active.Succeeded) return new(active, 0);
        var nativeRequest = new NativeTokenRequest
        {
            StructSize = (uint)sizeof(NativeTokenRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token,
        };
        var nativeResult = new NativeAudioMusicStateResult
        {
            StructSize = (uint)sizeof(NativeAudioMusicStateResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            6011, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Phase);
    }

    public DlumaBoolResult MusicSetFilter(DlumaAudioFilterRequest request) =>
        Filter(6012, request);

    public DlumaBoolResult MusicRelease(DlumaAudioMusicReleaseRequest request)
    {
        var nativeRequest = new NativeAudioMusicReleaseRequest
        {
            StructSize = (uint)sizeof(NativeAudioMusicReleaseRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token,
            ResumeDelayMs = request.ResumeDelayMs,
        };
        return InvokeBool(6013, ref nativeRequest);
    }

    private DlumaBoolResult Filter(
        uint operationId,
        DlumaAudioFilterRequest request)
    {
        var nativeRequest = new NativeAudioFilterRequest
        {
            StructSize = (uint)sizeof(NativeAudioFilterRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token,
            WetMix = request.WetMix,
            CutoffHz = request.CutoffHz,
        };
        return InvokeBool(operationId, ref nativeRequest);
    }

    private DlumaTokenResult InvokeToken<T>(
        uint operationId,
        ref T request) where T : unmanaged
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded) return new(active, 0);
        var nativeResult = new NativeTokenResult
        {
            StructSize = (uint)sizeof(NativeTokenResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref request, ref nativeResult);
        return new(call, nativeResult.Token);
    }

    private DlumaBoolResult InvokeBool<T>(
        uint operationId,
        ref T request) where T : unmanaged
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded) return new(active, false);
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref request, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }
}

internal sealed unsafe class QuestsApi(NativeModContext context) : IQuestsApi
{
    public DlumaBoolResult AdvanceToPhase(
        DlumaQuestTransitionRequest request) =>
        Invoke(7001, request);

    public DlumaBoolResult Start(DlumaQuestTransitionRequest request) =>
        Invoke(7002, request);

    private DlumaBoolResult Invoke(
        uint operationId,
        DlumaQuestTransitionRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var quest = new Utf8Lease(request.QuestId);
        using var phase = new Utf8Lease(request.PhaseId);
        var nativeRequest = new NativeQuestTransitionRequest
        {
            StructSize = (uint)sizeof(NativeQuestTransitionRequest),
            StructVersion = NativeConstants.StructVersion,
            QuestId = quest.View,
            PhaseId = phase.View,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }
}

internal sealed unsafe class ActionsApi(NativeModContext context) : IActionsApi
{
    public DlumaBoolResult ChainRun(DlumaNamedRequest request)
    {
        var active = context.RequireActive(8001);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var name = new Utf8Lease(request.Name);
        var nativeRequest = new NativeNamedRequest
        {
            StructSize = (uint)sizeof(NativeNamedRequest),
            StructVersion = NativeConstants.StructVersion,
            Name = name.View,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            8001, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }
}

internal sealed unsafe class PlayerApi(NativeModContext context) : IPlayerApi
{
    private const int NameCapacity = 65;
    private const int MaximumNames = 64;

    public DlumaObjectResult Local(DlumaNoRequest request)
    {
        var active = context.RequireActive(3001);
        if (!active.Succeeded)
        {
            return new(active, default, string.Empty, string.Empty);
        }

        byte* name = stackalloc byte[32];
        byte* className = stackalloc byte[32];
        var nativeRequest = NoRequest();
        var nativeResult = new NativeObjectResult
        {
            StructSize = (uint)sizeof(NativeObjectResult),
            StructVersion = NativeConstants.StructVersion,
            Name = Buffer(name, 32),
            ClassName = Buffer(className, 32),
        };
        var call = context.Bridge.Invoke(
            3001, ref nativeRequest, ref nativeResult);
        return new(
            call,
            NativeConversions.ToManaged(nativeResult.Handle),
            Read(name, 32),
            Read(className, 32));
    }

    public DlumaVec3Result Position(DlumaObjectRequest request) =>
        Vector(3002, request);

    public DlumaVec3Result LookDirection(DlumaObjectRequest request) =>
        Vector(3003, request);

    public DlumaVec3Result BodyDirection(DlumaObjectRequest request) =>
        Vector(3004, request);

    public DlumaFloatResult Health(DlumaObjectRequest request) =>
        Float(3005, request);

    public DlumaFloatResult MaxHealth(DlumaObjectRequest request) =>
        Float(3006, request);

    public DlumaBoolResult Alive(DlumaObjectRequest request)
    {
        var active = context.RequireActive(3007);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        var nativeRequest = ObjectRequest(request);
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            3007, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaStringListResult VariableNames(DlumaNoRequest request)
    {
        var active = context.RequireActive(3008);
        if (!active.Succeeded)
        {
            return new(active, [], 0, 0);
        }

        NativeStringBuffer* items =
            stackalloc NativeStringBuffer[MaximumNames];
        byte* storage = stackalloc byte[MaximumNames * NameCapacity];
        for (var index = 0; index < MaximumNames; ++index)
        {
            items[index] = Buffer(
                storage + (index * NameCapacity), NameCapacity);
        }

        var nativeRequest = NoRequest();
        var nativeResult = new NativeStringListResult
        {
            StructSize = (uint)sizeof(NativeStringListResult),
            StructVersion = NativeConstants.StructVersion,
            Items = items,
            Capacity = MaximumNames,
        };
        var call = context.Bridge.Invoke(
            3008, ref nativeRequest, ref nativeResult);
        var count = Math.Min(nativeResult.Count, (uint)MaximumNames);
        var names = new string[count];
        for (var index = 0; index < count; ++index)
        {
            names[index] = Read(
                storage + (index * NameCapacity), NameCapacity);
        }
        return new(call, names, MaximumNames, nativeResult.Count);
    }

    public DlumaPlayerVariableResult VariableGet(
        DlumaPlayerVariableRequest request)
    {
        var active = context.RequireActive(3009);
        if (!active.Succeeded)
        {
            return new(active, string.Empty, 0, 0, 0, false);
        }

        ArgumentNullException.ThrowIfNull(request.Name);
        using var name = new Utf8Lease(request.Name);
        byte* resultName = stackalloc byte[NameCapacity];
        var nativeRequest = new NativePlayerVariableRequest
        {
            StructSize = (uint)sizeof(NativePlayerVariableRequest),
            StructVersion = NativeConstants.StructVersion,
            Player = NativeConversions.ToNative(request.Player),
            Name = name.View,
        };
        var nativeResult = new NativePlayerVariableResult
        {
            StructSize = (uint)sizeof(NativePlayerVariableResult),
            StructVersion = NativeConstants.StructVersion,
            Name = Buffer(resultName, NameCapacity),
        };
        var call = context.Bridge.Invoke(
            3009, ref nativeRequest, ref nativeResult);
        return new(
            call,
            Read(resultName, NameCapacity),
            nativeResult.Value,
            nativeResult.Minimum,
            nativeResult.Maximum,
            nativeResult.Overridden != 0);
    }

    public DlumaBoolResult VariableSet(DlumaPlayerVariableSetRequest request)
    {
        var active = context.RequireActive(3010);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        ArgumentNullException.ThrowIfNull(request.Name);
        using var name = new Utf8Lease(request.Name);
        var nativeRequest = new NativePlayerVariableSetRequest
        {
            StructSize = (uint)sizeof(NativePlayerVariableSetRequest),
            StructVersion = NativeConstants.StructVersion,
            Player = NativeConversions.ToNative(request.Player),
            Name = name.View,
            Value = request.Value,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            3010, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaBoolResult VariableReset(DlumaPlayerVariableRequest request)
    {
        var active = context.RequireActive(3011);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        ArgumentNullException.ThrowIfNull(request.Name);
        using var name = new Utf8Lease(request.Name);
        var nativeRequest = new NativePlayerVariableRequest
        {
            StructSize = (uint)sizeof(NativePlayerVariableRequest),
            StructVersion = NativeConstants.StructVersion,
            Player = NativeConversions.ToNative(request.Player),
            Name = name.View,
        };
        var nativeResult = new NativeBoolResult
        {
            StructSize = (uint)sizeof(NativeBoolResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            3011, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    public DlumaVehicleStateResult VehicleState(DlumaNoRequest request)
    {
        var active = context.RequireActive(3012);
        if (!active.Succeeded)
        {
            return new(
                active, false, false, false,
                string.Empty, string.Empty, default,
                0, 0, 0);
        }

        byte* vehicleType = stackalloc byte[33];
        byte* className = stackalloc byte[97];
        var nativeRequest = NoRequest();
        var nativeResult = new NativeVehicleStateResult
        {
            StructSize = (uint)sizeof(NativeVehicleStateResult),
            StructVersion = NativeConstants.StructVersion,
            VehicleType = Buffer(vehicleType, 33),
            ClassName = Buffer(className, 97),
        };
        var call = context.Bridge.Invoke(
            3012, ref nativeRequest, ref nativeResult);
        return new(
            call,
            nativeResult.PlayerPresent != 0,
            nativeResult.VehiclePresent != 0,
            nativeResult.IsLocalDriver != 0,
            Read(vehicleType, 33),
            Read(className, 97),
            new DlumaVec3(
                nativeResult.LinearVelocity.X,
                nativeResult.LinearVelocity.Y,
                nativeResult.LinearVelocity.Z),
            nativeResult.SpeedMps,
            nativeResult.SpeedMph,
            nativeResult.LevelEpoch);
    }

    private DlumaVec3Result Vector(
        uint operationId,
        DlumaObjectRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, default);
        }

        var nativeRequest = ObjectRequest(request);
        var nativeResult = new NativeVec3Result
        {
            StructSize = (uint)sizeof(NativeVec3Result),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(
            call,
            new DlumaVec3(
                nativeResult.Value.X,
                nativeResult.Value.Y,
                nativeResult.Value.Z));
    }

    private DlumaFloatResult Float(
        uint operationId,
        DlumaObjectRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, 0);
        }

        var nativeRequest = ObjectRequest(request);
        var nativeResult = new NativeFloatResult
        {
            StructSize = (uint)sizeof(NativeFloatResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value);
    }

    private static NativeNoRequest NoRequest() => new()
    {
        StructSize = (uint)sizeof(NativeNoRequest),
        StructVersion = NativeConstants.StructVersion,
    };

    private static NativeObjectRequest ObjectRequest(
        DlumaObjectRequest request) => new()
    {
        StructSize = (uint)sizeof(NativeObjectRequest),
        StructVersion = NativeConstants.StructVersion,
        Handle = NativeConversions.ToNative(request.Handle),
    };

    private static NativeStringBuffer Buffer(
        byte* value,
        int capacity) => new()
    {
        Data = value,
        Capacity = checked((ulong)capacity),
    };

    private static string Read(byte* value, int capacity)
    {
        var length = 0;
        while (length < capacity && value[length] != 0)
        {
            ++length;
        }

        return System.Text.Encoding.UTF8.GetString(
            new ReadOnlySpan<byte>(value, length));
    }
}

internal sealed unsafe class ConfigurationApi(
    NativeModContext context) : IConfigurationApi
{
    public event EventHandler<DlumaConfigurationChangedEventArgs>? ChangedEvent;

    public DlumaScalarResult Get(DlumaConfigGetRequest request) =>
        ScalarInterop.GetConfiguration(context, request);

    public DlumaTokenResult Changed(DlumaEventSubscribeRequest request) =>
        ScalarInterop.Subscribe(context, 9002, request);

    internal void Dispatch(NativeEventV1 value)
    {
        if (value.Kind != 10)
        {
            return;
        }

        ChangedEvent?.Invoke(
            context,
            new DlumaConfigurationChangedEventArgs(
                NativeUtf8.Read(value.Name),
                value.CallbackId,
                value.Token));
    }
}

internal sealed unsafe class StorageApi(NativeModContext context) : IStorageApi
{
    public DlumaScalarResult Get(DlumaStorageGetRequest request) =>
        ScalarInterop.GetStorage(context, request);

    public DlumaBoolResult Set(DlumaStorageSetRequest request) =>
        ScalarInterop.SetStorage(context, request);

    public DlumaBoolResult Delete(DlumaConfigGetRequest request) =>
        ScalarInterop.DeleteStorage(context, request);
}

internal sealed unsafe class CompanionContentApi(
    NativeModContext context) : ICompanionContentApi
{
    private const ulong MaximumEntryBytes = 1024UL * 1024UL;

    public DlumaCompanionOpenResult Open(DlumaCompanionOpenRequest request)
    {
        var active = context.RequireActive(11001);
        if (!active.Succeeded)
        {
            return new(active, 0, 0, string.Empty);
        }

        using var path = new Utf8Lease(request.RelativePath);
        byte* sha256 = stackalloc byte[65];
        var nativeRequest = new NativeCompanionOpenRequest
        {
            StructSize = (uint)sizeof(NativeCompanionOpenRequest),
            StructVersion = NativeConstants.StructVersion,
            RelativePath = path.View,
        };
        var nativeResult = new NativeCompanionOpenResult
        {
            StructSize = (uint)sizeof(NativeCompanionOpenResult),
            StructVersion = NativeConstants.StructVersion,
            Sha256 = new NativeStringBuffer
            {
                Data = sha256,
                Capacity = 65,
            },
        };
        var call = context.Bridge.Invoke(
            11001, ref nativeRequest, ref nativeResult);
        return new(
            call,
            nativeResult.Token,
            nativeResult.Size,
            ScalarInterop.ReadNullTerminated(sha256, 65));
    }

    public DlumaCompanionEntryReadResult EntryRead(
        DlumaCompanionEntryReadRequest request)
    {
        var active = context.RequireActive(11002);
        if (!active.Succeeded)
        {
            return new(active, [], string.Empty);
        }

        using var entryPath = new Utf8Lease(request.EntryPath);
        byte* sha256 = stackalloc byte[65];
        var nativeRequest = new NativeCompanionEntryReadRequest
        {
            StructSize = (uint)sizeof(NativeCompanionEntryReadRequest),
            StructVersion = NativeConstants.StructVersion,
            Token = request.Token,
            EntryPath = entryPath.View,
        };
        var nativeResult = new NativeCompanionEntryReadResult
        {
            StructSize = (uint)sizeof(NativeCompanionEntryReadResult),
            StructVersion = NativeConstants.StructVersion,
            Sha256 = new NativeStringBuffer
            {
                Data = sha256,
                Capacity = 65,
            },
        };

        var probe = context.Bridge.Invoke(
            11002, ref nativeRequest, ref nativeResult);
        if (probe.Status is not DlumaStatus.InsufficientBuffer)
        {
            return new(probe, [], string.Empty);
        }
        if (nativeResult.Bytes.Required > MaximumEntryBytes ||
            nativeResult.Bytes.Required > int.MaxValue)
        {
            return new(
                DlumaCallResult.Fail(
                    DlumaStatus.LimitExceeded,
                    new DlumaError(
                        "entry_too_large",
                        "The companion entry exceeds the 1 MiB managed read limit.",
                        false,
                        11002)),
                [],
                string.Empty);
        }

        var bytes = new byte[checked((int)nativeResult.Bytes.Required)];
        fixed (byte* data = bytes)
        {
            nativeResult.Bytes = new NativeByteBuffer
            {
                Data = data,
                Capacity = checked((ulong)bytes.LongLength),
            };
            var call = context.Bridge.Invoke(
                11002, ref nativeRequest, ref nativeResult);
            if (!call.Succeeded)
            {
                return new(call, [], string.Empty);
            }

            if (nativeResult.Bytes.Required < (ulong)bytes.LongLength)
            {
                Array.Resize(
                    ref bytes,
                    checked((int)nativeResult.Bytes.Required));
            }
            return new(
                call,
                bytes,
                ScalarInterop.ReadNullTerminated(sha256, 65));
        }
    }
}

internal static unsafe class ScalarInterop
{
    internal static DlumaScalarResult GetConfiguration(
        NativeModContext context,
        DlumaConfigGetRequest request)
    {
        var active = context.RequireActive(9001);
        if (!active.Succeeded)
        {
            return new(active, DlumaValue.None);
        }

        using var key = new Utf8Lease(request.Key);
        var nativeRequest = ConfigRequest(key);
        var nativeResult = ScalarResult();
        var call = context.Bridge.Invoke(
            9001, ref nativeRequest, ref nativeResult);
        return new(call, FromNative(nativeResult.Value));
    }

    internal static DlumaTokenResult Subscribe(
        NativeModContext context,
        uint operationId,
        DlumaEventSubscribeRequest request)
    {
        var active = context.RequireActive(operationId);
        if (!active.Succeeded)
        {
            return new(active, 0);
        }

        using var name = new Utf8Lease(request.Name);
        var nativeRequest = new NativeEventSubscribeRequest
        {
            StructSize = (uint)sizeof(NativeEventSubscribeRequest),
            StructVersion = NativeConstants.StructVersion,
            Name = name.View,
            CallbackId = request.CallbackId,
        };
        var nativeResult = new NativeTokenResult
        {
            StructSize = (uint)sizeof(NativeTokenResult),
            StructVersion = NativeConstants.StructVersion,
        };
        var call = context.Bridge.Invoke(
            operationId, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Token);
    }

    internal static DlumaScalarResult GetStorage(
        NativeModContext context,
        DlumaStorageGetRequest request)
    {
        var active = context.RequireActive(10001);
        if (!active.Succeeded)
        {
            return new(active, DlumaValue.None);
        }

        using var key = new Utf8Lease(request.Key);
        using var fallback = new NativeScalarLease(request.Fallback);
        var nativeRequest = new NativeStorageGetRequest
        {
            StructSize = (uint)sizeof(NativeStorageGetRequest),
            StructVersion = NativeConstants.StructVersion,
            Key = key.View,
            Fallback = fallback.Value,
            HasFallback = request.HasFallback ? 1u : 0u,
        };
        var nativeResult = ScalarResult();
        var call = context.Bridge.Invoke(
            10001, ref nativeRequest, ref nativeResult);
        return new(call, FromNative(nativeResult.Value));
    }

    internal static DlumaBoolResult SetStorage(
        NativeModContext context,
        DlumaStorageSetRequest request)
    {
        var active = context.RequireActive(10002);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var key = new Utf8Lease(request.Key);
        using var value = new NativeScalarLease(request.Value);
        var nativeRequest = new NativeStorageSetRequest
        {
            StructSize = (uint)sizeof(NativeStorageSetRequest),
            StructVersion = NativeConstants.StructVersion,
            Key = key.View,
            Value = value.Value,
        };
        var nativeResult = BoolResult();
        var call = context.Bridge.Invoke(
            10002, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    internal static DlumaBoolResult DeleteStorage(
        NativeModContext context,
        DlumaConfigGetRequest request)
    {
        var active = context.RequireActive(10003);
        if (!active.Succeeded)
        {
            return new(active, false);
        }

        using var key = new Utf8Lease(request.Key);
        var nativeRequest = ConfigRequest(key);
        var nativeResult = BoolResult();
        var call = context.Bridge.Invoke(
            10003, ref nativeRequest, ref nativeResult);
        return new(call, nativeResult.Value != 0);
    }

    internal static string ReadNullTerminated(byte* value, int capacity)
    {
        var length = 0;
        while (length < capacity && value[length] != 0)
        {
            ++length;
        }

        return System.Text.Encoding.UTF8.GetString(
            new ReadOnlySpan<byte>(value, length));
    }

    private static NativeConfigGetRequest ConfigRequest(Utf8Lease key) => new()
    {
        StructSize = (uint)sizeof(NativeConfigGetRequest),
        StructVersion = NativeConstants.StructVersion,
        Key = key.View,
    };

    private static NativeScalarResult ScalarResult() => new()
    {
        StructSize = (uint)sizeof(NativeScalarResult),
        StructVersion = NativeConstants.StructVersion,
    };

    private static NativeBoolResult BoolResult() => new()
    {
        StructSize = (uint)sizeof(NativeBoolResult),
        StructVersion = NativeConstants.StructVersion,
    };

    private static DlumaValue FromNative(NativeScalar value) =>
        value.Kind switch
        {
            0 => DlumaValue.None,
            1 => DlumaValue.From(value.IntegerValue != 0),
            2 => DlumaValue.From(value.IntegerValue),
            3 => DlumaValue.From(value.NumberValue),
            4 => DlumaValue.From(NativeUtf8.Read(value.StringValue)),
            _ => throw new InvalidDataException(
                "The native DLUMA scalar kind is invalid."),
        };
}

internal sealed unsafe class NativeScalarLease : IDisposable
{
    private readonly Utf8Lease? _string;

    internal NativeScalarLease(DlumaValue value)
    {
        var native = new NativeScalar
        {
            Kind = (uint)value.Kind,
        };
        switch (value.Kind)
        {
            case DlumaValueKind.None:
                break;
            case DlumaValueKind.Boolean:
                native.IntegerValue = value.BooleanValue ? 1 : 0;
                break;
            case DlumaValueKind.Integer:
                native.IntegerValue = value.IntegerValue;
                break;
            case DlumaValueKind.Number:
                if (!double.IsFinite(value.NumberValue))
                {
                    throw new ArgumentOutOfRangeException(nameof(value));
                }

                native.NumberValue = value.NumberValue;
                break;
            case DlumaValueKind.String:
                _string = new Utf8Lease(value.StringValue ?? string.Empty);
                native.StringValue = _string.View;
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(value));
        }

        Value = native;
    }

    internal NativeScalar Value { get; }

    public void Dispose() => _string?.Dispose();
}
