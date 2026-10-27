from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = (CUSTOM / "AnimationEventDefinitionsObservation.h").read_text(
    encoding="utf-8"
)
SOURCE = (CUSTOM / "AnimationEventDefinitionsObservation.cpp").read_text(
    encoding="utf-8"
)
PROJECT = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")


def test_native_animation_event_definition_observation_replay():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_animation_event_definitions_observation_fake_tests.bat",
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
                / "animation-event-definitions-observation-fake"
                / "animation_event_definitions_observation_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert completed.stdout.count("[PASS]") == 16
    assert (
        "animation event definition observation cases passed: 16"
        in completed.stdout
    )


def test_exact_validated_animation_event_target_is_encoded():
    expected = {
        "kSupportedGamedllTimestamp": "0x623397AAU",
        "kSupportedGamedllImageSize": "0x02082000U",
        "kLoadDefinitionsRva": "0x00C18C00U",
        "kLoadDefinitionsEndRva": "0x00C19349U",
        "kLoadDefinitionsStolenLength": "18U",
        "kManagerRootRva": "0x01DF47B8U",
        "kManagerDefinitionCountOffset": "0x08U",
        "kStartupCallRva": "0x003BFBC4U",
        "kParserCallRva": "0x00C18CC1U",
        "kParserTargetRva": "0x012C64C0U",
        "kTreeReleaseCallRva": "0x00C18D8AU",
        "kTreeReleaseTargetRva": "0x0039FD70U",
        "kFactoryCallRva": "0x00C19175U",
        "kFactoryTargetRva": "0x00C19350U",
        "kConsumerGetterRva": "0x00C19560U",
    }
    for name, value in expected.items():
        assert re.search(rf"{name}\s*=\s*{value}", HEADER)
    assert (
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
        in HEADER
    )
    assert 'kStartupPath[] = "AnimEventData.scr"' in HEADER
    assert (
        "bool/byte __fastcall(manager* RCX, ttl::string_base<char> const* RDX)"
        in HEADER
    )

    prefix_match = re.search(
        r"kLoadDefinitionsPrefix\[\]\s*=\s*\{([^}]*)\}", HEADER, re.S
    )
    assert prefix_match
    actual_prefix = bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9A-Fa-f]{2})", prefix_match.group(1))
    )
    expected_prefix = bytes.fromhex(
        "48 89 4C 24 08 53 55 56 57 41 55 48 81 EC 60 01 00 00 "
        "48 8B 02 45 33 ED 48 8D 35 11 90 84 00 48 85 C0 48 8B "
        "E9 44 89 AC 24 98 01 00 00 48 0F 45"
    )
    assert actual_prefix == expected_prefix


def test_model_tracks_every_required_provenance_dimension():
    for token in (
        "manager",
        "path_complete",
        "definition_count_before",
        "definition_count_after",
        "provider_provenance_complete",
        "b0_provider",
        "input_hash",
        "input_size",
        "parser_registration_generation",
        "parser_succeeded",
        "generation",
        "canonical_tree_fingerprint_pointer_free",
        "consumer_inventory_complete",
        "observation_complete",
    ):
        assert token in HEADER


def test_parser_pointer_not_wrapper_return_controls_success():
    assert re.search(
        r"parser_succeeded\s*=\s*\n?\s*"
        r"observation->parser_observed\s*&&\s*"
        r"parser\.result_object\s*!=\s*0",
        SOURCE,
    )
    disagreement = SOURCE.index("function_return_disagrees_with_parser")
    success = SOURCE.index("parser_succeeded =")
    assert success < disagreement
    success_assignment = SOURCE[success:disagreement]
    assert "function_returned_true" not in success_assignment
    assert "ParserFailedStateRetained" in SOURCE


def test_tree_fingerprint_and_consumer_inventory_are_independent_hard_gates():
    complete = re.search(
        r"observation->observation_complete\s*=([^;]+);", SOURCE, re.S
    )
    assert complete
    expression = complete.group(1)
    for required in (
        "record_storage_complete",
        "provenance_complete",
        "parser_succeeded",
        "canonical_tree_fingerprint_complete",
        "consumer_inventory_evidence_complete",
    ):
        assert required in expression
    assert (
        "canonical_tree_fingerprint_generation ==\n"
        "            observation->generation"
        in SOURCE
    )
    assert (
        "finish.consumer_inventory_generation == observation->generation"
        in SOURCE
    )


def test_observer_is_fixed_capacity_pod_and_has_no_action_callbacks():
    for token in (
        "kActiveLoadCapacity = 8",
        "kObservationCapacity = 64",
        "ActiveLoad active_loads[kActiveLoadCapacity]",
        "Observation observations[kObservationCapacity]",
        "std::is_trivially_copyable<Observation>",
        "std::is_trivially_copyable<Ledger>",
    ):
        assert token in HEADER or token in SOURCE
    for forbidden in (
        "std::vector",
        "std::string",
        "new ",
        "malloc(",
        "fopen(",
        "WriteFile(",
        "dluma_write_utf8_file(",
        "throw ",
        "ReloadCallback",
        "ResetCallback",
        "MutationCallback",
        "PerformFrontendContentReset(",
    ):
        assert forbidden not in HEADER
        assert forbidden not in SOURCE


def test_generation_checked_tokens_and_ambiguity_are_fail_closed():
    for token in (
        "active.observation.generation != token.generation",
        "observation.provider_ambiguous = true",
        "observation.parser_ambiguous = true",
        "loss.stale_token",
        "loss.duplicate_provider",
        "loss.duplicate_parser",
    ):
        assert token in SOURCE
    assert SOURCE.count("observation.provider = evidence;") == 1
    assert SOURCE.count("observation.parser = evidence;") == 1


def test_observer_translation_unit_is_an_isolated_project_entry():
    assert (
        'src\\fixes\\custom_map_asset_isolation\\'
        'AnimationEventDefinitionsObservation.cpp' in PROJECT
    )
    assert (
        'src\\fixes\\custom_map_asset_isolation\\'
        'AnimationEventDefinitionsObservation.h' in PROJECT
    )
