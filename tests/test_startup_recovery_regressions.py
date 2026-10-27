from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_profile_cache_flush_is_best_effort_not_marker_success() -> None:
    source = (ROOT / "src" / "dlw_startup_recovery.c").read_text(
        encoding="utf-8"
    )
    marker = source[
        source.index("static int dluma_startup_recovery_write_marker_locked") :
        source.index("static void dluma_startup_recovery_clear_prompt_locked")
    ]
    assert "key writes as authoritative" in marker
    assert "(void)WritePrivateProfileStringW" in marker
    assert "return written;" in marker
    assert "written = WritePrivateProfileStringW(\n            0, 0, 0" not in marker


def test_recovery_modal_is_one_shot_even_if_persistence_fails() -> None:
    source = (ROOT / "src" / "dlw_startup_recovery.c").read_text(
        encoding="utf-8"
    )
    prompt = source[
        source.index("int dluma_startup_recovery_prompt_before_game_init") :
        source.index("int dluma_startup_recovery_complete_prompt")
    ]
    assert "if (shown)" in prompt
    assert "dluma_startup_recovery_complete_prompt();" in prompt
    assert prompt.index("dluma_startup_recovery_complete_prompt();") < prompt.index(
        "if (persisted)"
    )


def test_prompt_completion_keeps_current_early_startup_armed() -> None:
    source = (ROOT / "src" / "dlw_startup_recovery.c").read_text(
        encoding="utf-8"
    )
    complete = source[source.index("int dluma_startup_recovery_complete_prompt") :]
    assert 'state = had_pending && !runtime_started ? "armed" : "resolved";' in complete
    assert "startup_recovery_prompt_complete_startup_continues" in complete
    assert "startup_recovery_prompt_complete_after_startup" in complete
    assert "dluma_startup_recovery_clear_prompt_locked();" in complete


def test_successful_host_start_resolves_normal_startup_marker() -> None:
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
    host = exports[
        exports.index("host_started = dluma_host_start();") :
        exports.index("return manager;", exports.index("host_started = dluma_host_start();"))
    ]
    assert "dluma_startup_recovery_snapshot_prompt(&prompt)" in host
    assert "dluma_startup_recovery_complete_prompt()" in host
    assert host.index("dluma_startup_recovery_snapshot_prompt") < host.index(
        "dluma_startup_recovery_complete_prompt"
    )
