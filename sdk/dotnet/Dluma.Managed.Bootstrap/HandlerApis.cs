using Dluma.Managed.Sdk;

namespace Dluma.Managed.Bootstrap;

internal sealed unsafe class ModHandlersApi(NativeModContext context) : IModHandlersApi
{
    public event EventHandler<DlumaModHandlerEventArgs>? Invoked;

    public DlumaTokenResult Register(DlumaModHandlerRegistrationRequest request) =>
        HandlerApiCalls.Register(context, 15001, request.Handler, request.CallbackId);

    public DlumaBoolResult Unregister(DlumaTokenRequest request) =>
        HandlerApiCalls.Unregister(context, 15003, request);

    public DlumaBoolResult Invoke(DlumaModHandlerInvokeRequest request)
    {
        var active = context.RequireActive(15002);
        if (!active.Succeeded) return new(active, false);
        using var mod = new Utf8Lease(request.Mod);
        using var handler = new Utf8Lease(request.Handler);
        using var args = new Utf8Lease(request.ArgumentsJson);
        var input = new NativeHandlerInvokeRequest
        {
            StructSize = (uint)sizeof(NativeHandlerInvokeRequest), StructVersion = 1,
            Mod = mod.View, Handler = handler.View, ArgumentsJson = args.View,
        };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(15002, ref input, ref output);
        return new(call, output.Value != 0);
    }

    internal void Dispatch(NativeModHandlerEventV1 value) => Invoked?.Invoke(context,
        new DlumaModHandlerEventArgs(value.CallbackId, value.Token, value.SourceKind,
            value.MapGeneration, NativeUtf8.Read(value.SourceMod), NativeUtf8.Read(value.QuestId),
            NativeUtf8.Read(value.PhaseId), NativeUtf8.Read(value.Provider), NativeUtf8.Read(value.Handler),
            NativeUtf8.Read(value.ArgumentsJson)));
}

internal sealed unsafe class QuestPhasesApi(NativeModContext context) : IQuestPhasesApi
{
    public event EventHandler<DlumaQuestPhaseEventArgs>? PhaseEntered;
    public DlumaTokenResult ProviderRegister(DlumaQuestPhaseRegistrationRequest request) =>
        HandlerApiCalls.Register(context, 12001, request.Key, request.CallbackId);
    public DlumaTokenResult ObserverRegister(DlumaQuestPhaseRegistrationRequest request) =>
        HandlerApiCalls.Register(context, 12002, request.Key, request.CallbackId);
    public DlumaBoolResult Unregister(DlumaTokenRequest request) =>
        HandlerApiCalls.Unregister(context, 12003, request);
    internal void Dispatch(NativeQuestPhaseEventV1 value) => PhaseEntered?.Invoke(context,
        new DlumaQuestPhaseEventArgs(value.CallbackId, value.Token, value.Kind, value.MapGeneration,
            value.Sequence, value.RuntimeKind, value.Completion, value.WaitSeconds,
            NativeUtf8.Read(value.QuestId), NativeUtf8.Read(value.PhaseId), NativeUtf8.Read(value.ContractId),
            NativeUtf8.Read(value.Provider), NativeUtf8.Read(value.Handler), NativeUtf8.Read(value.BindingHash),
            NativeUtf8.Read(value.ArgumentsJson)));
}

