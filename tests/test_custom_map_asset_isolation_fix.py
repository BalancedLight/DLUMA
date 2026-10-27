from __future__ import annotations

from collections import Counter
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
FIXES = ROOT / "src" / "fixes"
CUSTOM = FIXES / "custom_map_asset_isolation"
FIX_SOURCE = CUSTOM / "CustomMapAssetIsolationFix.cpp"
FIX_HEADER = CUSTOM / "CustomMapAssetIsolationFix.h"
TRACE_SOURCE = CUSTOM / "CustomMapAssetTrace.cpp"
TRACE_HEADER = CUSTOM / "CustomMapAssetTrace.h"
INI = (
    ROOT
    / "config"
    / "dluma.ini"
)
REPORT = CUSTOM.parents[2] / "docs" / "fixes" / "custom_map_asset_isolation" / "INVESTIGATION_REPORT.md"
MANUAL = REPORT.with_name("MANUAL_TEST.md")


def _strip_cpp_comments_and_literals(source: str) -> str:
    pattern = re.compile(
        r"//[^\r\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.MULTILINE | re.DOTALL,
    )
    return pattern.sub(lambda match: " " * len(match.group(0)), source)


def _extract_cpp_function(source: str, function_name: str) -> tuple[str, str]:
    """Return one complete C++ function and its comment/literal-scrubbed form."""

    scrubbed = _strip_cpp_comments_and_literals(source)
    pattern = re.compile(rf"\b{re.escape(function_name)}\s*\(")
    for match in pattern.finditer(scrubbed):
        # A member/namespace-qualified occurrence is a call (or a definition
        # outside the source-file style covered by these contracts), not the
        # unqualified definition under test.
        prefix = scrubbed[: match.start()].rstrip()
        if prefix.endswith((".", "->", "::")):
            continue

        open_paren = scrubbed.find("(", match.start())
        paren_depth = 0
        close_paren = -1
        for index in range(open_paren, len(scrubbed)):
            if scrubbed[index] == "(":
                paren_depth += 1
            elif scrubbed[index] == ")":
                paren_depth -= 1
                if paren_depth == 0:
                    close_paren = index + 1
                    break
        assert close_paren >= 0, f"unterminated declarator for {function_name}"

        # A declaration reaches ';' first. A nested call in an if/while
        # condition has another ')' between the candidate call and the block.
        body_start = scrubbed.find("{", close_paren)
        declaration_end = scrubbed.find(";", close_paren)
        if body_start < 0 or (
            declaration_end >= 0 and declaration_end < body_start
        ):
            continue
        suffix = scrubbed[close_paren:body_start]
        if any(token in suffix for token in ("(", ")", "=", ",")):
            continue

        depth = 0
        for index in range(body_start, len(scrubbed)):
            if scrubbed[index] == "{":
                depth += 1
            elif scrubbed[index] == "}":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    return (
                        source[match.start() : end],
                        scrubbed[match.start() : end],
                    )
        raise AssertionError(f"unterminated C++ function {function_name}")
    raise AssertionError(f"missing C++ function definition {function_name}")


def _extract_cpp_struct(source: str, struct_name: str) -> tuple[str, str]:
    scrubbed = _strip_cpp_comments_and_literals(source)
    match = re.search(rf"\bstruct\s+{re.escape(struct_name)}\s*\{{", scrubbed)
    assert match is not None, f"missing C++ struct {struct_name}"
    body_start = scrubbed.find("{", match.start())
    depth = 0
    for index in range(body_start, len(scrubbed)):
        if scrubbed[index] == "{":
            depth += 1
        elif scrubbed[index] == "}":
            depth -= 1
            if depth == 0:
                end = scrubbed.find(";", index) + 1
                return source[match.start() : end], scrubbed[match.start() : end]
    raise AssertionError(f"unterminated C++ struct {struct_name}")


def _extract_cpp_calls(source: str, function_name: str) -> list[tuple[str, str]]:
    """Return balanced calls to ``function_name`` from a C++ source fragment."""

    scrubbed = _strip_cpp_comments_and_literals(source)
    pattern = re.compile(rf"\b{re.escape(function_name)}\s*\(")
    calls: list[tuple[str, str]] = []
    position = 0
    while match := pattern.search(scrubbed, position):
        open_paren = scrubbed.find("(", match.start())
        depth = 0
        for index in range(open_paren, len(scrubbed)):
            if scrubbed[index] == "(":
                depth += 1
            elif scrubbed[index] == ")":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    calls.append(
                        (
                            source[match.start() : end],
                            scrubbed[match.start() : end],
                        )
                    )
                    position = end
                    break
        else:
            raise AssertionError(f"unterminated call to {function_name}")
    return calls


def _assert_hex_bytes(source: str, values: tuple[int, ...]) -> None:
    pattern = r"\{\s*" + r"\s*,\s*".join(
        rf"0x{value:02X}" for value in values
    ) + r"\s*\}"
    assert re.search(pattern, source, re.IGNORECASE)


