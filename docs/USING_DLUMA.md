# Using DLUMA

DLUMA runs in Dying Light Player and integrates with Developer Tools. Use the complete package for your release; its helper libraries, content pack, and runtime folder belong beside the loader module.

## Install and start

1. Subscribe to the DLUMA item in Dying Light's Steam Workshop and let Steam finish downloading it, or extract the complete release archive into a DLUMA project's `mods` folder.
2. Start the DLUMA Workshop project in Player. For local development, install the package into the local project's `mods` directory as described in [Building DLUMA](BUILDING.md).
3. Read the first-launch message. **Yes** continues; **No** exits Player. Acceptance is remembered for your Windows account.
4. Open **Mods** from the main menu to review discovered mods.

For a manual update, close Player first. Move the previous version's
`DLUMA (version).mod` and `dluma_runtime` folder to a backup outside the
project's `mods` folder, then extract the new archive into `mods`. From the
backup, copy the `mod_config`, `mod_data`, `markers`, and `crashes` folders if
present. Also copy `global_mods.ini`, `native_mods.ini`,
`custom_main_menu_activation.ini`, and `startup_recovery.ini` if present.
Place them inside the new `dluma_runtime` folder. Keep the backup until you
have checked the update. Leave `dluma.ini` and unrelated mods in place.

## Enable and configure mods

Select a mod to view its source, version, access requirements, and configuration. Choose **Enable** to approve it. Native DLL and C# mods run as part of the game process, so enable them only when you trust their author and source.

Native and C# changes take effect on the next launch. An update that changes a mod's package may require approval again. Keep the complete package together; editing its executable files or manifest invalidates its package lock.

Use a mod's configuration screen to change its supported settings. An option's description explains what it does and whether a restart is required.

For text settings and log searches, use the on-screen keyboard to edit the value and select **Save**. **Shift** and **Symbols** provide additional characters; **Clear** empties the current value.

To replace a value using a physical keyboard, select **Type text**, press Enter to activate the field, enter the replacement, then choose **Use text** and **Save**.

## Loader settings

Open **Options → DLUMA** to change loader settings. General options include presence integration and Developer Mode. The Game Fixes category contains the built-in fixes available for the current host.

Developer Mode exposes additional options and Lua limits. Its startup changes, such as developer keys, require a restart. **Reset custom project on exit** is visible only when Developer Mode is enabled.

**Enable nightly updates** checks for the latest verified build from GitHub when Player starts. It is off by default. When an update is available, DLUMA shows its commit and asks whether to download it. Accepted updates replace the loader package after Player closes; your settings, logs, and other mods are retained. Leave Player closed until the updater finishes.

Turning nightly updates off offers to restore the stable package saved before the first nightly update. Restoration also runs after Player closes. Steam may replace local nightly files when it updates or restores the Workshop subscription.

## Troubleshooting

If a problem starts after enabling or updating a mod, disable that mod and restart Player. Check the mod's version requirements and any dependencies. Keep a copy of the steps that reproduce the problem and the relevant log or crash report.

Runtime files, logs, and crash reports are stored in the loader's `dluma_runtime` directory. The creator application's **Open logs** command can help locate a log. First-launch acknowledgement is stored separately in your Windows user profile.

When reporting a problem, include the DLUMA version, Player or Editor version, installed mods, and reproduction steps. Remove private paths or account details from shared logs.
