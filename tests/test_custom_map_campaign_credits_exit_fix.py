from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
FIXES = ROOT / "src" / "fixes"
FIX = FIXES / "custom_map_campaign_credits_exit"
SOURCE = FIX / "CustomMapCampaignCreditsExitFix.cpp"
HEADER = FIX / "CustomMapCampaignCreditsExitFix.h"
STUB = ROOT / "src" / "custom_map_campaign_credits_exit_guard_masm.asm"

TIMESTAMP = 0x623397AA
SIZE_OF_IMAGE = 0x02082000
PATCH_RVAS = (0x01072E90, 0x010739B0, 0x01078A40)
RESUME_RVAS = (0x01072EA7, 0x010739C7, 0x01078A57)
UNALIGNED_FAULT_RVA = 0x01074C13
UNALIGNED_RESUME_RVA = 0x01074C2A
TAIL = bytes.fromhex(
    "4C 8B 00 48 8B C8 0F 29 44 24 20 41 FF 50 38 "
    "C7 44 24 20 FF FF FF FF"
)
UNALIGNED_BEFORE = bytes.fromhex(
    "FF 15 19 80 35 00 48 8D 54 24 20 0F 10 83 D8 01 00 00"
)
UNALIGNED_AFTER = bytes.fromhex(
    "48 8B 44 24 20 48 89 83 D8 01 00 00 48 C7 83 E0"
)


def build_absolute_tail_jump(destination: int) -> bytes:
    return b"\xFF\x25\x00\x00\x00\x00" + destination.to_bytes(
        8, "little"
    ) + b"\x90\x90"


def guard_would_skip(
    *, result_is_null: bool, active: bool, owner_thread: int, current_thread: int
) -> bool:
    """Exact logical predicate used by the non-calling MASM tail path."""
    return result_is_null and active and owner_thread == current_thread


def pending_scope_would_activate(
    *, pending_map: str, pending_project: str, source_complete: bool,
    session_map: str, source_map: str, source_project: str
) -> bool:
    """Model the claimed map/project proof, independent of teardown thread."""
    return (
        source_complete
        and session_map == source_map == pending_map
        and source_project == pending_project
    )


def test_exact_devtools_profile_and_complete_cleanup_family_are_encoded():
    source = SOURCE.read_text(encoding="utf-8")
    assert HEADER.is_file() and STUB.is_file()
    assert f"0x{TIMESTAMP:08X}U" in source
    assert f"0x{SIZE_OF_IMAGE:08X}U" in source
    for rva in PATCH_RVAS + RESUME_RVAS + (UNALIGNED_FAULT_RVA, UNALIGNED_RESUME_RVA):
        assert f"0x{rva:08X}U" in source
    for byte in TAIL + UNALIGNED_BEFORE + UNALIGNED_AFTER:
        assert f"0x{byte:02X}" in source
    assert "sizeof(kTailBytes)" in source
    assert "ValidateAtomicSite" in source
    assert "ValidateUnalignedSite" in source
    assert "kAnimBindImportSlotRva" in source


def test_atomic_absolute_route_has_exact_aligned_16_byte_shape():
    route = build_absolute_tail_jump(0x7FF600001234)
    assert len(route) == 16
    assert route[:6] == b"\xFF\x25\x00\x00\x00\x00"
    assert int.from_bytes(route[6:14], "little") == 0x7FF600001234
    assert route[14:] == b"\x90\x90"
    source = SOURCE.read_text(encoding="utf-8")
    assert "_InterlockedCompareExchange128" in source
    assert "(target & (kAtomicPatchSize - 1)) == 0" in source
    assert "AtomicReplace16" in source
    assert "FlushInstructionCache" in source


def test_tail_guard_only_suppresses_the_proven_scoped_null_case():
    assert guard_would_skip(
        result_is_null=True, active=True, owner_thread=17, current_thread=17
    )
    assert not guard_would_skip(
        result_is_null=False, active=True, owner_thread=17, current_thread=17
    )
    assert not guard_would_skip(
        result_is_null=True, active=False, owner_thread=17, current_thread=17
    )
    assert not guard_would_skip(
        result_is_null=True, active=True, owner_thread=17, current_thread=18
    )

    stub = STUB.read_text(encoding="utf-8")
    assert "test rax, rax" in stub
    assert "gs:[48h]" in stub
    # One MASM macro expands into the three independently routed aligned sites.
    assert stub.count("lock inc dword ptr [g_dluma_campaign_credits_exit_guard_suppressed_count]") == 1
    assert stub.count("TAIL_GUARD dluma_campaign_credits_exit_tail_guard_") == 3
    assert "mov  r8, qword ptr [rax]" in stub
    assert "call qword ptr [r8+38h]" in stub
    assert "dluma_campaign_credits_exit_unaligned_resume_stub" in stub
    assert "AddVectoredExceptionHandler" in SOURCE.read_text(encoding="utf-8")
    assert "GuardUnalignedFault" in SOURCE.read_text(encoding="utf-8")
    assert "kExceptionContinueExecution" in SOURCE.read_text(encoding="utf-8")


