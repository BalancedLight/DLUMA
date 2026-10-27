from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {
    ".bat",
    ".c",
    ".cmd",
    ".cpp",
    ".cs",
    ".h",
    ".ini",
    ".json",
    ".md",
    ".ps1",
    ".py",
    ".txt",
    ".xaml",
    ".xml",
    ".yml",
    ".yaml",
}
LOCAL_ONLY_ROOTS = {
    ".codex-notes",
    "_codex_checkpoints",
    "bin",
    "codex",
    "runtime_test",
}
PRIVATE_PATH_MARKERS = (
    "c:\\users\\",
    "d:\\backups\\",
    "e:\\debugging\\",
    "e:\\steamlibrary\\",
    "f:\\code projects\\",
    "f:\\dyinglighttools\\",
    "f:\\steamlibrary\\",
    "c:\\\\users\\\\",
    "d:\\\\backups\\\\",
    "e:\\\\debugging\\\\",
    "e:\\\\steamlibrary\\\\",
    "f:\\\\code projects\\\\",
    "f:\\\\dyinglighttools\\\\",
    "f:\\\\steamlibrary\\\\",
)


def _tracked_existing_files() -> list[Path]:
    completed = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    paths = []
    for raw in completed.stdout.split(b"\0"):
        if not raw:
            continue
        relative = Path(raw.decode("utf-8"))
        if (ROOT / relative).is_file():
            paths.append(relative)
    return paths


def test_local_state_and_build_artifacts_are_not_tracked() -> None:
    offenders = []
    for relative in _tracked_existing_files():
        if relative.name == "AGENTS.md":
            offenders.append(relative.as_posix())
        elif relative.parts[0] in LOCAL_ONLY_ROOTS:
            offenders.append(relative.as_posix())
        elif relative.suffix.lower() in {".obj", ".pdb", ".user"}:
            offenders.append(relative.as_posix())
    assert offenders == []


def test_public_project_text_has_no_private_workstation_roots() -> None:
    offenders = []
    for relative in _tracked_existing_files():
        if relative.parts[0] == "third_party" or relative == Path("tests/test_repository_hygiene.py"):
            continue
        if relative.suffix.lower() not in TEXT_SUFFIXES:
            continue
        text = (ROOT / relative).read_text(encoding="utf-8", errors="strict").lower()
        for marker in PRIVATE_PATH_MARKERS:
            if marker in text:
                offenders.append(f"{relative.as_posix()}: {marker}")
    assert offenders == []


def test_msvc_scripts_do_not_pin_visual_studio_version_or_edition() -> None:
    offenders = []
    for relative in _tracked_existing_files():
        if relative.suffix.lower() not in {".bat", ".cmd", ".cs", ".py"}:
            continue
        text = (ROOT / relative).read_text(encoding="utf-8", errors="strict")
        if "Microsoft Visual Studio\\18\\Community" in text:
            offenders.append(relative.as_posix())
    assert offenders == []


def test_debug_native_mod_builds_keep_pdbs_in_build_directories() -> None:
    fixtures = (ROOT / "tools" / "build_native_mod_fake_tests.bat").read_text(encoding="utf-8")

    assert '/Fd"%DLW_FAKE_OUT%\\%%F.pdb"' in fixtures


def test_managed_harness_keeps_object_in_build_directory() -> None:
    script = (ROOT / "tools" / "build_managed_bootstrap_harness.bat").read_text(encoding="utf-8")

    assert '/Fo:"%OUT_DIR%\\managed_bootstrap_harness.obj"' in script


def test_workshop_menu_harness_keeps_object_in_build_directory() -> None:
    script = (ROOT / "tools" / "build_workshop_menu_fake_tests.bat").read_text(encoding="utf-8")

    assert '/Fo"build\\workshop-menu-fake\\workshop_menu_metadata_fake_harness.obj"' in script


def test_local_test_path_configuration_is_private_and_optional() -> None:
    ignored = (ROOT / ".gitignore").read_text(encoding="utf-8")
    example = (ROOT / "tests" / "local_paths.example.json").read_text(
        encoding="utf-8"
    )

    assert "tests/local_paths.json" in ignored
    assert "E:\\" not in example
    assert "F:\\" not in example

