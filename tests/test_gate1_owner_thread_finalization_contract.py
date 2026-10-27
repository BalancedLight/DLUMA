from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHUTDOWN = ROOT / "src" / "dlw_shutdown.c"


def test_exit_game_marshals_content_finalization_before_stop_gate():
    source = SHUTDOWN.read_text(encoding="utf-8")

    marshal = source.index("dluma_shutdown_finalize_content_on_owner_thread")
    request = source.index("PostMessageW", marshal)
    wait = source.index("WaitForSingleObject", request)
    quiesce = source.index("static void dluma_request_quiesce_impl", wait)
    finalize_call = source.index(
        "content_finalized = dluma_shutdown_finalize_content_on_owner_thread();",
        quiesce,
    )
    stop_gate = source.index(
        "dluma_atomic_exchange(&g_dluma.stop_requested, 1);", finalize_call
    )

    assert marshal < request < wait < quiesce < finalize_call < stop_gate
    assert "DLUMA_CONTENT_FINALIZE_MESSAGE" in source
    assert "owner_pump_timeout" in source


def test_window_owner_validates_independent_window_route_before_drain():
    source = SHUTDOWN.read_text(encoding="utf-8")
    window_proc = source.index("static LRESULT CALLBACK dluma_game_window_proc")
    message = source.index("message == DLUMA_CONTENT_FINALIZE_MESSAGE", window_proc)
    thread_check = source.index("GetCurrentThreadId", message)
    owner = source.index(
        "dluma_engine_current_thread_can_finalize_content", thread_check
    )
    drain = source.index(
        "dluma_custom_map_asset_trace_game_thread_finalize()", owner
    )
    signal = source.index("SetEvent(g_dluma_content_finalize_event)", drain)

    assert message < thread_check < owner < drain < signal
    assert "validated_window_owner_mismatch" in source
    assert "game_frame_authoritative_thread_id" not in source


def test_finalization_event_is_lifetime_bounded():
    source = SHUTDOWN.read_text(encoding="utf-8")
    start = source.index("int dluma_shutdown_hooks_start")
    stop = source.index("int dluma_shutdown_hooks_stop")

    assert "CreateEventW" in source[start:stop]
    assert "CloseHandle(g_dluma_content_finalize_event)" in source[stop:]
    assert "g_dluma_content_finalize_state = 0" in source[start:]


def test_exact_natural_exit_owner_is_an_independent_finalization_proof():
    shutdown = SHUTDOWN.read_text(encoding="utf-8")
    engine = (ROOT / "src/dlw_engine.c").read_text(encoding="utf-8")
    content = (
        ROOT
        / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")

    for field in (
        "g_dluma_exit_owner_thread_id",
        "g_dluma_exit_owner_active_token",
        "g_dluma_exit_owner_conflict",
        "g_dluma_content_finalize_exit_owner_token",
    ):
        assert field in shutdown
    proof = shutdown.split(
        "int dluma_shutdown_snapshot_natural_exit_owner(", 1
    )[1].split(
        "static int dluma_shutdown_finalize_content_on_owner_thread", 1
    )[0]
    assert "expected_exit_owner_calls == 1U" in proof
    assert "observed_exit_owner_calls == 1U" in proof
    assert "expected_thread_id == proof.observed_thread_id" in proof
    assert "expected_token == proof.observed_token" in proof
    assert "*g_dluma_exit_iat_slot == (void*)dluma_exit_game_hook" in proof

    extended = engine.split(
        "int dluma_engine_current_thread_can_finalize_content_ex(", 1
    )[1].split(
        "const char* dluma_content_finalization_authority_proof_name", 1
    )[0]
    assert "DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE" in extended
    assert "DLUMA_CONTENT_FINALIZATION_PROOF_WINDOW_OWNER" in extended
    assert "DLUMA_CONTENT_FINALIZATION_PROOF_NATURAL_SHUTDOWN_OWNER" in extended
    assert "natural_shutdown->expected_token ==" in extended

    for diagnostic in (
        "content_finalization_serialized_active_thread",
        "content_finalization_serialized_token",
        "content_finalization_window_expected_thread",
        "content_finalization_window_observed_thread",
        "content_finalization_natural_expected_thread",
        "content_finalization_natural_observed_thread",
        "content_finalization_natural_expected_token",
        "content_finalization_natural_observed_token",
    ):
        assert diagnostic in content
    assert "serialized_route_or_window_owner_mismatch" not in content
