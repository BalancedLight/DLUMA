using Dluma.Managed.Sdk;

namespace ManagedModFixture;

public sealed class FixtureMod : IDlumaMod
{
    private IModContext? _context;

    public void OnStart(IModContext context)
    {
        _context = context;
        context.Events.Updated += OnUpdated;
        _ = context.Core.Log(
            new DlumaLogRequest("INFO", "fixture:start"));
    }

    public void OnStop(DlumaStopReason reason)
    {
        var context = _context ??
            throw new InvalidOperationException(
                "Fixture stopped without a context.");
        context.Events.Updated -= OnUpdated;
        _ = context.Core.Log(
            new DlumaLogRequest("INFO", $"fixture:stop:{reason}"));
        _context = null;
    }

    private static void OnUpdated(
        object? sender,
        DlumaUpdateEventArgs value)
    {
        var context = (IModContext?)sender ??
            throw new InvalidOperationException(
                "Update sender was not the mod context.");
        _ = context.Core.Log(
            new DlumaLogRequest(
                "INFO",
                $"fixture:update:{value.DeltaSeconds:F3}"));
    }
}
