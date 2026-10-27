# Custom-map QUEST `LevelName(...)` crash

## Affected condition

The Windows crash occurs when a displayable quest is active, the QUEST tab is
opened, that quest resolves to a custom map, and the map has a nonempty
`LevelName(...)` directive.

The required internal declaration is different and must remain:

```scr
Map("my_custom_map")
```

## Root cause

`CampaignManager::GetMapInfo` (function RVA `0x002D2F70`) builds a map-info
tree, finds and saves the requested `LevelInfoDI*`, destroys the owning tree,
builds a replacement tree, and then returns data calculated from the freed
node. The freed object's quest-map-label pointer is cleared while its length
remains nonzero. QUEST-menu code later reaches `memcpy(destination, nullptr,
stale_length + 1)`.

Changing a localization string cannot repair this ownership and lifetime bug.

## Native fix

The loader patches RVA `0x002D2FC8` in memory. The original instruction is:

```text
E8 93 05 00 00    call the second map-info tree destruction
```

It is replaced with a relative jump to RVA `0x002D2FE5`:

```text
E9 18 00 00 00    jump over the second destruction/rebuild block
```

The displacement is calculated from the loaded image addresses. The patch
keeps the first map-info tree alive, so the returned `LevelInfoDI` remains
valid and `LevelName("My Custom Map")` can safely remain in user content.

The fix does not patch `memcpy`, suppress access violations, rewrite
startprops, or blank the map name. Without the loader, omitting
`LevelName(...)` remains a fallback, but produces a missing/blank display
label rather than addressing the engine bug.

## Build support and safety

The currently supported PE record is:

```text
TimeDateStamp: 0x623397AA
SizeOfImage:   0x02082000
```

Both examined Windows DLLs share that PE identity and instruction context:

```text
Player SHA-256:
cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95

Developer Tools SHA-256:
8e19c89c316e319e1e88d0c7a6f6a935c4b9c381905cab48449cee8fd6ccba54
```

The loader additionally validates the loaded PE headers, image bounds,
neighboring instructions, and exact target bytes. Unknown builds and byte
mismatches are skipped without failing other loader systems. The patch is
in-memory only and reversible. Shutdown restores only a patch owned by this
loader instance, and refuses to overwrite bytes changed by another component.

The compatibility switch is enabled by default:

```ini
[GameFixes]
CustomMapQuestLevelNameCrash=1
```

Workshop Lua cannot enable, disable, replace, or directly apply native fixes.

