# Disposable DLUMA feature project

This folder installs two project-scoped probes into a **new disposable**
Editor-authored project. Use a unique new map and a disposable save. Python 3
and a DLUMA build with the corresponding capabilities are required. Setup never
launches the Editor or Player or changes saves. It rejects collisions and links;
if interrupted after writing, inspect the partial output before trying again.
Never point it at a real project.

In the Editor, create a new disposable project and author a new map with a unique
internal name. Save and build/export it with the Editor, then close the project.
Confirm the new project has `desc.scr`, its map's compiled assets, and no other
installed project claims that exact map name. Do not copy an existing native map
and merely rename its folder. Run from this repository:

```powershell
python test-projects/setup.py --project "<new-workshop-project>" --confirm-new-project --unique-map-confirmed --boss-map "<exact-new-map-name>"
```

Setup refuses existing probe packages or destination files. It writes
`data/scripts/lua/project/probe.hud.asset-a/` and `probe.hud.asset-b/`, their
validated lock files, and the optional boss sources under
`data/quests/<map>/` and `data/scripts/boss/`. It never overwrites `desc.scr` or
the Editor-authored map. Reopen the project, choose **Document > Open**, select the staged
`dluma_boss_probe.pxsl`, and press **F7** to compile the boss quest. Source
staging does not guarantee a playable quest. The optional `--template`
plus `--output` copy mode is for isolated offline preparation only: it duplicates
the native map identity and must not be used while the source claims that map.

Open the new project in the Editor and build/play its authored map normally. Enable
and approve **DLUMA Feature Probe A** and **B**. Their stable IDs are
`probe.hud.asset-a` and `probe.hud.asset-b`; both expose the handler `shared`.
The `probe.hud.asset-a` handlers are `shared`, `document`, `textures`,
`controls`, and `reset`; B exposes `shared`. Do not install another copy of
those IDs in the same project. A needs `ui.overlay`, `quest.phase.handle`, and
`configuration.read`; B needs `quest.phase.handle`. Each build contains exactly one DLUMA quest scenario. It may start automatically
on map load; the in-game pause menu may not expose mod settings. The project-scoped
handlers also work through mod settings where those settings are available. Look for `FEATURE_TEST` in the DLUMA log. The
switches invoke real APIs and do not pretend an unavailable capability succeeded.

For a visible callback and status console, open PowerShell with the Player's
active runtime `dluma.log` path and leave this running while testing:

```powershell
Get-Content -LiteralPath "<active-DLUMA-runtime-directory>\dluma.log" -Tail 30 -Wait | Where-Object { $_ -match 'FEATURE_TEST' }
```

Boss callbacks should print `handler=A shared` and `handler=B shared`; a missing
line is not a pass. Stop the console with Ctrl+C. On a failed or recovery-required
asset status, the probe retains the token and stops polling. The timed scenario requests reset automatically. The `reset` handler can also
request restoration when invoked in the same active mod state. If a pending restore times out, invoking the handler again in that same
state resumes polling without sending a duplicate restore. Switching scenarios
and restarting the map unloads the mod and loses its in-memory token; unload
requests cleanup but does not prove restoration. The settings off switch offers the same operation where
settings are available. Check the log for the confirmed restored/stopped state before starting
another request.

To prepare the Git test, run these commands only on the new disposable project:

```powershell
$project = "<new-workshop-project>"
git -C $project init
git -C $project config user.name "DLUMA Test"
git -C $project config user.email "test@example.invalid"
Set-Content -LiteralPath (Join-Path $project "feature-test.txt") -Value "baseline"
git -C $project add -- feature-test.txt
git -C $project commit -m "Create disposable test file"
Add-Content -LiteralPath (Join-Path $project "feature-test.txt") -Value "change to review"
Set-Content -LiteralPath (Join-Path $project "unrelated-test.txt") -Value "leave untracked"
```

The test identity is local to that disposable repository. Open the project in
the Editor, then follow the Git row below.

## Local HUD inputs

For a visible document test, append `--original-hud "<complete-local-hud.xui>"`
to the setup command above. Supply the other optional inputs in the same run;
setup refuses existing outputs on a later run.

Setup checks that the input is an XML canvas containing one scene, preserves it
as `hud_crane.xui`, and makes `hud_test.xui` with a visible white **DLUMA TEST**
panel. The panel reuses an existing font from the supplied HUD, preferring
`HUDTextNormal`; setup refuses automatic generation if no font is present.
This structural check does not establish compatibility with a particular
game build. Alternatively add `--test-hud "<complete-modified-hud.xui>"` to
supply your own compatible document; it must contain `EffectPanel` and its
`Visual` child for the controls test. `effect-controls.xui` is project-owned
source; no localization merge is needed for the generated literal marker.

