from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"


def test_audio_semantic_delta_native_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         r"tools\build_audio_semantic_delta_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [ROOT / "build" / "audio-semantic-delta-fake"
         / "audio_semantic_delta_fake_harness.exe"],
        cwd=ROOT,
        check=True,
    )


def test_audio_semantic_snapshot_and_delta_are_production_wired():
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    for filename in (
        "AudioSemanticWindowsSnapshot.cpp",
        "AudioSemanticWindowsSnapshot.h",
        "AudioSemanticDelta.cpp",
        "AudioSemanticDelta.h",
        "AudioMinimumSemanticPlan.cpp",
        "AudioMinimumSemanticPlan.h",
    ):
        assert filename in project
    for contract in (
        "CaptureAudioScriptSemanticSnapshot",
        "CaptureAudioBankSemanticSnapshot",
        "CaptureProductionAudioMinimumBaseline",
        "AssessProductionAudioMinimumAtResetBoundary",
        "kFailedFmodBankLookupNoInsertionPathValidated",
        "AudioMinimumDisposition::Touched",
        "AudioMinimumDisposition::Incomplete",
    ):
        assert contract in source


def test_destructive_audio_adapter_remains_disabled():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    assert "kAudioRetainedReferenceModelCoverageComplete = false" in source
    assert "const audio::CustomAudioResetOperations unsupported_audio = {}" in source


def test_runtime10_property_and_bank_tree_contracts_are_exact_and_bounded():
    header = (CUSTOM / "AudioSemanticWindowsSnapshot.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "AudioSemanticWindowsSnapshot.cpp").read_text(
        encoding="utf-8"
    )
    joined = header + source
    for contract in (
        "kAudioScriptPropertyTreeRva = 0x00AD5360U",
        "kAudioScriptPropertyTreeAllocatorTagRva",
        "0x00AD5370U",
        "kAudioPropertyNodeKeyOffset = 0x0000U",
        "kAudioPropertyNodeValueVectorOffset = 0x0010U",
        "kAudioPropertyNodeLinkOffset = 0x0028U",
        "kAudioPropertyNodeParentOffset = 0x0030U",
        "kAudioPropertyNodeRightOffset = 0x0038U",
        "kAudioPropertyNodeColorOffset = 0x0040U",
        "kAudioPropertyNodeReadableSize = 0x0048U",
        "kAudioScriptPropertyTreeCapacity = 4096U",
        "dluma_capture_complete_audio_script_property_tree_b0",
        "dluma_compare_fmod_bank_key_in_engine_order",
        "static_cast<std::int32_t>(left_bits)",
        "AudioBankNodeSemanticDiagnostic",
        "property_tree_raw_allocator_tag",
        "property_tree_header_validation_result",
    ):
        assert contract in joined
    assert "PropertyFrame stack[kAudioScriptPropertyTreeCapacity]" not in source
    assert (
        "std::uintptr_t visited[kAudioScriptPropertyTreeCapacity]"
        not in source
    )
    harness = (ROOT / "tests" / "audio_semantic_delta_fake_harness.cpp").read_text(
        encoding="utf-8"
    )
    assert "kStockPropertyGroupCount = 1382U" in harness
    assert "TestCompleteStockPropertyTreeAndCorruption" in harness
    assert "TestSignedFmodBankOrdering" in harness


def test_audio_semantic_report_keeps_optional_diagnostics_separate():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    for field in (
        "prerequisite_audio_trace_available",
        "prerequisite_full_path_provider_probe_available",
        "prerequisite_baseline_layout_validated",
        "prerequisite_engine_full_profile_valid",
        "missing_prerequisites",
        "audio_trace",
        "full_path_provider_probe",
        "baseline_layout",
        "engine_full_profile",
    ):
        assert field in source
    minimum_capture = source[
        source.index("bool CaptureProductionAudioMinimumSemanticPair(") :
        source.index(
            "audiominimum::AudioMinimumOperationCoverage\n"
            "CaptureAudioMinimumOperationCoverage(std::uint32_t epoch)"
        )
    ]
    assert "g_audio_minimum_hooks_available" in minimum_capture
    assert "g_audio_baseline_layout_validated" in minimum_capture
    assert "g_engine_full_profile_valid" in minimum_capture
    assert "g_audio_trace_available" not in minimum_capture
    assert "g_audio_full_path_provider_probe_available" not in minimum_capture
