from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOTNET_ROOT = Path(
    os.environ.get("DOTNET_ROOT_X64")
    or os.environ.get("DOTNET_ROOT")
    or str(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "dotnet")
)
HOSTFXR = DOTNET_ROOT / "host" / "fxr" / "10.0.13" / "hostfxr.dll"
BOOTSTRAP = (
    ROOT
    / "sdk"
    / "dotnet"
    / "Dluma.Managed.Bootstrap"
    / "bin"
    / "Release"
    / "net10.0"
    / "Dluma.Managed.Bootstrap.dll"
)
FIXTURE = (
    ROOT
    / "tests"
    / "managed_mod_fixture"
    / "bin"
    / "Release"
    / "net10.0"
    / "ManagedModFixture.dll"
)
HARNESS = (
    ROOT
    / "build"
    / "managed-bootstrap-harness"
    / "managed_bootstrap_harness.exe"
)
def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        arguments,
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )


def test_managed_bootstrap_loads_only_supplied_artifact_and_runs_lifecycle() -> None:
    assert HOSTFXR.is_file(), "Pinned .NET 10.0.13 hostfxr is unavailable"
    run(
        "dotnet",
        "build",
        str(
            ROOT
            / "sdk"
            / "dotnet"
            / "Dluma.Managed.Bootstrap"
            / "Dluma.Managed.Bootstrap.csproj"
        ),
        "-c",
        "Release",
        "--nologo",
    )
    run(
        "dotnet",
        "build",
        str(ROOT / "tests" / "managed_mod_fixture" / "ManagedModFixture.csproj"),
        "-c",
        "Release",
        "--nologo",
    )
    run(
        "cmd.exe",
        "/d",
        "/s",
        "/c",
        "call tools\\build_managed_bootstrap_harness.bat",
    )
    result = run(
        str(HARNESS),
        str(DOTNET_ROOT),
        str(HOSTFXR),
        str(BOOTSTRAP),
        str(FIXTURE),
    )
    assert "immutable artifact load passed" in result.stdout
