import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_manifest_runtime_fake_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_dlw_mod_manifest_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert completed.stdout.count("[PASS]") == 7
    assert "DLUMA manifest fake cases passed: 7" in completed.stdout


def test_manifest_runtime_is_catalog_driven_and_length_aware():
    source = (ROOT / "src" / "core" / "dlw_mod_manifest.c").read_text(
        encoding="utf-8"
    )
    header = (ROOT / "src" / "core" / "dlw_mod_manifest.h").read_text(
        encoding="utf-8"
    )
    assert "g_dluma_generated_access_names" in source
    assert "DLUMA_API_CATALOG_SHA256" in source
    assert "DLUMA_MOD_MANIFEST_MAX_BYTES" in header
    assert "DLUMA_MOD_LOCK_MAX_ARTIFACTS" in header
    assert "dluma_mod_relative_path_validate_and_normalize" in source
    assert "DLUMA_STATUS_INSUFFICIENT_BUFFER" in source
    assert "DLUMA_MOD_HOST_EDITOR" in header
    assert "quest_phase_handlers" in source
    assert "DLUMA_API_IMMEDIATELY_PREVIOUS_CATALOG_SHA256" in source
