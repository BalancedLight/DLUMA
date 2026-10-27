# Example native DLUMA API 1.0 MOD

Build the API V2 module with either script from an x64 Visual Studio tools
prompt:

```bat
build_native_mod_example_msvc.bat Release
build_native_mod_example_clang.bat Release
```

The Release build stages and locks the canonical package at:

```text
mods\native\global\example.native.dllmod\
```

Copy that directory into the matching `mods\native\global` root of a Workshop
project. The loader hashes the manifest, lock, module, and complete package
tree without loading it. It appears disabled with a full-trust native-code
warning; approval applies to those exact bytes and takes effect on the next
launch.

This module exports only `DLUMANativeMod_GetApiV2`. It never initializes a second
game-script manager or loads `gamedll_x64_rwdi.dll`. On the validated Player
profile it registers `example.native.phase` as a primary custom quest-phase
handler, observes its own contract, and observes the generic Lua sample's
`example.global_loader_status:example.lua.phase` contract.
