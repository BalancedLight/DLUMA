using Dluma.Managed.Sdk;

namespace HudAssets;

public sealed class Mod : IDlumaMod
{
    private IModContext? context;
    private ulong token, timer;
    private bool document, restoring;
    private int ticks;

    public void OnStart(IModContext value)
    {
        context = value;
        value.ModHandlers.Invoked += OnHandler;
        value.Core.TimerFired += OnTimer;
        if (!value.ModHandlers.Register(new("swap-document", 1)).Call.Succeeded ||
            !value.ModHandlers.Register(new("swap-flashbacks", 2)).Call.Succeeded ||
            !value.ModHandlers.Register(new("restore-assets", 4)).Call.Succeeded)
            throw new InvalidOperationException("Could not register asset handlers.");
    }

    private void Log(string message) => context?.Core.Log(new("INFO", "HUD assets: " + message));
    private void OnHandler(object? sender, DlumaModHandlerEventArgs e)
    {
        if (context is null) return;
        if (e.CallbackId == 4) {
            if (token == 0) { Log("no active request"); return; }
            if (RequestRestore()) StartPolling();
            return;
        }
        if (token != 0 || (e.CallbackId != 1 && e.CallbackId != 2)) return;
        document = e.CallbackId == 1;
        DlumaTokenResult result;
        if (document) result = context.Hud.SwapDocument(new("menu/hud/original.xui", "menu/hud/custom.xui"));
        else {
            var originals = Enumerable.Range(0, 11).Select(i => $"hud_bossfight_flash_{i}").ToArray();
            var replacements = Enumerable.Range(0, 11).Select(i => $"example_flash_{i}").ToArray();
            result = context.Hud.SwapFlashbackTextures(new(originals, replacements, 11));
        }
        if (!result.Call.Succeeded) { Log($"request failed: {result.Call.Status}"); return; }
        token = result.Token; restoring = false; ticks = 0; Log($"request token={token}");
        StartPolling();
        if (timer == 0) RequestRestore();
    }
    private void StartPolling()
    {
        if (context is null || token == 0 || timer != 0) return;
        ticks = 0;
        var scheduled = context.Core.TimerEvery(new(0.25, 3));
        if (!scheduled.Call.Succeeded) { Log("could not schedule status checks; use restore-assets to try again"); return; }
        timer = scheduled.Token;
    }
    private bool RequestRestore()
    {
        if (context is null || token == 0) return false;
        if (restoring) return true;
        var result = document ? context.Hud.RestoreDocument(new(token)) : context.Hud.RestoreFlashbackTextures(new(token));
        if (!result.Call.Succeeded || !result.Value) {
            Log($"restore request failed: {result.Call.Status}");
            StopPolling();
            return false;
        }
        restoring = true; ticks = 0;
        return true;
    }
    private void OnTimer(object? sender, DlumaTimerEventArgs e)
    {
        if (context is null || e.CallbackId != 3 || e.Token != timer || token == 0) return;
        uint state, reason;
        if (document) {
            var r = context.Hud.DocumentStatus(new(token));
            if (!r.Call.Succeeded) { Log($"status failed: {r.Call.Status}"); StopPolling(); return; }
            state = r.State; reason = r.Reason;
        } else {
            var r = context.Hud.FlashbackTextureStatus(new(token));
            if (!r.Call.Succeeded) { Log($"status failed: {r.Call.Status}"); StopPolling(); return; }
            state = r.State; reason = r.Reason;
        }
        Log($"state={state} reason={reason}");
        bool applied = document ? state == 4 : state == 3;
        bool restored = document ? state == 6 : state == 5;
        bool failed = document ? state is 7 or 8 : state == 6;
        if (restored) { Clear(); return; }
        if (failed) { Log("failed or requires recovery; inspect reason, then use restore-assets"); StopPolling(); return; }
        if (applied && !restoring) { RequestRestore(); return; }
        if (++ticks >= 120) {
            Log("status check timed out; inspect the request, then use restore-assets");
            StopPolling();
        }
    }
    private void StopPolling()
    {
        if (context is not null && timer != 0) _ = context.Core.TokenCancel(new(timer));
        timer = 0;
    }
    private void Clear()
    {
        StopPolling();
        token = 0; restoring = false;
    }
    public void OnStop(DlumaStopReason reason)
    {
        if (context is null) return;
        RequestRestore(); StopPolling();
        context.ModHandlers.Invoked -= OnHandler;
        context.Core.TimerFired -= OnTimer;
        context = null;
    }
}
