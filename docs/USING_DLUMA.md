# Using DLUMA

DLUMA runs in Dying Light Player and integrates with Developer Tools. Use the complete package for your release; its helper libraries, content pack, and runtime folder belong beside the loader module.

## Install and start

1. Subscribe to the DLUMA item in Dying Light's Steam Workshop and let Steam finish downloading it, or install a complete release package using its included instructions.
2. Start the DLUMA Workshop project in Player. For local development, install the package into the local project's `mods` directory as described in [Building DLUMA](BUILDING.md).
3. Read the first-launch message. **Yes** continues; **No** exits Player. Acceptance is remembered for your Windows account.
4. Open **Mods** from the main menu to review discovered mods.

## Enable and configure mods

Select a mod to view its source, version, access requirements, and configuration. Choose **Enable** to approve it. Native DLL and C# mods run as part of the game process, so enable them only when you trust their author and source.

Native and C# changes take effect on the next launch. An update that changes a mod's package may require approval again. Keep the complete package together; editing its executable files or manifest invalidates its package lock.

Use a mod's configuration screen to change its supported settings. An option's description explains what it does and whether a restart is required.

## Loader settings

Open the loader settings from the Mods menu. General options include presence integration and Developer Mode. The Game Fixes category contains the built-in fixes available for the current host.

Developer Mode exposes additional options and Lua limits. Its startup changes, such as developer keys, require a restart. **Reset custom project on exit** is visible only when Developer Mode is enabled.

## Troubleshooting

If a problem starts after enabling or updating a mod, disable that mod and restart Player. Check the mod's version requirements and any dependencies. Keep a copy of the steps that reproduce the problem and the relevant log or crash report.

Runtime files, logs, and crash reports are stored in the loader's `dluma_runtime` directory. The creator application's **Open logs** command can help locate a log. First-launch acknowledgement is stored separately in your Windows user profile.

When reporting a problem, include the DLUMA version, Player or Editor version, installed mods, and reproduction steps. Remove private paths or account details from shared logs.
