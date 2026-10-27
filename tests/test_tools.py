from __future__ import annotations

import importlib.util
import json
import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
import xml.etree.ElementTree as ET

from tools import build_managed_runtime

ROOT = Path(__file__).resolve().parents[1]
LOADER_VERSION = re.search(
    r'^\s*#define\s+DLUMA_VERSION\s+"(\d+(?:\.\d+)+)"\s*$',
    (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8"),
    re.MULTILINE,
).group(1)
MOD_NAME = f"DLUMA ({LOADER_VERSION}).mod"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


validator = load_module("validate_dlscript", ROOT / "tools" / "validate_dlscript.py")
peverify = load_module("verify_pe", ROOT / "tools" / "verify_pe.py")


def test_invalid_script_is_rejected():
    result = validator.validate_text("dlscript 1\non map_load\nunknown\n")
    assert not result.valid


def test_prebuilt_mod_exports_and_imports():
    result = peverify.inspect(ROOT / "bin" / "x64" / "Release" / MOD_NAME)
    assert result["machine"] == "0x8664"
    assert result["is_dll"]
    assert {"InitializeGameScriptDLL", "ShutdownGameScriptDLL", "DLUMA_GetVersion"}.issubset(result["exports"])
    assert set(result["imports"]) <= {"KERNEL32.dll", "USER32.dll"}
    assert result["dynamic_base"]
    assert result["nx_compatible"]
    assert result["high_entropy_va"]
    assert result["control_flow_guard"]
    assert result["security_cookie"] != "0x0000000000000000"
    imported = {name for names in result["imported_functions"].values() for name in names}
    assert "InterlockedExchange" not in imported
    assert "InterlockedCompareExchange" not in imported


def test_prebuilt_lua_helper_exports_and_imports():
    helper = (
        ROOT
        / "build"
        / "package"
        / "Release"
        / "dluma_lua54.dll"
    )
    assert helper.is_file()
    result = peverify.inspect(helper)
    assert result["machine"] == "0x8664"
    assert result["is_dll"]
    assert "DLUMA_Lua54_GetApi" in result["exports"]
    assert set(result["imports"]) <= {"KERNEL32.dll"}
    imported = {
        name
        for names in result["imported_functions"].values()
        for name in names
    }
    assert "InterlockedExchange" not in imported
    assert "InterlockedCompareExchange" not in imported


def test_engine_source_has_no_build_specific_game_global_rva():
    source = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    assert "0xACA818" not in source
    assert "0x16800" not in source
    assert 'GetProcAddress(state->engine_module, "CreateGame")' in source


def test_release_package_loader_matches_the_release_build():
    package = (ROOT / "build" / "package" / "Release" / MOD_NAME).read_bytes()
    release = (ROOT / "bin" / "x64" / "Release" / MOD_NAME).read_bytes()
    assert package == release


def test_installed_artifact_hash_matches_build_output():
    """Opt-in post-install identity gate used by the release workflow."""
    installed_path = os.environ.get("DLUMA_INSTALLED_MOD_PATH")
    if not installed_path:
        return
    release = ROOT / "bin" / "x64" / "Release" / MOD_NAME
    installed = Path(installed_path)
    assert installed.is_file()
    assert release.read_bytes() == installed.read_bytes()


def test_visual_studio_build_creates_clean_non_live_package():
    project_path = ROOT / "DLUMA.vcxproj"
    project_text = project_path.read_text(encoding="utf-8")
    project = ET.fromstring(project_text)
    namespace = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}
    target = project.find("msbuild:Target[@Name='PackageWorkshop']", namespace)

    assert target is not None
    assert target.attrib["AfterTargets"] == "Build"
    assert "'$(DesignTimeBuild)' != 'true'" in target.attrib["Condition"]
    assert "'$(Configuration)' == 'Release'" not in target.attrib["Condition"]
    assert target.find("msbuild:RemoveDir", namespace) is not None
    assert target.find("msbuild:Copy", namespace) is not None
    commands = [item.attrib["Command"] for item in target.findall("msbuild:Exec", namespace)]
    helper = next(command for command in commands if "build_lua54_helper_msvc.bat" in command)
    discord_helper = next(
        command
        for command in commands
        if "build_discord_presence_helper_msvc.bat" in command
    )
    command = next(command for command in commands if "build_content_pack.ps1" in command)
    assert "$(ProjectDir)tools\\build_lua54_helper_msvc.bat\" $(Configuration)" in helper
    assert "$(DlumaDiscordSdkRoot)" not in project_text
    assert "discord_partner_sdk" not in project_text
    assert (
        "$(ProjectDir)tools\\build_discord_presence_helper_msvc.bat\" "
        "$(Configuration) \"$(OutDir).\""
    ) in discord_helper
    assert "$(SystemRoot)\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" in command
    assert "tools\\build_content_pack.ps1" in command
    assert "$(DlumaContentPackPath)" in command
    assert "verify_package_manifest.py" in project_text
    assert "DlumaSubscribedWorkshopModsDir" not in ET.tostring(
        target, encoding="unicode"
    )
    assert "DlumaDevToolsWorkshopModsDir" not in ET.tostring(
        target, encoding="unicode"
    )