For the texture test, also provide `--original-texture-def "<complete-hudtextures.def>"`,
`--texture-dir "<source-dds-directory>"`, and `--compiled-texture-dir
"<compiled-pairs-directory>"`. Supply a complete compatible definition locally;
setup requires each stock flashback alias exactly once, refuses conflicting test
aliases, and writes the original plus eleven test aliases to
`data/menu/texturedefs/hudtextures.def`. It never overwrites an existing file.

The source directory must contain `flash_0.dds` through `flash_10.dds`, each
uncompressed 1280 x 720 32-bit DDS. Compile those same images through the Editor's
supported resource workflow using target names `dluma_test_flash_0.dds` through
`dluma_test_flash_10.dds` at `data/textures/dluma-feature-test/`. Supply the
resulting 22 `.dds_obj` and `.dds_obj_dep` files together in the compiled-pairs
directory. Setup copies sources and objects into their matching project-relative
`data/textures/` and `assets_pc/textures/` trees; it checks source dimensions,
the presence of every object, and each dependency's source path, dimensions and
format. These checks do not prove that compiled pixels match the supplied source;
retain your compiler receipt and check the visual result. Raw DDS alone is
insufficient. The resource compiler's output routing depends on its staging, so
setup does not invoke an arbitrary compiler executable.

Game-derived HUDs, definitions and texture files
stay local and are subject to their own rights.

To stage one authored boss scenario add `--boss-map <exact-map-name>` and
`--unique-map-confirmed`. The default is `--scenario document`; choose
`callbacks`, `textures`, `controls`, `reset`, `textures-missing`,
`document-missing`, `textures-interrupt`, or `document-interrupt` to build
another scenario.
Only the selected quest is emitted, so a map load cannot start multiple test
quests in parallel. Every mode retains the root quest ID `DlumaBossProbe` and marks it
`-final=true`, following the local Editor example; its entry name and boss
script identify the chosen mode. It writes quest source under `data/quests/<map>/` and
boss scripts under `data/scripts/boss/`. Choose **Document > Open**, open the staged `dluma_boss_probe.pxsl`, and
press **F7**; run the map on a disposable save. The quest may start automatically.
The chatter references require The Following voice resources; ordinary builds do not expose the
periodic scheduler. Do not count source staging or a compiler exit as a pass.

To switch scenarios later, close the Editor and Player, then use a new backup
folder outside the project:

```powershell
python test-projects/select_scenario.py --project "<new-workshop-project>" --map "<exact-new-map-name>" --scenario textures --backup-dir "<new-external-backup>" --confirm-disposable
```

The selector refuses a modified or unrecognized generated quest and retains
both original source files in the backup. Reopen the PXSL with **Document >
Open** and press F7 after every switch. It does not overwrite compiled XML;
playing before F7 may still run the previous scenario. If the process is
interrupted, inspect the backup and both project files before continuing.

## Test matrix

The document scenario uses three complete HUD files: `hud_normal.xui` preserves
the supplied original bytes, and `hud_red.xui` / `hud_green.xui` add a centered,
opaque 256 × 256 square. It uses the original HUD's `white` image and
`menu_antialias.mat`, so it adds no font or texture dependency. The square is
always shown in each authored variant and has no animation.

Start the `document` scenario. Expect a red square for about 12 seconds, then a
green square for about 12 seconds, then the normal HUD without a square. Each
request restores the normal HUD and waits for confirmed restoration before the
next starts; a brief normal-HUD frame can occur between red and green. These are
different document files, not calls that recolor one live element. A failed or
uncertain red request stops the sequence and retains its recovery token; invoke
`reset` or let the authored reset phase request cleanup. An absent square, an
incorrect color, or missing final restoration fails this visual check. Logs
alone cannot pass it.

