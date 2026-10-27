from pathlib import Path
import argparse
import hashlib
import re
import sys
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from verify_quest_bindings import (
    BindingError,
    discover_project_quest_xml,
    validate as validate_quest_bindings,
)
from validate_localization import SUPPORTED as LOCALE_CODES, validate as validate_localization

SOURCES = (
    (ROOT / 'PakAssets/XUI/DLUMA_Main_PC_workshop.xui', 'data/menu/scr/DLM_Main_PC_workshop.xui'),
    (ROOT / 'PakAssets/XUI/DLUMA_MenuExtras.xui', 'data/menu/scr/MenuExtras.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMAMods.xui', 'data/menu/scr/MenuDLUMAMods.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMALogs.xui', 'data/menu/scr/MenuDLUMALogs.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMAModDetails.xui', 'data/menu/scr/MenuDLUMAModDetails.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMAModConfig.xui', 'data/menu/scr/MenuDLUMAModConfig.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMACustomMainMenu.xui', 'data/menu/scr/MenuDLUMACustomMainMenu.xui'),
    (ROOT / 'PakAssets/XUI/DLUMA_MenuOptions.xui', 'data/menu/scr/MenuOptions.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMALoaderSettings.xui', 'data/menu/scr/MenuDLUMALoaderSettings.xui'),
    (ROOT / 'PakAssets/XUI/MenuDLUMAValueEditor.xui', 'data/menu/scr/MenuDLUMAValueEditor.xui'),
    (ROOT / 'PakAssets/XUI/DLUMA_SteamWorkshop.xui', 'data/menu/scr/DLM_SteamWorkshop.xui'),
    (ROOT / 'PakAssets/Camera/menubackgroundcameraactions_menu_lvl_new.scr', 'data/menu/menubackgroundcameraactions_menu_lvl_new.scr'),
    (ROOT / 'PakAssets/Camera/menuscreen3dplacement_menu_lvl_new.scr', 'data/menu/menuscreen3dplacement_menu_lvl_new.scr'),
    *tuple(
        (ROOT / f'PakAssets/Locale/{code}/loader_texts_all.scr',
         f'data/locale/{code}/loader_texts_all.scr')
        for code in LOCALE_CODES
    ),
)


def packed_source_bytes(source: Path, entry_name: str) -> bytes:
    """Return the exact bytes consumed by Chrome's runtime parsers.

    Loader locale catalogs remain UTF-8 in source control for translators, but
    the retail/DevTools `texts*.scr` corpus is UTF-16LE with a BOM.  The engine
    opens a UTF-8 file successfully yet registers no String() rows, which made
    Localized_FromFile report success while XUI rendered literal &DLUMA_*& keys.
    Transcode only locale SCR entries at the deterministic PAK boundary.
    """
    normalized_entry = entry_name.replace('\\', '/')
    if normalized_entry.startswith('data/locale/') and normalized_entry.endswith('/loader_texts_all.scr'):
        text = source.read_text(encoding='utf-8-sig')
        text = text.replace('\r\n', '\n').replace('\r', '\n')
        return ('\ufeff' + text.replace('\n', '\r\n')).encode('utf-16le')
    return source.read_bytes()


def validate_quest_binding_preflight(
    bindings_path: Path | None,
    xml_paths: list[Path] | None,
    project_root: Path | None,
    project: str | None,
    map_name: str | None,
) -> None:
    """Validate an explicitly supplied custom-map binding route before packing.

    The loader PAK has no authoritative knowledge of a Workshop project's
    compiler output, so validation is opt-in at this boundary.  Once any quest
    argument is supplied, however, all custom-map identity inputs are required
    and an incomplete route is a hard packaging error.
    """
    xml_paths = xml_paths or []
    supplied = (
        bindings_path is not None
        or bool(xml_paths)
        or project_root is not None
        or project is not None
        or map_name is not None
    )
    if not supplied:
        return
    if bindings_path is None:
        raise ValueError("--quest-bindings is required for quest binding preflight")
    if project_root is None:
        raise ValueError("--quest-project-root is required for quest binding preflight")
    if map_name is None:
        raise ValueError("--quest-map is required for quest binding preflight")
    try:
        resolved_project_root = project_root.resolve()
        complete_xml_paths: list[Path] = []
        seen_xml_paths: set[str] = set()
        for path in [*xml_paths, *discover_project_quest_xml(resolved_project_root)]:
            resolved = path.resolve()
            key = str(resolved).casefold()
            if key not in seen_xml_paths:
                seen_xml_paths.add(key)
                complete_xml_paths.append(resolved)
        validate_quest_bindings(
            bindings_path.resolve(),
            complete_xml_paths,
            project_root=resolved_project_root,
            project=project,
            map_name=map_name,
        )
    except BindingError as exc:
        raise ValueError(f"quest binding preflight failed: {exc}") from exc


