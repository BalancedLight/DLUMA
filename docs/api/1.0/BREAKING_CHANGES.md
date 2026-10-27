# Migrating to DLUMA API 1.0

DLUMA 0.5.0 replaces the earlier DLW names across the SDK, packages, and runtime. Rebuild owned mods together with the loader update.

| Previous name | DLUMA name |
| --- | --- |
| `dlw_api.h` | `dluma_api.h` |
| `dlw.hpp` | `dluma.hpp` |
| `Dlw*` types and `DLW_*` symbols | `Dluma*` types and `DLUMA_*` symbols |
| `DLWNativeMod_GetApiV2` | `DLUMANativeMod_GetApiV2` |
| `Dlw.Managed.*` assemblies | `Dluma.Managed.*` assemblies |
| `IDlwMod` | `IDlumaMod` |
| Lua global `dlw` | Lua global `dluma` |
| `dlw.*` service identifiers | `dluma.*` service identifiers |
| `mod.dlw.json` | `mod.dluma.json` |
| `mod.dlw.lock.json` | `mod.dluma.lock.json` |
| `config.dlw.json` | `config.dluma.json` |

Replace the SDK snapshot, update source imports and names, and update package metadata to the DLUMA schemas. Preserve your stable mod ID. Build the mod, stage its complete package, and regenerate the lock after all package content has changed.

The API remains version 1.0. Numeric capability and operation IDs and ABI record layouts are retained; the renamed native entry point and managed assembly identities require recompilation. Existing package approvals do not cover newly built files, so players review the new package and restart Player.

Older Lua `dlua` APIs and native ABI V1 mods need a source migration to the current package and lifecycle model. There is no automatic source converter. Start from a current example or creator template, move your behavior to the common API, and validate the resulting package.