def test_visual_studio_build_log_is_outside_cleaned_directories():
    project_path = ROOT / "DLUMA.vcxproj"
    project = ET.fromstring(project_path.read_text(encoding="utf-8"))
    namespace = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}

    build_log = project.find(".//msbuild:BuildLog/msbuild:Path", namespace)
    prepare = project.find(
        "msbuild:Target[@Name='PrepareDlumaBuildDirectories']", namespace
    )

    assert build_log is not None
    assert build_log.text == (
        "$(ProjectDir)build\\logs\\$(Configuration)\\$(MSBuildProjectName).log"
    )
    assert prepare is not None
    removed = prepare.find("msbuild:RemoveDir", namespace)
    assert removed is not None
    assert "$(IntDir)" in removed.attrib["Directories"]
    assert "$(ProjectDir)build\\logs" not in removed.attrib["Directories"]


def test_visual_studio_project_includes_native_implementation_sources():
    project_path = ROOT / "DLUMA.vcxproj"
    project_text = project_path.read_text(encoding="utf-8")
    project = ET.fromstring(project_text)
    namespace = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}
    sources = {
        item.attrib["Include"]
        for item in project.findall(".//msbuild:ClCompile", namespace)
        if "Include" in item.attrib
    }

    assert "src\\dlw_localization.c" in sources
    assert "src\\dlw_workshop_preview.c" in sources
    assert "<TreatWChar_tAsBuiltInType>false</TreatWChar_tAsBuiltInType>" in project_text
    assert "/Zc:wchar_t-" not in project_text
    assert (
        "<ModuleDefinitionFile>$(ProjectDir)build\\exports.def"
        "</ModuleDefinitionFile>"
    ) in project_text


