import json
import subprocess
from pathlib import Path

from tools.dluma_mod_package import write_lock


ROOT = Path(__file__).resolve().parents[1]
HARNESS = (
    ROOT
    / "build"
    / "package-runtime"
    / "dlw_mod_package_runtime_harness.exe"
)


def _run(package_root: Path, expectation: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(HARNESS), str(package_root), expectation],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )


def test_runtime_package_verifies_lock_tree_and_revalidates(tmp_path: Path):
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_dlw_mod_package_runtime_harness.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=60,
    )
    package_root = (
        tmp_path
        / "workshop"
        / "mods"
        / "native"
        / "global"
        / "test.native"
    )
    package_root.mkdir(parents=True)
    manifest = {
        "schema": "dluma.mod.v1",
        "api_version": "1.0",
        "id": "test.native",
        "name": "Runtime package",
        "version": "1.0.0",
        "description": "Runtime lock proof",
        "author": "DLUMA",
        "language": "native",
        "scope": "global",
        "entry": "test.mod",
        "load_priority": 0,
        "access": ["game.read"],
    }
    (package_root / "mod.dluma.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    (package_root / "test.mod").write_bytes(b"MZ fake fixture bytes")
    write_lock(package_root)

    valid = _run(package_root, "valid")
    assert "valid test.native" in valid.stdout

    changed_after_approval = _run(package_root, "mutate")
    assert "changed artifact rejected" in changed_after_approval.stdout
    write_lock(package_root)

    (package_root / "unlisted.dll").write_bytes(b"not in lock")
    rejected = _run(package_root, "invalid")
    assert "rejected" in rejected.stdout
    (package_root / "unlisted.dll").unlink()

    (package_root / "test.mod").write_bytes(b"changed after review")
    changed = _run(package_root, "invalid")
    assert "rejected" in changed.stdout


def test_package_runtime_rejects_reparse_and_hardlink_surfaces_in_source():
    source = (
        ROOT / "src" / "core" / "dlw_mod_package_runtime.c"
    ).read_text(encoding="utf-8")
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in source
    assert "nNumberOfLinks != 1U" in source
    assert "dluma_package_verify_tree_recursive" in source
    assert "dluma_mod_package_revalidate" in source
