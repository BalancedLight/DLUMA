# Dual-boss scenario health: verification-first implementation plan

> **Execution:** Use Superpowers' test-driven/plan-execution workflow to implement each task. This is a **proposal**, not a claim of completed implementation or runtime verification.

**Goal:** Confirm two independently monitored bosses in one DL1 boss-fight scenario without changing engine behavior. If truly necessary, add a safe, opt-in two-bar HUD to DLUMA for attested Windows Developer Tools Player builds.

**Architecture:** Keep native `BossFight` and `PhaseTransition` semantics. Author two boss bindings and use separate scenario paths for independent thresholds. Gate the optional real-time HUD behind an exact-build, game-thread, read-only actor-health snapshot provider and a verified `ui_surface` backend. Do not redefine quest phase semantics merely to render additional health bars.

**Evidence and design:** `docs/superpowers/specs/2026-11-08-two-boss-health-design.md`.

## Ground rules

- Base DL1, DLC17, DLC49, DevTools Player, Editor and retail Game must remain distinct. DLC17's `boss_fight_mother.scr` is a source pattern, not a base-map dependency.
- **No binary write/hook** until exact Windows build/module hash and ABI are established. Named DL1 macOS 1.49 functions are naming/algorithm evidence only; Windows field offsets and vtable entries cannot be copied from them.
- Do not ship an invented EDF, PXSL macro, `BossFightBinding` object preset, in-game health getter, Lua API or native address.
- Mutations for a future implementation must occur on feature branches, with tests before production code. Changes here are documentation only.

## Task 1 — Establish stock-editor placement and a minimal two-boss fixture

**Files to add once verified:** `examples/dual_boss_scenario/README.md`; a map/project-relative `data/scripts/boss_fight_two.scr`; companion `data/quests/<level>/<quest>.pxsl` with exact names confirmed against the template and Editor; optional documented starter map resources from `DevTools Template Maps.zip :: Template/data/maps/example/`.

- [ ] In the installed Editor, check **Object Types → BossFightBinding** exposure. `DL Base Game Paks.zip :: Data0.pak :: data/editor/scripts/assets_object_types.scr` references `data/defaults/objecttypes/bossfightbinding.df2`, but that preset member could not be opened in the supplied archives. If missing, identify the precise alternative supported placement route by creating/saving a tiny Editor map. Do not blindly synthesize a `.df2`.
- [ ] Place two distinct AI spawnboxes and two binding objects with unique tag values (example-only placeholders `boss_a` and `boss_b`); assign each binding's `m_GameObjects` to its own spawnbox. Confirm both bindings are registered when the bossfight begins.
- [ ] Verify scenario data/script lookup and exact quest phase syntax using `DW/Data/Quests/_underlay/quest-macros-public.edf` (`,bossfight name script_name =`) and `Template/data/quests/example/example_kill_and_run.pxsl`. Compile via the released DevTools quest workflow; document commands only after observing what this release actually accepts.
- [ ] Reproduce with **zero DLUMA modifications**. Record screenshots/logs of the active phase, spawned AIs, and health.

**Acceptance:** Both bosses spawn; named bindings resolve independently; map project compiles; neither a missing/empty binding nor an absent AI causes accidental completion. Distinguish *compiled* from *live Player validated*.

## Task 2 — Prove vanilla health transition semantics

**Evidence anchors:** `DL - The Following.zip :: DataDLC17_0.pak :: data/scripts/boss_fight_mother.scr` (multiple `PhaseTransition` + one `LifeBelow` each); `DyingLightDebug.zip :: .../gamedll_x64_rwdi.dll.c` (`LifeBelow` parser at `sub_180A9CE90`); named macOS `PhaseTransition::Execute` (iterates all AIs for one binding).

- [ ] Exercise **separate bindings** with A threshold 70%, B threshold 35%, and a state-machine branch for whichever threshold is crossed first. On A-first path wait for B; on B-first path wait for A. Use existing `PhaseTransition()`, `Conditions()`, `LifeBelow()`, `Target()` constructs only.
- [ ] Exercise **one shared binding** containing A and B, `LifeBelow(\"both\", 50.0)`: confirm that it transitions only after *both* are at or below 50%. Explicitly probe the zero-AI case and spawn timing.
- [ ] Verify `LifeBelow` compares percentages, threshold boundary behavior, and that duplicate `LifeBelow` entries inside one transition are *not* a safe conjunction. Prefer separate transition paths over duplicated fields.
- [ ] Test A dies first, B dies first, simultaneous damage, despawn, respawn/reload, and map restart. Record whether dead AI is retained in the binding's collection and how this affects transitions.
- [ ] Document the confirmed authorable recipe. **Stop here** if the user requirement is only scenario progression; no DLUMA fix is necessary.

