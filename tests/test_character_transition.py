"""Execute the portable coordinator against a durable-store fake, never game saves."""
import subprocess
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def coordinator_executable(tmp_path_factory) -> Path:
    tmp_path = tmp_path_factory.mktemp("character-transition")
    executable = tmp_path / "character_transition_tests.exe"
    obj = tmp_path / "character_transition_tests.obj"
    command = (
        'call tools\\setup_msvc.bat && cl.exe /nologo /TC /MT /W4 /WX '
        '/D_CRT_SECURE_NO_WARNINGS '
        f'/Fe"{executable}" /Fo"{obj}" '
        'tests\\character_transition_fake_harness.c'
    )
    script = tmp_path / "compile.cmd"
    script.write_text("@echo off\n" + command + "\nexit /b %errorlevel%\n", encoding="utf-8")
    built = subprocess.run(
        ["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
        capture_output=True, text=True, timeout=90,
    )
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


@pytest.mark.parametrize("scenario", [None, "capture-crash", "world-retry", "configuration"])
def test_character_transition_recovery(coordinator_executable: Path, scenario) -> None:
    command = [str(coordinator_executable)]
    if scenario:
        command.append(scenario)
    run = subprocess.run(command, capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: character transition recovery" in run.stdout