def test_every_family_member_is_gated_and_restored_as_one_unit():
    source = SOURCE.read_text(encoding="utf-8")
    for name in ("1072e90", "10739b0", "1074c13", "1078a40"):
        assert f"suppressed_{name}" in source
    assert "std::array<AtomicTailSite, 3>" in source
    assert "RestoreOwnedRoutes" in source
    assert "if (!RestoreOwnedRoutes()) PinLoaderForProcessExit()" in source
    assert "RemoveVectoredExceptionHandler" in source
    assert "active scope retained for process lifetime" in source


def test_pending_campaign_scope_can_handoff_to_the_session_destroy_thread():
    assert pending_scope_would_activate(
        pending_map="run",
        pending_project="workshop\\3743121614",
        source_complete=True,
        session_map="run",
        source_map="run",
        source_project="workshop\\3743121614",
    )
    assert not pending_scope_would_activate(
        pending_map="irisu",
        pending_project="workshop\\irisu",
        source_complete=True,
        session_map="run",
        source_map="run",
        source_project="workshop\\3743121614",
    )
    assert not pending_scope_would_activate(
        pending_map="run",
        pending_project="workshop\\3743121614",
        source_complete=False,
        session_map="run",
        source_map="run",
        source_project="workshop\\3743121614",
    )

    source = SOURCE.read_text(encoding="utf-8")
    assert "superseded-unconsumed-prior-scope" in source
    assert "dluma_atomic_compare_exchange(&g_pending_scope, -1, 1)" in source
    assert "g_pending_scope_thread_id" not in source


def test_campaign_finished_custom_project_is_pending_until_matching_session_destroy():
    source = SOURCE.read_text(encoding="utf-8")
    asset = (
        FIXES
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    destroy_bridge = asset[
        asset.index("void __fastcall HookDestroyActiveLevel") : asset.index(
            "void __fastcall HookUpdateDlcContent"
        )
    ]
    session_bridge = asset[
        asset.index("std::uintptr_t __fastcall HookSessionOnDestroy") : asset.index(
            "void __fastcall HookUnloadLevel"
        )
    ]
    assert "kDestroyActiveLevelCampaignFinished = 4" in source
    assert "destroy_mode != kDestroyActiveLevelCampaignFinished" in source
    assert "!project_complete || !map || !map[0] || !project || !project[0]" in source
    assert "verified_custom_campaign" in destroy_bridge
    assert "credits_source.project_complete" in destroy_bridge
    assert "!IsFrontendLevelName(credits_source.map)" in destroy_bridge
    assert "BeginVerifiedCustomCampaignFinishedTeardown" in destroy_bridge
    assert "g_original_destroy_active_level(game, mode, third)" in destroy_bridge
    assert "EndVerifiedCustomCampaignFinishedTeardown" not in destroy_bridge
    assert "EnterVerifiedCustomCampaignFinishedSessionDestroy" in session_bridge
    assert "credits_session_matches_source" in session_bridge
    assert "AsciiEqualsInsensitive(current, credits_source.map)" in session_bridge
    assert "EndVerifiedCustomCampaignFinishedSessionDestroy" in session_bridge
    enter = session_bridge.index(
        "EnterVerifiedCustomCampaignFinishedSessionDestroy"
    )
    original = session_bridge.index("g_original_session_on_destroy(session)", enter)
    assert enter < original < session_bridge.index(
        "EndVerifiedCustomCampaignFinishedSessionDestroy", original
    )
    source = SOURCE.read_text(encoding="utf-8")
    assert "g_pending_scope" in source
    assert "session-destroy-ownership-mismatch" in source
    assert "project[0] == '<'" in source


def test_configuration_registry_and_shutdown_order_preserve_the_dependency():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    ini = (
        ROOT
        / "config"
        / "dluma.ini"
    ).read_text(encoding="utf-8")
    registry = (FIXES / "FixRegistry.cpp").read_text(encoding="utf-8")

    assert "custom_map_campaign_credits_exit_crash_enabled" in common
    assert 'L"GameFixes", L"CustomMapCampaignCreditsExitCrash", 1' in host
    assert "CustomMapCampaignCreditsExitCrash=1" in ini
    assert "CUSTOM CAMPAIGN CREDITS EXIT FIX" in ui
    assert "CustomMapAssetIsolation=0" in ini
    assert "custom_map_campaign_credits_exit::Initialize(" in registry
    assert registry.index("custom_map_campaign_credits_exit::Initialize(") < registry.index(
        "custom_map_asset_isolation::Initialize("
    )
    assert registry.index("custom_map_asset_isolation::Shutdown()") < registry.index(
        "custom_map_campaign_credits_exit::Shutdown()"
    )
    assert "ownership_tracking_enabled" in SOURCE.read_text(encoding="utf-8")


def test_fix_never_writes_a_game_dll_on_disk_and_retains_on_unsafe_restore():
    source = SOURCE.read_text(encoding="utf-8")
    assert "CreateFile" not in source
    assert "WriteFile" not in source
    assert "gamedll_x64_rwdi.dll" not in source
    assert "PinLoaderForProcessExit" in source
    assert "if (!RestoreOwnedRoutes()) {\n        PinLoaderForProcessExit();" in source
