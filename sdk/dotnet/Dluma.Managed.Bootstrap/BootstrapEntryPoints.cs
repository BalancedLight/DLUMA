using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Dluma.Managed.Sdk;

namespace Dluma.Managed.Bootstrap;

internal sealed class ManagedModState
{
    internal required ulong Handle { get; init; }

    internal required string ModId { get; init; }

    internal required ModAssemblyLoadContext LoadContext { get; init; }

    internal required Type EntryType { get; init; }

    internal required NativeModContext Context { get; init; }

    internal object Gate { get; } = new();

    internal IDlumaMod? Instance { get; set; }

    internal bool Active { get; set; }

    internal bool Shutdown { get; set; }
}

public static unsafe class BootstrapEntryPoints
{
    private const uint MaximumArtifacts = 256;
    private const ulong MaximumArtifactBytes = 256UL * 1024UL * 1024UL;

    private static readonly object RegistryGate = new();
    private static readonly Dictionary<ulong, ManagedModState> Registry = [];
    private static ulong _nextHandle = 1;

    [UnmanagedCallersOnly(
        EntryPoint = "DLUMA_ManagedBootstrap_GetApi",
        CallConvs = [typeof(CallConvCdecl)])]
    public static int GetApi(
        uint requestedAbiVersion,
        void* output,
        uint outputSize,
        byte* diagnostic,
        uint diagnosticCapacity)
    {
        try
        {
            if (requestedAbiVersion != NativeConstants.AbiVersion)
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Unsupported managed bootstrap ABI version.");
            }

            if (output is null ||
                outputSize < (uint)sizeof(NativeBootstrapApiV1))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Managed bootstrap API output buffer is too small.");
            }

