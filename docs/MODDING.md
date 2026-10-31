# Creating a DLUMA mod

DLUMA API 1.0 is shared by Lua, C#, C, and C++ mods. Choose the language that suits your project, then use the same catalog-defined operations, configuration, ownership, and lifecycle concepts.

Start with a complete example:

- [`NativeModExample`](../examples/native-mods/NativeModExample/): a small C native mod using the public SDK.
- [`ThirdPerson`](../examples/native-mods/ThirdPerson/): a complete C++ mod with configuration, saved settings, a native game adapter, and package staging.

The creator application under `workbench/` can also create a project from a language template.

## Build the native examples

From the generic example directory in an x64 Visual Studio tools prompt:

```bat
build_native_mod_example_msvc.bat Release
```

From the Third Person directory:

```bat
build_thirdperson_msvc.bat Release
```

Each example's README describes its outputs and installation. Third Person includes its SDK snapshot and package tools so it can be built separately from the loader repository.

## Package layout

Every API mod directory contains `mod.dluma.json` and `mod.dluma.lock.json`. A mod with configurable settings also supplies `config.dluma.json`.

```text
data/scripts/lua/global/<mod-id>/
mods/csharp/global/<mod-id>/
mods/native/global/<mod-id>/
```

Project-scoped mods use `project` in place of `global`. Map-scoped mods use `maps/<map-name>/<mod-id>` after the language root. The manifest's language, scope, map, and ID must agree with its package directory.

Use a stable ID for your mod. Keep source, build output, and package inputs separate so every release can be staged from a clean checkout.

## Entry points and API versions

- C and C++ include the generated SDK and export `DLUMANativeMod_GetApiV2`.
- C# references `Dluma.Managed.Sdk` and implements `IDlumaMod`.
- Lua starts with `local mod = dluma.mod()`.

The application version and API version are separate. DLUMA 0.5.1 provides API 1.0. Use the [API reference](api/1.0/API_REFERENCE.md) and [capability matrix](api/1.0/CAPABILITY_MATRIX.md) to choose operations and handle capabilities that are unavailable on the active game build.

## Validate and package

Run the package tool against the final mod directory after all binaries and companion files have been staged:

```bat
python tools/validate_mod_package.py lock "path/to/mod-directory"
python tools/validate_mod_package.py validate "path/to/mod-directory" --require-lock
python tools/validate_mod_package.py package "path/to/mod-directory" --out "build/MyMod.zip"
```

These commands are run from the loader repository root. The lock records the package's manifest, files, sizes, and hashes. Rebuild it whenever package content changes. Keep the ZIP output outside the mod directory.

Native and C# mods require player approval and a restart. Updates to approved content may require approval again. Package manifests should describe the mod clearly and declare the access it uses; full-trust native and managed code must still be treated as part of the game process.

## Migrate an older mod

Use [the migration guide](api/1.0/BREAKING_CHANGES.md) to replace the old DLW SDK and package names. Preserve your stable mod ID, rebuild against the DLUMA SDK, regenerate the package lock, and check the mod in Player before releasing an update.