def test_visual_studio_live_deployment_is_shared_opt_in_and_local_only():
    project_path = ROOT / "DLUMA.vcxproj"
    project_text = project_path.read_text(encoding="utf-8")
    project = ET.fromstring(project_text)
    namespace = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}
    shared = project.find(
        "msbuild:Target[@Name='DeployLocalPackage']", namespace
    )
    explicit = project.find("msbuild:Target[@Name='DeployLocal']", namespace)
    automatic = project.find(
        "msbuild:Target[@Name='AutoDeployLocalAfterBuild']", namespace
    )

    assert "DlumaSubscribedWorkshopModsDir" in project_text
    assert "DlumaDevToolsWorkshopModsDir" in project_text
    assert "X:\\FixtureLibrary" not in project_text
    assert "Y:\\FixtureLibrary" not in project_text
    assert "DLUMA.local.props" in project_text
    assert "DLUMA.local.props.example" in project_text
    assert "*.dll" not in project_text
    assert "*.pak" not in project_text
    assert (
        "<TargetName>DLUMA "
        "($(DlumaVersion))</TargetName>"
    ) in project_text

    assert shared is not None
    assert shared.attrib["DependsOnTargets"] == "ValidateDlumaLocalDeployment"
    assert explicit is not None
    assert explicit.attrib["DependsOnTargets"] == "Build;DeployLocalPackage"
    assert "DlumaAutoDeployLocal" not in explicit.attrib["Condition"]
    assert automatic is not None
    assert automatic.attrib["AfterTargets"] == "PackageWorkshop"
    assert automatic.attrib["DependsOnTargets"] == "DeployLocalPackage"
    auto_condition = automatic.attrib["Condition"]
    assert "'$(Platform)' == 'x64'" in auto_condition
    assert "'$(Configuration)' == 'Debug'" in auto_condition
    assert "'$(Configuration)' == 'Release'" in auto_condition
    assert "'$(DlumaAutoDeployLocal)' == 'true'" in auto_condition
    assert "Exists('$(DlumaLocalPropsPath)')" in auto_condition
    assert "'$(DlumaSkipPackage)' != 'true'" in auto_condition

    assert project_text.count("tools\\deploy_local_package.py") == 1
    assert not explicit.findall("*")
    assert not automatic.findall("*")
    commands = [
        item.attrib["Command"]
        for item in shared.findall("msbuild:Exec", namespace)
    ]
    assert "verify_package_manifest.py" in commands[0]
    assert "verify_pe.py" in commands[1]
    assert "deploy_local_package.py" in commands[2]
    assert "CBFC0D950EE2A4C4273AC3903A73A3A" in commands[2]


def test_versioned_loader_deployment_removes_stale_siblings():
    project = ET.fromstring(
        (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    )
    namespace = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}
    deploy = project.find(
        "msbuild:Target[@Name='DeployLocalPackage']", namespace
    )

    assert deploy is not None
    helper = (ROOT / "tools" / "deploy_local_package.py").read_text(
        encoding="utf-8"
    )
    assert "--example-dir" not in helper
    assert "LOADER_MOD_PATTERN" in helper
    assert "_versioned_loader_mods(destination)" in helper
    assert "stale_mod.name.casefold() == mod_name.casefold()" in helper


def test_package_manifest_rejects_extra_runtime_files(tmp_path):
    package = tmp_path / "package"
    package.mkdir()
    mod_name = "loader.mod"
    for name in (
        mod_name,
        "dluma.pak",
        "dluma_lua54.dll",
        "LICENSE_LUA_5.4.8.txt",
        "dluma_discord_presence.dll",
    ):
        (package / name).write_bytes(b"x")
    runtime_sources = []
    runtime_version = build_managed_runtime.RUNTIME_VERSION
    for index, relative in enumerate(
        (
            f"dotnet/host/fxr/{runtime_version}/hostfxr.dll",
            (
                "dotnet/shared/Microsoft.NETCore.App/"
                f"{runtime_version}/coreclr.dll"
            ),
            (
                "dotnet/shared/Microsoft.NETCore.App/"
                f"{runtime_version}/hostpolicy.dll"
            ),
            (
                "dotnet/shared/Microsoft.NETCore.App/"
                f"{runtime_version}/System.Private.CoreLib.dll"
            ),
            "dotnet/dotnet.exe",
            (
                "dotnet/shared/Microsoft.WindowsDesktop.App/"
                f"{runtime_version}/PresentationFramework.dll"
            ),
            "managed/Dluma.Managed.Bootstrap.dll",
            "managed/Dluma.Managed.Bootstrap.deps.json",
            "managed/Dluma.Managed.Bootstrap.runtimeconfig.json",
            "managed/Dluma.Managed.Sdk.dll",
            "diagnostics/loader_address_ranges.json",
            "licenses/dotnet/LICENSE.txt",
            "licenses/dotnet/ThirdPartyNotices.txt",
            "quest-graph/Dluma.QuestGraph.exe",
            "quest-graph/Dluma.QuestGraph.dll",
            "quest-graph/Dluma.QuestGraph.deps.json",
            "quest-graph/Dluma.QuestGraph.runtimeconfig.json",
            "quest-graph/Dluma.QuestGraph.Core.dll",
            "quest-graph/Nodify.dll",
            "quest-graph/THIRD_PARTY_NOTICES.md",
            "quest-graph/Licenses/Nodify-MIT.txt",
        )
    ):
        source = tmp_path / "runtime-source" / str(index)
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(relative.encode("utf-8"))
        runtime_sources.append(
            build_managed_runtime.SourceFile(
                source,
                PurePosixPath(relative),
            )
        )
    runtime_sources.sort(
        key=lambda item: item.destination.as_posix().casefold()
    )
    catalog_hash = (
        ROOT / "api" / "generated" / "dluma_api_catalog.sha256"
    ).read_text(encoding="ascii").strip()
    runtime_manifest = build_managed_runtime.build_manifest(
        runtime_sources,
        runtime_version,
        catalog_hash,
    )
    build_managed_runtime.write_runtime(
        package,
        runtime_sources,
        runtime_manifest,
    )
    command = [
        sys.executable,
        str(ROOT / "tools" / "verify_package_manifest.py"),
        "--package-dir",
        str(package),
        "--mod-name",
        mod_name,
    ]
    subprocess.run(command, check=True, capture_output=True, text=True)
    (package / "foreign.dll").write_bytes(b"x")
    rejected = subprocess.run(command, check=False, capture_output=True, text=True)
    assert rejected.returncode != 0
    assert "unexpected=['foreign.dll']" in rejected.stderr


