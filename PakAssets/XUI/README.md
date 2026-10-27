# Menu assets

These XUI sources are compiled into the DLUMA content pack.

- `DLUMA_Main_PC_workshop.xui` adds the Mods entry to the main menu and contains the first-launch confirmation and new-mod notification.
- `MenuDLUMAMods.xui` displays installed mods. Its shared enable dialog keeps the title and actions visible while long metadata, warnings, and permissions scroll inside a clipped body.
- `DLUMA_MenuOptions.xui` adds Loader Settings to the game's Options menu.
- `MenuDLUMALoaderSettings.xui` provides setting categories, editable values, contextual descriptions, and the project-reset confirmation.
- `MenuDLUMAModConfig.xui` displays configuration supplied by a mod.
- `MenuDLUMAModDetails.xui` displays complete mod metadata, identifiers, and approval information.
- `MenuDLUMACustomMainMenu.xui` selects a custom main menu.
- `DLUMA_MenuExtras.xui` adds Logs to Extras, and `MenuDLUMALogs.xui` displays the log viewer.
- `DLUMA_SteamWorkshop.xui` displays Workshop projects and their descriptions.

The files use the game's XUI XML dialect and stock element classes. Save them as UTF-8 without a BOM. Keep control IDs aligned with the native UI bindings when changing a layout. Use multiline text inside a masked scroll viewport for content whose height depends on mod metadata or translation length.

The loader redirects its main-menu asset by filename and uses the game's menu history for navigation. The `B_Mods` and `B_DLUMALogs` IDs are recognized by the native menu integration; custom menu assets can expose those IDs to provide the corresponding navigation.

