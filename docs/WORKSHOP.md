# Publishing DLUMA on Steam Workshop

Build and check a Release package before uploading. Keep the source repository, release archive, and Workshop project separate so the uploaded item contains the runtime package and project metadata rather than build files or local settings.

## Prepare a release

Release packages and Workshop staging exclude the user's `dluma.ini`. Keep installation-specific settings out of upload content; the loader creates its defaults as needed, and updates preserve existing settings.

From an x64 Visual Studio Native Tools prompt at the repository root:

```bat
build_windows_msvc.bat Release Offline
python tools/prepare_release.py --package-dir build/package/Release --output-dir release
```

The Release build creates or refreshes `release/workshop/content/mods/`; it does not create upload metadata. The command creates the versioned release ZIP and `SHA256SUMS`. Keep those files with the release notes. The complete runtime payload is in `build/package/Release/`.

For configurable upload metadata, copy `config/workshop.example.json` to the ignored `config/workshop.local.json`, fill in the title, description, preview, visibility, and Workshop item ID, then run:

```bat
python tools/prepare_release.py --package-dir build/package/Release --output-dir release --workshop-config config/workshop.local.json
```

The command creates `release/workshop/content/mods/` and `release/workshop/workshop.vdf`. Later Release builds refresh the content in this recognized staging directory while preserving the existing VDF, preview, and other metadata. README files are omitted from the release ZIP and Workshop content; licenses and third-party notices remain included. The JSON accepts `publishedfileid`, `visibility`, `title`, `description`, `changenote`, and an optional `previewfile` path relative to the JSON file. Visibility defaults to `private`. The command prepares files without logging in to Steam or uploading them. Keep Steam credentials out of configuration and source control.

## Publish through Developer Tools

1. Open the Workshop project used to distribute DLUMA in Developer Tools.
2. Copy the complete runtime payload into the project's `mods` directory. Keep `dluma_runtime/` beside the `.mod`, helpers, and PAK.
3. Set the project title, description, preview, and item metadata. Include the supported game/Player version and the early-development notice in the description.
4. Launch the staged project in Player and check startup, the Mods menu, enabling a native mod, and shutdown.
5. Use **Document → Share** in Developer Tools to publish the project. Start with private visibility for a release review, then change visibility when the item is ready.

Techland's [Developer Tools FAQ](https://steamcommunity.com/workshop/discussions/-1/611703999966865614/?appid=239140) documents the **Document → Share** upload route.

## Update an existing item

Use the existing Workshop item ID for an update so subscribers receive the new version. Rebuild the package, prepare the release archive again, stage the new runtime files, and publish through the same project. Include concise change notes and mention API or mod migration requirements.

After uploading, check the downloaded subscriber copy in Player. Keep user settings out of the release payload and preserve the previous release archive for troubleshooting.

## Maintainer upload testing with SteamCMD

The generated `workshop.vdf` can also be used for an upload test through an account with permission to publish to the game's Workshop. Steam's [Workshop implementation guide](https://partner.steamgames.com/doc/features/workshop/implementation#SteamCmdIntegration) describes the VDF fields and the `workshop_build_item` command.

Start SteamCMD, sign in interactively, and run:

```text
workshop_build_item "path/to/release/workshop/workshop.vdf"
```

A new item uses a zero item ID. An update uses the published item ID. SteamCMD writes the resulting item ID back into its VDF; retain it in your local upload configuration for the next update. Review the configured content folder and visibility before each upload.
