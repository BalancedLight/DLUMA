# Third Person

A standalone DLUMA mod for Dying Light Player, including Player launched from Developer Tools.

Press **T** to switch between first person and a right-shoulder camera. Hold **Left Alt** and move the mouse to look around your character; release Alt to return to the shoulder view. Camera controls leave the player's position and movement under the game.

| Control | Action |
|---|---|
| T | Toggle first person / third person |
| Hold Left Alt + mouse | Orbit around the player |
| Alt + Left / Right | Move the shoulder offset left / right |
| Alt + Up / Down | Raise / lower the camera |
| Alt + Page Up / Page Down | Move closer / farther away |
| Alt + Home | Reset the camera offsets |

Offsets are saved automatically when adjustment ends. Each game session starts in first person. Aiming and firing return the camera to the normal shoulder view while keeping third person enabled. Holding right click brings the camera 20% closer with a smooth transition; releasing it restores your configured distance. Menus and scripted cameras temporarily take control.

## Install

Create `mods/native/global/thirdperson.camera` in the DLUMA installation and extract the release ZIP's contents into that folder. Enable **Third Person** through DLUMA's Mods menu, approve the native package, and restart Player. The complete staged folder under `dist/mods/native/global` can also be copied directly.

Open **Third Person** in DLUMA's mod configuration to change its controls and camera framing. Select a key binding and press the key you want to use. **Horizontal camera angle (degrees)** rotates the camera around your character; zero keeps the normal shoulder view. **Camera elevation (degrees)** raises the camera for positive values and lowers it for negative values. The angle settings combine with your saved keyboard offsets.

**Adjustment speed (%)** controls how quickly the camera moves while an adjustment key is held; 100% is the normal speed. **Orbit return time (ms)** controls how long the camera takes to return after releasing the orbit key; 250 ms is a quarter of a second. To remove the mod, disable it and restart Player before deleting its folder.

**Smooth camera movement** is enabled by default. It smooths position follow and vertical movement around a stable shoulder-height target while keeping mouse look, orbit, and aim responsive. **Smoothing time (ms)** sets how gradually the camera settles, from 20 to 300 ms; the default is 120 ms. Higher values feel softer. Camera collision responds automatically with smoothing enabled or disabled, pulls in near obstacles, and eases outward when space clears.

The adapter recognizes the supported Player executable and the two verified game/engine module variants by SHA-256. Other builds leave the game camera in control and write one loader log message.

## Build

Use the x64 Visual Studio C++ tools and Python 3:

```bat
build_thirdperson_msvc.bat Debug
build_thirdperson_msvc.bat Release
tools\check_core.bat
```

LLVM users can run `build_thirdperson_clang.bat Release` with `clang-cl` on PATH.

The Release build validates the DLL export and package lock, then produces:

```text
dist/ThirdPerson-1.1.0.zip
dist/mods/native/global/thirdperson.camera/
```

An optional second argument to either build script supplies an explicit deployment destination. The build stages locally when no destination is given. The SDK snapshot and package tools are included; a loader checkout is not needed to build this project.

## Development

`controller.cpp` owns camera state, orbit, framing, and save timing. `camera_motion.cpp` handles position follow and collision recovery. `mod.cpp` integrates the public SDK lifecycle, events, configuration, and storage. `engine_adapter.cpp` contains the Windows camera and input bindings.

The native adapter selects existing camera objects and updates camera transforms. It selects the equipped outfit's third-person mesh through the game's appearance module and restores the first-person selection when releasing its view. It does not call player creation, teleport, or player world-position setters. It reacquires the current player visual after appearance refreshes and before the stock third-person update.

The controller checks cover state restoration, orbit and combat transitions, saved-offset timing, and camera geometry. Gameplay acceptance covers outfit changes, respawns, aiming, tight-space collision, menus, focus changes, scripted cameras, and repeated toggles while standing next to walls. Check that toggling or orbiting never changes the player's world position. 
