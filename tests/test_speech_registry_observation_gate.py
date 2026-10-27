from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "SpeechRegistryObservationGate.h"
SOURCE = CUSTOM / "SpeechRegistryObservationGate.cpp"
HARNESS = ROOT / "tests" / "speech_registry_observation_gate_fake_harness.cpp"
SETUP_MSVC = ROOT / "tools" / "setup_msvc.bat"


def _msvc_environment(output: Path) -> dict[str, str]:
    bootstrap = output / "capture_msvc_environment.cmd"
    bootstrap.write_text(
        "@echo off\r\n"
        f'call "{SETUP_MSVC}" >nul\r\n'
        "set\r\n",
        encoding="utf-8",
        newline="",
    )
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/c",
            str(bootstrap.relative_to(ROOT)),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    environment: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        normalized_name = name.upper()
        if normalized_name == "PATH" and len(value) <= len(
            environment.get(normalized_name, "")
        ):
            continue
        environment[normalized_name] = value
    return environment


def test_native_speech_registry_observation_gate_harness():
    output = ROOT / "build" / "speech-registry-observation-gate-fake"
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "speech_registry_observation_gate_fake_harness.exe"
    environment = _msvc_environment(output)
    compiler = (
        Path(environment["VCTOOLSINSTALLDIR"])
        / "bin"
        / "HostX64"
        / "x64"
        / "cl.exe"
    )
    compiled = subprocess.run(
        [
            str(compiler),
            "/nologo",
            "/TP",
            "/MT",
            "/O2",
            "/Oi-",
            "/W4",
            "/WX",
            "/EHs-c-",
            "/GR-",
            "/std:c++17",
            "/D_HAS_EXCEPTIONS=0",
            "/D_CRT_SECURE_NO_WARNINGS",
            "/I",
            str(ROOT / "src"),
            f'/Fo{output}{os.sep}',
            f"/Fd{output / 'speech_registry_observation_gate_fake_harness.pdb'}",
            f"/Fe{executable}",
            str(HARNESS),
            str(SOURCE),
            "/link",
            "/stack:8388608",
            "kernel32.lib",
            "user32.lib",
        ],
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    completed = subprocess.run(
        [str(executable)],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 6
    assert "speech registry observation gate cases passed: 6" in (
        completed.stdout
    )


def test_gate_contract_is_fixed_pod_and_hook_safe():
    joined = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(
        encoding="utf-8"
    )
    for contract in (
        "SpeechRegistryObservationGate",
        "PVOID ownership_lock",
        "volatile LONG admission_state",
        "volatile LONGLONG revision",
        "volatile LONGLONG completed_mutation_count",
        "BeginSpeechRegistryMutation",
        "EndSpeechRegistryMutation",
        "TryAcquireSpeechRegistrySnapshot",
        "CompleteSpeechRegistrySnapshot",
        "AbandonSpeechRegistrySnapshot",
        "observed_entry_count",
    ):
        assert contract in joined
    for forbidden in (
        "std::vector",
        "std::map",
        "std::string",
        "std::mutex",
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "memcpy(",
        "memset(",
        "fopen(",
        "WriteFile(",
        "Sleep(",
        "WaitForSingleObject(",
        "_mm_pause()",
        "throw ",
        "\ntry {",
        "catch (",
    ):
        assert forbidden not in joined


def test_gate_protocol_is_shared_writer_exclusive_try_reader():
    source = SOURCE.read_text(encoding="utf-8")
    harness = HARNESS.read_text(encoding="utf-8")
    for protocol in (
        "kSnapshotOwnerBit",
        "kWriterCountMask",
        "AcquireSRWLockShared(&gate->ownership_lock)",
        "ReleaseSRWLockShared(&gate->ownership_lock)",
        "TryAcquireSRWLockExclusive(&gate->ownership_lock)",
        "ReleaseSRWLockExclusive(&gate->ownership_lock)",
        "dluma_atomic_increment(&gate->admission_state)",
        "&gate->admission_state, kSnapshotOwnerBit, 0",
        "dluma_atomic_decrement(&gate->admission_state)",
        "IncrementSaturating(&gate->completed_mutation_count)",
        "IncrementSaturating(&gate->revision)",
    ):
        assert protocol in source
    for case in (
        "failed_snapshot_is_nonblocking",
        "writer_waits_only_for_bounded_reader_and_never_overlaps",
        "nested_writers_never_wait_for_each_other",
        "concurrent_writer_is_admitted_beside_writer",
        "revision_is_monotonic_and_stamp_is_stable",
    ):
        assert f'"{case}"' in harness


def test_snapshot_acquire_is_nonblocking_and_completion_is_stable():
    source = SOURCE.read_text(encoding="utf-8")
    acquire = source[source.index("bool TryAcquireSpeechRegistrySnapshot(") :]
    acquire = acquire[: acquire.index("\n}")]
    assert "for (;;)" not in acquire
    assert "_mm_pause" not in acquire
    assert "return false" in acquire

    complete = source[source.index("bool CompleteSpeechRegistrySnapshot(") :]
    complete = complete[: complete.index("\n}")]
    assert "AtomicLoadState(gate) == kSnapshotOwnerBit" in complete
    assert "AtomicLoadCounter(&gate->revision) == revision" in complete
    assert "AtomicLoadCounter(&gate->completed_mutation_count)" in complete
    assert "ReleaseSnapshotOwnership(token)" in complete