internal static unsafe class HandlerApiCalls
{
    internal static DlumaTokenResult Register(NativeModContext context, uint operation, string key, ulong callbackId)
    {
        var active = context.RequireActive(operation);
        if (!active.Succeeded) return new(active, 0);
        using var value = new Utf8Lease(key);
        var input = new NativeHandlerRegistrationRequest
        {
            StructSize = (uint)sizeof(NativeHandlerRegistrationRequest), StructVersion = 1,
            Handler = value.View, CallbackId = callbackId,
        };
        var output = new NativeTokenResult { StructSize = (uint)sizeof(NativeTokenResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(operation, ref input, ref output);
        return new(call, output.Token);
    }

    internal static DlumaBoolResult Unregister(NativeModContext context, uint operation, DlumaTokenRequest request)
    {
        var active = context.RequireActive(operation);
        if (!active.Succeeded) return new(active, false);
        var input = new NativeTokenRequest
        {
            StructSize = (uint)sizeof(NativeTokenRequest), StructVersion = 1, Token = request.Token,
        };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(operation, ref input, ref output);
        return new(call, output.Value != 0);
    }
}

internal sealed unsafe class HudApi(NativeModContext context) : IHudApi
{
    public DlumaBoolResult Show(DlumaHudVisibilityRequest request) => Call(14001, request);
    public DlumaBoolResult Hide(DlumaHudVisibilityRequest request) => Call(14002, request);
    public DlumaBoolResult Reset(DlumaHudVisibilityRequest request) => Call(14003, request);
    public DlumaBoolResult SetPosition(DlumaHudPositionRequest request)
    {
        var active = context.RequireActive(14004);
        if (!active.Succeeded) return new(active, false);
        using var screen = new Utf8Lease(request.Screen);
        using var target = new Utf8Lease(request.Target);
        var input = new NativeHudPositionRequest
        {
            StructSize = (uint)sizeof(NativeHudPositionRequest), StructVersion = 1,
            Screen = screen.View, Target = target.View,
            Position = new NativeVec3 { X = request.Position.X, Y = request.Position.Y, Z = request.Position.Z },
        };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(14004, ref input, ref output);
        return new(call, output.Value != 0);
    }

    public DlumaBoolResult SetOpacity(DlumaHudOpacityRequest request)
    {
        var active = context.RequireActive(14005);
        if (!active.Succeeded) return new(active, false);
        using var screen = new Utf8Lease(request.Screen);
        using var target = new Utf8Lease(request.Target);
        var input = new NativeHudOpacityRequest { StructSize = (uint)sizeof(NativeHudOpacityRequest),
            StructVersion = 1, Screen = screen.View, Target = target.View, Opacity = request.Opacity };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(14005, ref input, ref output);
        return new(call, output.Value != 0);
    }

    public DlumaBoolResult SetColor(DlumaHudColorRequest request)
    {
        var active = context.RequireActive(14006);
        if (!active.Succeeded) return new(active, false);
        using var screen = new Utf8Lease(request.Screen);
        using var target = new Utf8Lease(request.Target);
        var input = new NativeHudColorRequest { StructSize = (uint)sizeof(NativeHudColorRequest),
            StructVersion = 1, Screen = screen.View, Target = target.View,
            Red = request.Red, Green = request.Green, Blue = request.Blue, Alpha = request.Alpha };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(14006, ref input, ref output);
        return new(call, output.Value != 0);
    }

    public DlumaBoolResult SetTextKey(DlumaHudNamedRequest request) => Named(14007, request);
    public DlumaBoolResult StartAnimation(DlumaHudNamedRequest request) => Named(14008, request);

    public DlumaTokenResult SwapDocument(DlumaHudDocumentSwapRequest request)
    {
        var active=context.RequireActive(14009); if(!active.Succeeded) return new(active,0);
        using var original=new Utf8Lease(request.OriginalDocument);
        using var replacement=new Utf8Lease(request.ReplacementDocument);
        var input=new NativeHudDocumentSwapRequest { StructSize=(uint)sizeof(NativeHudDocumentSwapRequest), StructVersion=1,
            OriginalDocument=original.View, ReplacementDocument=replacement.View };
        var output=new NativeTokenResult { StructSize=(uint)sizeof(NativeTokenResult), StructVersion=1 };
        var call=context.Bridge.Invoke(14009,ref input,ref output); return new(call,output.Token);
    }
    public DlumaBoolResult RestoreDocument(DlumaTokenRequest request) => AssetCommand(14010,request);
    public DlumaBoolResult CancelDocument(DlumaTokenRequest request) => AssetCommand(14012,request);
    public DlumaBoolResult RestoreFlashbackTextures(DlumaTokenRequest request) => AssetCommand(14015,request);
    private DlumaBoolResult AssetCommand(uint operation,DlumaTokenRequest request)
    {
        var active=context.RequireActive(operation); if(!active.Succeeded) return new(active,false);
        var input=new NativeTokenRequest { StructSize=(uint)sizeof(NativeTokenRequest), StructVersion=1, Token=request.Token };
        var output=new NativeBoolResult { StructSize=(uint)sizeof(NativeBoolResult), StructVersion=1 };
        var call=context.Bridge.Invoke(operation,ref input,ref output); return new(call,output.Value!=0);
    }
    public DlumaHudDocumentStatusResult DocumentStatus(DlumaTokenRequest request)
    {
        var active=context.RequireActive(14011); if(!active.Succeeded) return new(active,0,0,0);
        var input=new NativeTokenRequest { StructSize=(uint)sizeof(NativeTokenRequest), StructVersion=1, Token=request.Token };
        var output=new NativeHudAssetStatusResult { StructSize=(uint)sizeof(NativeHudAssetStatusResult), StructVersion=1 };
        var call=context.Bridge.Invoke(14011,ref input,ref output); return new(call,output.Token,output.State,output.Reason);
    }
    public DlumaHudFlashbackTextureStatusResult FlashbackTextureStatus(DlumaTokenRequest request)
    {
        var active=context.RequireActive(14014); if(!active.Succeeded) return new(active,0,0,0,0,0);
        var input=new NativeTokenRequest { StructSize=(uint)sizeof(NativeTokenRequest), StructVersion=1, Token=request.Token };
        var output=new NativeHudTextureStatusResult { StructSize=(uint)sizeof(NativeHudTextureStatusResult), StructVersion=1 };
        var call=context.Bridge.Invoke(14014,ref input,ref output);
        return new(call,output.Token,output.State,output.Reason,output.ReadyCount,output.AssetCount);
    }
    public DlumaTokenResult SwapFlashbackTextures(DlumaHudFlashbackTextureRequest request)
    {
        var active=context.RequireActive(14013); if(!active.Succeeded) return new(active,0);
        if(request.Count!=11 || request.Originals is null || request.Replacements is null ||
           request.Originals.Count!=11 || request.Replacements.Count!=11) {
            var invalid=DlumaCallResult.Fail(DlumaStatus.InvalidArgument,
                new DlumaError("invalid_argument","Exactly eleven alias pairs are required.",false,14013));
            return new(invalid,0);
        }
        var leases=new Utf8Lease[22]; NativeStringView* originals=stackalloc NativeStringView[11];
        NativeStringView* replacements=stackalloc NativeStringView[11];
        try {
            for(var i=0;i<11;++i) { leases[i]=new Utf8Lease(request.Originals[i]); leases[i+11]=new Utf8Lease(request.Replacements[i]);
                originals[i]=leases[i].View; replacements[i]=leases[i+11].View; }
            var input=new NativeHudTextureRequest { StructSize=(uint)sizeof(NativeHudTextureRequest), StructVersion=1,
                Originals=originals, Replacements=replacements, Count=11 };
            var output=new NativeTokenResult { StructSize=(uint)sizeof(NativeTokenResult), StructVersion=1 };
            var call=context.Bridge.Invoke(14013,ref input,ref output); return new(call,output.Token);
        } finally { foreach(var lease in leases) lease?.Dispose(); }
    }

    private DlumaBoolResult Named(uint operation, DlumaHudNamedRequest request)
    {
        var active = context.RequireActive(operation);
        if (!active.Succeeded) return new(active, false);
        using var screen = new Utf8Lease(request.Screen);
        using var target = new Utf8Lease(request.Target);
        using var name = new Utf8Lease(request.Name);
        var input = new NativeHudNamedRequest { StructSize = (uint)sizeof(NativeHudNamedRequest),
            StructVersion = 1, Screen = screen.View, Target = target.View, Name = name.View };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(operation, ref input, ref output);
        return new(call, output.Value != 0);
    }

    private DlumaBoolResult Call(uint operation, DlumaHudVisibilityRequest request)
    {
        var active = context.RequireActive(operation);
        if (!active.Succeeded) return new(active, false);
        using var screen = new Utf8Lease(request.Screen);
        using var target = new Utf8Lease(request.Target);
        var input = new NativeHudVisibilityRequest
        {
            StructSize = (uint)sizeof(NativeHudVisibilityRequest), StructVersion = 1,
            Screen = screen.View, Target = target.View,
        };
        var output = new NativeBoolResult { StructSize = (uint)sizeof(NativeBoolResult), StructVersion = 1 };
        var call = context.Bridge.Invoke(operation, ref input, ref output);
        return new(call, output.Value != 0);
    }
}
