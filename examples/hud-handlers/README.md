# HUD handler examples

Each example registers `hide-radar` and `restore-radar`. The first hides the
radar; the second removes that mod's visibility rule. Handler calls include a
JSON object and are delivered on the game thread. The examples log the request
or its phase so calls can be followed in the DLUMA log.

The examples use separate IDs: `example.hud.c`, `example.hud.cpp`,
`example.hud.csharp`, and `example.hud.lua`. Install one example and use its ID
in your quest or boss script. They each control their own rule, so a rule from
another active mod can also keep the radar hidden.

## Build and install

From an x64 Visual Studio Developer Command Prompt with Python and the .NET 10
SDK installed, run from this directory:

```bat
build.cmd
```

The script builds the two native DLLs and the managed assembly, stages all four
packages under `build/examples/hud-handlers/` at the repository root, then
creates and validates each package lock. Copy the chosen package folder to the
corresponding global mod directory:

| Example | Destination |
| --- | --- |
| C or C++ | `mods/native/global/` |
| C# | `mods/csharp/global/` |
| Lua | `data/scripts/lua/global/` |

Native and C# mods require approval and a restart. Enable the selected mod and
run a quest or encounter containing the matching actions. Availability depends
on the game build and HUD state; inspect the DLUMA log if an action fails.

## Author actions

The [quest fragment](quest.pxsl) and [boss fragment](boss.scr) target the Lua
example. Replace `example.hud.lua` with the chosen example ID. Insert the quest
lines in appropriate phases of an existing PXSL quest. Compile it and package
the resulting XML and adjacent `.dlumaq.json` together. Insert the boss
`CallMod` lines into the existing encounter and exit phases of a boss scenario.
Re-entering a phase can invoke its handler again.

Two installed mods may use the same handler name. Each call addresses the
specified mod independently:

```scr
CallMod("example.hud.lua", "hide-radar", "{}")
CallMod("example.hud.csharp", "hide-radar", "{}")
```

## Move an authored element

The direct XUI element operations in this section currently return
`unavailable`. The examples describe their API for a build that enables them.
HUD visibility controls remain available when their capability is ready.

For a unique element in a loaded XUI screen, a Lua handler can set its relative
position. Replace `ExampleMarker` with an ID or slash-separated path in your
own screen:

```lua
local ok, err = dluma.hud.set_position("ExampleMarker", 20, 40, 0, "HUD.xui")
if not ok then dluma.log("Move failed: " .. tostring(err and err.code)) end
```

The equivalent C# call is:

```csharp
var moved = context.Hud.SetPosition(new("HUD.xui", "ExampleMarker", new(20, 40, 0)));
if (!moved.Call.Succeeded) context.Core.Log(new("WARN", $"Move failed: {moved.Call.Status}"));
```

Position changes apply immediately in the screen's native relative coordinate
system. An animation or game update can change the position afterward. The
operation is gated by the current game build and returns an error if the
screen or element cannot be uniquely resolved.

The optional [XUI effect handler](xui-effect.lua) demonstrates showing a
panel, selecting a localization key, moving it, changing a view's color,
starting its authored `Shake` animation, and fading the view with a timer.
Matching project-owned source inputs are included:
[controls and Shake timeline](xui-effect-controls.xui) and
[English text entries](xui-effect-texts.scr). Merge the `EffectPanel` group,
including its nested timelines, into the scene in your project's existing
`HUD.xui`; merge the two `String` entries into its already-loaded locale catalog.
Do not replace your complete HUD or locale catalog with these small examples.
The canvas can be opened in an XUI authoring tool, but no document-loading API
is implied. The image uses the game's existing `white` texture name and the text
uses its `HUDNormalOutline` font; no game resources are distributed here.

Add the handler to your mod and configure the example's matching IDs and keys.
Color requires an `IUIElementView`, text a
supported `IUIText`, and animation an `IUIElementTimeLine`. The setters apply
immediately; a later animation or game update may change them. Set the example's
baseline position, color, and opacity to your own authored values. On completion
or cancellation the example hides the panel, attempts to restore those values,
and removes its visibility rule. A screen that has already retired cannot be
restored, and the example cannot stop an authored animation already running.
Visibility reset alone does not restore position, color, or opacity.
The text operation confirms submission, not that a key was found or displayed.
Animation identifiers must be printable ASCII and match the authored name. The
animation operation confirms acceptance, not that animation became visible.

For C#, the corresponding calls take the same screen and target selectors:

```csharp
context.Hud.SetTextKey(new("HUD.xui", "EffectPanel/Label", "DLUMA_Example_EffectOne"));
context.Hud.SetColor(new("HUD.xui", "EffectPanel/Visual", 1, 0, 0, 1));
context.Hud.SetOpacity(new("HUD.xui", "EffectPanel/Visual", 0.5f));
context.Hud.StartAnimation(new("HUD.xui", "EffectPanel", "Shake"));
```

## Request an infection flashback

The infection-flashback operation currently returns `unavailable`. On a build
that enables it, a project or map mod with `world.write` can request the game's
stock local-player flashback:

```lua
local ok, err = dluma.actions.infection_flashback()
if not ok then dluma.log("Flashback request failed: " .. tostring(err and err.code)) end
```

In C#, call `context.Actions.InfectionFlashback(new())`. An accepted request
does not guarantee that a flashback image was rendered. The operation is
available only when the current Player build and local player are supported.
If a request fails, it may already have reached the game; do not automatically
retry it or immediately trigger another effect.
