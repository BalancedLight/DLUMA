# Workbench application

`Dluma.Workbench` is the repository's WPF creator application. It locates the
repository from its generated API catalog, creates mod projects under a
user-selected directory, and stages packages only to a user-selected target.

No Steam library, Workshop item, game executable, or runtime log path is
compiled into the application. `Open logs` first checks
`<stage destination>\dluma_runtime\dluma.log` and otherwise asks
for the log. `Launch Player` asks for `DyingLightPlayer.exe` before its explicit
live-run confirmation.

For repeat local use, these optional environment variables may be set outside
the repository:

```text
DLUMA_PLAYER_PATH=<absolute path to DyingLightPlayer.exe>
DLUMA_RUNTIME_LOG=<absolute path to dluma.log>
```

Build and test from the repository root:

```powershell
dotnet build workbench\Dluma.Workbench.slnx -c Release
dotnet test workbench\Dluma.Workbench.slnx -c Release
```