def test_object_manifest_rejects_missing_and_foreign_objects(tmp_path):
    object_dir = tmp_path / "objects"
    object_dir.mkdir()
    (object_dir / "expected.obj").write_bytes(b"object")
    (object_dir / "foreign.obj").write_bytes(b"foreign")

    rejected = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "verify_object_set.py"),
            "--object-dir",
            str(object_dir),
            "--expected",
            "expected.obj,missing.obj",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert rejected.returncode != 0
    output = rejected.stderr.lower()
    assert "missing.obj" in output
    assert "foreign.obj" in output


def test_ci_runs_geometry_and_rejects_tracked_visual_studio_state():
    workflow = (ROOT / ".github" / "workflows" / "windows-ci.yml").read_text(
        encoding="utf-8"
    )
    assert "--deselect" not in workflow
    assert "git ls-files -- \".vs\"" in workflow




def test_windows_ci_provisions_pinned_dotnet_runtime_before_build():
    workflow = (ROOT / ".github" / "workflows" / "windows-ci.yml").read_text(
        encoding="utf-8"
    )

    setup_index = workflow.index("uses: actions/setup-dotnet@v6")
    verify_index = workflow.index("name: Verify pinned .NET runtime")
    restore_index = workflow.index(
        "name: Restore visual quest editor from lock files"
    )
    build_index = workflow.index("name: Build Release and Debug")

    assert setup_index < verify_index < restore_index < build_index
    sdk = json.loads((ROOT / "global.json").read_text(encoding="utf-8"))["sdk"]
    assert sdk["version"] == "10.0.400"
    assert sdk["rollForward"] == "disable"
    assert sdk["allowPrerelease"] is False
    assert "global-json-file: global.json" in workflow
    assert "architecture: x64" in workflow
    assert 'Get-Command dotnet -CommandType Application' in workflow
    assert "host\\fxr\\10.0.13\\hostfxr.dll" in workflow
    assert "shared\\Microsoft.NETCore.App\\10.0.13" in workflow
    assert "Microsoft.NETCore.App 10.0.13" in workflow
    assert "Microsoft.WindowsDesktop.App 10.0.13" in workflow
    assert "Dluma.QuestGraph.Tests.csproj --locked-mode" in workflow
    assert "Dluma.QuestGraph.Core.Tests.csproj --locked-mode" in workflow
    assert "smoke_quest_graph_package.ps1" in workflow
    assert '"DOTNET_ROOT_X64=$dotnetRoot"' in workflow
    assert '"DOTNET_ROOT=$dotnetRoot"' in workflow
    assert "--info" in workflow
    assert "--list-runtimes" in workflow

    bootstrap_project = (
        ROOT / "sdk" / "dotnet" / "Dluma.Managed.Bootstrap" / "Dluma.Managed.Bootstrap.csproj"
    ).read_text(encoding="utf-8")
    assert build_managed_runtime.RUNTIME_VERSION == "10.0.13"
    assert "<RuntimeFrameworkVersion>10.0.13</RuntimeFrameworkVersion>" in bootstrap_project
    assert "<RollForward>Disable</RollForward>" in bootstrap_project