HOOK_ORIGINALS = {
    "HookXuiManagerConstructor": ("g_original_xui_manager_constructor", "manager,level", True),
    "HookXuiManagerDestructor": ("g_original_xui_manager_destructor", "manager", True),
    "HookXuiXmlLoad": ("g_original_xui_xml_load", "manager,path,context", True),
    "HookXuiBinaryPackLoad": ("g_original_xui_binary_pack_load", "manager,pack,output,option", True),
    "HookXuiBinaryFileLoad": ("g_original_xui_binary_file_load", "manager,path,first_option,second_option,first_output,second_output", True),
    "HookAudioVoiceFactory": ("g_original_audio_voice_factory", "owner,name,spatial,parameters", True),
    "HookAudioWorkerUpdate": ("g_original_audio_worker_update", "owner", True),
    "HookAudioBankNameQuery": ("g_original_audio_bank_name_query", "owner,name,output", True),
    "HookAudioNamedExecute": ("g_original_audio_named_execute", "name", True),
    "HookAudioScriptClear": ("g_original_audio_script_clear", "", False),
    "HookAudioScriptReload": ("g_original_audio_script_reload", "", True),
    "HookAudioUserStreamFactory": ("g_original_audio_user_stream_factory", "owner,output,channels,rate,format", True),
    "HookAudioSpeechStart": ("g_original_audio_speech_start", "manager,model,parameters", True),
    "HookAudioSpeechData": ("g_original_audio_speech_data", "manager,wave,text", True),
    "HookAudioSpeechUpdate": ("g_original_audio_speech_update", "time", False),
    "HookAudioOwnerConstructor": ("g_original_audio_owner_constructor", "owner", True),
    "HookAudioOwnerDestructor": ("g_original_audio_owner_destructor", "owner", False),
    "HookPleaseWaitFinished": (
        "g_original_please_wait_finished",
        "game_di",
        False,
    ),
    "HookDestroyingSessionCallback": (
        "g_original_destroying_session_callback",
        "game_di",
        False,
    ),
    "HookDestroyStateAdvance": (
        "g_original_destroy_state_advance",
        "game_di",
        False,
    ),
    "HookShowMenuMain": (
        "g_original_show_menu_main",
        "menu,reason,data",
        False,
    ),
    "HookChangeActiveLevel": ("g_original_change_active_level", "session", False),
    "HookChangeAfterFade": ("g_original_change_after_fade", "session", False),
    "HookSessionOnDestroy": ("g_original_session_on_destroy", "session", True),
    "HookUnloadLevel": ("g_original_unload_level", "session", False),
    "HookLoadLevel": ("g_original_load_level", "session", True),
    "HookDestroyActiveLevel": (
        "g_original_destroy_active_level",
        "game,mode,third",
        False,
    ),
    "HookUpdateDlcContent": ("g_original_update_dlc_content", "game", False),
    "HookLoadModuleBegin": (
        "g_original_load_module_begin",
        "game,name,object,progress",
        True,
    ),
    "HookLoadModuleEnd": (
        "g_original_load_module_end",
        "game,load_data,successful",
        True,
    ),
    "HookFsAddPath": ("g_original_fs_add_path", "path,flags", True),
    "HookFsAddObject": ("g_original_fs_add_object", "source,flags", False),
    "HookFsRemovePath": ("g_original_fs_remove_path", "path", True),
    "HookFsRemoveObject": ("g_original_fs_remove_object", "source", True),
    "HookFsOpen": ("g_original_fs_open", "path,mode,flags", True),
    "HookAudioFsOpenNative": (
        "g_original_fs_open_native",
        "explicit_source,full_path,mode,flags",
        True,
    ),
    "HookGameLoadPack": (
        "g_original_game_load_pack",
        "game,name,first_flag,second_flag,output_handle",
        True,
    ),
    "HookGameFreePack": ("g_original_game_free_pack", "game,name", False),
    "HookLevelReleasePacks": ("g_original_level_release_packs", "level", False),
    "HookOnDemandEvict": ("g_original_on_demand_evict", "runtime,pack", False),
    "HookInternalLevelPackRelease": (
        "g_original_internal_pack_release",
        "runtime,pack,release",
        False,
    ),
    "HookCampaignManagerLoad": (
        "g_original_campaign_manager_load",
        "manager_interface,manifest_path,campaign_argument",
        True,
    ),
    "HookParserTextFile": ("g_original_parser_text_file", "path", True),
    "HookParserTextFileDef": (
        "g_original_parser_text_file_def",
        "path,definition",
        True,
    ),
    "HookParserMemory": (
        "g_original_parser_memory",
        "input,count,source_name",
        True,
    ),
    "HookCtcParseFileConstructor": (
        "g_original_ctc_parse_file_constructor",
        "parser,option,path,owner,final_option",
        True,
    ),
    "HookCachePlayerLevelPreset": (
        "g_original_cache_player_level_preset",
        "container,primary,secondary",
        False,
    ),
    "HookRebuildPlayerLevelPreset": (
        "g_original_rebuild_player_level_preset",
        "preset",
        False,
    ),
    "HookFindPlayerLevelPreset": (
        "g_original_find_player_level_preset",
        "container,primary,secondary,type",
        True,
    ),
    "HookRemoveCachedPlayerLevelPresets": (
        "g_original_remove_cached_player_level_presets",
        "container",
        False,
    ),
    "HookLoadVariablesFromFile": (
        "g_original_load_variables_from_file",
        "path,definitions,maximum_level,type",
        False,
    ),
    "HookPlayerStateRemoveCachedPresets": (
        "g_original_player_state_remove_cached_presets",
        "state",
        False,
    ),
    "HookPlayerStateInitializeVariables": (
        "g_original_player_state_initialize_variables",
        "state",
        False,
    ),
    "HookPlayerStateSetProgressTemplate": (
        "g_original_player_state_set_progress_template",
        "state,progression,class_progression",
        False,
    ),
    "HookPlayerStateSetVariablesToLevel": (
        "g_original_player_state_set_variables_to_level",
        "state,level,type",
        False,
    ),
    "HookPlayerStateClear": (
        "g_original_player_state_clear",
        "state",
        False,
    ),
    "HookPlayerStateLoad": (
        "g_original_player_state_load",
        "state,save_context",
        True,
    ),
    "HookPlayerStateParseAppearances": (
        "g_original_player_state_parse_appearances",
        "state",
        False,
    ),
    "HookPlayerStateConstructor": (
        "g_original_player_state_constructor",
        "state",
        True,
    ),
    "HookPlayerStateCompleteDestructor": (
        "g_original_player_state_complete_destructor",
        "state",
        False,
    ),
    "HookReloadPlayerScripts": (
        "g_original_reload_player_scripts",
        "output,manager",
        True,
    ),
    "HookLocalClientRecreateRepresentation": (
        "g_original_local_client_recreate_representation",
        "client,reset_player_state",
        False,
    ),
    "HookLocalClientCreateRepresentation": (
        "g_original_local_client_create_representation",
        "local_client,argument2,argument3,argument4,position,rotation",
        False,
    ),
    "HookIgsObjectInitObject": (
        "g_original_igs_object_init_object",
        "object,parent",
        False,
    ),
    "HookPlayerDiOnCreate": (
        "g_original_player_di_on_create",
        "player_di",
        False,
    ),
    "HookPlayerDiOnDestroy": (
        "g_original_player_di_on_destroy",
        "player_di,destroy_time",
        False,
    ),
    "HookRuntime15InventoryBaseClear": (
        "g_original_inventory_base_clear",
        "inventory,reason",
        True,
    ),
    "HookRuntime15InventoryObserverRemoved": (
        "g_original_inventory_observer_removed",
        "observer,inventory,item,reason",
        False,
    ),
    "HookRuntime15InventoryBaseGetAllItems": (
        "g_original_inventory_base_get_all_items",
        "inventory,output",
        True,
    ),
    "HookTutorialPlayerDiOnCreate": (
        "g_original_tutorial_player_di_on_create",
        "player_di,argument",
        True,
    ),
    "HookPlayerVariablesGetFValue": (
        "g_original_player_variables_get_fvalue",
        "variables,property_id",
        True,
    ),
    "HookPlayerFppGetDefaultFov": (
        "g_original_player_fpp_get_default_fov",
        "player_fpp",
        True,
    ),
    "HookPlayerFppGetCameraDefaultFov": (
        "g_original_player_fpp_get_camera_default_fov",
        "camera_target",
        True,
    ),
    "HookMaterialReloadLocalMaterials": (
        "g_original_material_reload_local_materials",
        "manager",
        True,
    ),
    "HookMaterialsPackLoad": (
        "g_original_materials_pack_load",
        "pack,path,flags",
        True,
    ),
    "HookMaterialsPackDestructor": (
        "g_original_materials_pack_destructor",
        "pack",
        False,
    ),
    "HookMaterialExistingRebind": (
        "g_original_material_existing_rebind",
        "pack,binding,logical_name,material_key",
        True,
    ),
    "HookAudioScriptLoad": (
        "g_original_audio_script_load",
        "path",
        True,
    ),
    "HookAudioParserMemory": (
        "g_original_audio_parser_memory",
        "input,count,source_name,option",
        True,
    ),
    "HookSoundBankManagerLoad": (
        "g_original_sound_bank_manager_load",
        "manager,bank_name,reload_flag,localised",
        True,
    ),
    "HookAudioFmodLoadBanks": (
        "g_original_audio_fmod_load_banks",
        "audio,bank_name,localised",
        True,
    ),
    "HookToolsLoadAudioBanks": (
        "g_original_tools_load_audio_banks",
        "tools,bank_name,localised",
        True,
    ),
    "HookToolsAudioScriptExecute": (
        "g_original_tools_audio_script_execute",
        "tools,procedure_name",
        True,
    ),
    "HookSpeechBankLoad": (
        "g_original_speech_bank_load",
        "speech_bank,bank_name,localised",
        False,
    ),
    "HookSpeechBankRemoveLocalised": (
        "g_original_speech_bank_remove_localised",
        "speech_bank",
        False,
    ),
    "HookSpeechBankRemoveAll": (
        "g_original_speech_bank_remove_all",
        "speech_bank",
        False,
    ),
    "HookSpeechManagerCreate": (
        "g_original_speech_manager_create",
        "",
        True,
    ),
    "HookSpeechManagerDestroy": (
        "g_original_speech_manager_destroy",
        "",
        True,
    ),
}

MANDATORY_HOOK_STATUSES = {
    "SessionDI::ChangeActiveLevel",
    "ChangeActiveLevelAfterFadeOnSyncfr",
    "SessionDI::LoadLevel/vslot+0x958",
    "GameDI::DestroyActiveLevel",
    "SessionDI::OnDestroy/frontend reset barrier",
    "SessionDI::UnloadLevel",
    "GameDI::UpdateDLCContent",
    "IGame::LoadModuleBegin boundary",
    "IGame::LoadModuleEnd boundary",
    "fs::add_source(path)",
    "fs::add_source(source) exact-handle capture",
    "fs::rem_source(path)",
    "fs::rem_source(source)",
    "fs::open watched (gamedll IAT)",
    "fs::open watched (engine IAT)",
    "fs::open watched (imagelib IAT)",
    "fs::open watched (filesystem direct callers)",
    "IGame::LoadPack",
    "IGame::FreePack",
    "CLevel::ReleasePacks",
}

