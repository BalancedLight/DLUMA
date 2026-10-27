from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"


def test_startup_core_requires_vfs_player_loader_and_audio_baselines():
    header = (CUSTOM / "StartupBaselineCaptureState.h").read_text(
        encoding="utf-8"
    )
    marker = "kStartupB0TransactionCoreDomainMask"
    start = header.index(marker)
    contract = header[start : start + 520]
    assert "StartupB0Domain::LoaderState" in contract
    assert "StartupB0Domain::VfsSourceInventory" in contract
    assert "StartupB0Domain::Player" in contract
    assert "StartupB0Domain::Audio" in contract
    assert "authoritative stable zero-owner" in header


def test_loader_owned_sources_mount_after_exact_content_hooks():
    source = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
    initialize = source.index("static void* dluma_player_initialize(void)")
    fixes = source.index("dluma::fixes::InitializeAllFixes", initialize)
    mount = source.index("pack_mounted = dluma_mount_content_pack();", initialize)
    original = source.index("manager = g_original_initialize();", initialize)
    assert fixes < mount < original
    assert "exact fs::add_source path" in source
    assert "successful-add ordinal" in source


def test_exit_finalization_is_marshaled_before_global_stop_gate():
    source = (ROOT / "src" / "dlw_shutdown.c").read_text(encoding="utf-8")
    assert "DLUMA_CONTENT_FINALIZE_MESSAGE" in source
    assert "dluma_shutdown_finalize_content_on_owner_thread" in source
    assert "validated_window_owner_mismatch" in source
    assert "owner_pump_timeout" in source

    function = source.index("static void dluma_request_quiesce_impl")
    finalize = source.index(
        "dluma_shutdown_finalize_content_on_owner_thread", function
    )
    stop_gate = source.index(
        "dluma_atomic_exchange(&g_dluma.stop_requested, 1)", function
    )
    assert finalize < stop_gate

    window_proc = source.index("static LRESULT CALLBACK dluma_game_window_proc")
    message = source.index("message == DLUMA_CONTENT_FINALIZE_MESSAGE", window_proc)
    special_finalize = source.index(
        "dluma_custom_map_asset_trace_game_thread_finalize_from_exit_game_owner(",
        message,
    )
    generic_finalize = source.index(
        "dluma_custom_map_asset_trace_game_thread_finalize()", message
    )
    signal = source.index("SetEvent(g_dluma_content_finalize_event)", message)
    assert message < special_finalize < signal
    assert message < generic_finalize < signal


def test_exit_owner_count_crosses_the_window_message_boundary():
    source = (ROOT / "src" / "dlw_shutdown.c").read_text(encoding="utf-8")
    assert "g_dluma_content_finalize_exit_owner_calls" in source
    request = source.index("static int dluma_shutdown_finalize_content_on_owner_thread")
    publish = source.index(
        "&g_dluma_content_finalize_exit_owner_calls", request
    )
    post = source.index("PostMessageW(", request)
    window_proc = source.index("static LRESULT CALLBACK dluma_game_window_proc")
    consume = source.index(
        "&g_dluma_content_finalize_exit_owner_calls", window_proc
    )
    special = source.index(
        "dluma_custom_map_asset_trace_game_thread_finalize_from_exit_game_owner(",
        consume,
    )
    assert publish < post < window_proc < consume < special


def test_epoch_zero_provenance_is_not_a_candidate_epoch():
    header = (CUSTOM / "CandidateRetainingConsumerProvenance.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "CandidateRetainingConsumerProvenance.cpp").read_text(
        encoding="utf-8"
    )
    assert "std::uint32_t epoch" in header
    assert "!input->epoch" in source
    assert "if (!ledger || !epoch) return nullptr;" in source
