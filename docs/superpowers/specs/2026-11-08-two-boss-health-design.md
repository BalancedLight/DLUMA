# Two-boss scenario health and optional HUD — proposed design

**Status:** Proposal for review. No product code or runtime test has been performed. This design distinguishes behavior already supported by DL1 from an optional DLUMA enhancement.

## Intent and host

Allow authors of **Dying Light 1** custom-map encounters to track two simultaneously active boss AIs, make stage decisions at independently chosen health thresholds, and optionally show **two simultaneous boss health bars** in Developer Tools Player. Preserve vanilla game-script semantics where they are already adequate. Do not assume released Editor behavior carries into the retail Game. Initial patch scope is **Windows x64 Developer Tools Player** on explicitly attested modules/builds; retail Game is excluded.

## Findings that constrain the design

1. `BossFightBinding` has an editable `m_Tag` and vector `m_GameObjects` referencing game objects. `BossFightBinding::GetAIs` enumerates `AIBaseSpawnBox` objects in that vector and collects their AIs. Windows RTTI field registration corroborates the data shape, while the named macOS 1.49 decompile supplies semantic call names. Evidence: `DyingLightDebug.zip :: DyingLightDebug/windows - no names/Dev Tools RE/Dying Light Editor/gamedll_x64_rwdi.dll.c` anchors `m_GameObjects`, `m_Tag`; `DyingLightDebug/libgamedll.dylib.NAMED.c` anchor `BossFightBinding::GetAIs`.
2. The named macOS `PhaseTransition::Execute` evaluates the current/max health ratio for **every** AI returned by a named binding and blocks its `LifeBelow(tag, threshold)` transition if **any** exceeds that threshold. `PhaseTransition::LoadConditions`/the Windows `LifeBelow` parser store **one binding and one threshold per transition**. This supports shared-group thresholds, not two independent `LifeBelow` entries in one `Conditions()` block. Evidence: same decompilations, anchors `PhaseTransition::Execute`, `LifeBelow`, Windows `sub_180A9CE90`.
3. Shipped DLC17 `data/scripts/boss_fight_mother.scr` demonstrates multiple `PhaseTransition()` blocks, each with one `LifeBelow(\"boss\", ...)`; `boss_fight_minions.scr` sets `SetMaxSpawned(\"bossfight_compound_spawn_02\", 2)`. Its compiled quest XML has `type=\"boss fight\"` and `script_name`. Evidence: `DL - The Following.zip :: DataDLC17_0.pak :: data/scripts/boss_fight_mother.scr`, `data/scripts/boss_fight_minions.scr`, `data/quests/wasteland_final/wasteland_final_finalboss_quest.xml`. DLC17 is a pattern, **not proof** that its unique scripts/objects load in a base-game-only map.
4. The retail base `data/menu/hud/hud.xui` defines one `HudBossHpBar`. Named macOS `HealthBarTarget::Execute` targets the first AI from a binding and updates the singular HUD manager bar. This suggests a **single stock presentation target** even though the transition logic can examine multiple AIs. A current Windows Player test is still required. Evidence: `DL Base Game Paks.zip :: Data0.pak :: data/menu/hud/hud.xui`; named macOS `HealthBarTarget::Execute`, `HudBossHpBar::StartBossFight`.
5. Shipped `data/editor/scripts/assets_object_types.scr` references `BossFightBinding` and `data/defaults/objecttypes/bossfightbinding.df2`, but that preset file was **not found** in the provided base/DevTools archives. Do **not** claim a verified click-through placement path until the Editor is tested or a released preset is recovered.

## Approaches and recommendation

- **A — vanilla shared binding:** put both AI spawnboxes into one `BossFightBinding`, use one `LifeBelow(\"both\", P)` for an all-bosses-below-P gate. Lowest risk, one shared threshold.
- **B — vanilla separate bindings and state-machine branches (recommended for quest rules):** one binding per boss; in a phase, use **separate** `PhaseTransition()` blocks for `LifeBelow(\"boss_a\", PA)` and `LifeBelow(\"boss_b\", PB)`. Transition to an A-met or B-met phase; each waits for the remaining boss before completing. This handles distinct thresholds without a native patch. It does not promise a continuously queryable pair of health values to arbitrary mods.
- **C — optional DLUMA two-bar overlay (conditional enhancement):** only if a live Developer Tools Player test confirms two native bars cannot appear simultaneously. Sample both AIs read-only and display an owner/map-scoped DLUMA HUD surface; **do not replace the vanilla quest parser or write to AI health**. Keep stock bar unchanged until suppression is separately verified.

Decision: implement **B** in authored content first. Only build **C** if the author truly needs two visible bars, live sampling, or the supported native tests disprove the vanilla approach. Do **not** patch the scenario engine merely to add two health checks.

## Goals and acceptance

- Independently configurable A/B threshold values, and either kill order, lead to exactly one correct final progression.
- Both must **actually spawn/be tracked** before a zero-AI result can count as meeting a health condition. Empty/missing bindings must not silently finish an encounter.
- If the optional overlay is pursued: two labeled bars with their own percent health, correct updates under simultaneous damage, death/despawn/respawn, and no false cross-target handoffs.
- No unknown-build writes, no unvalidated native calls, no silent enablement in the Editor or retail Game.
- Map unload, mod disable, owner change, and Player shutdown remove subscriptions and UI without stale pointers or persistent suppression.
- Existing boss fights and quest HUD behavior stay unchanged.

## Non-goals

Unlimited bosses, changing gameplay damage/death rules, automatic drop-in support for all DLC/hosts, hijacking retail multiplayer network state, patching native `HudBossHpBar` layout, or exposing unaudited native pointers to Lua/C#.

## Proposed boundaries

- **Scenario source:** custom map-relative `data/scripts/boss_fight_two.scr` and compiled quest phase `type=\"boss fight\"` referring to that script. The released quest macro is `,bossfight name script_name =` in `Dying Light Developer Tools.zip :: DW/Data/Quests/_underlay/quest-macros-public.edf`. The template quest tree is `DevTools Template Maps.zip :: Template/data/quests/example/`. Script path/load details and the missing binding preset require Editor/Player validation.
- **Read-only sampler (conditional):** exact-build, game-thread snapshots of 2 named binding targets; copied `current/max/alive/identity/generation` fields only. Never retain native actor pointers beyond the validated sampling call.
- **Presentation (conditional):** existing DLUMA `ui_surface` owner-bound overlay. `src/game/dlw_ui_surface_backend.c` currently has `DLUMA_SURFACE_BACKEND_VERIFIED 0`; `src/game/dlw_hud_surface_service.c` deliberately advertises its capability as unavailable. Therefore an actual two-bar overlay **cannot be claimed supported today**; its backend must first pass separate exact-build runtime validation.
- **Quests:** DLUMA's `quest_phases` provider/observer API is for DLUMA-authored extension phases and does **not** automatically provide a readout of native `BossFightBinding` actors. Avoid conflating those systems.

## Required validation matrix

Run the same map in a declared Windows DevTools Player build: A-first, B-first, simultaneous crossing, missing A, missing B, death/despawn, respawn, reload, multiplayer client if relevant. Observe phase-name changes and HUD elements independently. Capture binary identities and logs/screenshots; compilation by itself is not runtime proof.

## Open implementation decisions (not user-approved)

Exact supported Player binary pair, binding placement in the released Editor, stable AI spawn identity and live HP accessor ABI, UI overlay presentation contract, and whether the stock bar should remain visible next to the optional dual-bar display. These are validation gates; do not invent missing APIs or presets.
