from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = (CUSTOM / "AudioStartupObservationLedger.h").read_text(
    encoding="utf-8"
)
SOURCE = (CUSTOM / "AudioStartupObservationLedger.cpp").read_text(
    encoding="utf-8"
)
HARNESS = (
    ROOT / "tests" / "audio_startup_observation_ledger_fake_harness.cpp"
).read_text(encoding="utf-8")


def test_native_audio_startup_observation_replay():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_audio_startup_observation_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    completed = subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "audio-startup-observation-fake"
                / "audio_startup_observation_ledger_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 14
    assert "audio startup observation cases passed: 14" in completed.stdout


def test_ledger_is_fixed_capacity_pod_and_hook_io_free():
    for contract in (
        "kAudioStartupScriptCapacity = 32",
        "kAudioStartupBankCapacity = 256",
        "kAudioStartupLogicalPathCapacity = 256",
        "kAudioStartupProviderPathCapacity = 1024",
        "AudioStartupBankOwner::Unknown",
        "bool localized_observed",
        "coverage_started_before_audio_singletons",
        "hook_plan_complete",
        "volatile LONG lock",
        "volatile LONG contention_mutations",
        "volatile LONGLONG revision",
    ):
        assert contract in HEADER or contract in SOURCE
    for forbidden in (
        "std::vector",
        "std::map",
        "std::string",
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "fopen(",
        "WriteFile(",
        "dluma_write_utf8_file(",
        "throw ",
    ):
        assert forbidden not in SOURCE


def test_every_observation_loss_invalidates_a_frozen_revision():
    for field in (
        "lock_contention",
        "script_overflow",
        "bank_overflow",
        "incomplete_observation",
    ):
        assert re.search(
            rf"(?:\+\+ledger->loss\.{field};|"
            rf"&ledger->loss\.{field}\)\);)\s*Touch\(ledger\);",
            SOURCE,
        ), f"{field} must invalidate a frozen audio observation revision"
    assert "loss.prepublication_observation" in SOURCE
    assert "loss.script_depth_overflow" in SOURCE
    for case in (
        "invalid_records_fail_closed_and_advance_revision",
        "script_capacity_is_bounded_and_nonblocking",
        "bank_capacity_is_bounded_and_nonblocking",
        "contention_drops_immediately_and_invalidates_revision",
        "prepublication_observation_poisons_authority",
        "script_depth_overflow_is_latched",
    ):
        assert f'"{case}"' in HARNESS


def test_incomplete_observations_are_never_silently_authoritative():
    bank = SOURCE[SOURCE.index("bool RecordAudioStartupBank(") :]
    assert "!observation.observation_complete" in bank
    assert "&ledger->loss.incomplete_observation" in bank
    assert "++ledger->loss.incomplete_observation" not in bank
    assert "AudioStartupObservationLossEmpty" in SOURCE
    assert "incomplete_bank_is_retained_but_poisons_authority" in HARNESS


def test_capture_rejects_cross_lock_contention_mutations():
    capture = SOURCE[SOURCE.index(
        "bool CaptureAudioStartupObservationSnapshot(") :]
    for contract in (
        "AtomicLoadContentionMutations(ledger)",
        "AtomicLoadRevision(ledger)",
        "AtomicLoadCounter(&ledger->loss.lock_contention)",
        "AtomicLoadCounter(&ledger->loss.script_overflow)",
        "AtomicLoadCounter(&ledger->loss.bank_overflow)",
        "AtomicLoadCounter(&ledger->loss.incomplete_observation)",
        "AtomicLoadCounter(&ledger->loss.prepublication_observation)",
        "AtomicLoadCounter(&ledger->loss.script_depth_overflow)",
        "lock_contention_after != lock_contention_before",
        "revision_after != revision_before",
        "std::memset(output, 0, sizeof(*output))",
    ):
        assert contract in capture
    assert "if (!TryAcquire(ledger)) return false;" in capture
    assert "AcquireForProducer(ledger)" not in capture
    assert "output->loss = ledger->loss" not in capture
    assert "snapshot_reader_contention_does_not_poison_loss" in HARNESS
    assert (
        "in_flight_contention_mutation_rejects_capture_deterministically"
        in HARNESS
    )
    assert "concurrent_contention_never_returns_torn_snapshot" in HARNESS
    assert "SnapshotRevisionMatchesAllRecordedMutations" in HARNESS


def test_only_producers_poison_the_loss_latch_on_lock_contention():
    for producer in (
        "void SetAudioStartupHookPlanComplete(",
        "bool RecordAudioStartupScript(",
        "bool RecordAudioStartupBank(",
        "bool RecordAudioStartupScriptDepthOverflow(",
        "bool RecordAudioStartupMutationFence(",
    ):
        body = SOURCE[SOURCE.index(producer) :]
        body = body[: body.index("\n}")]
        assert "AcquireForProducer(ledger)" in body

    capture = SOURCE[SOURCE.index(
        "bool CaptureAudioStartupObservationSnapshot(") :]
    capture = capture[: capture.index("\n}")]
    assert "TryAcquire(ledger)" in capture
    assert "RecordLockContention(ledger)" not in capture
