# Third-party notices

DLUMA source and examples are covered by the root MIT license except where a file or dependency carries a different notice. Dying Light, its game data, and Steam are owned by their respective rights holders.

## Lua 5.4.8

Lua is included in `third_party/lua-5.4.8/` under its MIT license. The complete license is in `third_party/lua-5.4.8/LICENSE` and is copied into release packages as `LICENSE_LUA_5.4.8.txt`.

## .NET 10

Release packages include a private Microsoft .NET runtime and Windows Desktop runtime. Their license and third-party notices are copied from the pinned runtime installation into `dluma_runtime/licenses/dotnet/`. Keep both files when redistributing the complete runtime package.

## Nodify

The visual quest editor uses Nodify 7.3.0, copyright Miroiu Emanuel, under the MIT license. The complete notice is included with the quest editor in `Licenses/Nodify-MIT.txt` and `THIRD_PARTY_NOTICES.md`.

## Game assets and mod content

The source-code license does not grant rights to Dying Light game data or to separately licensed mod content. Stock-derived menu and camera resources remain subject to their original rights, including `PakAssets/Camera/menubackgroundcameraactions_menu_lvl_new.scr`, `PakAssets/Camera/menuscreen3dplacement_menu_lvl_new.scr`, and the main-menu, options, and extras XUI resources. They are excluded from the project's MIT license. Confirm redistribution permission for these resources before publishing a package that includes them.

Check the ownership and license of any additional artwork, audio, or game content before adding it to a public source repository or Workshop item.
