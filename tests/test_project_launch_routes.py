from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_all_three_project_launch_route_native_harnesses() -> None:
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_project_launch_routes_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, (
        completed.stdout + "\n" + completed.stderr
    )


def test_production_has_three_exact_owner_wrappers_and_one_gate() -> None:
    routes = (ROOT / "src/dlw_project_launch_routes.c").read_text(
        encoding="utf-8"
    )
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    for owner, premount, update_return, on_start in (
        ("MENU_CUSTOM_GAME", "0x007D9F90UL", "0x007DA1CAUL", "0x007D82B0UL"),
        ("MENU_START_LEVEL_DI", "0x0091A220UL", "0x0091A2E8UL", "0x00919A50UL"),
        ("MENU_STEAM_WORKSHOP", "0x0091C6C0UL", "0x0091C713UL", "0x0091B520UL"),
    ):
        assert f"DLUMA_PROJECT_LAUNCH_OWNER_{owner}" in routes
        assert premount in routes
        assert update_return in routes
        assert on_start in routes
    assert "dluma_ui_custom_game_premount_hook" in ui
    assert "dluma_ui_start_level_premount_hook" in ui
    assert "dluma_ui_workshop_premount_hook" in ui
    assert "dluma_ui_project_premount_hook(" in ui
    assert "dluma_project_launch_admission_begin_premount(" in ui
    assert "dluma_project_launch_admission_attest_on_start_level(" in ui
    assert "dluma_project_launch_admission_note_on_start_level_original(" in ui
    assert "dluma_ui_install_project_launch_hooks()" in ui
    assert "dluma_project_launch_validate_owner_rtti(" in ui
    assert "dluma_project_launch_validate_ui_data_void_rtti(" in ui
    assert "dluma_ui_workshop_copy_ttl_path(" in ui
    assert "dluma_ui_workshop_content_name_matches_request(" in ui
    assert "dluma_ui_workshop_bind_premount_request(" in ui
    assert "custom_content_identity_corroborated" in ui
    passthrough = ui.index(
        "Several loader-owned pages intentionally reuse the concrete"
    )
    capture = ui.index(
        "capture = dluma_ui_capture_project_premount(", passthrough
    )
    stock_screen_gate = ui.rindex(
        "!dluma_ui_is_stock_workshop_screen(screen)",
        0,
        passthrough,
    )
    assert stock_screen_gate < passthrough < capture
    assert "descriptor->premount_stolen_size" in ui
    assert "dluma_ui_make_indirect_jump(" in ui
    assert "MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE" in ui
    assert "PAGE_EXECUTE_READ" in ui
    assert "state->premount_route_bytes" in ui
    assert "state->premount_original_bytes" in ui
    assert "dluma_ui_release_project_launch_trampolines()" in ui
    assert '"isolation_disabled"' in ui


def test_workshop_direct_presenter_routes_make_an_exact_fresh_request() -> None:
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    derive = ui[
        ui.index(
            "static int "
            "dluma_ui_workshop_derive_direct_premount_identity("
        ) :
        ui.index(
            "static int dluma_ui_workshop_bind_premount_request(",
        )
    ]
    for exact_identity_step in (
        "dluma_project_root_canonicalize(",
        "dluma_utf8_to_wide(",
        "dluma_workshop_menu_find_metadata_for_project(",
        "metadata.exact_project",
        "dluma_ui_store_project_root_identity(",
    ):
        assert exact_identity_step in derive

    hook = ui[
        ui.index("static ULONG_PTR dluma_ui_project_premount_hook(") :
        ui.index(
            "static ULONG_PTR __fastcall "
            "dluma_ui_custom_game_premount_hook("
        )
    ]
    identity = hook.index(
        "direct_identity_available =\n"
        "            dluma_ui_workshop_derive_direct_premount_identity("
    )
    lock = hook.index("if (!dluma_ui_project_launch_lock())")
    direct_request = hook.index(
        "DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER", lock
    )
    gate = hook.rfind(
        "admission.admission ==\n"
        "            DLUMA_CUSTOM_PROJECT_ADMISSION_ALLOWED",
        0,
        identity,
    )
    begin = hook.index(
        "dluma_project_launch_admission_begin_premount(",
        direct_request,
    )
    assert 0 <= gate < identity < lock < direct_request < begin
    assert (
        "!g_dluma_project_launch_admission.request_valid"
        in hook[lock:direct_request]
    )
    assert "dluma_ui_all_project_launch_hooks_installed()" in hook[
        gate:identity
    ]
    assert hook.count("dluma_ui_workshop_bind_premount_request(") >= 2