            var api = (NativeBootstrapApiV1*)output;
            *api = new NativeBootstrapApiV1
            {
                StructSize = (uint)sizeof(NativeBootstrapApiV1),
                StructVersion = NativeConstants.StructVersion,
                AbiVersion = NativeConstants.AbiVersion,
                Load = &Load,
                Activate = &Activate,
                Deactivate = &Deactivate,
                Dispatch = &Dispatch,
                Shutdown = &Shutdown,
            };
            NativeUtf8.WriteDiagnostic(
                diagnostic,
                diagnosticCapacity,
                string.Empty);
            return 1;
        }
        catch (Exception exception)
        {
            return FailException(
                diagnostic,
                diagnosticCapacity,
                "bootstrap negotiation",
                exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Load(
        NativeModLoadV1* load,
        ulong* outputHandle,
        byte* diagnostic,
        uint diagnosticCapacity)
    {
        try
        {
            if (load is null ||
                load->StructSize < (uint)sizeof(NativeModLoadV1) ||
                load->StructVersion != NativeConstants.StructVersion ||
                outputHandle is null)
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Malformed managed mod load request.");
            }

            if (load->Artifacts is null ||
                load->ArtifactCount is 0 or > MaximumArtifacts)
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Managed mod artifact count is invalid.");
            }

            var modId = NativeUtf8.Read(load->ModId);
            var entryTypeName = NativeUtf8.Read(load->EntryType);
            var entryPath = NormalizeRelativePath(
                NativeUtf8.Read(load->EntryPath));
            if (string.IsNullOrWhiteSpace(modId) ||
                string.IsNullOrWhiteSpace(entryTypeName) ||
                string.IsNullOrWhiteSpace(entryPath))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Managed manifest identity, entry path, and entry type are required.");
            }

            var artifacts = CopyArtifacts(load);
            if (!artifacts.TryGetValue(entryPath, out var entryBytes))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "The approved lock does not contain the managed entry assembly.");
            }

            var loadContext = new ModAssemblyLoadContext(modId, artifacts);
            var assembly = loadContext.LoadEntryAssembly(entryBytes);
            var entryType = assembly.GetType(
                entryTypeName,
                throwOnError: true,
                ignoreCase: false);
            if (entryType is null ||
                !typeof(IDlumaMod).IsAssignableFrom(entryType) ||
                entryType.IsAbstract ||
                entryType.IsInterface ||
                entryType.ContainsGenericParameters ||
                entryType.GetConstructor(
                    BindingFlags.Instance | BindingFlags.Public,
                    binder: null,
                    Type.EmptyTypes,
                    modifiers: null) is null)
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "The managed entry type must be a concrete public IDlumaMod with a public parameterless constructor.");
            }

            var bridge = new NativeBridge(load->Bridge);
            var state = new ManagedModState
            {
                Handle = NextHandle(),
                ModId = modId,
                LoadContext = loadContext,
                EntryType = entryType,
                Context = new NativeModContext(modId, bridge),
            };
            lock (RegistryGate)
            {
                Registry.Add(state.Handle, state);
            }

            *outputHandle = state.Handle;
            NativeUtf8.WriteDiagnostic(
                diagnostic,
                diagnosticCapacity,
                string.Empty);
            return 1;
        }
        catch (Exception exception)
        {
            return FailException(
                diagnostic,
                diagnosticCapacity,
                "managed mod load",
                exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Activate(
        ulong handle,
        NativeActivationV1* activation,
        byte* diagnostic,
        uint diagnosticCapacity)
    {
        try
        {
            if (activation is null ||
                activation->StructSize <
                    (uint)sizeof(NativeActivationV1) ||
                activation->StructVersion !=
                    NativeConstants.StructVersion)
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Malformed managed activation request.");
            }

            if (!TryGetState(handle, out var state))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Unknown managed mod handle.");
            }

            lock (state.Gate)
            {
                if (state.Shutdown)
                {
                    return Fail(
                        diagnostic,
                        diagnosticCapacity,
                        "Managed mod has already shut down.");
                }

                if (state.Active)
                {
                    return Fail(
                        diagnostic,
                        diagnosticCapacity,
                        "Managed mod scope is already active.");
                }

                var instance = (IDlumaMod?)Activator.CreateInstance(
                    state.EntryType);
                if (instance is null)
                {
                    return Fail(
                        diagnostic,
                        diagnosticCapacity,
                        "Managed entry constructor returned no instance.");
                }

                state.Instance = instance;
                state.Context.Activate();
                try
                {
                    instance.OnStart(state.Context);
                    state.Active = true;
                }
                catch
                {
                    state.Context.Revoke();
                    state.Instance = null;
                    try
                    {
                        instance.OnStop(DlumaStopReason.Recovery);
                    }
                    catch
                    {
                        // Preserve the original OnStart failure.
                    }

                    throw;
                }
            }

            NativeUtf8.WriteDiagnostic(
                diagnostic,
                diagnosticCapacity,
                string.Empty);
            return 1;
        }
        catch (Exception exception)
        {
            return FailException(
                diagnostic,
                diagnosticCapacity,
                "managed activation",
                exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Deactivate(
        ulong handle,
        uint reason,
        byte* diagnostic,
        uint diagnosticCapacity)
    {
        try
        {
            if (!TryGetState(handle, out var state))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Unknown managed mod handle.");
            }

            StopState(state, ToStopReason(reason), permanent: false);
            NativeUtf8.WriteDiagnostic(
                diagnostic,
                diagnosticCapacity,
                string.Empty);
            return 1;
        }
        catch (Exception exception)
        {
            return FailException(
                diagnostic,
                diagnosticCapacity,
                "managed deactivation",
                exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Dispatch(
        ulong handle,
        NativeEventV1* value,
        byte* diagnostic,
        uint diagnosticCapacity)
    {
        try
        {
            if (value is null ||
                value->StructSize < (uint)sizeof(NativeEventV1) ||
                value->StructVersion != NativeConstants.StructVersion)
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Malformed managed event.");
            }

            if (!TryGetState(handle, out var state))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Unknown managed mod handle.");
            }

            lock (state.Gate)
            {
                if (!state.Active || state.Shutdown)
                {
                    return 1;
                }

                state.Context.Dispatch(*value);
            }

            NativeUtf8.WriteDiagnostic(
                diagnostic,
                diagnosticCapacity,
                string.Empty);
            return 1;
        }
        catch (Exception exception)
        {
            return FailException(
                diagnostic,
                diagnosticCapacity,
                "managed event",
                exception);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Shutdown(
        ulong handle,
        uint reason,
        byte* diagnostic,
        uint diagnosticCapacity)
    {
        try
        {
            if (!TryGetState(handle, out var state))
            {
                return Fail(
                    diagnostic,
                    diagnosticCapacity,
                    "Unknown managed mod handle.");
            }

            StopState(state, ToStopReason(reason), permanent: true);
            NativeUtf8.WriteDiagnostic(
                diagnostic,
                diagnosticCapacity,
                string.Empty);
            return 1;
        }
        catch (Exception exception)
        {
            return FailException(
                diagnostic,
                diagnosticCapacity,
                "managed shutdown",
                exception);
        }
    }

    private static Dictionary<string, byte[]> CopyArtifacts(
        NativeModLoadV1* load)
    {
        var result = new Dictionary<string, byte[]>(
            StringComparer.OrdinalIgnoreCase);
        ulong total = 0;
        for (uint index = 0; index < load->ArtifactCount; ++index)
        {
            var artifact = &load->Artifacts[index];
            if (artifact->StructSize <
                    (uint)sizeof(NativeArtifactV1) ||
                artifact->StructVersion !=
                    NativeConstants.StructVersion ||
                artifact->Bytes is null ||
                artifact->Size is 0 or > MaximumArtifactBytes)
            {
                throw new InvalidDataException(
                    "Malformed approved managed artifact.");
            }

            total = checked(total + artifact->Size);
            if (total > MaximumArtifactBytes ||
                artifact->Size > int.MaxValue)
            {
                throw new InvalidDataException(
                    "Approved managed artifact bytes exceed the host limit.");
            }

            var relativePath = NormalizeRelativePath(
                NativeUtf8.Read(artifact->RelativePath));
            if (string.IsNullOrEmpty(relativePath) ||
                !result.TryAdd(
                    relativePath,
                    new ReadOnlySpan<byte>(
                        artifact->Bytes,
                        checked((int)artifact->Size)).ToArray()))
            {
                throw new InvalidDataException(
                    "Approved managed artifact paths are invalid or ambiguous.");
            }
        }

        return result;
    }

    private static string NormalizeRelativePath(string value)
    {
        if (string.IsNullOrWhiteSpace(value) ||
            Path.IsPathRooted(value) ||
            value.IndexOf('\0') >= 0)
        {
            throw new InvalidDataException(
                "Managed artifact paths must be canonical relative paths.");
        }

        var normalized = value.Replace('\\', '/');
        var segments = normalized.Split(
            '/',
            StringSplitOptions.RemoveEmptyEntries);
        if (segments.Length == 0 ||
            segments.Any(
                segment =>
                    segment is "." or ".." ||
                    segment.EndsWith(' ') ||
                    segment.EndsWith('.')))
        {
            throw new InvalidDataException(
                "Managed artifact path contains an unsafe segment.");
        }

        return string.Join('/', segments);
    }

    private static ulong NextHandle()
    {
        lock (RegistryGate)
        {
            if (_nextHandle == 0)
            {
                throw new InvalidOperationException(
                    "Managed mod handles are exhausted.");
            }

            return _nextHandle++;
        }
    }

    private static bool TryGetState(
        ulong handle,
        out ManagedModState state)
    {
        lock (RegistryGate)
        {
            return Registry.TryGetValue(handle, out state!);
        }
    }

    private static void StopState(
        ManagedModState state,
        DlumaStopReason reason,
        bool permanent)
    {
        Exception? callbackFailure = null;
        lock (state.Gate)
        {
            if (state.Active && state.Instance is not null)
            {
                try
                {
                    state.Instance.OnStop(reason);
                }
                catch (Exception exception)
                {
                    callbackFailure = exception;
                }
            }

            state.Context.Revoke();
            state.Instance = null;
            state.Active = false;
            state.Shutdown |= permanent;
        }

        if (callbackFailure is not null)
        {
            throw callbackFailure;
        }
    }

    private static DlumaStopReason ToStopReason(uint reason) => reason switch
    {
        1 => DlumaStopReason.ScopeDeactivated,
        2 => DlumaStopReason.Shutdown,
        3 => DlumaStopReason.Recovery,
        _ => DlumaStopReason.Recovery,
    };

    private static int Fail(
        byte* diagnostic,
        uint diagnosticCapacity,
        string message)
    {
        NativeUtf8.WriteDiagnostic(
            diagnostic,
            diagnosticCapacity,
            message);
        return 0;
    }

    private static int FailException(
        byte* diagnostic,
        uint diagnosticCapacity,
        string operation,
        Exception exception) =>
        Fail(
            diagnostic,
            diagnosticCapacity,
            $"{operation} failed: {exception.GetType().Name}: {exception.Message}");
}
