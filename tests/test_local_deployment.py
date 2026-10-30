from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import pytest

from tools import deploy_local_package as deployment


ROOT = Path(__file__).resolve().parents[1]
MOD_NAME = "DLUMA (0.4.0).mod"
NAMESPACE = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}


@dataclass(frozen=True)
class FakeDeployment:
    package: Path
    subscribed: Path
    devtools: Path
    built_mod: Path
    built_pak: Path


def _write(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def _make_fake_deployment(tmp_path: Path) -> FakeDeployment:
    package = tmp_path / "package"
    subscribed = tmp_path / "subscribed"
    devtools = tmp_path / "devtools"
    built_mod = tmp_path / "bin" / MOD_NAME
    built_pak = tmp_path / "bin" / "dluma.pak"
    mod_content = b"current verified release mod"
    pak_content = b"current deterministic content pack"
    top_level = {
        MOD_NAME: mod_content,
        "dluma.pak": pak_content,
        "dluma_lua54.dll": b"lua helper",
        "LICENSE_LUA_5.4.8.txt": b"lua license",
        "dluma_discord_presence.dll": b"discord helper",
        "dluma_nightly_updater.exe": b"nightly helper",
    }
    for name, content in top_level.items():
        _write(package / name, content)
    _write(package / "dluma_runtime" / "managed" / "runtime.dll", b"runtime")
    _write(built_mod, mod_content)
    _write(built_pak, pak_content)
    for root, suffix in ((subscribed, b"sub"), (devtools, b"dev")):
        _write(root / MOD_NAME, b"old installed current-name mod")
        _write(
            root / "DLUMA (0.3.1).mod",
            b"stale version",
        )
        _write(root / "unrelated.mod", b"must remain")
        _write(root / "dluma.ini", b"[loader]\r\nvalue=1\r\n")
        _write(root / "other.ini", b"[other]\r\nvalue=" + suffix + b"\r\n")
        _write(
            root / "dluma_runtime" / "global_mods.ini",
            b"[global_mods]\r\nenabled=true\r\n",
        )
        diagnostic_paths = {
            "dluma.log": b"loader log " + suffix,
            "lua_discovery_report.txt": b"discovery report " + suffix,
            "custom_map_asset_trace.log": b"asset trace " + suffix,
            "custom_map_asset_trace_summary.txt": b"trace summary " + suffix,
            "custom_content_ledger_report.txt": b"ledger report " + suffix,
            "frontend_content_cold_reboot_report.txt": (
                b"cold reboot report " + suffix
            ),
            "frontend_startup_baseline.txt": b"frontend baseline " + suffix,
        }
        for name, content in diagnostic_paths.items():
            path = root / "dluma_runtime" / name
            _write(path, content)
            os.utime(path, ns=(1_725_000_000_000_000_000,) * 2)
        crash_report = (
            root
            / "dluma_runtime"
            / "crashes"
            / "dluma_crash_session_20260730T182136Z_pid-23764.log"
        )
        _write(crash_report, b"crash report " + suffix)
        os.utime(crash_report, ns=(1_725_000_001_000_000_000,) * 2)
        _write(
            root / "dluma_runtime" / "unrelated_diagnostic.log",
            b"not loader-owned diagnostic evidence",
        )
        _write(root / "dluma_runtime" / "obsolete.bin", b"old runtime")

    return FakeDeployment(
        package=package,
        subscribed=subscribed,
        devtools=devtools,
        built_mod=built_mod,
        built_pak=built_pak,
    )


def _deploy(layout: FakeDeployment) -> deployment.DeploymentResult:
    return deployment.deploy_local_package(
        package_dir=layout.package,
        subscribed_dir=layout.subscribed,
        devtools_dir=layout.devtools,
        built_mod=layout.built_mod,
        built_pak=layout.built_pak,
        mod_name=MOD_NAME,
    )


def test_failed_runtime_copy_preserves_mod_data(tmp_path, monkeypatch):
    layout = _make_fake_deployment(tmp_path)
    saved = layout.subscribed / "dluma_runtime" / "mod_data" / "example" / "camera.dlws"
    saved.parent.mkdir(parents=True)
    saved.write_bytes(b"saved camera offsets")

    def fail_copy(*args, **kwargs):
        raise OSError("copy failed")

    monkeypatch.setattr(deployment.shutil, "copytree", fail_copy)
    with pytest.raises(deployment.DeploymentError):
        _deploy(layout)
    assert saved.read_bytes() == b"saved camera offsets"


@pytest.mark.parametrize("copy_fails", [False, True])
def test_deployment_preserves_binary_mod_configuration(tmp_path, monkeypatch, copy_fails):
    layout = _make_fake_deployment(tmp_path)
    settings = {}
    for index, root in enumerate((layout.subscribed, layout.devtools)):
        path = root / "dluma_runtime" / "mod_config" / "owner" / "setting.dlumac"
        content = b"DLWCFG1\n\x04\x00\xff" + bytes([index])
        _write(path, content)
        settings[path] = content
        _write(root / "dluma_runtime" / "mod_data" / "owner" / "state.bin", b"state")
    if copy_fails:
        def fail_copy(*args, **kwargs):
            raise OSError("copy failed")
        monkeypatch.setattr(deployment.shutil, "copytree", fail_copy)
        with pytest.raises(deployment.DeploymentError):
            _deploy(layout)
    else:
        _deploy(layout)
    for path, content in settings.items():
        assert path.read_bytes() == content
        assert (path.parents[2] / "mod_data" / "owner" / "state.bin").read_bytes() == b"state"


def test_shared_deployer_preserves_configuration_and_runtime_diagnostics(
    tmp_path,
):
    layout = _make_fake_deployment(tmp_path)
    for root in (layout.subscribed, layout.devtools):
        saved = root / "dluma_runtime" / "mod_data" / "example" / "camera.dlws"
        saved.parent.mkdir(parents=True)
        saved.write_bytes(b"saved camera offsets")
    ini_before = {
        root: {
            path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*.ini")
        }
        for root in (layout.subscribed, layout.devtools)
    }
    diagnostics_before = {
        root: deployment._snapshot_runtime_diagnostics(root)
        for root in (layout.subscribed, layout.devtools)
    }
    ini_expected = {
        root: deployment._expected_ini_after_animation_refresh(snapshot)
        for root, snapshot in ini_before.items()
    }

    result = _deploy(layout)

    assert result.mod_sha256 == deployment.sha256_file(layout.built_mod)
    assert result.pak_sha256 == deployment.sha256_file(layout.built_pak)
    assert result.subscribed_stale_mods_removed == (
        "DLUMA (0.3.1).mod",
    )
    assert result.devtools_stale_mods_removed == (
        "DLUMA (0.3.1).mod",
    )
    assert result.subscribed_ini_manifest_sha256 == (
        deployment._ini_manifest_sha256(ini_expected[layout.subscribed])
    )
    assert result.devtools_ini_manifest_sha256 == (
        deployment._ini_manifest_sha256(ini_expected[layout.devtools])
    )
    assert result.subscribed_runtime_diagnostic_manifest_sha256 == (
        deployment._runtime_diagnostic_manifest_sha256(
            diagnostics_before[layout.subscribed]
        )
    )
    assert result.devtools_runtime_diagnostic_manifest_sha256 == (
        deployment._runtime_diagnostic_manifest_sha256(
            diagnostics_before[layout.devtools]
        )
    )
    for root in (layout.subscribed, layout.devtools):
        assert (root / "dluma_runtime" / "mod_data" / "example" / "camera.dlws").read_bytes() == b"saved camera offsets"
        assert (root / MOD_NAME).read_bytes() == layout.built_mod.read_bytes()
        assert (
            root / "dluma.pak"
        ).read_bytes() == layout.built_pak.read_bytes()
        assert not (
            root / "DLUMA (0.3.1).mod"
        ).exists()
        assert (root / "unrelated.mod").read_bytes() == b"must remain"
        assert not (root / "dluma_runtime" / "obsolete.bin").exists()
        assert not (
            root / "dluma_runtime" / "unrelated_diagnostic.log"
        ).exists()
        assert (
            root / "dluma_runtime" / "managed" / "runtime.dll"
        ).read_bytes() == b"runtime"
        ini_after = {
            path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*.ini")
        }
        assert ini_after == ini_expected[root]
        assert ini_after["other.ini"] == ini_before[root]["other.ini"]
        assert ini_after["dluma_runtime/global_mods.ini"] == (
            ini_before[root]["dluma_runtime/global_mods.ini"]
        )
        assert (
            deployment._snapshot_runtime_diagnostics(root)
            == diagnostics_before[root]
        )
        loader_mods = [
            path
            for path in root.iterdir()
            if deployment.LOADER_MOD_PATTERN.fullmatch(path.name)
        ]
        assert [path.name for path in loader_mods] == [MOD_NAME]

    _deploy(layout)
    for root in (layout.subscribed, layout.devtools):
        assert deployment._snapshot_ini_files(root) == ini_expected[root]


@pytest.mark.parametrize(
    ("original", "expected"),
    [
        (
            b"[loader]\r\nvalue=1\r\n",
            b"[loader]\r\nvalue=1\r\n\r\n"
            b"[AnimationRefresh]\r\nEnabled=1\r\nRoute=ProjectRPack\r\n",
        ),
        (
            b"\xef\xbb\xbf[loader]\nvalue=1",
            b"\xef\xbb\xbf[loader]\nvalue=1\n\n"
            b"[AnimationRefresh]\nEnabled=1\nRoute=ProjectRPack\n",
        ),
        (
            b"[AnimationRefresh]\nRoute=RawLoose\n[other]\nvalue=1\n",
            b"[AnimationRefresh]\nRoute=RawLoose\nEnabled=1\n"
            b"[other]\nvalue=1\n",
        ),
        (
            b"[AnimationRefresh]\r\nEnabled=0\r\n; keep disabled\r\n",
            b"[AnimationRefresh]\r\nEnabled=0\r\n; keep disabled\r\n"
            b"Route=ProjectRPack\r\n",
        ),
        (
            b"[animationrefresh]\n enabled = 0 ; user choice\n"
            b" ROUTE = RawLoose\n",
            b"[animationrefresh]\n enabled = 0 ; user choice\n"
            b" ROUTE = RawLoose\n",
        ),
    ],
)
def test_animation_refresh_migration_preserves_existing_ini_bytes(
    original: bytes, expected: bytes
):
    actual = deployment._migrate_animation_refresh_ini(original)
    assert actual == expected
    assert deployment._migrate_animation_refresh_ini(actual) == actual


@pytest.mark.parametrize(
    "content",
    [
        b"[AnimationRefresh]\nEnabled=1\n[animationrefresh]\nRoute=ProjectRPack\n",
        b"[AnimationRefresh]\nEnabled=1\nenabled=0\n",
        b"[AnimationRefresh]\nRoute=ProjectRPack\nroute=RawLoose\n",
        b"[AnimationRefresh]\nEnabled\n",
        b"[AnimationRefresh\nEnabled=1\n",
        b"[AnimationRefresh]\x00\n",
    ],
)
def test_animation_refresh_migration_rejects_ambiguous_ini(content: bytes):
    with pytest.raises(deployment.DeploymentError):
        deployment._migrate_animation_refresh_ini(content)


def test_animation_refresh_migration_rejects_ambiguous_ini_file_casing():
    with pytest.raises(deployment.DeploymentError, match="file casing"):
        deployment._expected_ini_after_animation_refresh(
            {"dluma.ini": b"", "DLUMA.INI": b""}
        )


def test_animation_refresh_defaults_match_checked_in_template():
    template = (ROOT / "config" / "dluma.ini").read_text(
        encoding="utf-8"
    )
    section = template.split("[AnimationRefresh]", 1)[1].split("\n[", 1)[0]
    defaults = dict(deployment.ANIMATION_REFRESH_DEFAULTS)
    for key, value in defaults.items():
        assert re.search(
            rf"(?m)^{key.decode()}={value.decode()}$", section
        )


def test_local_deployment_preserves_explicit_animation_refresh_choices(tmp_path):
    layout = _make_fake_deployment(tmp_path)
    choices = b"[AnimationRefresh]\r\nEnabled=0\r\nRoute=RawLoose\r\n"
    for root in (layout.subscribed, layout.devtools):
        _write(root / "dluma.ini", choices)

    _deploy(layout)

    for root in (layout.subscribed, layout.devtools):
        assert (root / "dluma.ini").read_bytes() == choices


def test_local_deployment_creates_only_missing_loader_ini(tmp_path):
    layout = _make_fake_deployment(tmp_path)
    for root in (layout.subscribed, layout.devtools):
        (root / "dluma.ini").unlink()
    _deploy(layout)
    for root in (layout.subscribed, layout.devtools):
        assert (root / "dluma.ini").read_bytes() == (
            b"[AnimationRefresh]\r\nEnabled=1\r\nRoute=ProjectRPack\r\n"
        )


def test_ambiguous_animation_refresh_preflight_leaves_destinations_unchanged(
    tmp_path,
):
    layout = _make_fake_deployment(tmp_path)
    ambiguous = b"[AnimationRefresh]\nEnabled=1\nEnabled=0\n"
    _write(layout.devtools / "dluma.ini", ambiguous)
    before = {
        root: deployment._snapshot_ini_files(root)
        for root in (layout.subscribed, layout.devtools)
    }
    with pytest.raises(deployment.DeploymentError, match="duplicate"):
        _deploy(layout)
    for root in (layout.subscribed, layout.devtools):
        assert deployment._snapshot_ini_files(root) == before[root]
        assert (root / MOD_NAME).read_bytes() == b"old installed current-name mod"


def test_native_runtime_admission_matches_loader_owned_mutable_root_contract():
    source = (
        ROOT / "src" / "core" / "dlw_runtime_tree.c"
    ).read_text(encoding="utf-8")

    def array_entries(name: str) -> set[str]:
        match = re.search(
            rf"{name}\[\]\s*=\s*\{{(?P<body>.*?)\}};",
            source,
            re.DOTALL,
        )
        assert match is not None
        return set(re.findall(r'L"([^"]+)"', match.group("body")))

    diagnostic_files = {
        Path(path).name
        for path in deployment.PRESERVED_RUNTIME_DIAGNOSTIC_PATHS
    }
    expected_files = diagnostic_files | {
        "custom_main_menu_activation.ini",
        "global_mods.ini",
        "native_mods.ini",
        "startup_recovery.ini",
    }
    assert array_entries("g_dluma_runtime_mutable_root_files") == expected_files
    assert array_entries(
        "g_dluma_runtime_mutable_root_directories"
    ) == {"crashes", "markers", "mod_config", "mod_data"}
    assert "depth == 0U" in source
    assert "dluma_runtime_tree_mutable_root_entry(" in source


def test_source_hash_mismatch_fails_before_any_destination_mutation(tmp_path):
    layout = _make_fake_deployment(tmp_path)
    stale = (
        layout.subscribed
        / "DLUMA (0.3.1).mod"
    )
    old_runtime = layout.subscribed / "dluma_runtime" / "obsolete.bin"
    (layout.package / "dluma.pak").write_bytes(b"wrong package")

    with pytest.raises(deployment.DeploymentError, match="source PAK hash mismatch"):
        _deploy(layout)

    assert stale.read_bytes() == b"stale version"
    assert old_runtime.read_bytes() == b"old runtime"
    assert (layout.subscribed / MOD_NAME).read_bytes() == (
        b"old installed current-name mod"
    )


def test_packaged_runtime_diagnostic_fails_before_destination_mutation(
    tmp_path,
):
    layout = _make_fake_deployment(tmp_path)
    stale = (
        layout.subscribed
        / "DLUMA (0.3.1).mod"
    )
    old_runtime = layout.subscribed / "dluma_runtime" / "obsolete.bin"
    _write(
        layout.package / "dluma_runtime" / "dluma.log",
        b"stale packaged log",
    )

    with pytest.raises(
        deployment.DeploymentError,
        match="must not contain generated diagnostics",
    ):
        _deploy(layout)

    assert stale.read_bytes() == b"stale version"
    assert old_runtime.read_bytes() == b"old runtime"


def test_oversized_runtime_diagnostic_fails_before_destination_mutation(
    tmp_path, monkeypatch
):
    layout = _make_fake_deployment(tmp_path)
    stale = (
        layout.subscribed
        / "DLUMA (0.3.1).mod"
    )
    monkeypatch.setattr(
        deployment,
        "MAX_PRESERVED_RUNTIME_DIAGNOSTIC_BYTES",
        4,
    )

    with pytest.raises(
        deployment.DeploymentError,
        match="runtime diagnostic is too large to preserve",
    ):
        _deploy(layout)

    assert stale.read_bytes() == b"stale version"


def test_destination_mod_and_pak_hash_mismatches_fail_verification(tmp_path):
    layout = _make_fake_deployment(tmp_path)
    _deploy(layout)
    (layout.subscribed / MOD_NAME).write_bytes(b"corrupt installed mod")

    with pytest.raises(
        deployment.DeploymentError, match="deployed MOD hash mismatch"
    ):
        deployment.verify_deployment(
            subscribed_dir=layout.subscribed,
            devtools_dir=layout.devtools,
            built_mod=layout.built_mod,
            built_pak=layout.built_pak,
            mod_name=MOD_NAME,
        )

    (layout.subscribed / MOD_NAME).write_bytes(layout.built_mod.read_bytes())
    (layout.devtools / "dluma.pak").write_bytes(
        b"corrupt installed pak"
    )
    with pytest.raises(
        deployment.DeploymentError, match="deployed PAK hash mismatch"
    ):
        deployment.verify_deployment(
            subscribed_dir=layout.subscribed,
            devtools_dir=layout.devtools,
            built_mod=layout.built_mod,
            built_pak=layout.built_pak,
            mod_name=MOD_NAME,
        )


def test_rejected_runtime4_hash_and_duplicate_loader_mod_fail(tmp_path):
    layout = _make_fake_deployment(tmp_path)
    result = _deploy(layout)

    with pytest.raises(
        deployment.DeploymentError, match="still equals rejected stale SHA-256"
    ):
        deployment.verify_deployment(
            subscribed_dir=layout.subscribed,
            devtools_dir=layout.devtools,
            built_mod=layout.built_mod,
            built_pak=layout.built_pak,
            mod_name=MOD_NAME,
            rejected_mod_sha256=result.mod_sha256,
        )

    _write(
        layout.subscribed
        / "DLUMA (unexpected).mod",
        b"duplicate",
    )
    with pytest.raises(
        deployment.DeploymentError,
        match="destination must contain exactly",
    ):
        deployment.verify_deployment(
            subscribed_dir=layout.subscribed,
            devtools_dir=layout.devtools,
            built_mod=layout.built_mod,
            built_pak=layout.built_pak,
            mod_name=MOD_NAME,
        )


def test_msbuild_graph_deploys_both_build_configurations_and_shares_one_body():
    project_text = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    project = ET.fromstring(project_text)
    package = project.find(
        "msbuild:Target[@Name='PackageWorkshop']", NAMESPACE
    )
    shared = project.find(
        "msbuild:Target[@Name='DeployLocalPackage']", NAMESPACE
    )
    explicit = project.find("msbuild:Target[@Name='DeployLocal']", NAMESPACE)
    automatic = project.find(
        "msbuild:Target[@Name='AutoDeployLocalAfterBuild']", NAMESPACE
    )
    assert package is not None
    assert shared is not None
    assert explicit is not None
    assert automatic is not None

    assert package.attrib["AfterTargets"] == "Build"
    assert "DlumaSubscribedWorkshopModsDir" not in ET.tostring(
        package, encoding="unicode"
    )
    assert automatic.attrib["AfterTargets"] == "PackageWorkshop"
    condition = automatic.attrib["Condition"]
    assert "'$(Platform)' == 'x64'" in condition
    assert "'$(Configuration)' == 'Debug'" in condition
    assert "'$(Configuration)' == 'Release'" in condition
    assert "'$(DlumaAutoDeployLocal)' == 'true'" in condition
    assert "Exists('$(DlumaLocalPropsPath)')" in condition
    assert "'$(DlumaSkipPackage)' != 'true'" in condition
    assert explicit.attrib["DependsOnTargets"] == "Build;DeployLocalPackage"
    assert "DlumaAutoDeployLocal" not in explicit.attrib["Condition"]
    assert project_text.count("tools\\deploy_local_package.py") == 1

    commands = [
        child.attrib["Command"]
        for child in shared.findall("msbuild:Exec", NAMESPACE)
    ]
    assert len(commands) == 3
    assert "verify_package_manifest.py" in commands[0]
    assert "verify_pe.py" in commands[1]
    assert "deploy_local_package.py" in commands[2]


def test_local_props_example_is_opt_in_false_and_ci_safe():
    example = ET.fromstring(
        (ROOT / "DLUMA.local.props.example").read_text(
            encoding="utf-8"
        )
    )
    auto_value = example.find(".//msbuild:DlumaAutoDeployLocal", NAMESPACE)
    assert auto_value is not None
    assert auto_value.text == "false"

    project_text = (ROOT / "DLUMA.vcxproj").read_text(
        encoding="utf-8"
    )
    assert (
        "<DlumaAutoDeployLocal "
        "Condition=\"'$(DlumaAutoDeployLocal)' == ''\">false"
        "</DlumaAutoDeployLocal>"
    ) in project_text
    assert "and Exists('$(DlumaLocalPropsPath)')" in project_text
    assert (
        deployment.REJECTED_RUNTIME4_MOD_SHA256
        == "CBFC0D950EE2A4C4273AC3903A73A3AACED6BB39EB802F9F96ED544D6D5FD980"
    )


