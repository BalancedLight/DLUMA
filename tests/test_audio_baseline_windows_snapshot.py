from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "AudioBaselineWindowsSnapshot.h"
SOURCE = CUSTOM / "AudioBaselineWindowsSnapshot.cpp"


def test_native_audio_baseline_windows_snapshot_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_audio_baseline_windows_snapshot_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    completed = subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "audio-baseline-windows-snapshot-fake"
                / "audio_baseline_windows_snapshot_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    assert "audio_baseline_windows_snapshot_fake_harness: 22/22 ok" in (
        completed.stdout
    )


def test_exact_windows_audio_layout_is_visible_and_bounded():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for contract in (
        "0x623395D0U",
        "0x00E8D000U",
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C",
        "kWindowsAudioSingletonGlobalRva = 0x00AC8D78U",
        "kWindowsAudioFmodVtableRva = 0x008A9628U",
        "kWindowsAudioNoSoundVtableRva = 0x008AD6C8U",
        "kWindowsAudioFmodBankManagerOffset = 0x1E28U",
        "kWindowsAudioCriticalSectionRva = 0x00AD5160U",
        "kWindowsGameGlobalRva = 0x00ACA818U",
        "kWindowsGameAudioSettingsOffset = 0x03D8U",
        "kWindowsAudioScriptPathVectorRva = 0x00AD52E0U",
        "kWindowsSpeechManagerGlobalRva = 0x00ACCD38U",
        "kWindowsSpeechBankVectorOffset = 0x0038U",
        "kWindowsSpeechBankVectorTagOffset = 0x0048U",
        "kWindowsSpeechBankVectorTag = 16U",
        "kWindowsSpeechBankLocalisedOffset = 0x0010U",
        "kWindowsSpeechBankHashOffset = 0x0000U",
        "kWindowsSpeechBankDescriptorSize = 0x0018U",
        "kWindowsAudioVolumeGrainRva = 0x00A91E60U",
        "kWindowsAudioSettingsPodSize = 0x0058U",
        "kWindowsAudioSettingsGlobalSelectorOffset = 0x0048U",
        "kWindowsAudioSettingsGlobalSelectorCapacity = 256U",
        "kWindowsAudioSettingsFxVolumeOffset = 0x0034U",
        "kWindowsAudioSettingsWaveVolumeOffset = 0x0038U",
        "kWindowsAudioSettingsSpeechVolumeOffset = 0x003CU",
        "kWindowsAudioSettingsMovieVolumeOffset = 0x0040U",
        "kWindowsAudioSettingsMovieVolumeSourceOffset = 0x0044U",
        "kWindowsAudioBankNodeLinkOffset = 0x0048U",
        "kWindowsAudioBankNodeNativeFileOffset = 0x0018U",
        "kWindowsAudioBankNodeRecordHashOffset = 0x0020U",
        "kWindowsAudioBankNodeColorOffset = 0x0060U",
        "kWindowsAudioBankNodeReadableSize = 0x0068U",
        "kWindowsAudioBankCapacity = 192U",
        "kWindowsAudioScriptCapacity = 64U",
        "kWindowsSpeechBankCapacity = 192U",
    ):
        assert contract in joined


def test_snapshot_is_read_only_fixed_capacity_and_has_no_owner_calls():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    assert "ReadAudioBaselineWindowsMemoryFn" in joined
    assert "TryEnterAudioBaselineWindowsLockFn" in joined
    assert "ReadAudioBaselineWindowsFenceFn" in joined
    for forbidden in (
        "std::vector",
        "std::string",
        "std::map",
        "std::unordered",
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "WriteFile(",
        "fopen(",
        "printf(",
        "throw ",
        "try {",
        "catch (",
        "GetBanksNames(",
        "GetLoadedBanksNames(",
        "EnterCriticalSection(",
        "TryEnterCriticalSection(",
        "LoadBank(",
        "UnloadBank(",
        "ReloadSoundBanks(",
        "UnloadAllBanks(",
        "LoadAudioBanks(",
        "UnloadLocalisedAudioBanks(",
    ):
        assert forbidden not in joined