def test_quest_graph_bridge_binds_pipe_peer_and_reports_saved_disk_path():
    source = (ROOT / "src" / "dlw_editor_quest_graph_bridge.c").read_text(
        encoding="utf-8"
    )

    assert "GetNamedPipeClientProcessId(pipe, &peer_process_id)" in source
    assert "peer_process_id == expected_process_id" in source
    assert "WaitForSingleObject(expected_process, 0) == WAIT_TIMEOUT" in source
    assert "dluma_qg_pipe_peer_is_companion(pipe)" in source
    assert "client_process_identity_refused" in source
    assert "diskPath\\\":null" not in source
    assert "dluma_qg_json_escape(output, capacity, &used, disk_path)" in source
    assert "slot->source_path, request->disk_path" in source


def test_quest_graph_bridge_initializes_runtime_lease_contract():
    source = (ROOT / "src" / "dlw_editor_quest_graph_bridge.c").read_text(
        encoding="utf-8"
    )

    assert "g_dluma_qg.runtime_lease.struct_size =" in source
    assert "(unsigned int)sizeof(g_dluma_qg.runtime_lease)" in source


def test_quest_graph_bridge_uses_executable_edit_validation_core():
    source = (ROOT / "src" / "dlw_editor_quest_graph_bridge.c").read_text(
        encoding="utf-8"
    )
    core = (ROOT / "src" / "core" / "dlw_quest_graph_edits.c").read_text(
        encoding="utf-8"
    )

    assert "dluma_quest_graph_edits_validate(" in source
    assert "dluma_quest_graph_json_string(" in source
    assert '"edit_payload_invalid"' in core
    assert '"edit_range_invalid"' in core
    assert '"edit_ordering_invalid"' in core
    assert '"edit_result_too_large"' in core


def test_quest_graph_button_uses_visible_stock_toolbar_slot():
    source = (ROOT / "src" / "dlw_editor_quest_graph_bridge.c").read_text(
        encoding="utf-8"
    )

    assert "#define DLUMA_QG_PATCH_DIFF_CONTROL 3601" in source
    assert "patch_rectangle.left -" in source
    assert "dialog_origin.x - width - 8" in source
    assert "patch_rectangle.top - dialog_origin.y" in source
    assert "dialog_origin.y + 8" not in source


def test_quest_graph_document_resolution_has_bounded_workshop_fallback():
    bridge = (ROOT / "src" / "dlw_editor_quest_graph_bridge.c").read_text(
        encoding="utf-8"
    )
    launch = (ROOT / "src" / "dlw_editor_launch.c").read_text(
        encoding="utf-8"
    )

    assert "dluma_editor_launch_resolve_workshop_root(" in launch
    assert "dluma_qg_search_workshop_document(" in bridge
    assert "DLUMA_QG_MAX_WORKSHOP_PROJECTS" in bridge
    assert "DLUMA_QG_MAX_PROJECT_ENTRIES - entries" in bridge
    assert "dluma_project_candidate_root_is_contained(" in bridge
    assert "dluma_project_regular_file_is_contained(" in bridge


def test_private_runtime_hash_admission_is_hex_case_insensitive():
    source = (ROOT / "src" / "core" / "dlw_runtime_tree.c").read_text(
        encoding="utf-8"
    )

    assert "static int dluma_runtime_sha256_equal(" in source
    assert "dluma_runtime_sha256_text(left)" in source
    assert "dluma_runtime_sha256_text(right)" in source
    assert "dluma_runtime_sha256_equal(" in source
    assert "actual_sha256, record->sha256" in source
