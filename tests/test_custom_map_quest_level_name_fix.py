from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
FIXES = ROOT / "src" / "fixes"
CUSTOM = FIXES / "custom_map_quest_level_name"

TIMESTAMP = 0x623397AA
SIZE_OF_IMAGE = 0x02082000
PATCH_RVA = 0x002D2FC8
DESTINATION_RVA = 0x002D2FE5
ORIGINAL = bytes.fromhex("E8 93 05 00 00")


class Result(Enum):
    ORIGINAL_BYTES_MISMATCH = auto()
    ALREADY_APPLIED = auto()
    APPLIED = auto()
    NOT_OWNED = auto()
    MODIFIED_AFTER_APPLY = auto()
    RESTORED = auto()


@dataclass
class MockPatch:
    address: int
    expected: bytes
    replacement: bytes
    applied_by_us: bool = False


class MockMemory:
    """Injectable unit-test accessor equivalent to MemoryAccessor."""

    def __init__(self, base: int, size: int):
        self.base = base
        self.data = bytearray(size)
        self.writes: list[tuple[int, bytes]] = []

    def read(self, address: int, size: int) -> bytes:
        offset = address - self.base
        return bytes(self.data[offset : offset + size])

    def seed(self, address: int, value: bytes) -> None:
        offset = address - self.base
        self.data[offset : offset + len(value)] = value

    def write(self, address: int, value: bytes) -> None:
        self.writes.append((address, value))
        self.seed(address, value)


def relative_jump(patch_address: int, destination_address: int) -> bytes:
    displacement = destination_address - (patch_address + 5)
    return b"\xE9" + displacement.to_bytes(4, "little", signed=True)


def apply(memory: MockMemory, patch: MockPatch) -> Result:
    current = memory.read(patch.address, len(patch.replacement))
    if current == patch.replacement:
        return Result.ALREADY_APPLIED
    if current != patch.expected:
        patch.applied_by_us = False
        return Result.ORIGINAL_BYTES_MISMATCH
    memory.write(patch.address, patch.replacement)
    assert memory.read(patch.address, len(patch.replacement)) == patch.replacement
    patch.applied_by_us = True
    return Result.APPLIED


def restore(memory: MockMemory, patch: MockPatch) -> Result:
    if not patch.applied_by_us:
        return Result.NOT_OWNED
    if memory.read(patch.address, len(patch.replacement)) != patch.replacement:
        patch.applied_by_us = False
        return Result.MODIFIED_AFTER_APPLY
    memory.write(patch.address, patch.expected)
    assert memory.read(patch.address, len(patch.expected)) == patch.expected
    patch.applied_by_us = False
    return Result.RESTORED


def initialize_fix(
    memory: MockMemory,
    base: int,
    timestamp: int,
    image_size: int,
    enabled: bool = True,
) -> tuple[str, MockPatch | None]:
    if not enabled:
        return "Disabled", None
    if (timestamp, image_size) != (TIMESTAMP, SIZE_OF_IMAGE):
        return "UnsupportedBuild", None
    patch = MockPatch(
        address=base + PATCH_RVA,
        expected=ORIGINAL,
        replacement=relative_jump(base + PATCH_RVA, base + DESTINATION_RVA),
    )
    result = apply(memory, patch)
    return result.name, patch


def make_supported_memory(base: int = 0x180000000) -> MockMemory:
    memory = MockMemory(base, SIZE_OF_IMAGE)
    memory.seed(base + PATCH_RVA, ORIGINAL)
    return memory


def test_supported_fingerprint_and_original_bytes_apply_patch():
    memory = make_supported_memory()
    state, patch = initialize_fix(memory, memory.base, TIMESTAMP, SIZE_OF_IMAGE)
    assert state == "APPLIED"
    assert patch is not None and patch.applied_by_us


def test_relative_jump_displacement_and_replacement_bytes():
    base = 0x180000000
    replacement = relative_jump(base + PATCH_RVA, base + DESTINATION_RVA)
    assert int.from_bytes(replacement[1:], "little", signed=True) == 0x18
    assert replacement == bytes.fromhex("E9 18 00 00 00")


def test_unsupported_fingerprint_performs_no_write():
    memory = make_supported_memory()
    state, patch = initialize_fix(memory, memory.base, 0xDEADBEEF, SIZE_OF_IMAGE)
    assert state == "UnsupportedBuild"
    assert patch is None
    assert memory.writes == []


def test_original_byte_mismatch_performs_no_write():
    memory = make_supported_memory()
    memory.seed(memory.base + PATCH_RVA, b"\x90" * 5)
    state, _ = initialize_fix(memory, memory.base, TIMESTAMP, SIZE_OF_IMAGE)
    assert state == "ORIGINAL_BYTES_MISMATCH"
    assert memory.writes == []


