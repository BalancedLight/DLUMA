# Building DLUMA

The supported release build uses MSVC and produces a complete Workshop package. Build on x64 Windows with:

- Visual Studio C++ tools with the **v145** toolset, MSBuild, MASM, and a Windows SDK.
- Python 3.11 or newer available as `python`.
- .NET SDK **10.0.400** or a later patch in the same feature band, as selected by `global.json`.
- .NET and Windows Desktop runtime **10.0.13** available to the runtime packager. Use `DOTNET_ROOT_X64` or `DOTNET_ROOT` when your .NET installation is outside the normal location.

The visual quest editor restores the pinned Nodify package through NuGet. A clean build may need network access for that restore.

## Build the complete package

From the repository root in an x64 Visual Studio Native Tools command prompt:

```bat
build_windows_msvc.bat Release Offline
```

Use `Debug` instead of `Release` for a debug build. The optional `Offline` argument disables automatic local deployment even if local deployment settings exist. A normal checkout builds into repository-local output directories.

You can also open `DLUMA.sln` in Visual Studio and build `Release | x64` or `Debug | x64`.

The package includes:

```text
build/package/Release/
  DLUMA (0.5.0).mod
  dluma.pak
  dluma_lua54.dll
  dluma_discord_presence.dll
  LICENSE
  THIRD_PARTY_NOTICES.md
  LICENSE_LUA_5.4.8.txt
  dluma_runtime/
```

`dluma_runtime/` contains the managed bootstrap and SDK, visual quest editor, pinned .NET and Windows Desktop runtime, runtime manifest, dependency notices, and release diagnostics. Distribute the complete package rather than copying only the `.mod` file.

## Optional local deployment

Copy `DLUMA.local.props.example` to the ignored `DLUMA.local.props`. Set the Workshop and Developer Tools destination properties to existing `mods` directories on your machine. Set `DlumaAutoDeployLocal` to `true` only when you want successful builds to install the package there.

For a one-off local installation:

```bat
msbuild DLUMA.vcxproj /t:DeployLocal /p:Configuration=Release /p:Platform=x64
```

The deploy target validates the package and requires both configured destinations to exist. Keep local paths and Workshop item IDs in ignored local configuration. Building or deploying does not start Player.

`INSTALL_LOCAL_DEVTOOLS.bat` accepts the path to a local Developer Tools Workshop directory and installs the complete Release package into a DLUMA project there.

## Release archive

After building Release, prepare the distributable archive and checksums:

```bat
python tools/prepare_release.py --package-dir build/package/Release --output-dir release
```

The output includes `DLUMA-0.5.0.zip` and `SHA256SUMS`. See [Workshop publishing](WORKSHOP.md) for upload metadata and the publishing process.

## Check a change

Run the checks relevant to the components you changed:

```bat
python tools/generate_api.py --check
python tools/validate_localization.py
python tools/verify_pe.py --json "bin/x64/Release/DLUMA (0.5.0).mod"
python tools/verify_package_manifest.py --package-dir build/package/Release --mod-name "DLUMA (0.5.0).mod"
dotnet build sdk/dotnet/Dluma.Managed.slnx -c Release
```

The native build wrapper also runs the existing native mod core checks. UI changes need a menu check in Player or Developer Tools; package and API checks do not exercise the game UI.

The Clang wrapper is a development alternative. Use the MSVC path above to produce a release package.
