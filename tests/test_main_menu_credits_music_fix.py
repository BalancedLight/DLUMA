from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
FIXES = ROOT / "src" / "fixes"
FIX = FIXES / "main_menu_credits_music"
SOURCE = FIX / "MainMenuCreditsMusicFix.cpp"
HEADER = FIX / "MainMenuCreditsMusicFix.h"
INI = ROOT / "config" / "dluma.ini"


def _text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_default_on_config_ini_ui_and_docs_contract():
    common = _text(ROOT / "src" / "dlw_common.h")
    host = _text(ROOT / "src" / "dlw_host.c")
    ui = _text(ROOT / "src" / "dlw_ui.c")
    ini = _text(INI)

    assert "main_menu_credits_music_enabled" in common
    assert 'L"GameFixes", L"MainMenuCreditsMusic", 1' in host
    assert re.search(r"(?m)^MainMenuCreditsMusic=1$", ini)
    row = re.search(
        r'\{\s*L"GameFixes",\s*L"MainMenuCreditsMusic",\s*L"1",[\s\S]*?\},',
        ui,
    )
    assert row is not None
    assert "MAIN-MENU CREDITS MUSIC" in row.group(0)
    assert "PLAYS THE MAIN-MENU CREDITS MUSIC." in row.group(0)
    assert "DLUMA_LOADER_SETTING_BOOL" in row.group(0)
    assert "DLUMA_LOADER_CATEGORY_GAME_FIXES" in row.group(0)


def test_registry_order_and_project_compile_contract():
    registry = _text(FIXES / "FixRegistry.cpp")
    project = _text(ROOT / "DLUMA.vcxproj")

    include = '#include "main_menu_credits_music/MainMenuCreditsMusicFix.h"'
    assert include in registry
    init = registry.index("main_menu_credits_music::Initialize(")
    campaign = registry.index("custom_map_campaign_credits_exit::Initialize(")
    assert init < campaign
    shutdown = registry.index("main_menu_credits_music::Shutdown()")
    assert shutdown < registry.index("custom_map_quest_level_name::Shutdown()")
    assert "main_menu_credits_music::CurrentStatus()" in registry
    assert (
        '<ClCompile Include="src\\fixes\\main_menu_credits_music\\MainMenuCreditsMusicFix.cpp">'
        in project
    )
    assert (
        '<ClInclude Include="src\\fixes\\main_menu_credits_music\\MainMenuCreditsMusicFix.h" />'
        in project
    )


def test_exact_build_vtable_prefix_and_audio_export_guards():
    source = _text(SOURCE)

    for marker in (
        "0x623397AAU",
        "0x02082000U",
        "0x019D9898U",
        "0x04D0U",
        "0x04D8U",
        "0x08B8U",
        "0x0E50U",
        "0x0124E2C0U",
        "0x0124E560U",
        "0x0055A250U",
        "0x00937E80U",
        "0x01971248U",
        "0x01309668U",
        "0x011B70F0U",
        "0x011B7380U",
        "kShowScreenPrefix",
        "kHideScreenPrefix",
        "kCanPlayBackgroundMusicPrefix",
        "kHideModeProbe",
        "kPlayBackgroundMusicPrefix",
        "kStopBackgroundMusicPrefix",
        "ValidateTargets",
        "ValidateAudioCalls",
        "ValidateMainMenuMusicTargets",
        "GetProcAddress(context.engine_module, kPlaySound2DExport)",
        "GetProcAddress(context.engine_module, kStopSoundExport)",
        "GetProcAddress(context.engine_module, kGetObjectLevelExport)",
        "?PlaySound2D@IObject@gui@@QEAAHAEBV?$string_base@D@ttl@@_NMMMIM@Z",
        "?GetLevel@IObject@gui@@QEBAPEAVILevel@@XZ",
    ):
        assert marker in source
    assert "dl_menu_end_credits" in source
    assert "_NMMMIMI@Z" not in source
    assert "music_3.csb" not in source
    assert "LoadLibrary" not in source
    assert "ReadExact(hide + kHideModeProbeOffset" in source
    assert "kMainMenuMusicHandleOffset" not in source
    assert "StopWave" not in source


