import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_CASES = 17


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
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert completed.stdout.count("[PASS]") == EXPECTED_CASES
    assert (
        f"DLUMA API runtime fake cases passed: {EXPECTED_CASES}; failed: 0"
        in completed.stdout
    )


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
