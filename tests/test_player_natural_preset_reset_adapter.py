from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
HEADER = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "PlayerNaturalPresetResetAdapter.h"
)
SOURCE = HEADER.with_suffix(".cpp")
PRODUCTION = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "CustomMapAssetIsolationFix.cpp"
)


def test_runtime19_adapter_has_exact_prepared_only_contract():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    for phase in (
        "Unavailable",
        "Validated",
        "SnapshotReady",
        "PlanPrepared",
        "ExecutionAuthorized",
        "Executed",
        "Verified",
        "Failed",
    ):
        assert phase in header
    for failure in (
        "TargetUnvalidated",
        "ContainerRemoveTargetUnvalidated",
        "FindTargetUnvalidated",
        "WrongThread",
        "SessionActive",
        "LevelActive",
        "LocalPlayerDiActive",
        "LiveGameplayConsumer",
        "OwnerSlotMismatch",
        "OwnerGenerationMismatch",
        "OwnerSetUnstable",
        "InventoryRevisionChanged",
        "PreservationSnapshotIncomplete",
        "B0RowsNotAbsent",
        "ChangedMaskIncomplete",
        "CharacterOwnerChanged",
        "TimelineLoss",
        "CacheObserverLoss",
        "LockReentrancyUnproven",
        "AudioPlanUnprepared",
        "TransactionNotAtomic",
        "ClearObserved",
        "RepresentationRecreated",
        "PlayerDiCreated",
    ):
        assert failure in header
    assert "SourceKind::AssetsPc" in source
    assert "SourceKind::DataDirectory" in source
    assert "SourceKind::ProjectRoot" in source
    assert source.index("SourceKind::AssetsPc") < source.index(
        "SourceKind::DataDirectory"
    ) < source.index("SourceKind::ProjectRoot")
    assert "PlayerState::Clear" not in source
    assert "ReloadPlayerScripts" not in source


def test_runtime19_production_never_authorizes_or_executes_natural_reset():
    production = PRODUCTION.read_text(encoding="utf-8")
    assert '#include "PlayerNaturalPresetResetAdapter.h"' in production
    assert "PrepareProductionPlayerNaturalResetPlan(" in production
    assert "kPlayerStateRemoveCachedPresetsRva" in production
    assert "kRemoveCachedPlayerLevelPresetsRva" in production
    assert "kFindPlayerLevelPresetRva" in production
    assert "input.lock_and_reentrancy_contract_proven = false;" in production
    assert "input.preservation.complete = false;" in production
    assert "playernatural::AuthorizeExecution(" not in production
    assert "playernatural::ExecuteAndVerify(" not in production
    assert re.search(
        r"player_natural_plan_prepared\s*\|\|[\s\S]*?"
        r"player_absent_plan_prepared",
        production,
    )


def test_player_natural_preset_reset_native_fake_harness():
    command = [
        "cmd.exe",
        "/d",
        "/s",
        "/c",
        "call tools\\build_player_natural_preset_reset_adapter_fake_tests.bat",
    ]
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=120,
        check=False,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert (
        "player natural preset reset adapter fake harness: PASS"
        in completed.stdout
    )
