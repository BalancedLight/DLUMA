import re
import subprocess
from collections import Counter
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
# Keep the required behavioral coverage explicit without fixing the total number
# of cases: adding a native check must not require updating an unrelated count.
REQUIRED_CASES = {
    "lua_owner_initializes": 1,
    "csharp_owner_initializes": 1,
    "native_owner_initializes": 1,
    "cross_language_world_find_status_parity": 1,
    "cross_language_world_find_value_parity": 1,
    "handles_remain_owner_bound": 1,
    "embedded_nul_fails_before_backend": 1,
    "versioned_capability_query": 1,
    "implemented_operation_is_permission_gated": 1,
    "removed_operation_returns_removed": 1,
    "unknown_service_is_rejected": 1,
    "unsupported_service_version_is_rejected": 1,
    "truncated_service_header_is_rejected": 1,
    "foreign_handle_is_rejected": 1,
    "stale_handle_is_rejected": 1,
    "revoked_owner_cannot_enter_backend": 1,
    "duplicate_access_is_rejected": 1,
    # Each tuning policy is exercised for Lua, C# and native owners.
    "global_player_tuning_write_with_access": 3,
    "global_player_tuning_reset_with_access": 3,
    "global_player_tuning_requires_write_access": 3,
    "global_player_tuning_requires_game_thread": 3,
}


def assert_runtime_harness_success(completed):
    output = completed.stdout + completed.stderr
    assert completed.returncode == 0, (
        f"API runtime harness exited {completed.returncode}\n{output}"
    )
    assert "[FAIL]" not in output, output
    cases = Counter(
        re.findall(r"^[ \t]*\[PASS\][ \t]+(\S+)[ \t]*$", completed.stdout, re.M)
    )
    summaries = re.findall(
        r"^[ \t]*DLUMA API runtime fake cases passed:[ \t]*(\d+);"
        r"[ \t]*failed:[ \t]*(\d+)[ \t]*$",
        completed.stdout,
        re.M,
    )
    assert len(summaries) == 1, f"Missing or duplicate runtime summary\n{output}"
    passed, failed = map(int, summaries[0])
    assert failed == 0, f"Runtime summary reported failures\n{output}"
    assert passed == sum(cases.values()), (
        f"Runtime summary disagrees with named passes\n{output}"
    )
    missing = {
        name: minimum - cases[name]
        for name, minimum in REQUIRED_CASES.items()
        if cases[name] < minimum
    }
    assert not missing, f"Missing required runtime cases: {missing}\n{output}"


def test_language_neutral_runtime_fake_backend_contract():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_dlw_api_runtime_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert_runtime_harness_success(completed)


def runtime_output(*, extra_cases=(), missing_case=None, failed=0, total_offset=0):
    cases = [
        name
        for name, count in REQUIRED_CASES.items()
        for _ in range(count)
    ] + list(extra_cases)
    if missing_case is not None:
        cases.remove(missing_case)
    return "compiler output\n" + "".join(f"[PASS] {name}\n" for name in cases) + (
        f"DLUMA API runtime fake cases passed: {len(cases) + total_offset}; "
        f"failed: {failed}\n"
    )


def test_runtime_wrapper_accepts_additional_cases(monkeypatch):
    completed = subprocess.CompletedProcess(
        args=[], returncode=0,
        stdout=runtime_output(extra_cases=("new_policy_regression",)), stderr="",
    )
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: completed)
    test_language_neutral_runtime_fake_backend_contract()


@pytest.mark.parametrize(
    "returncode,stdout,stderr",
    [
        (1, runtime_output(), "compiler/linker diagnostic"),
        (0, runtime_output() + "[FAIL] new_policy_regression\n", ""),
        (0, runtime_output(), "[FAIL] new_policy_regression\n"),
        (0, runtime_output(failed=1), ""),
        (0, runtime_output(total_offset=1), ""),
        (0, runtime_output(missing_case="foreign_handle_is_rejected"), ""),
        (0, runtime_output(missing_case="global_player_tuning_requires_game_thread"), ""),
        (0, runtime_output().split("DLUMA API runtime fake cases passed:")[0], ""),
        (0, runtime_output() + runtime_output(), ""),
        (0, "", ""),
    ],
    ids=[
        "nonzero-exit", "failed-case", "stderr-failure", "failed-summary",
        "wrong-total", "missing-safety-case", "incomplete-language-coverage",
        "missing-summary", "duplicate-summary", "empty-output",
    ],
)
def test_runtime_wrapper_rejects_failures(monkeypatch, returncode, stdout, stderr):
    completed = subprocess.CompletedProcess(
        args=[], returncode=returncode, stdout=stdout, stderr=stderr,
    )
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: completed)
    with pytest.raises(AssertionError):
        test_language_neutral_runtime_fake_backend_contract()


def test_runtime_uses_generated_policy_and_service_tables():
    runtime = (ROOT / "src/core/dlw_api_runtime.c").read_text(encoding="utf-8")
    operations = (
        ROOT / "src/generated/dluma_api_operations.generated.h"
    ).read_text(encoding="utf-8")
    bindings = (
        ROOT / "src/generated/dluma_api_service_bindings.generated.inc"
    ).read_text(encoding="utf-8")
    assert "dluma_mod_owner_require" in runtime
    assert "dluma_capability_registry_ready" in runtime
    assert "dluma_api_validate_request" in runtime
    assert "g_dluma_generated_operation_policies" in operations
    assert "dluma_generated_fill_service" in bindings
    assert "DlumaLua54" not in runtime
    assert "DlumaLua54" not in operations
    assert "DlumaLua54" not in bindings