OPTIONAL_HOOK_STATUSES = {
    "GameDI::OnDestroyActiveLevelStateShowingPleaseWaitScreenFinished",
    "GameDI::OnDestroyActiveLevelStateDestroyingSessionOnSyncfr",
    "GameDI::AdvanceDestroyActiveLevelState",
    "MainMenuModule::ShowMenuMain state-6 guard",
    "CResourceLoadingRuntime::OnDemandEvictAllFromPack",
    "CResourceLoadingRuntime internal level-pack release",
    "CampaignManager::Load persistent SCampaign observer",
    "Parser_GetTreeFromTextFileDef (watched only)",
    "Parser_GetTreeFromMemory (watched scope only)",
    "Parser_GetTreeFromTextFile (watched only)",
    "CTCParse::CTCParse(file) input fingerprint",
    "GameplayParamsContainer::CachePlayerLevelPreset observer",
    "SPlayerLevelPreset base/progression/class rebuild observer",
    "SPlayerLevelPreset in-place rebuild observer",
    "GameplayParamsContainer::FindPlayerLevelPreset observer",
    "GameplayParamsContainer preset lookup observer",
    "GameplayParamsContainer natural whole-cache reset observer",
    "GameplayParamsContainer natural preset removal observer",
    "GameplayParamsContainer::LoadVariablesFromFile observer",
    "PlayerState::SetProgressTemplate observer",
    "PlayerState::RemovePrecachedLevelPresets observer",
    "PlayerState::InitializeVariables safe post-state observer",
    "PlayerState natural preset removal observer",
    "PlayerState::InitializeVariables observer",
    "PlayerState::Clear observer",
    "PlayerState::Load exact custom-route observer/prototype gate",
    "PlayerState::SetVariablesToLevel natural application observer",
    "PlayerState::SetVariablesToLevel observer",
    "PlayerState::ParseAppearancesWithPerks observer",
    "PlayerState complete-constructor inventory observer",
    "PlayerState complete-destructor inventory observer",
    "GameplayParamsManager::ReloadPlayerScripts observer",
    "LocalClientDI::RecreateRepresentation observer",
    "LocalClientDI::CreateRepresentation exact writer observer",
    "IGSObject::InitObject exact writer boundary observer",
    "PlayerDI::OnCreate PlayerState owner observer",
    "PlayerDI::OnDestroy PlayerState owner observer",
    "Runtime15 InventoryBaseForItems::Clear recorder",
    "Runtime15 InventoryBaseForItems::GetAllItems recorder",
    "Runtime15 PlayerDI inventory observer slot",
    "TutorialPlayerDI::OnCreate PlayerState owner observer",
    "CMaterialMgr::ReloadLocalMaterials observer",
    "CMaterialsPack::Load observer",
    "CMaterialsPack natural destructor observer",
    "CMaterialsPack existing-material rebind observer",
    "PlayerVariables::GetFValue filtered FOV observer",
    "PlayerFppVis::GetDefaultFOV observer",
    "PlayerFppVis::GetCameraDefaultFOV observer",
    "AudioScript::LoadScript audiodata observer",
    "ITools::LoadAudioBanks observer",
    "CAudioFMOD::LoadAudioBanks observer",
    "SoundBankManagerFMOD::LoadBank observer",
    "fs::open_native exact audio-provider observer",
    "fs::full_path exact selected-source interior observer",
    "ITools::AudioScriptExecute procedure observer",
    "SpeechBank::Load exact mutation observer",
    "SpeechBank::RemoveLocalised exact mutation observer",
    "SpeechBank::RemoveAll exact mutation observer",
    "SpeechManager singleton create/publish observer",
    "SpeechManager singleton destroy/unpublish observer",
}


def test_fix_and_trace_sources_are_registered_with_cpp_safety_flags():
    persistent_header = FIX_SOURCE.parent / "PersistentConsumerLedger.h"
    persistent_source = FIX_SOURCE.parent / "PersistentConsumerLedger.cpp"
    for path in (
        FIX_HEADER, FIX_SOURCE, TRACE_HEADER, TRACE_SOURCE,
        persistent_header, persistent_source,
    ):
        assert path.is_file()

    registry = (FIXES / "FixRegistry.cpp").read_text(encoding="utf-8")
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")

    assert '#include "custom_map_asset_isolation/CustomMapAssetIsolationFix.h"' in registry
    assert "custom_map_asset_isolation::Initialize(" in registry
    assert "custom_map_asset_isolation::Shutdown()" in registry

    for filename in (
        "CustomMapAssetIsolationFix.cpp", "CustomMapAssetTrace.cpp",
        "PersistentConsumerLedger.cpp",
    ):
        marker = f'<ClCompile Include="src\\fixes\\custom_map_asset_isolation\\{filename}">'
        start = project.index(marker)
        compile_block = project[start : project.index("</ClCompile>", start)]
        assert "<CompileAs>CompileAsCpp</CompileAs>" in compile_block
        assert "<ExceptionHandling>false</ExceptionHandling>" in compile_block
        assert "<RuntimeTypeInfo>false</RuntimeTypeInfo>" in compile_block
        assert "<LanguageStandard>stdcpp17</LanguageStandard>" in compile_block

    for filename in ("CustomMapAssetIsolationFix.h", "CustomMapAssetTrace.h"):
        assert (
            f'<ClInclude Include="src\\fixes\\custom_map_asset_isolation\\{filename}" />'
            in project
        )


def test_configuration_defaults_and_eight_full_path_watch_slots_are_explicit():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    ini = INI.read_text(encoding="utf-8")

    assert "#define DLUMA_ASSET_TRACE_MAX_WATCH_PATHS 8" in common
    assert "custom_map_asset_isolation_enabled" in common
    assert "custom_map_asset_isolation_watch_scr" in common
    assert re.search(r'L"GameFixes",\s*L"CustomMapAssetIsolation",\s*0', host)
    assert re.search(r"(?m)^CustomMapAssetIsolation=0$", ini)
    for retired_key in (
        "CustomMapAssetIsolationTrace",
        "CustomMapContentIsolation",
        "ResetSources",
        "ResetGlobalData",
        "ResetAudioBanks",
        "VerifyFrontendBaseline",
        "ExperimentalPlayerPresetRebuild",
    ):
        assert retired_key not in common
        assert retired_key not in host
        assert retired_key not in ini
    assert 'L"AssetIsolationTrace"' in host
    for index in range(1, 9):
        assert f'L"WatchScr{index}"' in host
        assert re.search(rf"(?m)^WatchScr{index}=", ini)
    assert re.search(
        r"(?m)^WatchScr1=data\\scripts\\dluma_asset_isolation_test\.scr$", ini
    )
    assert "basename-only matches are never" in ini


def test_game_fixes_ui_exposes_component_controls_without_overclaiming_status():
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    table = ui[
        ui.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings") :
        ui.index("g_dluma_loader_category_names")
    ]
    maximum = int(re.search(r"#define DLUMA_UI_MAX_SETTINGS\s+(\d+)U", ui).group(1))

    assert maximum >= table.count("DLUMA_LOADER_SETTING_")
    for section, key, fallback in (
        ("GameFixes", "CustomMapCampaignCreditsExitCrash", "1"),
        ("GameFixes", "CustomMapAssetIsolation", "0"),
        ("GameFixes", "HideInvalidWorkshopProjects", "1"),
        ("GameFixes", "CustomMapLoadingScreenContent", "1"),
    ):
        row = re.search(
            rf'\{{\s*L"{section}",\s*L"{key}",\s*L"{fallback}"[\s\S]*?\}},?', table
        )
        assert row is not None
        assert "DLUMA_LOADER_SETTING_BOOL" in row.group(0)
        assert "DLUMA_LOADER_CATEGORY_GAME_FIXES" in row.group(0)
    assert table.count("DLUMA_LOADER_CATEGORY_GAME_FIXES") == 7
    assert "RESET CUSTOM PROJECT ON EXIT" in table
    assert "EXPERIMENTAL. UNLOADS CUSTOM PROJECT RESOURCES AFTER EXIT, BUT IS NOT FULLY IMPLEMENTED." in table
    assert "TARGETED PLAYER PRESET REPAIR" not in table
    assert "DIAGNOSTICONLY MEANS TRACE HOOKS ACTIVE" not in table


def test_diagnostic_only_is_a_distinct_non_applied_registry_state():
    registry_header = (FIXES / "FixRegistry.h").read_text(encoding="utf-8")
    enum_body = re.search(
        r"enum class FixState\s*\{(?P<body>[\s\S]*?)\};", registry_header
    ).group("body")
    states = [part.strip() for part in enum_body.split(",") if part.strip()]

    assert states.count("DiagnosticOnly") == 1
    assert states.count("Applied") == 1
    assert states.index("DiagnosticOnly") != states.index("Applied")

    source = FIX_SOURCE.read_text(encoding="utf-8")
    component_enum = re.search(
        r"enum class ComponentRuntimeState\s*:[^{]+\{(?P<body>[\s\S]*?)\};",
        source,
    )
    assert component_enum is not None
    component_states = [
        part.strip()
        for part in component_enum.group("body").split(",")
        if part.strip()
    ]
    assert component_states.count("Armed") == 1
    assert component_states.count("Applied") == 1
    assert component_states.count("Verified") == 1
    assert component_states.index("Armed") < component_states.index("Applied")
    assert component_states.index("Applied") < component_states.index("Verified")

    implementation_paths = (
        *CUSTOM.glob("*.cpp"),
        *CUSTOM.glob("*.h"),
    )
    custom_sources = "\n".join(
        path.read_text(encoding="utf-8") for path in implementation_paths
    )
    executable_custom_sources = _strip_cpp_comments_and_literals(custom_sources)
    assert "MakeStatus(FixState::Applied" not in executable_custom_sources
    assert executable_custom_sources.count(
        "SetAggregateState(FixState::Applied"
    ) == 1
    _, reset_owner = _extract_cpp_function(
        source, "ExecuteFrontendResetAtPostC10"
    )
    verified_outcome = reset_owner.index(
        "outcome == cold::ColdRebootOutcome::Verified"
    )
    applied_outcome = reset_owner.index(
        "SetAggregateState(FixState::Applied)"
    )
    assert verified_outcome < applied_outcome

    initialize, _ = _extract_cpp_function(source, "Initialize")
    compact_initialize = re.sub(r"\s+", "", initialize)
    install_index = compact_initialize.index("InstallMandatoryRoutes()")
    armed_index = compact_initialize.index(
        "ComponentRuntimeState::Armed", install_index
    )
    assert install_index < armed_index
    assert (
        'dluma_log_pair("FIX","custom_content_reset_sources","armed")'
        in compact_initialize
    )
    assert "ComponentRuntimeState::Applied" not in initialize

    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    assert "ComponentRuntimeState::Applied" in reset
    _, verify = _extract_cpp_function(source, "VerifyFrontendBaseline")
    assert "ComponentRuntimeState::Verified" in verify

    manual = MANUAL.read_text(encoding="utf-8")
    assert "VFS source reset" in manual
    assert "runtime-proven exact remover is unchanged" in manual
    assert "cannot execute as a source-only fallback" in manual
    assert "Global Data and FOV rollback" in manual
    assert "Audio cold reboot" in manual
    assert "Overall frontend baseline" in manual
    assert "never a fabricated match" in manual


