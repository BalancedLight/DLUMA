using Dluma.Managed.Sdk;

namespace HudHandlers;

public sealed class Mod : IDlumaMod
{
    private IModContext? _context;

    public void OnStart(IModContext context)
    {
        _context = context;
        context.ModHandlers.Invoked += OnInvoked;
        var hide = context.ModHandlers.Register(new("hide-radar", 1));
        var restore = context.ModHandlers.Register(new("restore-radar", 2));
        if (!hide.Call.Succeeded || !restore.Call.Succeeded)
            throw new InvalidOperationException("Could not register HUD handlers.");
    }

    private void OnInvoked(object? sender, DlumaModHandlerEventArgs value)
    {
        var context = _context;
        if (context is null) return;
        _ = context.Core.Log(new("INFO", $"HUD handler source={value.SourceKind} map={value.MapGeneration} provider={value.Provider} handler={value.Handler} quest={value.QuestId} phase={value.PhaseId} arguments={value.ArgumentsJson}"));
        var request = new DlumaHudVisibilityRequest("HUD.xui", "radar");
        var result = value.CallbackId switch
        {
            1 => context.Hud.Hide(request),
            2 => context.Hud.Reset(request),
            _ => default
        };
        if (!result.Call.Succeeded)
            _ = context.Core.Log(new("WARN", $"HUD action failed: {result.Call.Status}"));
    }

    public void OnStop(DlumaStopReason reason)
    {
        if (_context is not null) _context.ModHandlers.Invoked -= OnInvoked;
        _context = null;
    }
}
