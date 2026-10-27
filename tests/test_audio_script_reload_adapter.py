from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def test_audio_script_reload_native_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            r"tools\build_audio_script_reload_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            ROOT
            / "build"
            / "audio-script-reload-fake"
            / "audio_script_reload_fake_harness.exe"
        ],
        cwd=ROOT,
        check=True,
    )


def test_reload_target_contract_is_exact_and_production_planned_fail_closed():
    source = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "AudioScriptReloadAdapter.cpp"
    ).read_text(encoding="utf-8")
    production = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "kAudioScriptClearRva + 0x2BU" not in source
    assert "kAudioScriptReloadRva + 0x2BU" in source
    assert "kAudioScriptReloadRva + 0x61U" in source
    assert "kAudioScriptClearRva" in source
    assert "kAudioScriptLoadRva" in source
    assert "InitializeAudioScriptReloadAdapter(" in production
    assert "CaptureAudioScriptReloadBaseline(" in production
    assert "PrepareProductionAudioScriptReloadPlan(" in production
    assert "PrepareAudioScriptReload(" in production
    assert "audio_script_reload_production_wiring_active" in production
    assert "authorization.receiver_validated = false" in production
    assert "authorization.abi_validated = false" in production
    # The live plan owner is reached, but no unresolved receiver is invoked.
    assert "ExecuteAndVerifyAudioScriptReload(" not in production