**Acceptance:** A/B threshold transitions work in either order and terminate only when expected; no accidental win on unresolved/spawnless bindings.

## Task 3 — Confirm whether dual *stock* HUD bars actually exist

**Evidence:** `DL Base Game Paks.zip :: Data0.pak :: data/menu/hud/hud.xui` defines one `HudBossHpBar`. The named macOS implementation's `HealthBarTarget::Execute` retrieves the first AI from a binding, stops the current stock bar, and starts one target; `HudBossHpBar::StartBossFight` appears to store one active AI.

- [ ] In the same live test, set two AI spawnboxes' **`m_SpawnAsBoss`** to the documented Editor property; this field is corroborated by `DyingLightDebug.zip :: .../gamedll_x64_rwdi.dll.c` descriptor `If true spawned AI will be set as boss, HudBossHpBar will be activated` and `Dying Light Developer Tools.zip :: DW/Data/Defaults/ObjectTypes/AI_SpawnBoxNumber.df2`.
- [ ] Count visible independent bars; change A then B health separately. Verify if the single XUI node is recycled, selected by most recently spawned boss, or a true second element is created. Do **not** infer the result from source files alone.
- [ ] If vanilla already gives two independent bars reliably, update docs and **cancel** the optional overlay implementation. Otherwise document exact missing behavior and conflict with stock HUD.
- [ ] Capture matching host/build, source actor identity, current/maximum HP and HUD state; do not treat a visual bar as evidence of scenario condition semantics.

**Acceptance:** A reproducible, build-identified yes/no result for dual stock health bars.

## Task 4 — Investigate DLUMA capability readiness (conditional)

**Read:** `api/catalog/ui_surface.json`, `src/game/dlw_hud_surface_service.c`, `src/game/dlw_ui_surface_backend.c`, `src/game/dlw_quest_phase_service.c`, `docs/api/1.0/HUD_SURFACES.md`, `docs/ARCHITECTURE.md`.

- [ ] Verify supported module identities and exact source-to-binary relationships; do not rely on unrelated macOS offsets. DLUMA `src/game/dlw_ui_surface_backend.c` currently sets `DLUMA_SURFACE_BACKEND_VERIFIED 0`, while `dluma_hud_surface_prepare_capabilities` reports unavailable. Keep this gate closed until both supported Player profiles have live create/update/layout/visibility/teardown success.
- [ ] Decide HUD ownership: one project/map-scoped mod owns one surface with **two bars** drawn into it; respect the existing four-slot total and one-surface-per-owner implementation. Do not extend ABI for a feature that can be composed in one surface.
- [ ] Confirm `ui_surface.suppress` lacks a stock `HudBossHpBar` bit in the current backend (`g_stock` lists different HUD elements). Prefer rendering two bars with the stock bar left intact or propose a separately reviewed and tested suppression change, never hide arbitrary stock elements.
- [ ] Confirm existing `quest_phases` provider/observer events are not a native boss health feed; do not misuse them as the sampler.

**Acceptance:** Documented capability gate, selected test profile, surface ownership policy, suppression decision, and explicit no-go criteria.

## Task 5 — Add a bounded read-only two-boss health sampler (only if Tasks 3–4 justify it)

**Proposed files:** `src/game/dlw_boss_health_service.c` and `.h`; `tests/boss_health_fake_harness.c`; `tests/test_boss_health_service.py`. These files do not exist in the inspected main branch and are proposals, not existing APIs.

- [ ] Write failing fake-harness tests for two distinct actor IDs, name/health isolation, A/B reorder, invalid target, null and stale objects, death/despawn/rebind, map-generation change, and owner revoke. Assert no per-frame pointer retention and a bounded per-frame work limit.
- [ ] Develop an **exact-build** Windows read-only snapshot adapter from verified matching `gamedll_x64_rwdi.dll` and Player host. Resolve by authored binding tag; collect a copied record `{binding_key, spawn_identity, map_generation, current_hp, max_hp, alive, status}` under verified engine game-thread context.
- [ ] Validate finite positive max HP, clamp presented ratio to [0,1], fail closed on zero max HP/invalid memory, and preserve distinct identifiers when actors respawn. Do not extrapolate from a `HumanAI*` or reuse stale native pointers.
- [ ] Expose *no* new public API until a capability/access model is approved. If a read API is needed, add a versioned, size-prefixed, ownership-scoped catalog operation and regenerate SDKs via `tools/generate_api.py`—never hand-edit generated ABI files.
- [ ] Run test harness and source/ABI compatibility checks before a live Player smoke test. Keep all unsupported binaries disabled.

