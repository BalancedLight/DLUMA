using Dluma.Managed.Sdk;

static void Assert(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

Assert(DlumaApiCatalog.Version == "1.0", "managed API version drifted");
Assert(DlumaApiCatalog.Sha256.Length == 64, "catalog hash is not SHA-256");
Assert(DlumaApiCatalog.Operations.Count > 0, "generated operation catalog is empty");
Assert(
    DlumaApiCatalog.Operations.Select(item => item.NumericId).Distinct().Count() ==
    DlumaApiCatalog.Operations.Count,
    "operation numeric IDs are not unique");
Assert(
    DlumaApiCatalog.Operations.All(item => item.Scopes.Length > 0),
    "an operation has no valid scope");

var ok = DlumaCallResult.Ok();
Assert(ok.Succeeded && ok.Error is null, "success result contract failed");
var failure = DlumaCallResult.Fail(
    DlumaStatus.Unavailable,
    new DlumaError("unsupported_capability", "Not ready."));
Assert(!failure.Succeeded && failure.Error is not null, "failure result contract failed");

var boolean = DlumaValue.From(true);
var integer = DlumaValue.From(42L);
var number = DlumaValue.From(0.5);
var text = DlumaValue.From("workbench");
Assert(boolean.Kind == DlumaValueKind.Boolean && boolean.BooleanValue, "boolean failed");
Assert(integer.Kind == DlumaValueKind.Integer && integer.IntegerValue == 42, "integer failed");
Assert(number.Kind == DlumaValueKind.Number && number.NumberValue == 0.5, "number failed");
Assert(text.Kind == DlumaValueKind.String && text.StringValue == "workbench", "text failed");

var fakeAudio = new FakeAudioApi();
var leaseResult = fakeAudio.AcquireMusic(1500);
Assert(leaseResult.Call.Succeeded && leaseResult.Handle is not null,
    "disposable music lease was not created");
var lease = leaseResult.Handle ??
    throw new InvalidOperationException("disposable music lease was null");
lease.Dispose();
lease.Dispose();
Assert(fakeAudio.ReleaseCalls == 1,
    "managed music lease disposal was not idempotent");

Console.WriteLine(
    $"DLUMA managed SDK contract tests passed ({DlumaApiCatalog.Sha256}).");

sealed class FakeAudioApi : IAudioApi
{
    public int ReleaseCalls { get; private set; }

    public DlumaBoolResult RadioDialogPlay(DlumaRadioDialogRequest request) =>
        new(DlumaCallResult.Ok(), true);
    public DlumaTokenResult CuePlay(DlumaAudioCuePlayRequest request) =>
        new(DlumaCallResult.Ok(), 22);
    public DlumaBoolResult CueStop(DlumaAudioCueStopRequest request) =>
        new(DlumaCallResult.Ok(), true);
    public DlumaTokenResult BankOpen(DlumaAudioBankOpenRequest request) =>
        new(DlumaCallResult.Ok(), 11);
    public DlumaU64Result CueDuration(DlumaAudioCueRequest request) =>
        new(DlumaCallResult.Ok(), 1000);
    public DlumaAudioCueStateResult CueState(DlumaTokenRequest request) =>
        new(DlumaCallResult.Ok(), true, 0, 1000);
    public DlumaBoolResult CueSeek(DlumaAudioCueSeekRequest request) =>
        new(DlumaCallResult.Ok(), true);
    public DlumaBoolResult CueSetGain(DlumaAudioCueGainRequest request) =>
        new(DlumaCallResult.Ok(), true);
    public DlumaBoolResult CueSetFilter(DlumaAudioFilterRequest request) =>
        new(DlumaCallResult.Fail(
            DlumaStatus.Unavailable,
            new DlumaError("unsupported_capability", "Not ready.")), false);
    public DlumaTokenResult MusicAcquire(DlumaAudioMusicAcquireRequest request) =>
        new(DlumaCallResult.Ok(), 33);
    public DlumaAudioMusicStateResult MusicState(DlumaTokenRequest request) =>
        new(DlumaCallResult.Ok(), 2);
    public DlumaBoolResult MusicSetFilter(DlumaAudioFilterRequest request) =>
        new(DlumaCallResult.Fail(
            DlumaStatus.Unavailable,
            new DlumaError("unsupported_capability", "Not ready.")), false);
    public DlumaBoolResult MusicRelease(DlumaAudioMusicReleaseRequest request)
    {
        ++ReleaseCalls;
        return new(DlumaCallResult.Ok(), true);
    }
}
