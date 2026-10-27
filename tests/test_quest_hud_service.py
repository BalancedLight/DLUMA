from __future__ import annotations

import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]


def test_shared_quest_hud_native_behavior() -> None:
    completed = subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_quest_hud_service_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True, encoding="utf-8", timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 2
    snapshot = json.loads(next(line[5:] for line in completed.stdout.splitlines()
                               if line.startswith("JSON:")))
    assert snapshot["revision"] == 42
    assert snapshot["header"]["text"] == 'A "quoted" \\ path\nCafé'
    assert snapshot["header"]["overridden"] is True
    assert snapshot["objectives"][0]["slot"] == 3
    assert snapshot["timer"]["id"] == "timer:1"


def test_shared_and_managed_quest_hud_routes_and_cleanup() -> None:
    backend = (ROOT / "src/game/dlw_game_backend.c").read_text(encoding="utf-8")
    host = (ROOT / "src/game/dlw_game_host_context.c").read_text(encoding="utf-8")
    managed = (ROOT / "sdk/dotnet/Dluma.Managed.Bootstrap/ModContext.cs").read_text(encoding="utf-8")
    for operation in (5003, 5004):
        assert f"case {operation}U:" in backend and f"case {operation}U:" in host
        assert f"Bridge.Invoke({operation}," in managed
        assert f"NativeBridge.Unavailable({operation})" not in managed
    for lifecycle in ("revoke", "advance_scope", "rebind_map"):
        body = host.split(f"void dluma_game_host_context_{lifecycle}(", 1)[1].split("\nvoid ", 1)[0]
        assert "dluma_game_host_quest_hud_clear(state)" in body
    assert "dluma_game_host_quest_hud_cancel(state, request->token, result)" in host


def test_managed_quest_hud_marshaling(tmp_path: Path) -> None:
    bootstrap = ROOT / "sdk/dotnet/Dluma.Managed.Bootstrap"
    production = ET.parse(bootstrap / "Dluma.Managed.Bootstrap.csproj")
    framework = production.findtext(".//TargetFramework")
    runtime = production.findtext(".//RuntimeFrameworkVersion")
    sources = [bootstrap / name for name in ("ModContext.cs", "Interop.cs", "NativeBridge.cs")]
    sources.append(ROOT / "tests/quest_hud_managed_fake_harness.cs")
    includes = "".join(f'<Compile Include="{escape(str(path))}" />' for path in sources)
    sdk = ROOT / "sdk/dotnet/Dluma.Managed.Sdk/Dluma.Managed.Sdk.csproj"
    project = tmp_path / "QuestHudBridge.csproj"
    project.write_text(
        f'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
        f'<OutputType>Exe</OutputType><TargetFramework>{framework}</TargetFramework>'
        f'<RuntimeFrameworkVersion>{runtime}</RuntimeFrameworkVersion><RollForward>Disable</RollForward>'
        '<AllowUnsafeBlocks>true</AllowUnsafeBlocks><ImplicitUsings>enable</ImplicitUsings>'
        '<Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors>'
        '<PlatformTarget>x64</PlatformTarget><EnableDefaultCompileItems>false</EnableDefaultCompileItems>'
        '</PropertyGroup><ItemGroup>' + includes +
        f'<ProjectReference Include="{escape(str(sdk))}" /></ItemGroup></Project>',
        encoding="utf-8",
    )
    completed = subprocess.run(
        ["dotnet", "run", "--project", str(project), "-c", "Release", "--verbosity", "quiet"],
        cwd=ROOT, check=True, capture_output=True, text=True, encoding="utf-8", timeout=90,
    )
    assert "Managed quest HUD bridge passed:" in completed.stdout