def test_raw_observation_never_claims_authoritative_audio_b0():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    for unresolved in (
        "AudioWindowsUnresolvedScriptProviders",
        "AudioWindowsUnresolvedBankProviders",
        "AudioWindowsUnresolvedBankContent",
        "AudioWindowsUnresolvedBankDependencyOrder",
        "AudioWindowsUnresolvedBankReferenceModel",
        "AudioWindowsUnresolvedBankInstanceGeneration",
        "AudioWindowsUnresolvedSpeechSynchronization",
        "AudioWindowsUnresolvedSpeechProviders",
        "AudioWindowsUnresolvedSpeechContent",
        "AudioWindowsUnresolvedSpeechDependencyOrder",
        "AudioWindowsUnresolvedSpeechReferenceModel",
        "AudioWindowsUnresolvedSpeechInstanceGeneration",
        "AudioWindowsUnresolvedAudioSettingsProvider",
    ):
        assert unresolved in header
        assert unresolved in source
    assert "authoritative_b0_complete = output->raw_snapshot_complete &&" in source
    assert "output->unresolved_fields == 0" in source
    assert "speech_registry_synchronization_proven" in header
    assert "if (capture_speech &&" in source
    assert "!CaptureSpeechBanks(context, pass, reason_mask)" in source
    assert "speech_owner_synchronization_proven = capture_speech &&" in source
    assert "speech_baseline_complete = false" in source
    assert "master_scale" not in header
    assert "master_scale" not in source
    assert "captured.pod" in source
    assert "audio_settings_semantic_v1" in source
    assert "header.capacity >" in source
    assert "ReadBoundedSettingsSelector" in source
    assert "HashBytes(&hash, captured.pod" not in source
    assert "music_volume" not in header
    assert "music_volume" not in source


def test_snapshot_has_double_read_single_workspace_and_nonblocking_barriers():
    source = SOURCE.read_text(encoding="utf-8")
    assert "RawPass workspace = {}" in source
    assert "RawPass first" not in source
    assert "RawPass second" not in source
    assert "EqualRawPassToSnapshot" in source
    assert "CopyRawPass(workspace, output)" in source
    assert "first_complete_fields" in source
    assert "VerifySpeechSynchronizedSecondPass" in source
    assert "first_pointers" in source and "second_pointers" in source
    assert "EqualVectorHeader(before, after)" in source
    assert "try_enter_audio_lock" in source
    assert "leave_audio_lock" in source
    assert "AudioWindowsReasonAudioLockBusy" in source
    assert "AudioWindowsReasonFenceChanged" in source


def test_unproven_speech_owner_is_a_hard_read_gate():
    source = SOURCE.read_text(encoding="utf-8")
    assert "const bool capture_speech =" in source
    assert "context->speech_registry_synchronization_proven" in source
    assert "CaptureRawPass(*context, capture_speech" in source
    assert "(!capture_speech || VerifySpeechSynchronizedSecondPass" in source
    assert "return output->sound_lock_snapshot_complete &&" in source


def test_retained_native_file_is_identity_only_not_provider_evidence():
    source = SOURCE.read_text(encoding="utf-8")
    provider_tokens = (
        "resolved_provider",
        "source_path",
        "source_record_index",
        "content_epoch",
        "successful_add_ordinal",
    )
    for token in provider_tokens:
        assert token not in source
    assert "output->native_file = BodyValue<std::uintptr_t>" in source
    assert "ReadMemory(context, output->native_file" not in source


def test_snapshot_helper_is_registered_in_native_project():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert "AudioBaselineWindowsSnapshot.cpp" in project
    assert "AudioBaselineWindowsSnapshot.h" in project
