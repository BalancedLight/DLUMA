import importlib.util
from pathlib import Path
import subprocess

import pytest

from tools.local_test_paths import DEVTOOLS_ROOT


ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = ROOT / "tools" / "generate_loader_symbol_manifest.py"
RELAY_TOOL = ROOT / "tools" / "verify_playerdi_ondestroy_relay.py"


def load_generator():
    spec = importlib.util.spec_from_file_location(
        "generate_loader_symbol_manifest", GENERATOR_PATH
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_loader_symbol_native_lookup_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c",
         r"tools\build_loader_symbols_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )


def test_encoded_symbol_ranges_resolve_queries():
    generator = load_generator()
    records = [
        {
            "start_rva": 0x100,
            "end_rva": 0x180,
            "source_line": 12,
            "name": "first_function",
            "source_file": "src/first.c",
            "classification": "function_body",
        },
        {
            "start_rva": 0x200,
            "end_rva": 0x280,
            "source_line": 24,
            "name": "second_function",
            "source_file": "src/second.c",
            "classification": "function_body",
        },
    ]
    blob = generator.encode_manifest(records, 0x12345678, 0x00400000)
    for rva, expected in ((0x100, "first_function"), (0x17F, "first_function"),
                          (0x200, "second_function"), (0x27F, "second_function")):
        resolved = generator.lookup_encoded_manifest(blob, rva)
        assert resolved is not None
        assert resolved["name"] == expected
        assert resolved["start_rva"] <= rva < resolved["end_rva"]
    assert generator.lookup_encoded_manifest(blob, 0x180) is None
    assert generator.lookup_encoded_manifest(blob, 0x280) is None




def test_symbol_manifest_rejects_hash_and_string_corruption():
    generator = load_generator()
    blob = bytearray(
        generator.encode_manifest(
            [
                {
                    "start_rva": 0x100,
                    "end_rva": 0x200,
                    "source_line": 4,
                    "name": "symbol",
                    "source_file": "source.c",
                    "classification": "function_body",
                }
            ],
            1,
            2,
        )
    )
    blob[-1] ^= 1
    assert generator.lookup_encoded_manifest(bytes(blob), 0x150) is None


def test_static_relay_fixture_and_exact_installed_image():
    subprocess.run(
        ["py", "-3", str(RELAY_TOOL), "--self-test"],
        cwd=ROOT,
        check=True,
    )
    installed = DEVTOOLS_ROOT / "gamedll_x64_rwdi.dll"
    if not installed.is_file():
        pytest.skip("supported local gamedll is not installed")
    subprocess.run(
        ["py", "-3", str(RELAY_TOOL), "--image", str(installed)],
        cwd=ROOT,
        check=True,
    )