def test_late_worker_annotation_does_not_create_transition_generations_or_cleanup():
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    fix_source = FIX_SOURCE.read_text(encoding="utf-8")

    assert "passive_destination_observed_after_load" in host
    assert "asset_scope_transition_too_late" not in host
    annotation_start = host.index("static void dluma_asset_trace_annotate_active_level")
    annotation_end = host.index("static void dluma_asset_trace_late_transition")
    annotation = host[annotation_start:annotation_end]
    assert "BeginTransitionContext" not in annotation
    assert "!map_name || !map_name[0]" in annotation
    _, native_annotation = _extract_cpp_function(
        fix_source, "dluma_custom_map_asset_trace_active_level_observed"
    )
    assert "!level" in native_annotation
    assert native_annotation.index("!level") < native_annotation.index(
        "PublishSimple("
    )
    assert "BeginTransitionContext" not in native_annotation
    for forbidden in (
        "fs::rem_source",
        "fs::refresh",
        "IGame::FreePack",
        "OnDemandEvictAllFromPack",
        "UnregRpack",
        "ReleasePack",
    ):
        assert forbidden not in host


def test_every_production_trace_hook_forwards_exactly_once_and_preserves_returns():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    scrubbed = _strip_cpp_comments_and_literals(source)
    definitions = set(
        re.findall(
            r"\b(Hook[A-Z][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*noexcept\s*\{",
            scrubbed,
        )
    )
    assert definitions == set(HOOK_ORIGINALS)

    for wrapper, (original, expected_arguments, preserves_return) in (
        HOOK_ORIGINALS.items()
    ):
        raw_body, scrubbed_body = _extract_cpp_function(source, wrapper)
        original_calls = _extract_cpp_calls(raw_body, original)
        if wrapper == "HookSessionOnDestroy":
            # There are two mutually exclusive forwarding sites: a nested
            # OnDestroy returns immediately, while the outermost invocation
            # binds its teardown context and then forwards once.  Each dynamic
            # hook invocation therefore calls the original exactly once.
            assert len(original_calls) == 2
            for _, call in original_calls:
                assert re.sub(r"\s+", "", call) == (
                    f"{original}({expected_arguments})"
                )
            compact_body = re.sub(r"\s+", "", scrubbed_body)
            depth_guard = compact_body.index("if(g_frontend_lifecycle.depth)")
            nested_call = compact_body.index(
                f"{original}({expected_arguments})", depth_guard
            )
            nested_return = compact_body.index("returnnested_result", nested_call)
            bind = compact_body.index("BindFrontendEpochAtOnDestroy(")
            outer_call = compact_body.rindex(f"{original}({expected_arguments})")
            assert depth_guard < nested_call < nested_return < bind < outer_call
            assert compact_body.count("returnnested_result") == 1
            # A failed outer bind returns the already-preserved result early;
            # the successful outer path returns that same value at the end.
            assert compact_body.count("returnresult") == 2
            continue
        if wrapper == "HookCampaignManagerLoad":
            # A nested load fails open through one forwarding site while the
            # outer bounded observation scope uses the other.  No dynamic
            # invocation can reach both sites.
            assert len(original_calls) == 2
            for _, captured_call in original_calls:
                assert re.sub(r"\s+", "", captured_call) == (
                    f"{original}({expected_arguments})"
                )
            compact_body = re.sub(r"\s+", "", scrubbed_body)
            nested_guard = compact_body.index("if(!owns_scope)")
            nested_call = compact_body.index(
                f"{original}({expected_arguments})", nested_guard
            )
            nested_return = compact_body.index("returnresult", nested_call)
            outer_call = compact_body.rindex(
                f"{original}({expected_arguments})"
            )
            assert nested_guard < nested_call < nested_return < outer_call
            assert compact_body.count("returnresult") == 2
            continue
        if wrapper == "HookMaterialReloadLocalMaterials":
            # A recursive reload forwards through the guarded site and exits;
            # the outer observation scope forwards through the second site.
            # No dynamic invocation can reach both calls.
            assert len(original_calls) == 2
            for _, captured_call in original_calls:
                assert re.sub(r"\s+", "", captured_call) == (
                    f"{original}({expected_arguments})"
                )
            compact_body = re.sub(r"\s+", "", scrubbed_body)
            nested_guard = compact_body.index("if(scope->depth)")
            nested_call = compact_body.index(
                f"{original}({expected_arguments})", nested_guard
            )
            nested_return = compact_body.index(
                "returnnested_result", nested_call
            )
            outer_call = compact_body.rindex(
                f"{original}({expected_arguments})"
            )
            assert nested_guard < nested_call < nested_return < outer_call
            assert compact_body.count("returnnested_result") == 1
            assert compact_body.count("returnresult") == 1
            continue
        if wrapper == "HookPlayerDiOnDestroy":
            assert not original_calls
            observer_calls = _extract_cpp_calls(
                raw_body, "dluma_playerdi_ondestroy_observe_call"
            )
            assert len(observer_calls) == 1
            assert re.sub(r"\s+", "", observer_calls[0][1]) == (
                "dluma_playerdi_ondestroy_observe_call("
                f"{original},{expected_arguments})"
            )
            continue
        runtime15_helpers = {
            "HookRuntime15InventoryBaseClear": (
                "dluma_player_inventory_observe_clear_call",
                "inventory,reason",
            ),
            "HookRuntime15InventoryObserverRemoved": (
                "dluma_player_inventory_observe_removed_call",
                "observer,inventory,item,reason",
            ),
            "HookRuntime15InventoryBaseGetAllItems": (
                "dluma_player_inventory_observe_get_all_items_call",
                "inventory,output",
            ),
        }
        if wrapper in runtime15_helpers:
            helper, wrapper_arguments = runtime15_helpers[wrapper]
            assert not original_calls
            helper_calls = _extract_cpp_calls(raw_body, helper)
            assert len(helper_calls) == 1
            compact_helper = re.sub(r"\s+", "", helper_calls[0][1])
            assert compact_helper == (
                f"{helper}({original},{wrapper_arguments},"
                "Runtime15InventoryRead,Runtime15InventoryQuery,nullptr)"
            )
            continue

        assert len(original_calls) == 1, f"{wrapper} must call {original} exactly once"
        compact_call = re.sub(r"\s+", "", original_calls[0][1])
        assert compact_call == f"{original}({expected_arguments})"

        all_original_dispatches = re.findall(
            r"\bg_original_[A-Za-z0-9_]+\s*\(", scrubbed_body
        )
        assert len(all_original_dispatches) == 1
        if wrapper == "HookShowMenuMain":
            assert re.search(
                r"\bvoid\s+__fastcall\s+HookShowMenuMain\s*\(",
                scrubbed,
            )
            assert re.search(
                r"\busing\s+ShowMenuMainFn\s*=\s*void\s*\(__fastcall\s*\*\)",
                scrubbed,
            )
            assert not re.search(
                rf"\breturn\s+{re.escape(original)}\s*\(",
                scrubbed_body,
            )
        if preserves_return:
            assert re.search(
                rf"\bresult\s*=\s*{re.escape(original)}\s*\(",
                scrubbed_body,
            )
            expected_returns = (
                2 if wrapper == "HookSoundBankManagerLoad" else 1
            )
            assert len(re.findall(
                r"\breturn\s+result\s*;", scrubbed_body
            )) == expected_returns