def test_front_end_only_owned_sound_lifecycle_contract():
    source = _text(SOURCE)

    assert "constexpr int kFrontEndCreditsMode = 0;" in source
    assert "bool IsFrontEndCredits(void* screen)" in source
    assert "if (!screen || !g_play_sound_2d || !IsFrontEndCredits(screen)" in source
    assert "dluma_custom_main_menu_music_lease_active" not in source
    assert "if (IsFrontEndCredits(screen)) {" in source
    assert "StopOwnedCueFor(screen);" in source
    assert "g_owned_screen" in source
    assert "g_owned_sound_handle" in source
    assert "StopOwnedCueFor(g_owned_screen);" in source
    assert source.index("g_original_show(screen, parameters)") < source.index(
        "StartOwnedCue(screen)"
    )
    assert source.index("StopOwnedCueFor(screen)") < source.index(
        "g_original_hide(screen, animated)"
    )
    assert "HookCanPlayBackgroundMusic" in source
    assert "HookAllowBackgroundMusicPlayback" in source
    assert "return 0;" in source
    assert "void* ResolveMainMenuModule(void* screen)" in source
    assert "void* StopBaseMenuMusic(void* screen)" in source
    assert "void ResumeBaseMenuMusic(void* main_menu_module)" in source
    assert "dluma_custom_main_menu_music_resolve_credits_cue" in source
    assert 'custom_override ? "custom_override" : "stock_fallback"' in source
    assert '"main_menu_credits_music_cue_name"' in source
    assert "g_stop_background_music(main_menu_module);" in source
    assert "g_play_background_music(main_menu_module);" in source
    assert "g_owned_main_menu_module" in source
    assert source.index("StopBaseMenuMusic(screen)") < source.index(
        "g_play_sound_2d("
    )
    hide_hook = source[source.index("void __fastcall HookHideScreen") : source.index("unsigned char __fastcall HookCanPlayBackgroundMusic")]
    assert hide_hook.index("g_original_hide(screen, animated)") < hide_hook.index(
        "ResumeBaseMenuMusic(main_menu_module)"
    )
    assert "bool IsInGamedll(const void* address)" in source
    for outcome in (
        "stopped",
        "resumed",
        "level_unavailable",
        "unexpected_vtable",
        "derived_main_menu_vtable",
        "resume_unavailable",
    ):
        assert f'"{outcome}"' in source


def test_play_sound_2d_uses_the_exported_abi_and_neutral_playback_rate():
    source = _text(SOURCE)

    assert (
        "float volume,\n"
        "    float fade_in, float fade_out, unsigned int bus, float playback_rate"
    ) in source
    assert "float start_time" not in source
    assert "kCreditsCueVolume = 1.0f" in source
    assert "kCreditsCueFadeIn = 1.0f" in source
    assert "kCreditsCueFadeOut = 0.0f" in source
    assert "kCreditsCueBus = 0" in source
    assert "kCreditsCuePlaybackRate = 1.0f" in source
    assert (
        "g_play_sound_2d(\n"
        "        screen, &cue, 0, kCreditsCueVolume, kCreditsCueFadeIn,\n"
        "        kCreditsCueFadeOut, kCreditsCueBus, kCreditsCuePlaybackRate)"
    ) in source


def test_pointer_slots_restore_only_loader_owned_replacements():
    source = _text(SOURCE)

    assert "dluma_atomic_pointer_compare_exchange" in source
    assert "observed == patch.original" in source
    assert "observed == patch.replacement" in source
    assert "RestorePointer(g_hide_patch)" in source
    assert "RestorePointer(g_show_patch)" in source
    assert "RestorePointer(g_can_play_patch)" in source
    assert "RestorePointer(g_allow_background_music_patch)" in source
    assert "PinLoaderForProcessExit" in source
    assert "kShutdownWaitMs" in source


