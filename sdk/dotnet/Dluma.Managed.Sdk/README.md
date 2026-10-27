# Dluma.Managed.Sdk

This project contains the generated and handwritten managed façade for
**DLUMA API 1.0**.

Managed mods implement:

```csharp
public interface IDlumaMod
{
    void OnStart(IModContext context);
    void OnStop(DlumaStopReason reason);
}
```

The API is backed by the same generated domain service tables used by Lua and
C/C++. A managed mod is still full-trust code: manifest access declarations
are enforced by DLUMA services, but ordinary .NET APIs can bypass the SDK.

`Dluma.Generated.g.cs` is checked output from `api/catalog`. Regenerate it with:

```powershell
python tools\generate_api.py --write
```