def test_complete_hook_plan_enumerates_every_status_and_wrapper():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    raw_plan, scrubbed_plan = _extract_cpp_function(source, "BuildHookPlan")
    raw_observer_plan, scrubbed_observer_plan = _extract_cpp_function(
        source, "BuildMaterialAndPlayerObservationPlans"
    )
    _, scrubbed_disable_audio = _extract_cpp_function(
        source, "DisableAudioHookPlan"
    )
    assert scrubbed_plan.count("BuildMaterialAndPlayerObservationPlans()") == 1
    for forbidden in (
        "AddHookStatus(",
        "AddFunctionDetour(",
        "g_material_",
        "g_player_",
        "BuildMaterialAndPlayerObservationPlans(",
    ):
        assert forbidden not in scrubbed_disable_audio

    raw_complete_plan = raw_plan + "\n" + raw_observer_plan
    scrubbed_complete_plan = scrubbed_plan + "\n" + scrubbed_observer_plan
    calls_by_builder = {
        builder: _extract_cpp_calls(raw_complete_plan, builder)
        for builder in ("AddVtableHook", "AddIatHook", "AddHookStatus")
    }
    assert {name: len(calls) for name, calls in calls_by_builder.items()} == {
            "AddVtableHook": 9,
            "AddIatHook": 10,
            "AddHookStatus": 53,
    }

    observed_mandatory = set()
    observed_optional = set()
    for builder, calls in calls_by_builder.items():
        for raw_call, scrubbed_call in calls:
            if builder == "AddHookStatus" and "spec.name" in scrubbed_call:
                continue
            name_match = re.search(r'"((?:\\.|[^"\\])*)"', raw_call)
            assert name_match is not None
            final_flag = re.search(
                r",\s*(true|false)\s*\)\s*$", scrubbed_call
            )
            assert final_flag is not None
            target = (
                observed_mandatory
                if final_flag.group(1) == "true"
                else observed_optional
            )
            assert name_match.group(1) not in target
            target.add(name_match.group(1))

    optional_specs = re.search(
        r"\bOptionalDetourSpec\s+specs\s*\[\s*\]\s*=\s*\{"
        r"(?P<body>[\s\S]*?)\}\s*;",
        raw_plan,
    )
    assert optional_specs is not None
    observed_optional.update(
        re.findall(
            r'\{\s*&g_[A-Za-z0-9_]+\s*,\s*"([^"\\]+)"',
            optional_specs.group("body"),
        )
    )

    assert observed_mandatory == MANDATORY_HOOK_STATUSES
    assert observed_optional == OPTIONAL_HOOK_STATUSES

    expected_route_references = Counter(
        {
            "HookPleaseWaitFinished": 1,
            "HookDestroyingSessionCallback": 1,
            "HookDestroyStateAdvance": 1,
            "HookShowMenuMain": 1,
            "HookChangeActiveLevel": 3,
            "HookChangeAfterFade": 3,
            "HookSessionOnDestroy": 1,
            "HookUnloadLevel": 1,
            "HookLoadLevel": 3,
            "HookDestroyActiveLevel": 1,
            "HookUpdateDlcContent": 1,
            "HookLoadModuleBegin": 1,
            "HookLoadModuleEnd": 1,
            "HookFsAddPath": 1,
            "HookFsAddObject": 1,
            "HookFsRemovePath": 1,
            "HookFsRemoveObject": 1,
            "HookFsOpen": 4,
            "HookAudioFsOpenNative": 1,
            "HookGameLoadPack": 1,
            "HookGameFreePack": 1,
            "HookLevelReleasePacks": 1,
            "HookOnDemandEvict": 1,
            "HookInternalLevelPackRelease": 1,
            "HookCampaignManagerLoad": 1,
            "HookParserTextFileDef": 1,
            "HookParserMemory": 1,
            "HookParserTextFile": 1,
            "HookCtcParseFileConstructor": 1,
            "HookCachePlayerLevelPreset": 2,
            "HookRebuildPlayerLevelPreset": 2,
            "HookFindPlayerLevelPreset": 2,
            "HookRemoveCachedPlayerLevelPresets": 2,
            "HookLoadVariablesFromFile": 2,
            "HookPlayerStateRemoveCachedPresets": 2,
            "HookPlayerStateInitializeVariables": 2,
            "HookPlayerStateSetProgressTemplate": 1,
            "HookPlayerStateSetVariablesToLevel": 2,
            "HookPlayerStateClear": 1,
            "HookPlayerStateLoad": 1,
            "HookPlayerStateParseAppearances": 1,
            "HookPlayerStateConstructor": 1,
            "HookPlayerStateCompleteDestructor": 1,
            "HookReloadPlayerScripts": 1,
            "HookLocalClientRecreateRepresentation": 1,
            "HookLocalClientCreateRepresentation": 1,
            "HookIgsObjectInitObject": 1,
            "HookPlayerDiOnCreate": 1,
            "HookPlayerDiOnDestroy": 1,
            "HookRuntime15InventoryBaseClear": 1,
            "HookRuntime15InventoryObserverRemoved": 1,
            "HookRuntime15InventoryBaseGetAllItems": 1,
            "HookTutorialPlayerDiOnCreate": 1,
            "HookPlayerVariablesGetFValue": 1,
            "HookPlayerFppGetDefaultFov": 1,
            "HookPlayerFppGetCameraDefaultFov": 1,
            "HookMaterialReloadLocalMaterials": 1,
            "HookMaterialsPackLoad": 1,
            "HookMaterialsPackDestructor": 1,
            "HookMaterialExistingRebind": 1,
            "HookAudioScriptLoad": 1,
            "HookSoundBankManagerLoad": 1,
            "HookAudioFmodLoadBanks": 1,
            "HookToolsLoadAudioBanks": 1,
            "HookToolsAudioScriptExecute": 1,
            "HookSpeechBankLoad": 1,
            "HookSpeechBankRemoveLocalised": 1,
            "HookSpeechBankRemoveAll": 1,
            "HookSpeechManagerCreate": 1,
            "HookSpeechManagerDestroy": 1,
        }
    )
    observed_route_references = Counter(
        re.findall(
            r"&\s*(Hook[A-Z][A-Za-z0-9_]*)\b",
            scrubbed_complete_plan,
        )
    )
    assert observed_route_references == expected_route_references

    for evidence_name, expected_count in (
        ("update_calls", 6),
        ("fs_open_calls", 20),
    ):
        evidence = re.search(
            rf"\b{evidence_name}\s*\[\s*\]\s*=\s*\{{(?P<body>[\s\S]*?)\}}\s*;",
            scrubbed_plan,
        )
        assert evidence is not None
        entries = re.findall(
            r"\{\s*0x[0-9A-Fa-f]+U?\s*,\s*\{", evidence.group("body")
        )
        assert len(entries) == expected_count


def test_transition_generation_starts_before_unload_and_change_adopts_it():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    _, after_fade = _extract_cpp_function(source, "HookChangeAfterFade")
    _, change = _extract_cpp_function(source, "HookChangeActiveLevel")

    begin = after_fade.index("trace::BeginTransitionContext()")
    snapshot = after_fade.index("WriteTransitionLabels(")
    pending = after_fade.index("PublishPendingTransition(")
    original = after_fade.index("g_original_change_after_fade(session)")
    assert begin < snapshot < pending < original
    attestation = after_fade.index(
        "dluma_project_launch_attest_change_after_fade("
    )
    assert original < attestation

    consume = change.index("ConsumePendingTransition(session)")
    adopt = change.index("trace::PushTransitionContext(pending_generation)")
    fallback = change.index("trace::BeginTransitionContext()")
    original = change.index("g_original_change_active_level(session)")
    assert consume < adopt < original
    assert consume < fallback < original


def test_change_after_fade_publishes_exact_cross_thread_context_evidence():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    _, after_fade = _extract_cpp_function(source, "HookChangeAfterFade")
    for evidence in (
        "exact_change_after_fade_hook",
        "exact_hook_entry_rva",
        "transition_context_generation",
        "transition_context_depth",
        "transition_context_serialized",
        "transition_context_recursive",
        "transition_context_concurrent",
        "premount_frame_route_token",
        "current_frame_route_token",
        "transition_thread_id",
        "source_ownership_complete",
        "project_root_source_count",
        "data_directory_source_count",
        "assets_pc_source_count",
    ):
        assert evidence in after_fade
    assert "g_after_fade_status->validated" in after_fade
    assert "g_after_fade_status->installed" in after_fade
    assert "kChangeAfterFadeRva" in after_fade
    assert "QuarantineActiveEpochForCleanup" in after_fade
    assert "attestation.transition_thread_id ==" not in after_fade
    assert "attestation.transition_thread_id !=" not in after_fade