def test_fresh_workshop_request_requires_verified_cancelled_cleanup() -> None:
    admission = (
        ROOT / "src/dlw_project_launch_admission.c"
    ).read_text(encoding="utf-8")
    prepare = admission[
        admission.index(
            "dluma_project_launch_admission_prepare_cancelled_cleanup("
        ) :
        admission.index(
            "dluma_project_launch_admission_note_cancelled_cleanup_result("
        )
    ]
    for required_guard in (
        "state->attestation_pending",
        "if (state->token_active)",
        "state->selected_map_body_active",
        "state->update_dlc_content_active",
        "state->transition_started || state->project_admitted",
        "DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE",
        "DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP",
        "state->token_project_root_observed",
        "state->token_data_directory_observed",
        "state->token_assets_pc_observed",
        "state->token_epoch",
        "dluma_project_new_premount_evidence_admissible(",
        "state->request_generation <= state->token_generation",
    ):
        assert required_guard in prepare

    result = admission[
        admission.index(
            "dluma_project_launch_admission_note_cancelled_cleanup_result("
        ) :
        admission.index(
            "dluma_project_launch_admission_begin_premount("
        )
    ]
    for required_proof in (
        "result->succeeded",
        "result->prior_epoch_consumed",
        "result->live_registry_verified_absent",
        "result->loader_sources_unchanged",
        "result->replacement_source_absent",
        "result->active_source_count_after == 0U",
        "state->attestation_pending = 0U",
        "DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED",
        "state->admission_blocked = 1U",
    ):
        assert required_proof in result

    begin = admission[
        admission.index(
            "dluma_project_launch_admission_begin_premount("
        ) :
        admission.index(
            "void dluma_project_launch_admission_note_selected_map_original("
        )
    ]
    registry_gate = begin.index(
        "if (!evidence->restart_registry_can_admit)"
    )
    pending_gate = begin.index(
        "if (state->attestation_pending)"
    )
    arm = begin.index("DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT")
    assert registry_gate < pending_gate < arm
    assert (
        "result = DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN"
        in begin[pending_gate:arm]
    )

    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    prepare_call = ui.index(
        "dluma_project_launch_admission_prepare_cancelled_cleanup("
    )
    cleanup_call = ui.index(
        "dluma_custom_map_cancelled_presentation_cleanup(",
        prepare_call,
    )
    result_call = ui.index(
        "dluma_project_launch_admission_note_cancelled_cleanup_result(",
        cleanup_call,
    )
    begin_call = ui.index(
        "dluma_project_launch_admission_begin_premount(",
        result_call,
    )
    assert prepare_call < cleanup_call < result_call < begin_call
    assert (
        '"custom_project_attestation_pending",\n'
        '            "superseded_by_fresh_workshop_activation"'
    ) in ui


def test_report_retains_one_bounded_live_hit_per_owner_and_attestation_tuple() -> None:
    header = (ROOT / "src/dlw_project_launch_admission.h").read_text(
        encoding="utf-8"
    )
    report = (
        ROOT
        / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "premount_owner_hit_count[" in header
    assert "premount_owner_hits[" in header
    for field in (
        "custom_project_premount_owner_hit owner=",
        "content_name=",
        "content_identity_corroborated=",
        "custom_project_attestation_campaign=",
        "custom_project_attestation_map=",
        "custom_project_attestation_profile=",
        "custom_project_attestation_original_invocation_count=",
    ):
        assert field in report


def test_update_observer_binds_only_exact_selected_map_callers() -> None:
    source = (
        ROOT
        / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    for return_rva in ("0x007DA1CAU", "0x0091A2E8U", "0x0091C713U"):
        assert return_rva in source
    enter = source.index("dluma_project_launch_note_update_dlc_content(")
    original = source.index("g_original_update_dlc_content(game);", enter)
    assert enter < original
