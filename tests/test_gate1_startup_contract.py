from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"


def test_startup_core_requires_vfs_player_loader_and_audio_baselines():
    header = (CUSTOM / "StartupBaselineCaptureState.h").read_text(
        encoding="utf-8"
    )
    core = header[
        header.index("kStartupB0TransactionCoreDomainMask") :
        header.index("struct StartupB0AdmissionSample")
    ]

    assert "StartupB0Domain::LoaderState" in core
    assert "StartupB0DomainBit(StartupB0Domain::VfsSourceInventory)" in core
    assert "StartupB0DomainBit(StartupB0Domain::Player)" in core
    assert "StartupB0DomainBit(StartupB0Domain::Audio)" in core
    assert "Player and Audio are no longer" in core


def test_loader_owned_sources_are_mounted_after_exact_source_hooks_start():
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")

    initialize = exports[
        exports.index("static void* dluma_player_initialize(void)") :
        exports.index("static __int64 dluma_player_shutdown(void)")
    ]
    fixes = initialize.index("dluma::fixes::InitializeAllFixes(runtime_context)")
    mount = initialize.index("pack_mounted = dluma_mount_content_pack()")

    assert fixes < mount
    assert "exact fs::add_source path" in initialize[fixes:mount + 256]


def test_epoch_zero_cannot_create_retaining_candidates_for_future_projects():
    provenance = (
        CUSTOM / "CandidateRetainingConsumerProvenance.cpp"
    ).read_text(encoding="utf-8")
    record = provenance[
        provenance.index("CandidateLedgerResult RecordCandidateRetainingConsumer(") :
        provenance.index("CandidateLedgerResult CorrelateCandidateRetainingOwner(")
    ]

    assert "!input->epoch" in record
    assert "return CandidateLedgerResult::InvalidArguments" in record


def test_raw_registry_inventory_remains_diagnostic_not_fabricated():
    inventory = (CUSTOM / "FilesystemSourceInventory.cpp").read_text(
        encoding="utf-8"
    )

    for contract in (
        "SynchronizationNotHeld",
        "LayoutNotValidated",
        "RegistryUnavailable",
        "FallbackMismatch",
        "RegistryChangedDuringCapture",
    ):
        assert contract in inventory

    assert "filesystem:native-fallback" in inventory
