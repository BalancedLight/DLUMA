# DLUMA API 1.0

DLUMA API 1.0 is the common contract used by Lua, C#, C, and C++ mods. Its canonical source is `api/catalog/`; generated SDKs and metadata carry the catalog version and hash.

Use the [API reference](API_REFERENCE.md) for operations and the [capability matrix](CAPABILITY_MATRIX.md) for implementation status. All languages share ownership, scope, access declarations, threading rules, handle lifetimes, quotas, and structured results. An operation whose game adapter is unavailable returns `unsupported_capability`.

## Entry points

| Language | Entry point |
| --- | --- |
| Lua | `local mod = dluma.mod()` |
| C# | Implement `IDlumaMod.OnStart` and `IDlumaMod.OnStop` |
| C and C++ | Export `DLUMANativeMod_GetApiV2` |

Native, then C#, then Lua activate in that order. Shutdown reverses it. Lua is sandboxed; native and C# code have full process access and require player approval that takes effect on the next launch.

## Packages

A mod directory contains `mod.dluma.json` and `mod.dluma.lock.json`. Configurable mods also provide `config.dluma.json`.

- Lua: `data/scripts/lua/<scope>/<mod-id>/`
- C#: `mods/csharp/<scope>/<mod-id>/`
- Native: `mods/native/<scope>/<mod-id>/`

Map packages insert `maps/<map-name>` after the language root. Folder-derived language, scope, map, and mod ID must match the manifest. The lock records package files by relative path, size, and SHA-256.

Approval is evaluated against the complete selected package, including its configuration schema and companion content. Editable player preferences are stored separately from the package schema.

See [Creating a DLUMA mod](../../MODDING.md) for examples and packaging commands, or [migration](BREAKING_CHANGES.md) for older mods.

## Configuration key bindings

Configuration schemas can declare `type: "keybind"` with an integer default Windows virtual-key code from 1 through 254. The configuration screen displays a key name and captures a replacement when the player edits the binding.

Mods receive an integer from `configuration.get`. Key bindings use the same saved-value format as integer settings; bounds and step are implicit.