def validate() -> None:
    for source, entry_name in SOURCES:
        if source.suffix.casefold() != '.xui':
            continue
        try:
            ET.fromstring(source.read_bytes())
        except ET.ParseError as exc:
            raise ValueError(f'{entry_name} is not valid XML: {exc}') from exc

    localization_errors = validate_localization(check_pack=False)
    if localization_errors:
        raise ValueError('localization validation failed: ' + '; '.join(localization_errors))
    main_text = (ROOT / 'PakAssets/XUI/DLUMA_Main_PC_workshop.xui').read_text(encoding='utf-8')
    extras_path = ROOT / 'PakAssets/XUI/DLUMA_MenuExtras.xui'
    extras_text = extras_path.read_text(encoding='utf-8')
    text = (ROOT / 'PakAssets/XUI/MenuDLUMAMods.xui').read_text(encoding='utf-8')
    logs_path = ROOT / 'PakAssets/XUI/MenuDLUMALogs.xui'
    logs_text = logs_path.read_text(encoding='utf-8')
    options_text = (ROOT / 'PakAssets/XUI/DLUMA_MenuOptions.xui').read_text(encoding='utf-8')
    settings_text = (ROOT / 'PakAssets/XUI/MenuDLUMALoaderSettings.xui').read_text(encoding='utf-8')
    config_text = (ROOT / 'PakAssets/XUI/MenuDLUMAModConfig.xui').read_text(encoding='utf-8')
    custom_menu_text = (ROOT / 'PakAssets/XUI/MenuDLUMACustomMainMenu.xui').read_text(encoding='utf-8')
    details_text = (ROOT / 'PakAssets/XUI/MenuDLUMAModDetails.xui').read_text(encoding='utf-8')
    workshop_text = (ROOT / 'PakAssets/XUI/DLUMA_SteamWorkshop.xui').read_text(encoding='utf-8')
    camera_text = (ROOT / 'PakAssets/Camera/menubackgroundcameraactions_menu_lvl_new.scr').read_text(encoding='utf-8')
    placement_text = (ROOT / 'PakAssets/Camera/menuscreen3dplacement_menu_lvl_new.scr').read_text(encoding='utf-8')
    for xui_path in (extras_path, logs_path):
        xui_bytes = xui_path.read_bytes()
        if xui_bytes.startswith(b'\xef\xbb\xbf'):
            raise ValueError(f'{xui_path.name} must be UTF-8 without BOM')
    if main_text.count('<Id>T_GameVersion</Id>') != 1:
        raise ValueError('loader main menu must retain exactly one stock T_GameVersion IUIText control')
    if main_text.count('<Id>T_DLUMALoaderVersion</Id>') != 1:
        raise ValueError('loader main menu must retain exactly one loader version IUIText control')
    if main_text.count('<Id>T_DLUMALoaderCommit</Id>') != 1:
        raise ValueError('loader main menu must retain exactly one loader commit IUIText control')
    game_version_pos = main_text.find('<Id>T_GameVersion</Id>')
    game_version_block = main_text[
        main_text.rfind('<MyText>', 0, game_version_pos):
        main_text.find('</MyText>', game_version_pos)]
    if '<Position>669.359985,676.760010,0.000000</Position>' not in game_version_block:
        raise ValueError('stock T_GameVersion must remain at its normal bottom-right position')
    loader_version_pos = main_text.find('<Id>T_DLUMALoaderVersion</Id>')
    loader_version_block = main_text[
        main_text.rfind('<MyText>', 0, loader_version_pos):
        main_text.find('</MyText>', loader_version_pos)]
    if '<Position>669.359985,622.000000,0.000000</Position>' not in loader_version_block:
        raise ValueError('loader version must remain above the loader commit line')
    loader_commit_pos = main_text.find('<Id>T_DLUMALoaderCommit</Id>')
    loader_commit_block = main_text[
        main_text.rfind('<MyText>', 0, loader_commit_pos):
        main_text.find('</MyText>', loader_commit_pos)]
    if '<Position>669.359985,642.000000,0.000000</Position>' not in loader_commit_block:
        raise ValueError('loader commit must remain immediately above stock game version')
    if not re.search(r'<Text\s*(?:/>|>\s*</Text>)', loader_commit_block):
        raise ValueError('loader commit control must start blank before developer-mode presentation')
    if '{version}' in main_text:
        raise ValueError('loader main-menu XUI must not expose an unsubstituted {version} placeholder')
    # Stock newsfeed controls are owned by MenuMainDI/CNewsFeedGroup.  The
    # native Player adapter queues loader notices through that lifecycle; do
    # not add a competing loader-owned XUI ticker tree.
    for stock_id in ('newsfeed', 'T_Message'):
        if main_text.count(f'<Id>{stock_id}</Id>') != 1:
            raise ValueError(f'loader main menu must retain one stock news control: {stock_id}')
    if not re.search(r'<Properties>\s*<Id>G_NewsFeed</Id>', main_text):
        raise ValueError('loader main menu must retain the stock G_NewsFeed control')
    for forbidden_loader_ticker_id in (
            'DLUMA_newsfeed', 'G_DLUMANewsTicker',
            'I_DLUMANewsTickerBackground', 'T_DLUMANewsMessage'):
        if f'<Id>{forbidden_loader_ticker_id}</Id>' in main_text:
            raise ValueError('main menu must use the stock CNewsFeedGroup, not a duplicate loader ticker')
    if '&amp;DLUMA_Ticker_StaticFallback&amp;' not in main_text:
        raise ValueError('stock ticker must have the localized Loader fallback before native messages queue')
    mods_pos = main_text.find('<Id>B_Mods</Id>')
    if mods_pos < 0:
        raise ValueError('DLUMA main XUI is missing B_Mods')
    mods_start = main_text.rfind('<XuiNavButton>', 0, mods_pos)
    mods_end = main_text.find('</XuiNavButton>', mods_pos)
    if mods_start < 0 or mods_end < 0:
        raise ValueError('B_Mods is not enclosed by XuiNavButton')
    mods_block = main_text[mods_start:mods_end + len('</XuiNavButton>')]
    if '<XuiNavButton>' not in mods_block or '<PressPath>MenuDLUMAMods.xui</PressPath>' not in mods_block:
        raise ValueError('B_Mods must be a native XuiNavButton targeting MenuDLUMAMods.xui through PressPath')
    if '<ClassOverride>' in mods_block:
        raise ValueError('B_Mods must retain the native UINaviButton class; do not override it with UiButtonMainDi')
    if '<!--' in extras_text:
        raise ValueError('DLUMA_MenuExtras.xui must not contain XML comments')
    if (
        '<Id>MenuExtras</Id>' not in extras_text
        or '<ClassOverride>MenuExtras</ClassOverride>' not in extras_text
    ):
        raise ValueError('DLUMA_MenuExtras.xui must preserve the stock MenuExtras controller')
    for object_id in (
        'G_MenuGroup', 'G_Buttons', 'B_PatchMovie', 'B_DownloadContent',
        'B_JoinLive', 'B_RedeemCode', 'B_Store', 'B_Leaderboards',
        'B_PersonalStats', 'B_Credits', 'B_DLUMALogs',
    ):
        if extras_text.count(f'<Id>{object_id}</Id>') != 1:
            raise ValueError(f'DLUMA_MenuExtras.xui must contain exactly one {object_id}')
    logs_pos = extras_text.index('<Id>B_DLUMALogs</Id>')
    logs_start = extras_text.rfind('<AdvButton>', 0, logs_pos)
    logs_end = extras_text.find('</AdvButton>', logs_pos)
    if logs_start < 0 or logs_end < 0:
        raise ValueError('B_DLUMALogs must be enclosed by AdvButton')
    logs_route_block = extras_text[logs_start:logs_end + len('</AdvButton>')]
    if '<ClassOverride>UiButtonMainDi</ClassOverride>' not in logs_route_block:
        raise ValueError('B_DLUMALogs must use the stock Extras button controller')
    if '<PressPath>' in logs_route_block:
        raise ValueError('B_DLUMALogs must use ID routing without PressPath')
    if '&amp;DLUMA_Menu_Logs&amp;' not in logs_route_block:
        raise ValueError('B_DLUMALogs must retain its localized text contract')
    credits_pos = extras_text.index('<Id>B_Credits</Id>')
    if logs_pos < credits_pos:
        raise ValueError('B_DLUMALogs must follow Credits in reverse-rendered UIStackPanel order')
    popup_marker = '<Id>G_DLUMANewGlobalModsPopup</Id>'
    if (
        main_text.count(popup_marker) != 2
        or not re.search(r'<Properties>\s*<Id>G_DLUMANewGlobalModsPopup</Id>', main_text)
        or not re.search(r'<Timeline>\s*<Id>G_DLUMANewGlobalModsPopup</Id>', main_text)
    ):
        raise ValueError('loader-owned main popup must have one object declaration and one timeline target')
    popup_ids = (
        'G_DLUMANewGlobalModsPanel',
        'T_DLUMANewGlobalModsTitle', 'T_DLUMANewGlobalModsText',
        'I_DLUMANewGlobalModsIcon', 'B_DLUMAReviewMods', 'B_DLUMALater',
    )
    for object_id in popup_ids:
        marker = f'<Id>{object_id}</Id>'
        if main_text.count(marker) != 1:
            raise ValueError(f'loader-owned main popup ID must appear exactly once: {object_id}')
    popup_start = main_text.index(popup_marker)
    popup_tail = main_text[popup_start:]
    if '<Show>false</Show>' not in popup_tail[:400]:
        raise ValueError('loader-owned new-global popup must start hidden')
    for frame in ('Show', 'EndShow', 'Hide', 'EndHide'):
        if f'<Name>{frame}</Name>' not in popup_tail:
            raise ValueError(f'loader-owned new-global popup is missing frame {frame}')
    for stock_id in ('G_NewsPopup', 'T_PopupTxt', 'I_PopupIcon', 'B_PopupClose'):
        if f'<Id>{stock_id}</Id>' in popup_tail:
            raise ValueError(f'loader popup must not reuse stock popup ID {stock_id}')
    if '<!--' in text:
        raise ValueError('MenuDLUMAMods.xui must not contain XML comments')
    if '<ClassOverride>MenuExtras</ClassOverride>' in text:
        raise ValueError('MenuDLUMAMods.xui must not derive from MenuExtras')
    if '<ClassOverride>UIMainMenuScreen</ClassOverride>' in text:
        raise ValueError('MenuDLUMAMods.xui must not use the bare UIMainMenuScreen override')
    if '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in text:
        raise ValueError('MenuDLUMAMods.xui must use MenuSteamWorkshop')
    for object_id in (
        'L_Maps', 'B_Start', 'B_Workshop',
        'B_Configure', 'B_CustomMenuConfigure', 'B_Back',
        'B_PrevScreenMode', 'B_NextScreenMode', 'T_CurrentMode',
        'T_ModsHintHeader', 'T_ModsHintBody', 'T_ModsStatus',
        'G_ApprovalPopup', 'T_ApprovalTitle',
        'T_ApprovalName', 'T_ApprovalId', 'T_ApprovalVersion',
        'T_ApprovalSource', 'T_ApprovalRisk', 'T_ApprovalPermissions',
        'T_ApprovalChange', 'T_ApprovalScope',
        'B_ApprovalConfirm', 'B_ApprovalCancel',
    ):
        if f'<Id>{object_id}</Id>' not in text:
            raise ValueError(f'MenuDLUMAMods.xui is missing {object_id}')
    if '<UISmartText>' in text:
        raise ValueError('MenuDLUMAMods.xui must not use UISmartText with the validated UIButton text setter')
    if '<Visual>ListBoxOptionsV</Visual>' not in text:
        raise ValueError('MenuDLUMAMods must use native ListBoxOptionsV rows')
    if '<Visual>MySceneVisual</Visual>' not in text:
        raise ValueError('MenuDLUMAMods must use the stock 3D scene presentation')
    if '<PressPath>MenuDLUMAModConfig.xui</PressPath>' not in text:
        raise ValueError('MODS Configure must use native PressPath navigation')
    if '<PressPath>MenuDLUMACustomMainMenu.xui</PressPath>' not in text:
        raise ValueError('built-in Custom Main Menu Configure must use native PressPath navigation')
    if '<PressPath>MenuDLUMAModDetails.xui</PressPath>' not in text:
        raise ValueError('MODS Details must use native PressPath navigation')
    details_start = text.index('<Id>B_Workshop</Id>')
    details_end = text.index('</Properties>', details_start)
    details_block = text[details_start:details_end]
    if '<Id>B_Details</Id>' in text or '<Show>false</Show>' in details_block:
        raise ValueError(
            'MODS Details must use the visible inherited B_Workshop action'
        )
    if (
        '<PressKey>22530</PressKey>' not in details_block
        or '<UnfocusedInput>true</UnfocusedInput>' not in details_block
    ):
        raise ValueError(
            'MODS Details must use the inherited native Workshop secondary action'
        )
    if (
        '<Id>G_DetailsPopup</Id>' in text
        or '<Id>B_DetailsClose</Id>' in text
    ):
        raise ValueError('MODS must not retain the retired in-screen Details overlay')
    toggle_start = text.index('<Id>B_Start</Id>')
    toggle_end = text.index('</Properties>', toggle_start)
    if '<PressKey>' in text[toggle_start:toggle_end]:
        raise ValueError('MODS Toggle/Review must remain the native primary action')
    approval_confirm_start = text.index('<Id>B_ApprovalConfirm</Id>')
    approval_confirm_end = text.index('</Properties>', approval_confirm_start)
    if '<PressKey>' in text[approval_confirm_start:approval_confirm_end]:
        raise ValueError('MODS review confirmation must remain the native primary action')
    approval_cancel_start = text.index('<Id>B_ApprovalCancel</Id>')
    approval_cancel_end = text.index('</Properties>', approval_cancel_start)
    if '<PressKey>22529</PressKey>' not in text[approval_cancel_start:approval_cancel_end]:
        raise ValueError('MODS review cancellation must use the native Back action')
    if 'B_DLUMAQuestLog' in text or 'MenuDLUMAQuestLog.xui' in text:
        raise ValueError('MenuDLUMAMods must not expose the retired Lua Quest Log')
    main_popup_id = main_text.index('<Id>T_DLUMANewGlobalModsText</Id>')
    main_popup_start = main_text.rfind('<AdvControl>', 0, main_popup_id)
    main_popup_end = main_text.find('</AdvControl>', main_popup_id)
    if main_popup_start < 0 or main_popup_end < 0:
        raise ValueError('T_DLUMANewGlobalModsText is not enclosed by AdvControl')
    main_popup_text = main_text[
        main_popup_start:main_popup_end + len('</AdvControl>')
    ]
    if '<AdvControl>' not in main_popup_text or '<ClassOverride>UIButton</ClassOverride>' not in main_popup_text:
        raise ValueError('the new-global popup body must use a UIButton HTML presenter')
    if (
        '<!--' in options_text
        or '<!--' in settings_text
        or '<!--' in config_text
        or '<!--' in custom_menu_text
        or '<!--' in details_text
    ):
        raise ValueError('loader options XUI files must not contain XML comments')
    if '<Id>MenuOptions</Id>' not in options_text or '<ClassOverride>MenuOptions</ClassOverride>' not in options_text:
        raise ValueError('DLUMA_MenuOptions.xui must preserve the stock MenuOptions root/controller contract')
    entry_pos = options_text.find('<Id>B_DLUMALoaderSettings</Id>')
    eye_pos = options_text.find('<Id>B_EyeTracking</Id>')
    if entry_pos < 0 or eye_pos < 0 or entry_pos < eye_pos:
        raise ValueError('B_DLUMALoaderSettings must follow B_EyeTracking in reverse-rendered UIStackPanel order')
    if '<PressPath>MenuDLUMALoaderSettings.xui</PressPath>' not in options_text:
        raise ValueError('B_DLUMALoaderSettings must use native PressPath navigation')
    if '<Id>MenuDLUMALoaderSettings</Id>' not in settings_text:
        raise ValueError('loader settings XUI root is missing')
    if '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in settings_text:
        raise ValueError('loader settings XUI must use the proven MenuSteamWorkshop bridge')
    for object_id in (
        'L_Maps', 'B_Start', 'B_Workshop', 'B_Back',
        'G_MenuGroup', 'B_PrevScreenMode', 'B_NextScreenMode',
        'T_CurrentMode', 'T_SettingsHintHeader', 'T_SettingsHintBody',
        'G_SettingsEditPopup', 'E_SettingsValue',
        'B_SettingsSave', 'B_SettingsCancel', 'T_SettingsStatus',
    ):
        if f'<Id>{object_id}</Id>' not in settings_text:
            raise ValueError(f'loader settings XUI is missing {object_id}')
    if '<UIEditBox>' not in settings_text or '<MaxChars>512</MaxChars>' not in settings_text:
        raise ValueError('loader settings text entry must use a bounded stock UIEditBox')
    if '<!--' in logs_text:
        raise ValueError('MenuDLUMALogs.xui must not contain XML comments')
    if (
        '<Id>MenuDLUMALogs</Id>' not in logs_text
        or '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in logs_text
        or '<Visual>MySceneVisual</Visual>' not in logs_text
    ):
        raise ValueError('Logs XUI must use the validated MenuSteamWorkshop scene contract')
    for object_id in (
        'G_MenuGroup', 'L_Maps', 'B_Start', 'B_Workshop', 'B_Back',
        'B_PrevScreenMode', 'B_NextScreenMode', 'T_CurrentMode',
        'E_LogsSearch', 'B_Search', 'B_ApplySearch', 'T_LogsDetailHeader',
        'G_LogsRowMessages', 'T_LogRow0Message', 'T_LogRow1Message',
        'T_LogRow2Message', 'T_LogRow3Message', 'T_LogRow4Message',
        'T_LogRow5Message', 'T_LogRow6Message', 'T_LogRow7Message',
        'T_LogRow8Message', 'I_LogsListRail',
        'T_LogsDetailSeverity', 'T_LogsDetail', 'T_LogsStatus',
    ):
        if logs_text.count(f'<Id>{object_id}</Id>') != 1:
            raise ValueError(f'MenuDLUMALogs.xui must contain exactly one {object_id}')
    if (
        '<Visual>ListBoxOptionsV</Visual>' not in logs_text
        or '<UIEditBox>' not in logs_text
        or '<MaxChars>128</MaxChars>' not in logs_text
    ):
        raise ValueError('Logs must use native option rows and a bounded search edit box')
    list_pos = logs_text.index('<Id>L_Maps</Id>')
    list_end = logs_text.index('</Properties>', list_pos)
    list_block = logs_text[list_pos:list_end]
    if (
        '<NavUp>L_Maps</NavUp>' not in list_block
        or '<NavDown>L_Maps</NavDown>' not in list_block
        or '<FocusSetSelection>true</FocusSetSelection>' not in list_block
    ):
        raise ValueError('Logs list must preserve focus and clamped directional navigation')
    edit_pos = logs_text.index('<Id>E_LogsSearch</Id>')
    edit_end = logs_text.index('</Properties>', edit_pos)
    edit_block = logs_text[edit_pos:edit_end]
    if '<Text>Enter text here...</Text>' in edit_block:
        raise ValueError('Logs search must open with a genuinely empty edit buffer')
    if '<CanFocus>false</CanFocus>' not in edit_block:
        raise ValueError('Logs search edit box must remain inert outside edit mode')
    message_group_pos = logs_text.index('<Id>G_LogsRowMessages</Id>')
    if message_group_pos < list_pos:
        raise ValueError('Logs fixed message columns must render after the native list')
    for row in range(9):
        row_pos = logs_text.index(f'<Id>T_LogRow{row}Message</Id>')
        row_end = logs_text.index('</Properties>', row_pos)
        row_block = logs_text[row_pos:row_end]
        if '<Show>false</Show>' not in row_block:
            raise ValueError(
                f'Logs row message {row} must remain runtime-owned'
            )
    search_pos = logs_text.index('<Id>B_Search</Id>')
    search_end = logs_text.index('</Properties>', search_pos)
    search_block = logs_text[search_pos:search_end]
    if '<Show>false</Show>' in search_block or '<CanFocus>false</CanFocus>' in search_block:
        raise ValueError('Logs Search must remain visible and focusable')
    accept_pos = logs_text.index('<Id>B_ApplySearch</Id>')
    accept_end = logs_text.index('</Properties>', accept_pos)
    accept_block = logs_text[accept_pos:accept_end]
    for required in (
        '<Position>-4096.000000,-4096.000000,0.000000</Position>',
        '<Show>false</Show>', '<CanFocus>false</CanFocus>',
        '<UnfocusedInput>true</UnfocusedInput>', '<PressKey>22528</PressKey>',
    ):
        if required not in accept_block:
            raise ValueError(
                'Logs search accept router must remain hidden, offscreen, and input-only'
            )
    if '<Id>MenuDLUMAModConfig</Id>' not in config_text:
        raise ValueError('Mod Config XUI root is missing')
    if '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in config_text:
        raise ValueError('Mod Config must use the proven MenuSteamWorkshop bridge')
    for object_id in (
        'L_Maps', 'B_Start', 'B_Workshop', 'B_Back',
        'G_MenuGroup', 'B_PrevScreenMode', 'B_NextScreenMode',
        'T_CurrentMode', 'T_ConfigHintHeader', 'T_ConfigHintBody',
        'T_ConfigStatus', 'G_ConfigEditPopup', 'E_ConfigValue',
        'B_ConfigSave', 'B_ConfigCancel',
    ):
        if f'<Id>{object_id}</Id>' not in config_text:
            raise ValueError(f'Mod Config XUI is missing {object_id}')
    if (
        '<Visual>ListBoxOptionsV</Visual>' not in config_text
        or '<UIEditBox>' not in config_text
        or '<MaxChars>128</MaxChars>' not in config_text
    ):
        raise ValueError('Mod Config must use native option rows and a bounded text editor')
    if '<Id>MenuDLUMACustomMainMenu</Id>' not in custom_menu_text:
        raise ValueError('Custom Main Menu selector XUI root is missing')
    if '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in custom_menu_text:
        raise ValueError('Custom Main Menu selector must use the proven MenuSteamWorkshop bridge')
    for object_id in (
        'L_Maps', 'B_Start', 'B_Workshop', 'B_Back', 'G_MenuGroup',
        'T_CurrentMode', 'T_CustomMenuHintHeader',
        'T_CustomMenuHintBody', 'T_CustomMenuStatus',
    ):
        if f'<Id>{object_id}</Id>' not in custom_menu_text:
            raise ValueError(f'Custom Main Menu selector is missing {object_id}')
    if '<Visual>ListBoxOptionsV</Visual>' not in custom_menu_text:
        raise ValueError('Custom Main Menu selector must use native scrolling option rows')
    if '<PressPath>' in custom_menu_text:
        raise ValueError('Custom Main Menu selector must preserve native Back history without a forward route')
    if '<Id>MenuDLUMAModDetails</Id>' not in details_text:
        raise ValueError('Mod Details XUI root is missing')
    if '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in details_text:
        raise ValueError('Mod Details must use the proven MenuSteamWorkshop bridge')
    for object_id in (
        'L_Maps', 'B_Start', 'B_Back', 'G_MenuGroup', 'B_PrevScreenMode',
        'B_NextScreenMode', 'T_CurrentMode', 'T_DetailsHintHeader',
        'T_DetailsHintBody', 'T_DetailsStatus', 'G_DetailsValueScroll',
        'LBScrollBar', 'G_DetailsValueContent',
    ):
        if f'<Id>{object_id}</Id>' not in details_text:
            raise ValueError(f'Mod Details XUI is missing {object_id}')
    if (
        '<Visual>MySceneVisual</Visual>' not in details_text
        or '<Visual>ListBoxOptionsV</Visual>' not in details_text
        or '<UIScrollView>' not in details_text
    ):
        raise ValueError(
            'Mod Details must use the stock 3D native-row and value-scroll contract'
        )
    for scroll_contract in (
        '<ClassOverride>UIScrollView</ClassOverride>',
        '<Visual>LBScrollBarCraftingV</Visual>',
        '<ClassOverride>UIMaskedGroup</ClassOverride>',
        '<TextMaskMaterial>menu_text_clip.mat</TextMaskMaterial>',
        '<AutoSizeToText>true</AutoSizeToText>',
    ):
        if scroll_contract not in details_text:
            raise ValueError(
                f'Mod Details value scroll contract is missing {scroll_contract}'
            )
    details_view_start = details_text.index('<Id>B_Start</Id>')
    details_view_end = details_text.index('</Properties>', details_view_start)
    details_view = details_text[details_view_start:details_view_end]
    if (
        '<Text>&amp;DLUMA_ModDetails_ViewScroll&amp;</Text>' not in details_view
        or '<PressKey>' in details_view
    ):
        raise ValueError(
            'Mod Details View/scroll must remain the localized native primary action'
        )
    details_back_start = details_text.index('<Id>B_Back</Id>')
    details_back_end = details_text.index('</Properties>', details_back_start)
    if '<PressKey>22529</PressKey>' not in details_text[details_back_start:details_back_end]:
        raise ValueError('Mod Details Back must use native history')
    if (
        '<Visual>MySceneVisual</Visual>' not in settings_text
        or '<Visual>ListBoxOptionsV</Visual>' not in settings_text
        or '<Visual>ListBoxHtmlV</Visual>' in settings_text
        or '<ClassOverride>UiFloatingGroupDi</ClassOverride>' in settings_text
    ):
        raise ValueError('loader settings must use the stock 3D scene and options-list presentation contract')
    settings_list_start = settings_text.find('<Id>L_Maps</Id>')
    settings_list_end = settings_text.find('</Properties>', settings_list_start)
    settings_list = settings_text[settings_list_start:settings_list_end]
    if (
        settings_list_start < 0
        or settings_list_end < 0
        or '<Visual>ListBoxOptionsV</Visual>' not in settings_list
    ):
        raise ValueError('loader settings list must retain its native options-list control')
    if (
        '<Id>G_DLUMASettingsRail</Id>' in settings_text
        or '<Id>B_DLUMASettingsRailBack</Id>' in settings_text
        or '<Visual>ButtonBackMainVertV</Visual>' in settings_text
    ):
        raise ValueError('loader settings must delegate its animated rail and Back route to stock MenuBackgroundDL')
    if '<Position>0.000000,529.000000,0.000000</Position>' not in settings_text:
        raise ValueError('loader settings action buttons must remain below the 523-pixel options panel')
    if (
        '<Position>313.000000,63.000000,0.000000</Position>' not in settings_text
        or '<ImagePath>menu_bg</ImagePath>' not in settings_text
        or '<Visual>ButtonDialogV</Visual>' not in settings_text
        or '<UseScreenTransform>false</UseScreenTransform>' not in settings_text
        or '<Id>I_SettingsBlack</Id>' in settings_text
    ):
        raise ValueError('loader settings XUI must use the stock options-panel shell without covering the 3D environment')
    if '<!--' in workshop_text:
        raise ValueError('DLUMA_SteamWorkshop.xui must not contain XML comments')
    if (
        '<Id>MenuSteamWorkshop</Id>' not in workshop_text
        or '<ClassOverride>MenuSteamWorkshop</ClassOverride>' not in workshop_text
        or '<Visual>ListBoxCustomMapV</Visual>' not in workshop_text
        or '<Visual>ListBoxHtmlV</Visual>' in workshop_text
        or '<Id>DLUMAListBoxCustomMapV</Id>' in workshop_text
        or '<Id>DLUMAListButtonCustomMapSlotV</Id>' in workshop_text
        or '<Visual>DLUMAListBoxCustomMapV</Visual>' in workshop_text
        or '<Visual>DLUMAListButtonCustomMapSlotV</Visual>' in workshop_text
    ):
        raise ValueError('Workshop redesign must preserve the native root/controller and stock custom-map row visual without screen-level visual declarations')
    for object_id in (
        'L_Maps', 'B_Start', 'B_Workshop', 'B_Refresh', 'B_Back',
        'T_MapName', 'G_Author', 'T_AuthorValue',
        'T_WorkshopIdValue', 'G_DescriptionScroll', 'G_MaskedGroup',
        'LBScrollBar',
        'T_Description',
    ):
        if f'<Id>{object_id}</Id>' not in workshop_text:
            raise ValueError(f'DLUMA_SteamWorkshop.xui is missing {object_id}')
    start_button = workshop_text[
        workshop_text.index('<Id>B_Start</Id>'):
        workshop_text.index('<Id>B_Workshop</Id>')
    ]
    workshop_button = workshop_text[
        workshop_text.index('<Id>B_Workshop</Id>'):
        workshop_text.index('<Id>B_Refresh</Id>')
    ]
    refresh_button = workshop_text[
        workshop_text.index('<Id>B_Refresh</Id>'):
        workshop_text.index('<Id>B_Back</Id>')
    ]
    back_button = workshop_text[workshop_text.index('<Id>B_Back</Id>'):]
    if '<PressKey>' in start_button:
        raise ValueError('Workshop Play must remain the default Enter action')
    if '<PressKey>22531</PressKey>' not in workshop_button:
        raise ValueError('Browse Workshop must use the stock secondary key')
    if '<PressKey>22530</PressKey>' not in refresh_button:
        raise ValueError('Workshop Refresh must use its distinct key')
    if '<PressKey>22529</PressKey>' not in back_button:
        raise ValueError('Workshop Back must retain the stock Back key')
    for scroll_contract in (
        '<ClassOverride>UIScrollView</ClassOverride>',
        '<Visual>LBScrollBarCraftingV</Visual>',
        '<ClassOverride>UIMaskedGroup</ClassOverride>',
        '<TextMaskMaterial>menu_text_clip.mat</TextMaskMaterial>',
        '<AutoSizeToText>true</AutoSizeToText>',
    ):
        if scroll_contract not in workshop_text:
            raise ValueError(
                f'Workshop description scroll contract is missing {scroll_contract}'
            )
    if (
        'Placement("MenuDLUMALoaderSettings", "menu_options_full_pos")' not in placement_text
        or 'Correction("MenuDLUMALoaderSettings", -100, -20, 0)' not in placement_text
    ):
        raise ValueError('loader settings must have an explicit stock-options 3D placement and aspect correction')
    for screen_name in (
        'MenuDLUMAMods', 'MenuDLUMAModConfig', 'MenuDLUMACustomMainMenu'
    ):
        if (
            f'Placement("{screen_name}", "menu_extras_pos")'
            not in placement_text
            or f'Correction("{screen_name}",' in placement_text
        ):
            raise ValueError(
                f'{screen_name} must use the uncorrected right-side Extras 3D placement'
            )
        if f'MenuWithCameraActions("{screen_name}")' not in camera_text:
            raise ValueError(f'{screen_name} must be present in the camera-action merge')
    if (
        'Placement("MenuDLUMAModDetails", "menu_leaderboards_pos")'
        not in placement_text
        or 'Correction("MenuDLUMAModDetails",' in placement_text
    ):
        raise ValueError(
            'MenuDLUMAModDetails must use the uncorrected stock wide-panel 3D placement'
        )
    if 'MenuWithCameraActions("MenuDLUMAModDetails")' not in camera_text:
        raise ValueError(
            'MenuDLUMAModDetails must be present in the camera-action merge'
        )
    if 'MenuWithCameraActions("MenuDLUMALogs")' not in camera_text:
        raise ValueError('MenuDLUMALogs must be present in the camera-action merge')
    if 'Placement("MenuDLUMALogs", "menu_extras_pos")' not in placement_text:
        raise ValueError('MenuDLUMALogs must reuse the stock Extras 3D placement')
    if any(
        'CameraAction(' in line and 'MenuDLUMALogs' in line
        for line in camera_text.splitlines()
    ):
        raise ValueError('MenuDLUMALogs must preserve Extras camera history without transitions')
    if (
        'CameraAction("MenuMain", "B_Mods", "", "OnButtonPressed", "", "Movie_extras_in", "MenuDLUMAMods")'
        not in camera_text
        or 'CameraAction("MenuDLUMAMods", "", "", "OnBack", "", "Movie_extras_out", "MenuMain")'
        not in camera_text
    ):
        raise ValueError('MODS must preserve its established right-side Extras camera route')
    if any(
        'Movie_options_' in line and 'MenuDLUMA' in line
        for line in camera_text.splitlines()
    ):
        raise ValueError(
            'the MODS screen stack must not move to the left-side Options camera'
        )
    if 'MenuDLUMAQuestLog' in placement_text or 'MenuDLUMAQuestLog' in camera_text:
        raise ValueError('loader camera merges must not retain the retired Lua Quest Log')
    for stock_placement in (
        'Placement("MenuMain", "menu_main_pos")',
        'Placement("MenuOptionsControlsKeyboard", "menu_options_full_pos")',
        'Placement("MenuOptionsEyeTracking", "menu_options_full_pos")',
    ):
        if stock_placement not in placement_text:
            raise ValueError(f'loader 3D placement merge must retain stock entry: {stock_placement}')
    forbidden_entries = {
        'data/menu/scr/MenuMain_PC_workshop.xui',
        'data/menu/scr/MenuSteamWorkshop.xui',
        'data/menu/scr/hud.xui',
    }
    if forbidden_entries.intersection({entry for _, entry in SOURCES}):
        raise ValueError('content pack must not replace stock menu XUI paths')
    for path, _ in SOURCES:
        if not path.is_file():
            raise FileNotFoundError(path)