def test_exact_layout_and_update_owner_call_anchors_are_mandatory():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    raw_plan, _ = _extract_cpp_function(source, "BuildHookPlan")
    for rva in (
        "0x003DD83D",
        "0x003DD855",
        "0x003DD89F",
        "0x003DD8B7",
        "0x003DD8CB",
        "0x003DD8F5",
        "0x00437748",
        "0x00435564",
        "0x0043556B",
        "0x0043566A",
        "0x0000A659",
        "0x00025778",
        "0x0045F3A8",
        "0x002ED584",
        "0x00014A78",
        "0x00014A85",
        "0x00014AB0",
        "0x00014AB5",
        "0x00014AC5",
    ):
        assert rva in raw_plan
    for anchor in (
        "native_file_source_layout",
        "zip_file_source_layout",
        "resource_pack_path_layout",
        "level_pack_collection_layout",
        "source_name_path_base",
        "source_name_path_flags",
        "source_name_indirect_text",
        "source_name_indirect_test",
        "source_name_inline_text",
        "session_active_level_layout",
        "session_requested_name_layout",
        "session_current_name_layout",
        "session_name_object_layout",
    ):
        assert re.search(
            rf"mandatory_ok\s*&=.*BytesEqual\([^;]*{anchor}",
            raw_plan,
            re.DOTALL,
        )


def test_cleanup_original_exceptions_are_exact_and_barrier_scoped():
    """Cleanup originals are limited to wrappers and the exact frontend reset."""

    forbidden_direct_calls = (
        "rem_source",
        "refresh",
        "FreePack",
        "UnregRpack",
        "ReleasePack",
        "OnDemandEvictAllFromPack",
    )
    implementation_paths = (
        *CUSTOM.glob("*.cpp"),
        *CUSTOM.glob("*.h"),
    )
    for path in implementation_paths:
        scrubbed = _strip_cpp_comments_and_literals(path.read_text(encoding="utf-8"))
        for name in forbidden_direct_calls:
            assert not re.search(
                rf"(?<![A-Za-z0-9_]){re.escape(name)}\s*\(", scrubbed
            ), f"{path.name} contains loader-initiated cleanup via {name}"

    source = FIX_SOURCE.read_text(encoding="utf-8")
    scrubbed = _strip_cpp_comments_and_literals(source)
    cleanup_originals = {
        "HookFsRemovePath": "g_original_fs_remove_path",
        "HookFsRemoveObject": "g_original_fs_remove_object",
        "HookGameFreePack": "g_original_game_free_pack",
        "HookLevelReleasePacks": "g_original_level_release_packs",
        "HookOnDemandEvict": "g_original_on_demand_evict",
    }
    for wrapper, original in cleanup_originals.items():
        _, wrapper_body = _extract_cpp_function(source, wrapper)
        call_pattern = re.compile(rf"\b{re.escape(original)}\s*\(")
        assert len(call_pattern.findall(wrapper_body)) == 1

        expected_total = 3 if original == "g_original_fs_remove_object" else 1
        assert len(call_pattern.findall(scrubbed)) == expected_total

    _, prepare = _extract_cpp_function(source, "PrepareFrontendContentResetPlan")
    raw_reset, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    _, vfs_reset = _extract_cpp_function(source, "ColdVfsReset")
    assert len(_extract_cpp_calls(raw_reset, "g_original_fs_remove_object")) == 0
    assert len(_extract_cpp_calls(raw_reset, "g_original_fs_remove_path")) == 0
    assert "DetachPreparedSource(index)" in reset
    assert "RemoveAndDestroyPathCreatedSource(" not in reset
    assert "!source.exact_handle_available" in prepare
    assert "!source.path_created_ownership" in prepare
    assert "epoch.outgoing_level_release_observed" in reset
    assert "g_original_game_free_pack" not in reset
    assert "g_original_level_release_packs" not in reset
    assert "g_original_on_demand_evict" not in reset
    preflight = prepare.index(
        "g_source_reset_destructors[index] =\n"
        "            ResolveSourceDeletingDestructor(source.source)"
    )
    prepared = prepare.index("g_prepared_source_reset.prepared = true")
    prepared_gate = reset.index("!g_prepared_source_reset.prepared")
    first_unlink = reset.index("DetachPreparedSource(index)")
    assert "RemoveAndDestroyPathCreatedSource(" not in prepare
    assert "PerformFrontendContentReset(" in vfs_reset
    assert preflight < prepared
    assert prepared_gate < first_unlink

    _, exact_reset = _extract_cpp_function(
        source, "DetachRetirementSource"
    )
    assert len(_extract_cpp_calls(
        exact_reset, "g_original_fs_remove_object"
    )) == 1
    guard = exact_reset.index("PreparedSourceRegistryIdentityStillStable")
    unlink = exact_reset.index("g_original_fs_remove_object(")
    assert guard < unlink
    assert "g_source_reset_destructors" not in exact_reset
    _, retirement = _extract_cpp_function(source, "ColdVfsRetireProviders")
    assert "state.reset_result != cold::DomainResult::Succeeded" in retirement
    assert "DestroyDetachedSource(" in retirement


def test_source_preflight_is_nonmutating_and_cold_vfs_owns_the_only_reset_call():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    lifecycle_header = (CUSTOM / "FrontendSourceRollbackLifecycle.h").read_text(
        encoding="utf-8"
    )
    lifecycle_source = (CUSTOM / "FrontendSourceRollbackLifecycle.cpp").read_text(
        encoding="utf-8"
    )

    # Native observation, unconsumed-presentation cancellation, and loaded
    # project detachment are separate call sites. Destruction of loaded
    # project providers occurs only in the later retirement phase.
    assert len(_extract_cpp_calls(source, "g_original_fs_remove_path")) == 1
    assert len(_extract_cpp_calls(source, "g_original_fs_remove_object")) == 3
    _, exact_remove = _extract_cpp_function(
        source, "DetachRetirementSource"
    )
    assert len(_extract_cpp_calls(exact_remove, "g_original_fs_remove_object")) == 1
    assert "g_original_fs_remove_path(" not in exact_remove

    _, prepare = _extract_cpp_function(source, "PrepareFrontendContentResetPlan")
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    _, vfs_preflight = _extract_cpp_function(source, "ColdVfsPreflight")
    _, vfs_reset = _extract_cpp_function(source, "ColdVfsReset")
    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    _, c10 = _extract_cpp_function(source, "HookDestroyingSessionCallback")
    compact_reset = re.sub(r"\s+", "", reset)
    compact_prepare = re.sub(r"\s+", "", prepare)
    snapshot = compact_prepare.index("SnapshotActiveSourcesThroughEpochComplete(")
    preflight = compact_prepare.index("ResolveSourceDeletingDestructor(source.source)")
    prepared = compact_prepare.index("g_prepared_source_reset.prepared=true")
    removal = compact_reset.index("DetachPreparedSource(")
    reset_gate = compact_reset.index("!g_prepared_source_reset.prepared")
    assert snapshot < preflight < prepared
    assert reset_gate < removal
    assert "g_source_reset_snapshot[index]" in compact_prepare
    assert "PrepareFrontendContentResetPlan(context->content_epoch)" in vfs_preflight
    assert len(_extract_cpp_calls(vfs_reset, "PerformFrontendContentReset")) == 1
    assert "PerformFrontendContentReset(" not in on_destroy
    assert "PerformFrontendContentReset(" not in c10
    assert source.count("PerformFrontendContentReset(") == 3

    # Parser tracing is observation-only and cannot reach any cleanup owner.
    parser_region = source[
        source.index("WatchedParserScope* CurrentWatchedParserScope") :
        source.index("void* __fastcall HookFsOpen")
    ]
    parser_executable = _strip_cpp_comments_and_literals(parser_region)
    for forbidden in (
        "g_original_fs_remove_path(",
        "g_original_fs_remove_object(",
        "g_original_game_free_pack(",
        "g_original_on_demand_evict(",
        "PerformFrontendContentReset(",
        "RemoveAndDestroyPathCreatedSource(",
        "refresh(",
        "shutdown(",
        "UnregRpack(",
        "ReleasePack(",
    ):
        assert forbidden not in parser_executable
    complete_parser, _ = _extract_cpp_function(
        source, "CompleteWatchedParserScope"
    )
    assert "LookupExactCustomSourceIdentity(" in complete_parser
    for token_field in (
        "current_identity.epoch == scope->token.epoch",
        "current_identity.source_record_index ==",
        "scope->token.source_record_index",
        "current_identity.successful_add_ordinal ==",
        "scope->token.source_add_ordinal",
    ):
        assert token_field in complete_parser

    # The separately compiled lifecycle authority retains the exact caller
    # distinction and contains no parser/cache concept.
    assert re.search(r"kOnDestroyUnloadReturnRva\s*=\s*0x0*431C83U", lifecycle_header)
    assert re.search(r"kAfterFadeUnloadReturnRva\s*=\s*0x0*434DD6U", lifecycle_header)
    assert "Parser" not in lifecycle_header + lifecycle_source
    assert "Cache" not in lifecycle_header + lifecycle_source


