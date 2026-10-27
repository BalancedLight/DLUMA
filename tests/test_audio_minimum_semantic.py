from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_audio_minimum_semantic_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_audio_minimum_semantic_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "audio-minimum-semantic-fake"
            / "audio_minimum_semantic_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_minimum_plan_is_production_wired_without_optional_speech_gate():
    project = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    production = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "AudioMinimumSemanticPlan.cpp" in project
    assert "CaptureProductionAudioMinimumBaseline" in production
    assert "AssessProductionAudioMinimumAtResetBoundary" in production
    capture = production[
        production.index("bool CaptureProductionAudioMinimumSemanticPair(") :
        production.index("bool CaptureAudioSpeechRegistryEqualsBaseline")
    ]
    decision = production[
        production.index("AssessProductionAudioMinimumAtResetBoundary") :
        production.index("enum class StartupAudioCaptureResult")
    ]
    assert "g_audio_touch_hooks_available" in production
    assert "g_audio_minimum_hooks_available" in capture
    assert "g_speech_registry_observation_available" not in decision
    active = production[
        production.index("AudioMinimumObservationActiveCallCount()") :
        production.index("bool ResolveAudioMinimumBankManagerUnderLock")
    ]
    assert "g_audio_script_load_status" in active
    assert "g_audio_fmod_load_banks_status" in active
    assert "g_sound_bank_manager_load_status" in active
    assert "g_speech_bank_load_status" not in active


def test_optional_speech_hook_failure_cannot_disable_minimum_audio_plan():
    production = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "bool IsAudioMinimumHookStatus(" in production
    assert "bool IsAudioOptionalTouchHookStatus(" in production

    relay_prepare = production[
        production.index("bool PrepareAllRelays() noexcept") :
        production.index("bool InstallMandatoryRoutes() noexcept")
    ]
    assert "DisableAudioMinimumHookPlan();" in relay_prepare
    assert "DisableAudioOptionalTouchHookPlan();" in relay_prepare
    assert "DisableAudioTouchHookPlan();" not in relay_prepare

    install = production[
        production.index("void InstallOptionalRoutes() noexcept") :
        production.index("void AppendIdentity(")
    ]
    assert "bool audio_minimum_failed = false;" in install
    assert "bool audio_optional_touch_failed = false;" in install
    assert "IsAudioMinimumHookStatus(candidate->owner)" in install
    assert "IsAudioOptionalTouchHookStatus(" in install
    assert (
        "g_audio_minimum_hooks_available = !audio_minimum_failed &&"
        in install
    )
    assert (
        "g_audio_touch_hooks_available =\n"
        "        !audio_optional_touch_failed &&\n"
        "        g_audio_minimum_hooks_available &&"
        in install
    )
