# DLUMA architecture

DLUMA is an x64 Windows game-script module that integrates mod discovery, approval, configuration, and execution into Dying Light Player and Developer Tools.

## Native loader

The native module forwards the game's `InitializeGameScriptDLL` and `ShutdownGameScriptDLL` entry points and provides the menu integration and host lifecycle. Built-in game fixes and exact-build adapters live under `src/`.

Game adapters identify supported binaries before using their bindings. The common API reports unavailable capabilities through structured results, allowing mods to handle differences between hosts and supported game builds.

## Shared API

`api/catalog/` describes operations, types, capability IDs, lifecycle, scopes, and access declarations. `tools/generate_api.py` generates the C ABI, C++ wrappers, managed contracts, Lua metadata, and API reference from that catalog.

The public API is versioned independently of the DLUMA application. Native records use size-prefixed structures. Keep record layouts and numeric identifiers stable within an API version.

## Mod hosts and packages

The native host loads approved DLL mods. The managed host uses the packaged .NET runtime and bootstrap. Lua 5.4 runs in a dedicated helper with resource limits and declared capabilities.

Native and C# code have the process's full access. Lua execution uses the sandbox and permissions defined by the loader. Startup order is native, C#, then Lua; shutdown reverses it.

Each API mod has a manifest and package lock. The lock describes the package's files, sizes, and hashes. Mod approval applies to the selected package and its declared configuration and companion content.

## Applications and content

The creator application creates source projects and stages mod packages. The visual quest editor edits PXSL and stores graph layout beside the source; Developer Tools performs native quest compilation.

`PakAssets/` supplies the loader menus, localization, and content assembled into `dluma.pak`. The release packager also includes helper libraries, managed assemblies, a private .NET runtime, and dependency notices.
