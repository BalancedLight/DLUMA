# DLUMA — Dying Light Universal Mod Assistant

DLUMA is a mod loader and creator toolkit for Dying Light Player and Developer Tools on Windows. It adds a Mods menu, shared mod configuration, and a common API for Lua, C#, C, and C++ mods.

Version **0.5.0** is an early release. Bugs, crashes, and unfinished features are expected. Dying Light and other installed mods can also affect your experience.

## Playing with DLUMA

Install the complete DLUMA package through Steam Workshop or follow the instructions included with the release. Start the Player with the DLUMA Workshop project, then open **Mods** from the main menu to review and configure installed mods.

To enable new and updated global mods automatically at startup, turn on **Automatically enable mods** in Options and accept the security warning. This is off by default. Mods you disable yourself stay disabled, and newly discovered mods still appear in the main-menu ticker.

The first launch asks whether you want to continue with an early version of DLUMA. **Yes** remembers your choice for your Windows user account. **No** exits Player.

Native and C# mods run with the same access as the game. Review their source and author before enabling them. Enabling or updating these mods requires a restart. Lua mods run in the loader's sandbox.

- [Installation, settings, and troubleshooting](docs/USING_DLUMA.md)
- [Creating mods](docs/MODDING.md)
- [DLUMA API 1.0](docs/api/1.0/README.md)
- [Building from source](docs/BUILDING.md)
- [Publishing and updating a Workshop item](docs/WORKSHOP.md)

## For mod authors

DLUMA API 1.0 provides the same operations, types, capabilities, configuration, and lifecycle across all supported languages. The generated SDKs are built from the catalog in `api/catalog/`.

| Language | Starting point |
| --- | --- |
| C | `sdk/c/dluma_api.h` |
| C++ | `sdk/cpp/dluma.hpp` |
| C# | `sdk/dotnet/Dluma.Managed.Sdk/` |
| Lua | `local mod = dluma.mod()` |

The native examples include a small starter mod and Third Person. They demonstrate complete source, build, and package layouts. See [the modding guide](docs/MODDING.md) for the package structure and approval process.

## Build

Install the .NET SDK specified in `global.json`, Python 3.11 or newer, and Visual Studio C++ tools with the v145 toolset, a Windows SDK, and MASM. From an x64 Native Tools command prompt:

```bat
build_windows_msvc.bat Release Offline
```

The complete package is written to `build/package/Release/`. Build and runtime dependencies, local deployment, and verification commands are described in [Building DLUMA](docs/BUILDING.md).

## Source layout

| Directory | Contents |
| --- | --- |
| `src/` | Native loader, game services, host adapters, and built-in fixes |
| `api/` | API catalog, package schemas, and generated contracts |
| `sdk/` | C, C++, managed, and Lua SDK surfaces |
| `examples/` | Native mod source examples |
| `workbench/` | Creator application and visual quest editor |
| `PakAssets/` | Menu layouts, localization, and loader content |
| `tools/` | Build, package, and validation tools |

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change. Project source is available under the [MIT license](LICENSE); bundled dependencies retain their own [licenses and notices](THIRD_PARTY_NOTICES.md).

## AI assistance

AI tools have been used to help develop and maintain DLUMA, including code, documentation, and testing.