# HUD asset request examples

These Lua and C# project-scoped examples register `swap-document` and
`swap-flashbacks` handlers under different stable mod IDs. Each starts one
request, checks its state on a timer, requests restoration once applied, and
continues checking until restoration is confirmed or an error or timeout needs
attention. Polling stops on failure, recovery-required status, status-query
failure, or after 120 checks (30 seconds); the request token remains owned by
the example. A texture request can stay ready until the boss uses its flashback
image. A restore can stay pending while that image is idle. Neither condition
means the request failed or that pixels changed.

The examples require `ui.overlay` and `quest.phase.handle`. With Python and the
.NET 10 SDK installed, run `examples\hud-assets\build.cmd` from a Windows
command prompt. It stages and validates both locked packages under
`build/examples/hud-assets/`. Copy the Lua package into the project's
`data/scripts/lua/project/` folder, or the C# package into its
`mods/csharp/project/` folder. Enable and approve the chosen mod as required.

Before running, supply two **complete, compatible** project HUD documents at
`data/menu/hud/original.xui` and `data/menu/hud/custom.xui`, and register
`example_flash_0` through `example_flash_10` in your project's texture
definitions. These are author-provided inputs; the example does not include or
replace a game's HUD, textures, or locale catalog. The eleven stock source
aliases are `hud_bossfight_flash_0` through `_10`. Update the paths and aliases
in the example to match your own authored inputs. If an input is absent or the
capability is unavailable, the example logs the error and leaves the result
unclaimed.

Use `CallMod("example.hud.assets.lua", "swap-document", "{}")` or
`CallMod("example.hud.assets.lua", "swap-flashbacks", "{}")` from a
compatible boss scenario. For the C# version use the stable ID
`example.hud.assets.csharp`. Only one request is run at a time by each sample.
After inspecting the logged state and reason, call `CallMod` with the same mod
ID and the `restore-assets` handler to request restoration and resume status
checks. If restoration was already accepted, the handler only resumes checks;
it does not send a duplicate restore request. A failed restore call retains the
token for another explicit attempt. A new swap is blocked until document state
6 (restored) or texture state 5 (stopped) is observed.

The examples request cleanup on unload but cannot guarantee that a retiring
screen remains available or that restoration completed. Do not automatically
retry an uncertain application or restoration.

Document and flashback texture capabilities are currently unavailable in
ordinary builds. The document backend is not yet qualified for use. The texture
operation targets the boss flashback image binding, not the entire HUD or
arbitrary UI, world, or character textures. The samples describe the API
contracts and require an available compatible backend. See the
[HUD API guide](../../docs/api/1.0/HUD_ACTIONS.md) for states and reasons.
