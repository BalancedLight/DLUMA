from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


SAMPLE_INI = (
    ROOT
    / "config"
    / "dluma.ini"
)

RETIRED_PERMISSION_ASSIGNMENT = re.compile(
    r"(?m)^\s*(?:"
    r"CustomMapAssetIsolationTrace|"
    r"ResetSources|"
    r"ResetGlobalData|"
    r"ResetAudioBanks|"
    r"VerifyFrontendBaseline|"
    r"FrontendContentColdReboot|"
    r"ExperimentalPlayerPresetRebuild"
    r")\s*="
)






def test_sample_ini_has_one_production_project_exit_permission_opted_out_by_default():
    ini = SAMPLE_INI.read_text(encoding="utf-8")
    assert re.findall(r"(?m)^CustomMapAssetIsolation=0$", ini) == [
        "CustomMapAssetIsolation=0"
    ]
    assert "Experimental project-resource unload." in ini
    assert RETIRED_PERMISSION_ASSIGNMENT.search(ini) is None
    assert "[CustomMapContentIsolation]" not in ini


def test_diagnostic_watch_list_does_not_gate_the_production_reset():
    source = (
        ROOT
        / "src"
        / "fixes"
        / "custom_map_asset_isolation"
        / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    assert "no valid full watched path; no hooks installed" not in source
    assert "reset admission is unchanged" in source
