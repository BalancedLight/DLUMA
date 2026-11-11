# HUD visibility and mod handlers

HUD calls run on the game thread and require `ui.overlay`. Handler registration
requires `quest.phase.handle`; invoking another mod through the API requires
`world.write`. Check `hud.visibility` and `actions.mod_handlers` capabilities.

Direct XUI position, opacity, color, text-key, and animation operations are
currently unavailable. Check their individual capabilities before calling
them; visibility and handler availability are independent.

For these five operations, an internal error is reported as
`xui_result_uncertain`: the UI may already have changed, so do not automatically
retry. An unavailable result before native entry remains retryable. Successful
submission does not preserve an override against later game or animation updates.
Position, opacity, and color calls write properties directly. `Reset` removes a
visibility rule; scripts must explicitly restore any property values they want
to undo before stopping an effect.

Position writes are instantaneous native relative-position changes; they do
not interpolate or add easing. The current experimental color binding requires
an `IUIElementView` target, such as an image or text view. A rectangle that does
not expose that interface is rejected; this does not establish that every XUI
type supports the same properties.

## HUD targets

`health`, `stamina`, `radar`, `weapons`, `crosshair`, and `reload` select stock HUD
groups. An authored XUI string ID selects one control in the specified screen.
Use `/` between ancestor IDs to disambiguate nested controls. The screen defaults
to `HUD.xui`; pass a screen filename separately. Duplicate IDs return an error.

Show and Hide remain active until Reset, mod cleanup, or a map change. Hide wins
when multiple mods override a control. Reset removes the caller's rule and lets
remaining rules or the game's latest requested visibility take effect. Showing a
child does not override its parent's visibility.

Lua:

```lua
dluma.hud.hide("radar")
dluma.hud.show("HudCrosshair", "HUD.xui")
dluma.hud.reset("radar")
```

C# uses `context.Hud.Hide(new DlumaHudVisibilityRequest("HUD.xui", "radar"))`,
with equivalent `Show` and `Reset` methods. C uses `DlumaHudV1.hide`, `show`, and
`reset`; C++ uses `context.hud().hide`, `show`, and `reset`. Native requests carry
`struct_size`, `struct_version = 1`, `screen`, and `target`; initialize result
record headers before calling, as with the existing SDK services.

## HUD documents and flashback textures

Document swaps and flashback texture replacement are gated by exact-build
capabilities `hud.document` and `hud.flashback_textures`. Check availability
before use. Both capabilities are currently unavailable in ordinary builds;
the document backend is not yet qualified for use. These operations apply only to the current project or map and need
`ui.overlay` permission. A returned token acknowledges a request; it does not
mean the change is visible or that restoration has completed.
The service may remain available to report or clean up an existing request
while the current screen is not ready for a new one; always check the result
of each begin call as well as the capability.

`swap_document(original, replacement)` accepts two project-relative paths under
the project's `data` directory. Both must be complete, compatible authored HUD
documents. The logical screen remains `HUD.xui`. Use `document_status(token)`
to inspect the request, and `restore_document(token)` to request restoration.
`cancel_document(token)` cancels a request that has not applied, or requests
restoration if application has begun. C# uses `SwapDocument`, `DocumentStatus`,
`RestoreDocument`, and `CancelDocument` on `context.Hud`.

Document status values are 1 preparing, 2 ready, 3 applying, 4 applied,
5 restoring, 6 restored, 7 failed, 8 recovery required, and 9 release pending.
An error or recovery-required state needs explicit handling; do not assume the
HUD was restored or automatically retry an uncertain native operation.

`swap_flashback_textures(pairs)` takes exactly eleven ordered tables with
`original` and `replacement` registered texture alias names. This API applies
to the boss flashback image selection path and remaps its image binding; it
does not replace the whole HUD or arbitrary UI or world textures. Use
`flashback_texture_status(token)` and `restore_flashback_textures(token)` to
observe and request cleanup. Texture states are 1 preparing, 2 ready, 3 active,
4 restoring, 5 stopped, and 6 failed. Ready means resources were prepared; it
does not establish that any pixels changed. C# uses
`SwapFlashbackTextures`, `FlashbackTextureStatus`, and
`RestoreFlashbackTextures` with two ordered lists and a count of 11.

The ordered originals are `hud_bossfight_flash_0` through
`hud_bossfight_flash_10`, defined in `data/menu/texturedefs/hudtextures.def`.
Register and compile replacement assets before requesting a swap. The backend
requires usable, fully resident backing whose dimensions match its definition;
an alias or raw source DDS alone is insufficient. Missing or partially prepared
assets must be handled through status and explicit cleanup.

General UI-image replacement and world/character-material replacement are
separate capabilities from this flashback helper. Neither general API is
currently available; do not use the flashback operation for those targets.

All tokens belong to the requesting mod and its current scope. Handle a failed
or unavailable call before using its result; neither an acknowledgment nor a
status query automatically retries a native operation. Preserve the token until
restoration is confirmed. A failed or timed-out status check does not prove that
the operation was undone; stop polling and inspect the reason before explicitly
requesting recovery.

See the [Lua and C# examples](../../../examples/hud-assets/README.md) for a
scoped restore handler, the [API reference](API_REFERENCE.md) for permissions
and availability, and the [HUD catalog](../../../api/catalog/hud.json) and
[managed SDK](../../../sdk/dotnet/Dluma.Managed.Sdk/) for generated contracts.

## Shared handlers

A handler is addressed by the owning mod's stable ID and a handler ID. Register
it when the mod activates and register it again after scope cleanup. The same
handler ID may be used by different mods: calls target the specified mod ID.
Arguments
are a JSON object. Each action triggers once per activation; re-entering a phase
can trigger it again. Calls do not wait for asynchronous work.

For a Lua mod whose manifest ID is `example.hud`:

```lua
local mod = dluma.mod()
mod:handle("hide-radar", function(_, event)
    dluma.hud.hide("radar")
end)
mod:handle("restore-radar", function(_, event)
    dluma.hud.reset("radar")
end)
```

Author the quest in PXSL:

```text
,call-mod <<hide_radar>> -mod=<<example.hud>> -handler=<<hide-radar>> -arguments=<<{}>> -wait=0
,call-mod <<restore_radar>> -mod=<<example.hud>> -handler=<<restore-radar>> -arguments=<<{}>> -wait=0
```

The compiler produces ordinary quest XML and an adjacent `.dlumaq.json` binding
file. Package both. Keep the binding file beside its matching compiled XML;
recompile after editing PXSL. Existing `call-lua`, `call-native`, and registered
quest aliases continue to work.

C and C++ register a callback ID through `DlumaModHandlersV1.register_handler`
and receive `DlumaModHandlerEventV1` through the optional `mod_handler_event`
export. C# registers through `context.ModHandlers.Register` and receives
`context.ModHandlers.Invoked`. Match the callback ID to the registered handler.
Events include provider, handler, arguments JSON, map generation, source kind,
phase ID, and quest/scenario ID (`quest_id` for both source kinds).

Boss scenario phase bodies use the same provider and handler IDs:

```text
Phase("Encounter")
{
    CallMod("example.hud", "hide-radar", "{}");
}
```

Add a matching `restore-radar` action to the encounter's exit phase. See
[Boss phase actions](../BOSS_PHASE_ACTIONS.md) for native lifecycle details.

Lua can invoke a handler directly:

```lua
dluma.mod_handlers.invoke("example.hud", "hide-radar", "{}")
```

HUD and native scenario capabilities depend on the loaded game build. Inspect
returned statuses before using results. Missing handlers and callback errors
are logged without holding quest or boss progression.
