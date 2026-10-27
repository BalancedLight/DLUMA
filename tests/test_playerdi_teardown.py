import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "fixes" / "custom_map_asset_isolation" / (
    "CustomMapAssetIsolationFix.cpp"
)
PROJECT = ROOT / "DLUMA.vcxproj"
CRASH = ROOT / "src" / "dlw_crash.c"


def test_playerdi_teardown_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         r"tools\build_playerdi_teardown_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "playerdi-teardown-fake"
                / "playerdi_teardown_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_playerdi_ondestroy_observer_is_compile_time_opt_in_only():
    project = PROJECT.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    assert re.search(
        r"DlumaPlayerDiOnDestroyObserverDiagnosticOptIn[\s\S]*?"
        r"Condition=\"'\$\(DlumaPlayerDiOnDestroyObserverDiagnosticOptIn\)' "
        r"== ''\">false",
        project,
    )
    assert (
        "DLUMA_ENABLE_PLAYERDI_ONDESTROY_DIAGNOSTIC_OBSERVER=1"
        in project
    )
    assert "kEnablePlayerDiOnDestroyDiagnosticObserver" in source
    assert "if (kEnablePlayerDiOnDestroyDiagnosticObserver)" in source
    assert "dluma_playerdi_ondestroy_observe_call(" in source
    assert "g_original_player_di_on_destroy(player_di)" not in source
    for stale_setting in (
        "playerdi_ondestroy_observer_enabled=",
        "PlayerDiOnDestroyObserver=",
        "playerdi_ondestroy_diagnostic_opt_in=",
    ):
        assert stale_setting not in (
            ROOT / "src" / "dlw_host.c"
        ).read_text(encoding="utf-8")


def test_default_plan_leaves_original_playerdi_entry_unpatched():
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("bool player_inventory_plan_ok =")
    end = source.index(
        "bool player_plan_ok =", start
    )
    plan = source[start:end]
    assert (
        "if (kEnablePlayerDiOnDestroyDiagnosticObserver)"
        in plan
    )
    assert (
        "reinterpret_cast<void*>(&HookPlayerDiOnDestroy)"
        not in plan
    )
    assert (
        "reinterpret_cast<void*>(\n"
        "                    &HookPlayerDiOnDestroy)"
        in plan
    )
    assert "DLUMA_PLAYERDI_ONDESTROY_STOLEN_SIZE" in plan


def test_diagnostic_wrapper_has_exact_void_this_float_abi_and_no_deep_reads():
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index(
        "void __fastcall HookPlayerDiOnDestroy("
    )
    end = source.index(
        "std::uint8_t __fastcall HookTutorialPlayerDiOnCreate(", start
    )
    wrapper = source[start:end]
    assert "void* player_di, float destroy_time" in wrapper
    assert "dluma_playerdi_ondestroy_observe_call(" in wrapper
    for forbidden in (
        "ReadBytes(",
        "PublishPlayerOwner(",
        "ObserveDestroyedState(",
        "dluma_log_",
        "ExpirePendingRepair(",
        "HeapAlloc(",
    ):
        assert forbidden not in wrapper


def test_exact_crash_decoder_is_wired_without_game_memory_mutation():
    crash = CRASH.read_text(encoding="utf-8")
    assert "DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA" in crash
    assert "dluma_playerdi_vector_copy_signature_matches(" in crash
    assert "dluma_playerdi_decode_vector_copy_fault(" in crash
    assert "__try" in crash
    assert "VirtualQuery(" in crash
    decoder_start = crash.index(
        "static void dluma_crash_emit_playerdi_vector_fault("
    )
    decoder_end = crash.index(
        "static void dluma_crash_emit_context(", decoder_start
    )
    decoder = crash[decoder_start:decoder_end]
    for mutator in (
        "VirtualProtect(",
        "WriteProcessMemory(",
        "InterlockedExchangePointer(",
        "memcpy((void*)",
    ):
        assert mutator not in decoder


def test_unvalidated_inventory_layout_is_explicitly_fail_closed():
    implementation = (
        ROOT / "src" / "dlw_playerdi_teardown.c"
    ).read_text(encoding="utf-8")
    function = implementation[
        implementation.index(
            "int dluma_playerdi_inventory_header_layout_validated(void)"
        ):
    ]
    assert "return 0;" in function
    assert "five PlayerDI container pointers" in function
    assert "inventory_header_layout_unvalidated" in (
        SOURCE.read_text(encoding="utf-8")
    )
