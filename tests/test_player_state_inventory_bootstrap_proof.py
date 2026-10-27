from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "PlayerStateInventoryBootstrapProof.h"
SOURCE = CUSTOM / "PlayerStateInventoryBootstrapProof.cpp"


def test_native_player_state_inventory_bootstrap_proof_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_player_state_inventory_bootstrap_proof_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "player-state-inventory-bootstrap-proof-fake"
                / "player_state_inventory_bootstrap_proof_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_proof_uses_exact_engine_publication_window_and_player_rtti_identity():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    for contract in (
        "kEngineInitializeCallbackGlobalRva = 0x00ACCC58U",
        "kEnginePublishedScriptManagerGlobalRva",
        "0x00ACCC68U",
        "kEngineFactoryGlobalRva = 0x00ACCCD0U",
        "kEngineInitializeCallbackCallRva = 0x0024B9A9U",
        "kEngineFactoryPublishRva = 0x0024BA3DU",
        "kBootstrapPlayerStateRttiInitializerRva",
        "0x0134D840U",
        "kBootstrapPlayerStateRttiFactoryRva = 0x00BBF2A0U",
        "kBootstrapPlayerStateRttiFactorySlot0Rva",
        "kBootstrapPlayerStateRttiFactorySlot1Rva",
        "closed_creator_set_validated = true",
    ):
        assert contract in header + source


def test_proof_is_observation_only_fixed_size_and_fail_closed():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    assert "ReadPlayerInventoryBootstrapMemoryFn" in joined
    assert "PlayerInventoryBootstrapProofFailure" in joined
    assert "precoverage_observation_loss" in joined
    for forbidden in (
        "std::vector",
        "std::string",
        "std::map",
        "std::unordered",
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "HeapWalk(",
        "VirtualQuery(",
        "WriteFile(",
        "fopen(",
        "printf(",
        "throw ",
        "try {",
        "catch (",
        "PlayerState::Clear(",
        "InitializeVariables(",
    ):
        assert forbidden not in joined


def test_proof_is_integrated_only_as_fail_closed_startup_authority():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert '#include "PlayerStateInventoryBootstrapProof.h"' in production
    assert "PlayerStateInventoryBootstrapProof.cpp" in project
    assert "PlayerStateInventoryBootstrapProof.h" in project

    proof = production[
        production.index("PlayerInventoryBootstrapProofInput") :
        production.index("BeginPlayerStateInventoryCoverageFromBootstrap")
    ]
    for required in (
        "g_context.engine_base",
        "g_context.gamedll_base",
        "&InitializeGameScriptDLL",
        "g_engine_full_profile_valid",
        "g_gamedll_full_profile_valid",
        "constructor_original_prefix_validated",
        "complete_destructor_original_prefix_validated",
        "constructor_route_installed",
        "complete_destructor_route_installed",
        "inventory_loss_latched",
        "inventory_overflow_latched",
        "ProvePlayerStateInventoryBootstrap",
        "if (player_inventory_bootstrap_proved)",
        "player_inventory_proof_result.evidence",
    ):
        assert required in proof

    assert production.index("ProvePlayerStateInventoryBootstrap(") < (
        production.index("BeginPlayerStateInventoryCoverageFromBootstrap(")
    )
    assert "player_state_inventory_bootstrap_proof" in production
