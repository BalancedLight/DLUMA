# Dluma.QuestGraph

`Dluma.QuestGraph` is the dedicated visual PXSL quest companion for Dying Light
Developer Tools. It is a skin over PXSL: graph topology is projected from PXSL and
the companion never emits native quest XML. Stock Developer Tools compilation remains
the only XML compiler route.

## Modes

- Offline mode opens persisted `.pxsl` files, parses them losslessly through
  `Dluma.QuestGraph.Core`, edits localized source spans, loads optional definition files,
  scans bounded parseable project resources, and saves
  `<quest>.pxsl.dlumagraph.json` layout sidecars.
- Connected mode is launched by the validated native Editor bridge with `--pipe`,
  `--session-token`, and `--editor-pid`. It adds revision/hash-checked live edits,
  live context snapshots, native source focus, stock Save, and stock Compile.
- The packaged exact-build Editor bridge adds the **Visual Quest Editor** button beside
  **Patch Diff** for supported QuestEditor builds. Failed admission leaves the stock UI
  unchanged; offline authoring remains available independently.
- If the Editor disconnects, connected source editing and original-file saves freeze.
  Layout remains in memory and **Save Copy** produces an explicit recoverable document.

## Graph behavior

The editor supports multiple document tabs, a searchable grouped runtime-catalog palette,
pan/zoom and minimap navigation, node selection/movement/collapse/notes, automatic
layout, direct connector rewiring, detached-node staging, output-to-empty creation,
stock-colored semantic ports, context menus, atomic multi-delete, Problems and Variables
views, source-location focus, and application-owned undo/redo. Unknown or malformed
regions remain visible opaque nodes; their exact text is never silently reformatted.

The sidecar stores layout only. Deleting it loses positions, notes, groups, and the
viewport, then the graph auto-arranges from PXSL. It never stores macro arguments,
variables, object references, graph topology, execution order, absolute project paths,
or any other semantic data. Invalid or future sidecars are preserved and are not
overwritten automatically.

## Safety boundaries

- Pipe framing and messages are documented in [PROTOCOL.md](PROTOCOL.md).
- Frames are capped at 16 MiB and raw source snapshots at 8 MiB; JSON depth,
  edit payloads, strings, and aggregate collection sizes are independently bounded.
- Runtime project paths are canonicalized and containment-checked. Optional context
  scans skip reparse points and use directory, file, per-file, and aggregate byte caps.
- No HWND, engine pointer, native object pointer, or process-local address crosses the
  pipe or enters a sidecar.
- The native server independently admits current-user clients, the spawned PID,
  packaged executable hash, the manifest-bound private runtime, and the exact
  supported Editor/QuestEditor build. Any unavailable prerequisite keeps the stock
  UI unchanged.

## Build

The project targets `net10.0-windows`, references `Dluma.QuestGraph.Core`, and pins
Nodify `7.3.0` through `packages.lock.json`.

```powershell
dotnet restore Dluma.QuestGraph.csproj --locked-mode
dotnet build Dluma.QuestGraph.csproj --no-restore --configuration Release
```

The repository packager supplies the package-owned .NET Desktop runtime. A globally
installed Windows Desktop Runtime is not a deployment prerequisite.