| Feature | Trigger and expected observation | Reset and pass/fail |
| --- | --- | --- |
| Git | In a new disposable project repository, edit a small tracked text file. In Editor source control select it, Stage, Unstage, Stage, review and Commit. | Check the commit contains only that file and an unrelated dirty file remains unchanged. A refused operation or wrong contents fails; no remote is needed. Initialize the copy using `git init`, configure a local test identity, and make an initial commit before opening the Editor. |
| Boss scoped callbacks | A compatible authored boss scenario calls `CallMod("probe.hud.asset-a", "shared", "{}")` and the same call for B. | Log must show both `handler=A shared` and `handler=B shared` from the boss scenario. Restart/unload the disposable map. The authored scenario must first pass Editor F7 compilation; direct invocation is not boss-script proof. |
| Flashback or chatter every 10–30 seconds | Requires a supported boss scheduler and a compiled authored scenario. | Observe actual effect times and stop after the scenario ends. **Unavailable in ordinary builds**; no mock or timer log constitutes a pass. A short run may sample only one branch. |
| Eleven textures | Build the `textures` scenario and start the map. After entry it requests all eleven aliases, waits eight seconds, then triggers a boss flashback. | After 20 seconds it requests reset and triggers another flashback 10 seconds later. Pass requires visibly changed and restored images and successful terminal status. Ready alone is not a visual pass; failures/timeouts are inconclusive or fail. |
| Complete HUD document | Build the `document` scenario and start the map. After a five-second entry wait it requests the altered HUD and holds it for 20 seconds. | It requests reset, then waits 20 seconds. Pass requires the visible original HUD and terminal restored status. Missing input and backend refusal must report failure safely. |
| Generic controls | Build the `controls` scenario and start the map. After the test HUD loads, the panel appears at its baseline, moves right and changes color/opacity after about two seconds, hides after another two, then shows again after two more. | The panel returns to its authored position, color, and opacity after another two seconds; the scenario then resets visibility and restores the original HUD. Observe each step. A failed or uncertain call stops the sequence and requires explicit reset. Text and animation remain unavailable pending native qualification. |
| Character state transitions | Native transition and save-writer support is not available in ordinary builds. | Do not reset the player or simulate success. |

Document and texture requests are mutually exclusive in the probe. A successful
request may remain pending until the game reaches the relevant resource; terminal
failure or a 60-second non-ready timeout stops polling without automatic retry.
If a restore fails or remains pending, preserve the logs and restart the
disposable session; do not infer restoration. On map unload the probe requests
cleanup, but a retiring screen may prevent it. Configuration switches should be
returned to off between runs so the next change is delivered.

## Negative and interrupted scenarios

Use the same disposable map and a private/solo session. Disable Content
Isolation for these asset tests. Each selected scenario is the only quest
emitted; compile its PXSL with F7 after switching. These scenarios submit the
real API requests and report actual statuses; no test fakes a pass.

- `textures-missing` submits all eleven `hud_bossfight_flash_0` through `_10`
  pairs, with `dluma_test_missing_5` deliberately absent in the middle. It
  waits ten seconds, invokes `reset`, then waits twenty seconds. Confirm the
  log reports the missing-resource failure and, after reset, state 5 (stopped).
  `ready` and `assets` counts show progress, but a positive count is evidence
  of partial acquisition only if the actual backend reports it. If the missing
  alias resolves or the result remains pending, do not pass the test.
- `document-missing` requests an absent original document path with the red
  HUD as replacement, waits ten seconds, then invokes `reset` and waits twenty
  seconds. Pass requires a clear request failure or failed status, an unchanged
  visible HUD, and confirmed restoration when a token was issued. The test
  must not install a file named `__dluma_missing_document__.xui`.
- `textures-interrupt` and `document-interrupt` request the asset, submit a
  second request after one second while the first token is held, and request
  reset one second later. After twenty seconds they attempt the same test once
  more, then reset again. Pass requires the busy message for the first
  overlapping attempt, no second admission before confirmed restoration, and a
  confirmed stopped/restored state for every accepted request. If cleanup is
  still pending at the repeat, the probe refuses it and logs busy; that is
  inconclusive, not a repeat pass. Observe the HUD/image where applicable.

On an error or timeout the probe retains its token. An explicit `reset` may
request restoration; it does not retry an uncertain operation automatically.
Unload requests cleanup but does not prove it completed. The negative texture
alias and missing document name must remain absent from all loaded sources.

## Migration

Once actual behavior is qualified, copy the small Lua logic and authored panel
into the destination project's own packages, choose non-colliding stable IDs,
update its boss `CallMod` targets, merge its own HUD/locale/texture definitions,
regenerate package locks using `tools/validate_mod_package.py lock <package>`,
and validate them. Never copy this disposable project's save or generated
map over a real project. Boss scheduler consent, state switching and visual
backend availability depend on the exact build and are not supplied by setup.
