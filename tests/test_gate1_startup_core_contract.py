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
