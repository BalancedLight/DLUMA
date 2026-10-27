from __future__ import annotations

from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize(
    "relative_path",
    (
        "tools/build_dlw_api_runtime_fake_tests.bat",
        "tools/build_project_launch_routes_fake_tests.bat",
        "tools/build_source_bypass_invariant_fake_tests.bat",
    ),
)
def test_msvc_fake_harnesses_use_initialized_or_discovered_toolchain(
    relative_path: str,
) -> None:
    script = (ROOT / relative_path).read_text(encoding="utf-8")

    assert "setlocal EnableExtensions" in script
    assert 'cd /d "%~dp0\\.."' in script
    assert (
        "where cl.exe >nul 2>nul || call tools\\setup_msvc.bat || exit /b 1"
    ) in script


def test_shared_msvc_setup_uses_vswhere_without_version_or_edition() -> None:
    script = (ROOT / "tools" / "setup_msvc.bat").read_text(encoding="utf-8")

    assert "%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe" in script
    assert "Microsoft.VisualStudio.Component.VC.Tools.x86.x64" in script
    assert "-latest -products *" in script
    assert "%VS_INSTALL%\\Common7\\Tools\\VsDevCmd.bat" in script
    assert "\\Community\\" not in script
