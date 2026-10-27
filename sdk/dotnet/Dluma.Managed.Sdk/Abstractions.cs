namespace Dluma.Managed.Sdk;

/// <summary>Why a managed mod is being stopped.</summary>
public enum DlumaStopReason
{
    ScopeDeactivated = 1,
    Shutdown = 2,
    Recovery = 3,
}

/// <summary>The normalized status returned by every DLUMA service.</summary>
public enum DlumaStatus
{
    Ok = 0,
    InvalidArgument = 1,
    Unavailable = 2,
    AccessDenied = 3,
    ScopeDenied = 4,
    WrongThread = 5,
    StaleHandle = 6,
    ForeignHandle = 7,
    NotFound = 8,
    LimitExceeded = 9,
    InsufficientBuffer = 10,
    ChangedSinceApproval = 11,
    OwnerRevoked = 12,
    Removed = 13,
    InternalError = 14,
}

/// <summary>A copied structured error. No native pointer crosses this contract.</summary>
public sealed record DlumaError(
    string Code,
    string Message,
    bool Retryable = false,
    uint OperationId = 0,
    DlumaCapabilityId? Capability = null,
    string? Access = null);

/// <summary>The common result carried by every generated managed result record.</summary>
public readonly record struct DlumaCallResult(DlumaStatus Status, DlumaError? Error = null)
{
    public bool Succeeded => Status is DlumaStatus.Ok;

    public static DlumaCallResult Ok() => new(DlumaStatus.Ok);

    public static DlumaCallResult Fail(DlumaStatus status, DlumaError error)
    {
        ArgumentNullException.ThrowIfNull(error);
        if (status is DlumaStatus.Ok)
        {
            throw new ArgumentOutOfRangeException(
                nameof(status), status, "A failed result cannot use Ok.");
        }

        return new(status, error);
    }
}

/// <summary>A generation- and owner-bound opaque object identity.</summary>
public readonly record struct DlumaObjectHandle(
    ulong OwnerId,
    ulong HandleId,
    uint Generation,
    uint Kind);

/// <summary>A copied game-space vector.</summary>
public readonly record struct DlumaVec3(float X, float Y, float Z);

/// <summary>The supported manifest, configuration, and storage scalar kinds.</summary>
public enum DlumaValueKind
{
    None = 0,
    Boolean = 1,
    Integer = 2,
    Number = 3,
    String = 4,
}

/// <summary>A bounded copied scalar value.</summary>
public readonly record struct DlumaValue
{
    private DlumaValue(
        DlumaValueKind kind,
        bool booleanValue,
        long integerValue,
        double numberValue,
        string? stringValue)
    {
        Kind = kind;
        BooleanValue = booleanValue;
        IntegerValue = integerValue;
        NumberValue = numberValue;
        StringValue = stringValue;
    }

    public DlumaValueKind Kind { get; }

    public bool BooleanValue { get; }

    public long IntegerValue { get; }

    public double NumberValue { get; }

    public string? StringValue { get; }

    public static DlumaValue None => default;

    public static DlumaValue From(bool value) =>
        new(DlumaValueKind.Boolean, value, default, default, null);

    public static DlumaValue From(long value) =>
        new(DlumaValueKind.Integer, default, value, default, null);

    public static DlumaValue From(double value) =>
        double.IsFinite(value)
            ? new(DlumaValueKind.Number, default, default, value, null)
            : throw new ArgumentOutOfRangeException(
                nameof(value), "DLUMA values must be finite.");

    public static DlumaValue From(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return new(DlumaValueKind.String, default, default, default, value);
    }
}

/// <summary>The one managed entry contract for DLUMA API 1.0 mods.</summary>
public interface IDlumaMod
{
    void OnStart(IModContext context);

    void OnStop(DlumaStopReason reason);
}

/// <summary>A copied bounded update event raised on the game thread.</summary>
public sealed class DlumaUpdateEventArgs(double deltaSeconds) : EventArgs
{
    public double DeltaSeconds { get; } = deltaSeconds;
}

/// <summary>An owner-bound timer event raised on the game thread.</summary>
public sealed class DlumaTimerEventArgs(
    ulong callbackId,
    ulong token,
    double monotonicMilliseconds) : EventArgs
{
    public ulong CallbackId { get; } = callbackId;

    public ulong Token { get; } = token;

    public double MonotonicMilliseconds { get; } =
        monotonicMilliseconds;
}

/// <summary>A copied manifest configuration change event.</summary>
public sealed class DlumaConfigurationChangedEventArgs(
    string key,
    ulong callbackId,
    ulong token) : EventArgs
{
    public string Key { get; } = key;

    public ulong CallbackId { get; } = callbackId;

    public ulong Token { get; } = token;
}

/// <summary>A copied exact-name game event raised on the game thread.</summary>
public sealed class DlumaNamedEventArgs(
    string name,
    ulong callbackId,
    bool state) : EventArgs
{
    public string Name { get; } = name;

    public ulong CallbackId { get; } = callbackId;

    public bool State { get; } = state;
}