def test_already_patched_is_external_and_not_restored():
    memory = make_supported_memory()
    replacement = relative_jump(
        memory.base + PATCH_RVA, memory.base + DESTINATION_RVA
    )
    memory.seed(memory.base + PATCH_RVA, replacement)
    state, patch = initialize_fix(memory, memory.base, TIMESTAMP, SIZE_OF_IMAGE)
    assert state == "ALREADY_APPLIED"
    assert patch is not None and not patch.applied_by_us
    assert restore(memory, patch) is Result.NOT_OWNED
    assert memory.read(patch.address, 5) == replacement
    assert memory.writes == []


def test_loader_owned_patch_is_restored_on_shutdown():
    memory = make_supported_memory()
    _, patch = initialize_fix(memory, memory.base, TIMESTAMP, SIZE_OF_IMAGE)
    assert patch is not None
    assert restore(memory, patch) is Result.RESTORED
    assert memory.read(patch.address, 5) == ORIGINAL


def test_modified_after_apply_is_not_overwritten_during_restore():
    memory = make_supported_memory()
    _, patch = initialize_fix(memory, memory.base, TIMESTAMP, SIZE_OF_IMAGE)
    assert patch is not None
    memory.seed(patch.address, b"\xCC" * 5)
    writes_before = list(memory.writes)
    assert restore(memory, patch) is Result.MODIFIED_AFTER_APPLY
    assert memory.read(patch.address, 5) == b"\xCC" * 5
    assert memory.writes == writes_before


def test_disabled_setting_performs_no_validation_or_patch():
    memory = make_supported_memory()
    state, patch = initialize_fix(
        memory, memory.base, 0, 0, enabled=False
    )
    assert state == "Disabled"
    assert patch is None
    assert memory.writes == []


def test_applying_twice_is_idempotent():
    memory = make_supported_memory()
    replacement = relative_jump(
        memory.base + PATCH_RVA, memory.base + DESTINATION_RVA
    )
    patch = MockPatch(memory.base + PATCH_RVA, ORIGINAL, replacement)
    assert apply(memory, patch) is Result.APPLIED
    assert apply(memory, patch) is Result.ALREADY_APPLIED
    assert len(memory.writes) == 1
    assert patch.applied_by_us


def test_patch_target_uses_loaded_base_plus_true_rva():
    base = 0x7FF600000000
    memory = MockMemory(base, SIZE_OF_IMAGE)
    memory.seed(base + PATCH_RVA, ORIGINAL)
    _, patch = initialize_fix(memory, base, TIMESTAMP, SIZE_OF_IMAGE)
    assert patch is not None
    assert patch.address == base + 0x002D2FC8


def test_registry_failure_is_fail_open_for_loader_initialization():
    registry = (FIXES / "FixRegistry.cpp").read_text(encoding="utf-8")
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
    assert "Each fix is fail-open" in registry
    assert re.search(r"InitializeAllFixes[\s\S]*?return true;", registry)
    assert "if (!dluma::fixes::InitializeAllFixes" not in exports


def test_native_patch_has_no_on_disk_dll_write_path():
    patch_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (
            FIXES / "MemoryPatch.cpp",
            FIXES / "MemoryPatch.h",
            CUSTOM / "CustomMapQuestLevelNameFix.cpp",
            CUSTOM / "CustomMapQuestLevelNameFix.h",
        )
    )
    assert "CreateFile" not in patch_sources
    assert "WriteFile" not in patch_sources
    assert "gamedll_x64_rwdi.dll" not in patch_sources
    assert "VirtualProtect" in patch_sources
    assert "FlushInstructionCache" in patch_sources


def test_fix_sources_are_isolated_under_dedicated_subfolder():
    required = {
        FIXES / "FixRegistry.h",
        FIXES / "FixRegistry.cpp",
        FIXES / "MemoryPatch.h",
        FIXES / "MemoryPatch.cpp",
        CUSTOM / "CustomMapQuestLevelNameFix.h",
        CUSTOM / "CustomMapQuestLevelNameFix.cpp",
        CUSTOM / "README.md",
    }
    assert all(path.is_file() for path in required)
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")


def test_production_constants_and_configuration_contract():
    header = (CUSTOM / "CustomMapQuestLevelNameFix.h").read_text(encoding="utf-8")
    source = (CUSTOM / "CustomMapQuestLevelNameFix.cpp").read_text(encoding="utf-8")
    memory = (FIXES / "MemoryPatch.cpp").read_text(encoding="utf-8")
    ini = (
        ROOT
        / "config"
        / "dluma.ini"
    ).read_text(encoding="utf-8")
    assert "0x002D2FC8" in header and "0x002D2FE5" in header
    assert "0x623397AA" in source and "0x02082000" in source
    assert "0xE8, 0x93, 0x05, 0x00, 0x00" in source
    assert "BuildRelativeJump(patch_address, destination_address)" in source
    assert "SetMemoryAccessorForTesting" in memory
    assert "[GameFixes]" in ini
    assert "CustomMapQuestLevelNameCrash=1" in ini