**Acceptance:** Independent, safe, copy-only snapshots and tests for both bosses on attested module hashes, no crashes or stale actor reads.

## Task 6 — Optional dual-bar presentation on the existing UI surface

**Proposed files:** an opt-in feature module, test fixture and documentation; reuse `src/game/dlw_hud_surface_service.c`, `src/game/dlw_ui_surface_backend.c`, `PakAssets/XUI/DLUMA_SurfaceCanvas.xui` instead of replacing stock HUD.

- [ ] Write failing tests for independent bar fill values, labels, invalid/missing health, transient respawn states, layout, UI ownership, teardown and absence of a fallback mode that spoofs a bar.
- [ ] Create **one** two-bar RGBA surface (bounded size) via approved `ui_surface.create`; update only while in a matching active bossfight/map generation. Show names and separate percentages; throttle updates to a measured safe rate.
- [ ] Preserve stock HUD by default. If duplicate stock bars are visually undesirable, first review and validate an explicit opt-in `HudBossHpBar` suppression capability, with exact restoration of prior state. Do not add an unreviewed suppression bit.
- [ ] On mod disable, world change, actor removal, Player exit, or capability loss, immediately hide/release owned surface and discard copied snapshots.
- [ ] Verify both simultaneous labels/fills in live Player; test rendering at alternate resolutions/aspect ratios, reload, mission success/failure, and with unrelated stock HUD overlays.

**Acceptance:** Two distinct, simultaneously updating bars without leaked HUD elements or persistent stock-HUD changes.

## Task 7 — Validate, document, review, and release-gate

- [ ] Run at minimum `python -m pytest -q tests/test_quest_phase_runtime.py tests/test_quest_phase_authoring.py tests/test_hud_surface_service.py tests/test_quest_hud_service.py tests/test_dual_host_runtime.py` if toolchain supports them, plus the new tests and the API backward compatibility suite. Run the standard Windows MSVC offline build documented in `docs/BUILDING.md`.
- [ ] Test Developer Tools **Player** with verified hash pair; distinguish this from the separate **Editor** process. Retail Game support must remain disabled unless independently studied and live tested for its exact build.
- [ ] Inspect app logs, teardown, scenario completion after A-first/B-first, concurrent damage, scene reload, multiplayer authority (only if intended), missing bindings, and unsupported build.
- [ ] Add docs at `docs/MODDING.md` or a linked guide, including exact verified Editor steps, sample project-relative file tree, the full compiled/authored scenario and acceptance screenshots.
- [ ] Peer-review binary provenance, absence of native writes, feature gate correctness and the decision to modify or leave stock HUD.
- [ ] Submit implementation PR **only after** stock tests and new tests pass. No merge or automatic feature enablement as part of this documentation PR.

## Milestone decision gates

1. **Vanilla works for two threshold checks?** Document and avoid changing the scenario engine.
2. **Vanilla shows two bars reliably?** Stop; do not write duplicate UI.
3. **No dual bar, but HUD surface unverified?** Keep feature gated, do not claim it is usable.
4. **Exact target actor sampler impossible/unsafe?** Stop; provide docs for vanilla scenario logic instead of a fragile hook.
5. **Runtime passes all criteria?** Release as explicit opt-in for that Player build only.

## Evidence and limits

This plan is based on **shipped DLC17 script/XML**; **shipped base XUI**; **DevTools EDF/preset references**; **Windows-static field/parser anchors** and **named macOS 1.49 semantic decompilation**; and **direct inspection of DLUMA main branch**. It is **not compiled/offline validated or live verified**. The supplied sources do not establish a complete `BossFightBinding.df2` file or prove that the released Editor exposes it in this exact build. The Windows decompilation and matching released Windows binary have distinct hashes and their exact correspondence is not yet proven. No new HUD API, hook address or runtime behavior is asserted.
