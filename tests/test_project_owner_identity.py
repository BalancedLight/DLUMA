from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _junction(link: Path, target: Path) -> None:
    completed = subprocess.run(
        ["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(target)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stdout + completed.stderr


def test_native_project_owner_identity_and_containment() -> None:
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_project_owner_identity_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )

    with tempfile.TemporaryDirectory(
        prefix="dluma-project-owner-"
    ) as temporary:
        test_root = Path(temporary)
        discovery = test_root / "workshop"
        project = discovery / "ProjectA"
        other = test_root / "outside"
        anchor = (
            project
            / "data"
            / "maps"
            / "dlua_example"
            / "dlua_example.scr"
        )
        outside_anchor = (
            other
            / "map_target"
            / "dlua_example.scr"
        )
        local_anchor = (
            other
            / "data"
            / "maps"
            / "dlua_example"
            / "dlua_example.scr"
        )
        anchor.parent.mkdir(parents=True)
        anchor.write_text("Map definition", encoding="utf-8")
        outside_anchor.parent.mkdir(parents=True)
        outside_anchor.write_text(
            "Escaped map definition", encoding="utf-8"
        )
        local_anchor.parent.mkdir(parents=True)
        local_anchor.write_text(
            "Local map definition", encoding="utf-8"
        )
        (project / "desc.scr").write_text(
            'id("3771245504")\n', encoding="utf-8"
        )
        (other / "desc.scr").write_text(
            'id("0")\n', encoding="utf-8"
        )

        alias = discovery / "ProjectAlias"
        escape = discovery / "EscapedProject"
        escaped_map_project = discovery / "ProjectWithEscapedMap"
        (escaped_map_project / "data" / "maps").mkdir(parents=True)
        _junction(alias, project)
        _junction(escape, other)
        _junction(
            escaped_map_project
            / "data"
            / "maps"
            / "dlua_example",
            outside_anchor.parent,
        )

        executable = (
            ROOT
            / "build"
            / "project-owner-identity-fake"
            / "project_owner_identity_fake_harness.exe"
        )
        completed = subprocess.run(
            [
                str(executable),
                str(discovery),
                str(project),
                str(alias),
                str(other),
                str(escape),
                str(escaped_map_project),
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert completed.stdout.count("[PASS]") == 6
        assert (
            "reparse escape and traversal fail closed"
            in completed.stdout
        )
        assert (
            "exact evidence selects either physical owner"
            in completed.stdout
        )
        assert (
            "pending proof retains exact-confirmed context"
            in completed.stdout
        )


def test_resolution_contract_retries_and_reconciles_same_name_owner() -> None:
    workshop = (ROOT / "src" / "dlw_workshop.c").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert "now - last_project_probe_ms >= 3000" in host
    assert "same_map_exact_project_transition" in host
    assert "active_project_owner_epoch" in host
    assert "active_project_owner_transition_generation" in host
    assert (
        "project_owner_context_retained_pending_exact_proof"
        in workshop
    )
    assert (
        "dluma_project_owner_pending_proof_retains_context"
        in workshop
    )
    assert "proof_consume_failed" in workshop
    assert "candidate_scan_incomplete" in workshop
    assert "error != ERROR_FILE_NOT_FOUND" in workshop
    assert "GetLastError() != ERROR_NO_MORE_FILES" in workshop
    assert "candidate_anchor_validation_failed" in workshop
    assert "no_matching_provider_identity" in workshop
    assert "dluma_candidate_map_directory_state" in workshop
    assert "dluma_project_regular_file_is_contained" in workshop
    assert 'Include="src\\dlw_project_owner_identity.c"' in project
