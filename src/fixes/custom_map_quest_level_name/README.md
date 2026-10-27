# Custom Map QUEST LevelName fix implementation

This folder owns the built-in native fix for the custom-map QUEST-tab crash.
It is registered through `src/fixes/FixRegistry.cpp`; loader startup and
shutdown only call the registry. The implementation does not expose memory
patching to Lua and does not write to a DLL file on disk.

## Affected condition

The crash requires all of the following: an active/displayable quest, the
QUEST tab being opened, the quest resolving to a custom map, and a nonempty
`LevelName(...)` in that map's startprop.

`CampaignManager::GetMapInfo` at RVA `0x002D2F70` saves a `LevelInfoDI*`,
destroys the tree that owns it, builds a replacement tree, and returns data
from the freed node. Destruction clears the quest-map-label pointer at
`LevelInfoDI + 0xB8` but leaves its length at `+0xC0` nonzero. The QUEST menu
later copies the stale-length string from a null pointer.

The in-memory patch changes the five bytes at RVA `0x002D2FC8` from:

```text
E8 93 05 00 00    call map-info tree destruction
```

to a calculated relative jump to RVA `0x002D2FE5`:

```text
E9 18 00 00 00    jmp over the second destroy/rebuild block
```

The selected `LevelInfoDI` consequently remains owned by the live tree.

## Content directives

`Map("my_custom_map")` must remain because it is the internal map declaration.
After this fix, `LevelName("My Custom Map")` can remain because the returned
`LevelInfoDI` and its label data remain live. Without the loader fix, omitting
`LevelName(...)` is only a fallback that produces a missing or blank label.
Changing localization strings does not repair the underlying lifetime bug.

## Safety boundary

Application requires a loaded, valid PE32+ image, the supported timestamp and
`SizeOfImage`, sufficient image bounds, validated neighboring instructions,
and either the exact original or already-patched bytes. The operation is
in-memory only, reversible, serialized, idempotent, and fail-open. Shutdown
restores bytes only when this loader instance applied them; external or
subsequently modified bytes are never overwritten.

Known examined binaries sharing the supported PE identity:

- Player SHA-256: `cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95`
- Developer Tools SHA-256: `8e19c89c316e319e1e88d0c7a6f6a935c4b9c381905cab48449cee8fd6ccba54`