def test_optional_parser_transaction_cannot_downgrade_source_routes():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    plan, _ = _extract_cpp_function(source, "BuildHookPlan")
    observer_plan, _ = _extract_cpp_function(
        source, "BuildMaterialAndPlayerObservationPlans"
    )
    complete_plan = plan + "\n" + observer_plan
    default_observer_statuses = {
        status for status in OPTIONAL_HOOK_STATUSES
        if "Player" in status or "preset" in status or
        "GameplayParamsContainer" in status
    }
    audio_observer_statuses = {
        status for status in OPTIONAL_HOOK_STATUSES
        if "Audio" in status or "audio" in status or
        "SoundBankManager" in status
    }
    cold_reboot_statuses = {
        status for status in OPTIONAL_HOOK_STATUSES
        if status.startswith("GameDI::OnDestroyActiveLevelState") or
        status == "GameDI::AdvanceDestroyActiveLevelState" or
        status.startswith("MainMenuModule::ShowMenuMain")
    }
    for status in OPTIONAL_HOOK_STATUSES - {
        "CResourceLoadingRuntime::OnDemandEvictAllFromPack",
        *default_observer_statuses,
        *audio_observer_statuses,
        *cold_reboot_statuses,
    }:
        call = next(
            raw
            for raw, _ in _extract_cpp_calls(complete_plan, "AddHookStatus")
            if f'"{status}"' in raw
        )
        assert re.search(r",\s*false\s*\)\s*$", call)
    for status in default_observer_statuses:
        assert f'"{status}"' in complete_plan
    for status in audio_observer_statuses:
        assert f'"{status}"' in complete_plan
    dynamic_status_call = next(
        raw
        for raw, scrubbed in _extract_cpp_calls(
            complete_plan, "AddHookStatus"
        )
        if "spec.name" in scrubbed
    )
    assert re.search(r",\s*false\s*\)\s*$", dynamic_status_call)

    _, prepare = _extract_cpp_function(source, "PrepareAllRelays")
    compact_prepare = re.sub(r"\s+", "", prepare)
    assert "IsParserHookStatus(route->owner)" in prepare
    assert "DisableParserHookPlan();continue;" in compact_prepare
    assert "IsDefaultLevelsHookStatus(route->owner)" in prepare
    assert "DisableDefaultLevelsHookPlan();continue;" in compact_prepare
    assert "IsAudioHookStatus(route->owner)" in prepare
    assert "DisableAudioHookPlan();continue;" in compact_prepare
    assert "returnfalse" not in compact_prepare[
        compact_prepare.index("if(route->owner&&!route->owner->mandatory)") :
        compact_prepare.index("ReleaseUnusedRelays();returnfalse;")
    ]

    _, install = _extract_cpp_function(source, "InstallOptionalRoutes")
    compact_install = re.sub(r"\s+", "", install)
    reverse_restore = compact_install.index(
        "for(std::size_treverse=g_hook_route_count;reverse>0;--reverse)"
    )
    disable = compact_install.index("DisableParserHookPlan();", reverse_restore)
    assert reverse_restore < disable
    assert "RestoreRoute(candidate)" in compact_install[reverse_restore:disable]
    assert "DisableDefaultLevelsHookPlan();" in compact_install
    assert "DisableAudioHookPlan();" in compact_install

    _, initialize = _extract_cpp_function(source, "Initialize")
    compact_initialize = re.sub(r"\s+", "", initialize)
    mandatory_install = compact_initialize.index("InstallMandatoryRoutes()")
    optional_install = compact_initialize.index("InstallOptionalRoutes()")
    armed = compact_initialize.index("ComponentRuntimeState::Armed")
    assert mandatory_install < optional_install < armed


def test_release_pack_label_priority_prefers_exact_on_destroy_binding():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    binding, _ = _extract_cpp_struct(source, "FrontendEpochBinding")
    assert "char outgoing_map[trace::kEventTextCapacity];" in binding
    binder, _ = _extract_cpp_function(source, "BindFrontendEpochAtOnDestroy")
    assert "binding.outgoing_map" in binder
    assert "current_level" in binder

    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    compact_destroy = re.sub(r"\s+", "", on_destroy)
    copy_map = compact_destroy.index(
        "CopyLoaderText(g_frontend_unload_map,sizeof(g_frontend_unload_map),binding.outgoing_map)"
    )
    original = compact_destroy.rindex("g_original_session_on_destroy(session)")
    assert copy_map < original

    release, _ = _extract_cpp_function(source, "HookLevelReleasePacks")
    compact_release = re.sub(r"\s+", "", release)
    on_destroy_label = compact_release.index(
        "if(exact_owned_unload_scope&&g_frontend_unload_map[0])"
    )
    transition_label = compact_release.index(
        "elseif(outgoing&&g_change_depth", on_destroy_label
    )
    annotation_label = compact_release.index(
        "if(ReadSourceLabels(&labels)", transition_label
    )
    unknown_label = compact_release.index(
        'CopyLiteral(map,sizeof(map),"<unknown>")', annotation_label
    )
    assert on_destroy_label < transition_label < annotation_label < unknown_label
    assert "AssetTraceLabelSource::OnDestroyBinding" in release
    assert "AssetTraceLabelSource::TransitionBinding" in release
    assert "AssetTraceLabelSource::ActiveMapAnnotation" in release


def test_ledger_report_carries_epoch_native_open_and_parser_metadata():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    report, _ = _extract_cpp_function(source, "WriteContentLedgerReport")
    for field in (
        "source_ptr=",
        "source_record_index=",
        "source_add_ordinal=",
        "source_kind=",
        "project_root=",
        "first_open_sequence=",
        "last_open_sequence=",
        "first_transition_generation=",
        "last_transition_generation=",
        "first_staging=",
        "last_staging=",
        "parser_outcome=",
        "parser_input_hash=",
        "parser_input_size=",
        "registry_key=",
        "registry_generation=",
        "parsed_object_epoch=",
        "existing_object=",
        "result_object=",
    ):
        assert field in report
    assert "ContentOpenStagingPhaseName(" in report
    assert "WatchedParserOutcomeName(" in report

    ledger_source = (CUSTOM / "CustomContentLedger.cpp").read_text(
        encoding="utf-8"
    )
    record_open, _ = _extract_cpp_function(ledger_source, "RecordCustomFileOpen")
    assert "ContentLifetimeClass::Unknown" in record_open
    assert "classification_complete = true" not in record_open


def test_parser_targets_are_exact_build_validated_and_hash_memory_only():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    for name, rva in (
        ("kParserTextFileDefRva", "0x012C5EB0U"),
        ("kParserMemoryRva", "0x012C6320U"),
        ("kParserTextFileRva", "0x012C64C0U"),
        ("kCtcParseFileConstructorRva", "0x012C7230U"),
    ):
        assert re.search(rf"{name}\s*=\s*{rva}", source)

    plan, _ = _extract_cpp_function(source, "BuildHookPlan")
    for callsite in (
        "0x012C5EE7U",
        "0x012C72B8U",
        "0x012C6074U",
        "0x012C63AFU",
        "0x012C650AU",
        "0x012C6551U",
        "0x013D11D8U",
    ):
        assert callsite in plan
    for prefix_name, expected in (
        (
            "parser_text_file_def_prefix",
            (0x40, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83,
             0xEC, 0x78, 0x45, 0x33, 0xFF, 0x48, 0x8B, 0xF2),
        ),
        (
            "parser_memory_prefix",
            (0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
             0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83,
             0xEC, 0x60),
        ),
        (
            "parser_text_file_prefix",
            (0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x41,
             0x54, 0x48, 0x83, 0xEC, 0x70, 0x45, 0x33, 0xE4),
        ),
        (
            "ctc_parse_file_constructor_prefix",
            (0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x57, 0x41,
             0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC,
             0x70),
        ),
    ):
        declaration = re.search(
            rf"{prefix_name}\[\]\s*=\s*\{{(?P<body>[\s\S]*?)\}};",
            plan,
        )
        assert declaration is not None
        _assert_hex_bytes(declaration.group(0), expected)

    observe_input, _ = _extract_cpp_function(source, "ObserveParserInput")
    assert "dluma_sha256_bytes_hex(input, byte_count" in observe_input
    assert "DLUMA_MAX_SCRIPT_FILE_BYTES" in observe_input
    for forbidden in (
        "g_original_fs_open(",
        "CreateFile",
        "ReadFile",
        "fopen(",
        "ifstream",
    ):
        assert forbidden not in observe_input
    assert "parser-input-exceeds-script-size-limit" in observe_input
    assert "parser-input-unreadable" in observe_input