def verify_archive_sources(output: Path) -> dict[str, str]:
    """Prove every archived entry equals the bytes derived from its source."""

    with zipfile.ZipFile(output) as zf:
        names = {name.replace('\\', '/') for name in zf.namelist()}
        hashes: dict[str, str] = {}
        for source, entry_name in SOURCES:
            expected_data = packed_source_bytes(source, entry_name)
            archived_data = zf.read(entry_name)
            if archived_data != expected_data:
                raise ValueError(
                    f'content pack source mismatch: {entry_name} does not '
                    f'exactly match {source}'
                )
            hashes[entry_name] = hashlib.sha256(archived_data).hexdigest().upper()
        for code in LOCALE_CODES:
            entry = f'data/locale/{code}/loader_texts_all.scr'
            data = zf.read(entry)
            if not data.startswith(b'\xff\xfe'):
                raise ValueError(f'{entry} must be UTF-16LE with BOM in the runtime PAK')
            data.decode('utf-16')
    expected = {entry for _, entry in SOURCES}
    if names != expected:
        raise ValueError(
            'content pack archive allowlist mismatch: '
            f'missing={sorted(expected - names)}, extra={sorted(names - expected)}'
        )
    return hashes


def build(output: Path) -> None:
    validate()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for source, entry_name in SOURCES:
            info = zipfile.ZipInfo(entry_name, date_time=(2000, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            zf.writestr(
                info,
                packed_source_bytes(source, entry_name),
                compress_type=zipfile.ZIP_DEFLATED,
                compresslevel=9,
            )
    hashes = verify_archive_sources(output)
    settings_entry = 'data/menu/scr/MenuDLUMALoaderSettings.xui'
    print(
        'Verified exact packed source: '
        f'{settings_entry} SHA256={hashes[settings_entry]}'
    )


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT / 'build/package/Release/dluma.pak')
    parser.add_argument(
        '--quest-bindings',
        type=Path,
        help='optional Lua/native binding manifest to validate before packaging',
    )
    parser.add_argument(
        '--quest-xml',
        type=Path,
        action='append',
        default=[],
        help='optional extra compiled quest XML; project-root Quest XML is discovered (repeatable)',
    )
    parser.add_argument(
        '--quest-project-root',
        type=Path,
        help='custom Workshop project root for quest-binding validation',
    )
    parser.add_argument(
        '--quest-project',
        help='exact custom-project identifier (defaults to --quest-project-root name)',
    )
    parser.add_argument(
        '--quest-map',
        help='exact custom-map identifier for quest-binding validation',
    )
    args = parser.parse_args()
    try:
        validate_quest_binding_preflight(
            args.quest_bindings,
            args.quest_xml,
            args.quest_project_root,
            args.quest_project,
            args.quest_map,
        )
    except ValueError as exc:
        parser.error(str(exc))
    build(args.output.resolve())
    print(f'Built Dying Light content pack: {args.output.resolve()}')
