# DLUMA Lua IntelliSense

This VS Code extension provides catalog-backed completion, hover documentation,
signature help, snippets, JSON validation, and access diagnostics for the Lua
surface of **DLUMA API 1.0**.

The extension reads the same generated API metadata used by the C, C++, C#,
Workbench, and public-documentation surfaces. Every catalog operation shows its
implementation status, capability, valid scopes, thread affinity, and required
`mod.dluma.json` access declaration. The embedded catalog version and hash are
checked by `python tools/generate_api.py --check` in the main repository.

## Package layout

Lua mods are package directories under the established scope roots:

```text
data/scripts/lua/global/<mod-id>/
data/scripts/lua/project/<mod-id>/
data/scripts/lua/maps/<map-name>/<mod-id>/
```

Each directory contains `mod.dluma.json`, `mod.dluma.lock.json`, and its entry
script. Metadata and access declarations belong in the JSON manifest. The Lua
entry starts with no inline metadata:

```lua
local mod = dluma.mod()

mod:on_load(function(ctx)
    dluma.log("info", "loaded")
end)

return mod
```

The extension validates every `mod.dluma.json` against the checked generated
schema. For a Lua file, it searches the file's directory and ancestors for the
nearest manifest and reports calls whose required access is missing.

## Completion and snippets

Type `dluma.` for root namespaces, including `dluma.world`, `dluma.player`,
`dluma.events.sensor`, `dluma.configuration`, `dluma.storage`, `dluma.ui`,
`dluma.companion_content`, `dluma.audio.bank`, `dluma.audio.cue`,
`dluma.audio.music`, `dluma.actions`, and `dluma.quests`. Type `mod:` for lifecycle and
event subscriptions, including `mod:handle_quest_phase` and
`mod:observe_quest_phase`. Their callbacks receive authored-phase details and decoded JSON arguments. Opaque object and
player handles also receive method completion after `:`.

Included snippets are:

- `dlumamod` — package entry object;
- `dlumasensorenter` — sensor-enter subscription;
- `dlumafind` — exact world lookup;
- `dlumaquest` — authored quest phase transition;
- `dlumaactionflow` — sensor-to-ActionChain flow.

`implemented` operations are available through the common runtime. `gated`
operations remain visible and return the same structured unavailable result in
all languages when an exact-build binding is not ready. `planned` operations
are visible but unavailable. `removed` operations exist only to provide a clear
DLUMA API 1.0 diagnostic.

The shared vehicle/audio surface includes `dluma.player.vehicle_state()`, bounded
binary `dluma.companion_content.read_entry()`, manifest-approved bank/cue handles,
and one process-wide exclusive music lease. Wave and music filters remain
visible but capability-gated; they return structured unavailable errors until
the required runtime support is available.

## Breaking change from Lua API 2.1

`dlua` and inline `dlua.mod { ... }` manifests are unsupported. Use `dluma`,
`dluma.mod()`, and the shared external manifest. The extension reports legacy
`dlua` usage rather than suggesting a compatibility shim or automatic source
conversion.

## Development check

From this extension directory, run:

```text
npm run check
```

Before shipping, also open an Extension Development Host and manually verify
root/nested completion, hover text, signature help, manifest JSON validation,
and missing-access diagnostics.
