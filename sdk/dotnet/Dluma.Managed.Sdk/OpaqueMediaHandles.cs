#nullable enable

namespace Dluma.Managed.Sdk;

/// <summary>A common managed result that returns an opaque disposable handle.</summary>
public readonly record struct DlumaHandleResult<T>(DlumaCallResult Call, T? Handle)
    where T : class, IDisposable;

/// <summary>Locally disposable view of one owner-bound companion token.</summary>
public sealed class DlumaCompanionHandle : IDisposable
{
    private readonly IModContext _context;
    private bool _disposed;

    internal DlumaCompanionHandle(IModContext context, ulong token)
    {
        _context = context;
        Token = token;
    }

    public ulong Token { get; }

    public DlumaCompanionEntryReadResult ReadEntry(string entryPath)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _context.CompanionContent.EntryRead(
            new DlumaCompanionEntryReadRequest(Token, entryPath));
    }

    public DlumaHandleResult<DlumaAudioBankHandle> OpenBank(string bankId)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var result = _context.Audio.BankOpen(
            new DlumaAudioBankOpenRequest(Token, bankId));
        return new(
            result.Call,
            result.Call.Succeeded && result.Token != 0
                ? new DlumaAudioBankHandle(_context.Audio, result.Token)
                : null);
    }

    public void Dispose() => _disposed = true;
}

/// <summary>Locally disposable view of one owner-bound approved CSB bank.</summary>
public sealed class DlumaAudioBankHandle : IDisposable
{
    private readonly IAudioApi _audio;
    private bool _disposed;

    internal DlumaAudioBankHandle(IAudioApi audio, ulong token)
    {
        _audio = audio;
        Token = token;
    }

    public ulong Token { get; }

    public DlumaU64Result CueDuration(string cue)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.CueDuration(new DlumaAudioCueRequest(Token, cue));
    }

    public DlumaHandleResult<DlumaAudioCueHandle> Play(
        string cue,
        ulong startMs = 0,
        float initialGain = 1.0f,
        bool loop = false,
        uint fadeInMs = 0,
        uint fadeOutMs = 0)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var result = _audio.CuePlay(
            new DlumaAudioCuePlayRequest(
                Token, cue, startMs, initialGain, loop, fadeInMs, fadeOutMs));
        return new(
            result.Call,
            result.Call.Succeeded && result.Token != 0
                ? new DlumaAudioCueHandle(_audio, result.Token)
                : null);
    }

    public void Dispose() => _disposed = true;
}

/// <summary>Disposable owner-bound cue; disposal performs an idempotent stop.</summary>
public sealed class DlumaAudioCueHandle : IDisposable
{
    private readonly IAudioApi _audio;
    private bool _disposed;

    internal DlumaAudioCueHandle(IAudioApi audio, ulong token)
    {
        _audio = audio;
        Token = token;
    }

    public ulong Token { get; }

    public DlumaAudioCueStateResult State()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.CueState(new DlumaTokenRequest(Token));
    }

    public DlumaBoolResult Seek(ulong positionMs)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.CueSeek(new DlumaAudioCueSeekRequest(Token, positionMs));
    }

    public DlumaBoolResult SetGain(float gain)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.CueSetGain(new DlumaAudioCueGainRequest(Token, gain));
    }

    public DlumaBoolResult SetFilter(float wetMix, float cutoffHz)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.CueSetFilter(
            new DlumaAudioFilterRequest(Token, wetMix, cutoffHz));
    }

    public DlumaBoolResult Stop(uint fadeOutMs = 0)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var result = _audio.CueStop(new DlumaAudioCueStopRequest(Token, fadeOutMs));
        if (result.Call.Succeeded) _disposed = true;
        return result;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _ = _audio.CueStop(new DlumaAudioCueStopRequest(Token, 0));
        _disposed = true;
    }
}

/// <summary>Disposable process-exclusive stock-music lease.</summary>
public sealed class DlumaMusicLease : IDisposable
{
    private readonly IAudioApi _audio;
    private bool _disposed;

    internal DlumaMusicLease(IAudioApi audio, ulong token)
    {
        _audio = audio;
        Token = token;
    }

    public ulong Token { get; }

    public DlumaAudioMusicStateResult State()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.MusicState(new DlumaTokenRequest(Token));
    }

    public DlumaBoolResult SetFilter(float wetMix, float cutoffHz)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _audio.MusicSetFilter(
            new DlumaAudioFilterRequest(Token, wetMix, cutoffHz));
    }

    public DlumaBoolResult Release(uint resumeDelayMs = 0)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var result = _audio.MusicRelease(
            new DlumaAudioMusicReleaseRequest(Token, resumeDelayMs));
        if (result.Call.Succeeded) _disposed = true;
        return result;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _ = _audio.MusicRelease(new DlumaAudioMusicReleaseRequest(Token, 0));
        _disposed = true;
    }
}

/// <summary>Idiomatic disposable-handle helpers over the generated contracts.</summary>
public static class DlumaMediaHandleExtensions
{
    public static DlumaHandleResult<DlumaCompanionHandle> OpenHandle(
        this ICompanionContentApi companionContent,
        IModContext context,
        string relativePath)
    {
        var result = companionContent.Open(new DlumaCompanionOpenRequest(relativePath));
        return new(
            result.Call,
            result.Call.Succeeded && result.Token != 0
                ? new DlumaCompanionHandle(context, result.Token)
                : null);
    }

    public static DlumaHandleResult<DlumaMusicLease> AcquireMusic(
        this IAudioApi audio,
        uint fadeOutMs = 0)
    {
        var result = audio.MusicAcquire(new DlumaAudioMusicAcquireRequest(fadeOutMs));
        return new(
            result.Call,
            result.Call.Succeeded && result.Token != 0
                ? new DlumaMusicLease(audio, result.Token)
                : null);
    }
}
