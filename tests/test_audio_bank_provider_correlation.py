from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIX_DIR = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = (FIX_DIR / "AudioBankProviderCorrelation.h").read_text(
    encoding="utf-8"
)
SOURCE = (FIX_DIR / "AudioBankProviderCorrelation.cpp").read_text(
    encoding="utf-8"
)
HARNESS = (
    ROOT / "tests" / "audio_bank_provider_correlation_fake_harness.cpp"
).read_text(encoding="utf-8")


def test_native_audio_provider_correlation_replay():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_audio_bank_provider_correlation_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "audio-bank-provider-fake"
                / "audio_bank_provider_correlation_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 23
    assert "audio bank provider correlation cases passed: 23" in completed.stdout


def test_helper_encodes_exact_player_open_native_contract():
    for contract in (
        "kSoundBankManagerNativeOpenCallRva = 0x00100C43U",
        "kSoundBankManagerNativeOpenReturnRva = 0x00100C49U",
        "kSpeechBankNativeOpenCallRva = 0x0032A8F2U",
        "kSpeechBankNativeOpenReturnRva = 0x0032A8F8U",
        "kFilesystemOpenNativeRva = 0x00003C80U",
        "kFilesystemFullPathRva = 0x00014D80U",
        "kFilesystemFullPathSelectedSourceJoinRva",
        "0x00014FABU",
        "kFilesystemFullPathProviderProbeRva",
        "0x00014FC0U",
        "kFilesystemFullPathProviderProbeStolenSize = 18U",
        "kSoundBankNativeOpenMode = 0",
        "kSoundBankNativeOpenFlags = 0x40U",
        "kSpeechBankNativeOpenFlags = 0x00U",
    ):
        assert contract in HEADER
    assert "explicit_source != 0" in SOURCE
    assert "caller_return_rva == kSoundBankManagerNativeOpenReturnRva" in SOURCE
    assert "caller_return_rva == kSpeechBankNativeOpenReturnRva" in SOURCE
    assert "const std::uint32_t expected_flags = fmod_call" in SOURCE
    assert "CacheHitNoOpen" in SOURCE
    assert "Never synthesize a provider" in SOURCE


def test_helper_is_bounded_tls_style_and_observation_only():
    for capacity in (
        "kAudioProviderFrameCapacity = 8",
        "kAudioProviderLogicalDepthCapacity = 32",
        "kAudioProviderCompletedOpenCapacity = 4",
    ):
        assert capacity in HEADER
    assert "AudioBankProviderFrame frames[kAudioProviderFrameCapacity]" in HEADER
    assert "suppression_depth" in HEADER
    assert "scope_generation" in HEADER
    assert "frame_index" in HEADER
    assert "thread_local" in HEADER
    for forbidden in (
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "std::vector",
        "std::map",
        "std::string",
        "fs::rem_source",
        "AudioShutdown(",
        "AudioInit(",
        "UnloadAllBanks(",
        "LoadAudioBanks(",
    ):
        assert forbidden not in SOURCE

    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    marker = (
        '<ClCompile Include="src\\fixes\\custom_map_asset_isolation\\'
        'AudioBankProviderCorrelation.cpp">'
    )
    start = project.index(marker)
    block = project[start : project.index("</ClCompile>", start)]
    for option in (
        "<CompileAs>CompileAsCpp</CompileAs>",
        "<ExceptionHandling>false</ExceptionHandling>",
        "<RuntimeTypeInfo>false</RuntimeTypeInfo>",
        "<LanguageStandard>stdcpp17</LanguageStandard>",
    ):
        assert option in block
    assert (
        '<ClInclude Include="src\\fixes\\custom_map_asset_isolation\\'
        'AudioBankProviderCorrelation.h" />'
    ) in project


def test_nested_generation_and_overflow_contracts_are_replayed():
    for case in (
        "nested_loads_keep_providers_separate",
        "open_attempt_survives_completed_reentry",
        "multiple_open_retains_first_and_fails_closed",
        "overflow_suppresses_instead_of_attributing_to_parent",
        "stale_scope_generation_cannot_close_reused_pointer",
        "stale_open_attempt_cannot_attach_to_reused_frame",
        "out_of_order_exit_cannot_corrupt_stack",
        "generation_never_wraps_to_identity_reuse",
        "speech_callsite_accepts_only_speech_contract",
        "native_open_flags_are_callsite_specific",
        "exact_full_path_source_is_carried_into_native_open",
        "required_full_path_source_missing_fails_closed",
        "multiple_full_path_sources_fail_closed",
    ):
        assert f'"{case}"' in HARNESS
    assert "first_open = observation" in SOURCE
    assert "multiple_open_loss = true" in SOURCE
    assert "active_generation" in SOURCE


def test_provider_path_and_exact_selected_source_are_kept_distinct():
    assert "provider_path" in HEADER
    assert "explicit_source" in HEADER
    assert "selected_source" in HEADER
    assert "exact_source_identity_available" in HEADER
    assert "ObserveAudioResolvedSource" in HEADER
    assert "resolved_source_observation_required" in HEADER
    assert "+0x18" not in SOURCE
    assert "get_source_name" not in SOURCE