def test_hook_producer_region_has_no_file_log_drain_xui_or_lua_io():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    end = source.index("bool AddVtableHook(")
    # Include every helper available to the wrappers, not just the wrapper
    # definitions themselves, so moving I/O one helper call away cannot evade
    # this contract.
    producer_region = _strip_cpp_comments_and_literals(source[:end])
    forbidden_calls = (
        "WriteFile",
        "WriteFileEx",
        "CreateFileA",
        "CreateFileW",
        "CloseHandle",
        "FlushFileBuffers",
        "DrainTraceRecords",
        "OpenTraceOutputs",
        "WriteSummaryFile",
        "dluma_write_utf8_file",
        "trace::Drain",
        "fopen",
        "_wfopen",
        "fwrite",
        "fprintf",
    )
    for name in forbidden_calls:
        assert not re.search(rf"\b{re.escape(name)}\s*\(", producer_region)
    # The validated pre-C08 lifecycle boundary reads the current generation.
    # This getter has no script dispatch or I/O; all other Lua-prefixed calls
    # remain forbidden in the native observation/helper region.
    producer_region = re.sub(r"\bdluma_lua_runtime_current_map_generation\s*\(\s*\)",
                             "0", producer_region)
    for prefix in ("dluma_log_", "dluma_ui_", "dluma_lua_"):
        assert not re.search(rf"\b{re.escape(prefix)}[A-Za-z0-9_]*\s*\(", producer_region)


def test_change_after_fade_snapshot_evidence_is_logged_by_the_consumer():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    hook_start = source.index("void __fastcall HookChangeAfterFade(")
    hook_end = source.index("bool AddVtableHook(")
    hook = source[hook_start:hook_end]
    drain = source[source.index("void DrainTraceRecords("):]

    assert "AssetTraceEventPayload attestation_event" in hook
    assert "PresentationSourceSetSnapshotResultName" in hook
    for field in (
        "presenter_root_raw",
        "presenter_root_canonical",
        "epoch_root_canonical",
        "root_equal",
        "source_set_snapshot_result",
        "active_source_count",
        "project_root_count",
        "data_directory_count",
        "assets_pc_count",
        "ownership_complete",
        "exact_handles_complete",
    ):
        assert f'"{field}"' in drain


def test_frontend_on_destroy_binding_retains_canonical_project_root():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    _, create = _extract_cpp_function(source, "CreateFrontendEpochBinding")
    _, clear = _extract_cpp_function(
        source, "ClearConsumedFrontendEpochBinding"
    )
    _, consume = _extract_cpp_function(
        source, "ConsumeFrontendEpochBinding"
    )

    assert "content::NormalizeProjectRoot(" in create
    assert "g_frontend_epoch_binding.value = canonical;" in create
    assert "content::ProjectRootsEqual(" in clear
    assert "content::ProjectRootsEqual(" in consume


def test_game_thread_drain_fails_closed_on_incomplete_output():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    _, open_outputs = _extract_cpp_function(source, "OpenTraceOutputs")
    _, write_summary = _extract_cpp_function(source, "WriteSummaryFile")
    _, drain = _extract_cpp_function(source, "DrainTraceRecords")
    trace_header = TRACE_HEADER.read_text(encoding="utf-8")
    trace_source = TRACE_SOURCE.read_text(encoding="utf-8")

    assert open_outputs.index("WriteTraceHeader()") < open_outputs.index(
        "WriteSummaryFile()"
    )
    assert "file_written != buffered" in drain
    assert "!FlushFileBuffers(g_trace_file)" in drain
    assert "g_summary.trace_output_incomplete = true" in drain
    assert "if (!drained_any && !final_drain)" in drain
    assert "trace_output_incomplete" in trace_header
    assert '"trace_output_incomplete="' in trace_source
    assert "kSummaryWriteCapacity = 1024 * 1024" in source
    assert "file_written == written" in write_summary
    assert "FlushFileBuffers(file)" in write_summary
    assert "CloseHandle(file)" in write_summary
    for coverage_field in (
        "trace_file_io_complete=",
        "event_stream_complete=",
        "summary_coverage_complete=",
        "rollback_barrier_coverage=",
        "watched_open_coverage=",
        "parser_cache_coverage=",
        "resource_trace_coverage=",
        "transition_trace_coverage=",
    ):
        assert coverage_field in trace_source


def test_route_transaction_source_contract_matches_failure_safe_model():
    source = FIX_SOURCE.read_text(encoding="utf-8")
    _, prepare_relay = _extract_cpp_function(source, "PrepareRelay")
    _, patch_pointer = _extract_cpp_function(source, "AtomicPatchPointer")
    _, patch_detour = _extract_cpp_function(source, "AtomicPatchDetour")
    _, validate_atomic = _extract_cpp_function(
        source, "ValidateAtomicRouteMutationSafety"
    )
    _, apply_route = _extract_cpp_function(source, "ApplyRoute")
    _, restore_route = _extract_cpp_function(source, "RestoreRoute")
    _, restore_all = _extract_cpp_function(source, "RestoreInstalledRoutes")
    _, install_mandatory = _extract_cpp_function(source, "InstallMandatoryRoutes")
    _, install_optional = _extract_cpp_function(source, "InstallOptionalRoutes")

    assert "FlushInstructionCache(GetCurrentProcess(), route->relay" in prepare_relay
    assert "_InterlockedCompareExchange64(" in patch_pointer
    assert "_InterlockedCompareExchange128(" in patch_detour
    assert "IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)" in validate_atomic
    assert "kAtomicDetourSize = 16" in source
    assert "AtomicPatchPointer" in apply_route
    assert "AtomicPatchDetour" in apply_route
    assert apply_route.index("AtomicPatchPointer") < apply_route.index(
        "route->installed = BytesEqual"
    )
    ownership_check = restore_route.index(
        "BytesEqual(route->address, route->routing_bytes"
    )
    original_write = restore_route.index("AtomicPatchPointer")
    assert ownership_check < original_write
    assert re.search(
        r"for\s*\(\s*std::size_t\s+index\s*=\s*g_hook_route_count\s*;"
        r"\s*index\s*>\s*0\s*;\s*--index\s*\)",
        restore_all,
    )
    assert "&g_hook_routes[index - 1]" in restore_all
    assert install_mandatory.count("RestoreInstalledRoutes(true)") >= 2
    assert "route->installed && !RestoreRoute(route)" in install_optional
    assert "route->enabled = false" in install_optional


def test_registry_shutdown_updates_by_fix_id_not_status_position():
    registry = (FIXES / "FixRegistry.cpp").read_text(encoding="utf-8")

    assert "FixIdEquals" in registry
    assert "UpdateStatus(statuses, custom_map_asset_isolation::Shutdown())" in registry
    assert "UpdateStatus(statuses, custom_map_quest_level_name::Shutdown())" in registry
    assert not re.search(r"statuses\s*\[\s*\d+\s*\]", registry)


def test_investigation_and_live_manual_trace_contracts_are_complete():
    report = REPORT.read_text(encoding="utf-8")
    manual = MANUAL.read_text(encoding="utf-8")

    for question in range(1, 21):
        assert re.search(rf"(?m)^{question}\. \*\*", report)
    for phrase in (
        "FixState::DiagnosticOnly",
        "Diagnostic hooks are active; asset cleanup is not enabled.",
        "script_cache_trace_unresolved",
        "passive_destination_observed_after_load",
    ):
        assert phrase in report
        if phrase == "Diagnostic hooks are active; asset cleanup is not enabled.":
            assert re.search(
                r"Diagnostic hooks are active; asset cleanup is\s+not enabled\.",
                manual,
            )
        else:
            assert phrase in manual

    for event in (
        "ChangeActiveLevelEnter",
        "ChangeActiveLevelBranch",
        "ChangeAfterFadePostUnload",
        "DestroyActiveLevelEnter",
        "UpdateDLCContentEnter",
        "LoadModuleBeginCall",
        "FsRemoveSourceEnter",
        "FsOpenWatchedResult",
        "GameLoadPackEnter",
        "GameFreePackEnter",
        "LevelReleasePacksPack",
        "QueueOverflow",
    ):
        assert event in manual
    for sequence in (
        "A → frontend → B",
        "A → B",
        "B → A",
        "A → B → A → B",
        "A → A",
        "B → B",
    ):
        assert sequence in manual
    for result in range(1, 8):
        assert re.search(rf"\*\*{result}\.", manual)


def test_trace_phase_preserves_exports_and_has_no_global_purge_dependency():
    custom_sources = "\n".join(
        path.read_text(encoding="utf-8") for path in CUSTOM.glob("*.cpp")
    )
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")

    assert "unload all rpacks" not in custom_sources.lower()
    assert "unload_all_rpacks" not in custom_sources.lower()
    assert "minhook" not in project.lower()
    assert "detours" not in project.lower()
    for export in (
        "InitializeGameScriptDLL",
        "ShutdownGameScriptDLL",
        "DLUMA_GetVersion",
    ):
        assert "__declspec(dllexport)" in exports
        assert export in exports