class _CreditsMusicLifecycle:
    """Offline ownership model for the native show/hide/shutdown contract."""

    def __init__(self, enabled=True, supported=True):
        self.enabled = enabled
        self.supported = supported
        self.base_menu_suspended = False
        self.base_menu_resumed = False
        self.owner = None
        self.handle = 0
        self.next_handle = 1

    def show(self, screen, mode):
        if not (self.enabled and self.supported and mode == 0):
            return
        self.base_menu_suspended = True
        self.base_menu_resumed = False
        self.stop(screen=self.owner)
        self.owner = screen
        self.handle = self.next_handle
        self.next_handle += 1

    def stop(self, screen):
        if screen is not None and screen == self.owner:
            self.owner = None
            self.handle = 0

    def hide(self, screen):
        self.stop(screen)
        if self.owner is None:
            self.base_menu_suspended = False
            self.base_menu_resumed = True

    def shutdown(self):
        self.stop(self.owner)


class _EngineOwnedMenuMusicLifecycle:
    """Models cue selection while the engine retains the one menu handle."""

    def __init__(self, custom_credits=True):
        self.cue = "mes_menu"
        self.handle = 1
        self.next_handle = 2
        self.custom_credits = custom_credits

    def ordinary_menu_transition(self):
        return self.handle

    def show_credits(self):
        self.handle = 0
        self.cue = (
            "mes_credits_menu"
            if self.custom_credits
            else "dl_menu_end_credits"
        )
        self.handle = self.next_handle
        self.next_handle += 1

    def hide_credits(self):
        self.handle = 0
        self.cue = "mes_menu"
        self.handle = self.next_handle
        self.next_handle += 1


def test_lifecycle_model_open_close_repeat_disable_unsupported_and_shutdown():
    state = _CreditsMusicLifecycle()
    state.show("credits", 0)
    assert state.base_menu_suspended and state.handle == 1
    state.hide("credits")  # Back/Escape/automatic close all reach HideScreen.
    assert not state.base_menu_suspended and state.base_menu_resumed and state.handle == 0

    state.show("credits", 0)
    first = state.handle
    state.show("credits", 0)
    assert state.handle > first and state.owner == "credits"
    state.shutdown()
    assert state.handle == 0 and state.owner is None

    disabled = _CreditsMusicLifecycle(enabled=False)
    disabled.show("credits", 0)
    assert disabled.handle == 0 and not disabled.base_menu_suspended
    unsupported = _CreditsMusicLifecycle(supported=False)
    unsupported.show("credits", 0)
    assert unsupported.handle == 0 and not unsupported.base_menu_suspended
    campaign = _CreditsMusicLifecycle()
    campaign.show("campaign_credits", 1)
    assert campaign.handle == 0 and not campaign.base_menu_suspended


def test_engine_owned_main_menu_cue_survives_navigation_and_hands_off_to_credits():
    state = _EngineOwnedMenuMusicLifecycle()
    original_handle = state.handle

    for _ in range(8):
        assert state.ordinary_menu_transition() == original_handle
        assert state.cue == "mes_menu"

    state.show_credits()
    assert state.cue == "mes_credits_menu"
    assert state.handle != 0

    state.hide_credits()
    assert state.cue == "mes_menu"
    assert state.handle != 0

    fallback = _EngineOwnedMenuMusicLifecycle(custom_credits=False)
    fallback.show_credits()
    assert fallback.cue == "dl_menu_end_credits"
    fallback.hide_credits()
    assert fallback.cue == "mes_menu"


def test_header_exposes_only_registry_lifecycle_surface():
    header = _text(HEADER)
    for declaration in ("FixStatus Initialize", "FixStatus Shutdown", "FixStatus CurrentStatus"):
        assert declaration in header
    assert "PlaySound2D" not in header
