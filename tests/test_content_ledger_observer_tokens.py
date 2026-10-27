"""Production observer sites must preserve the pre-operation incarnation."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp").read_text()


def function(name):
    for match in re.finditer(r"^[\w:*& \t]+\b" + re.escape(name) + r"\s*\(", SOURCE, re.M):
        brace = SOURCE.find("{", match.end())
        semicolon = SOURCE.find(";", match.end())
        if brace < 0 or 0 <= semicolon < brace:
            continue
        depth = 0
        for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[{}]', SOURCE[brace:]):
            if token[0] not in {"{", "}"}:
                continue
            depth += 1 if token[0] == "{" else -1
            if not depth:
                return SOURCE[match.start():brace + token.end()]
    raise AssertionError(f"Missing function: {name}")


def test_file_setters_keep_the_open_or_snapshot_token():
    opened = function("HookFsOpen")
    assert opened.index("RecordCustomFileOpen(") < opened.index("SnapshotFileMutationToken(")
    assert "mode, flags, &custom_file_mutation" in opened
    assert "custom_file_mutation_available ? &custom_file_mutation : nullptr" in opened
    assert "token.file_mutation = *file_mutation" in function("CorrelateWatchedOpenToken")
    completed = function("CompleteWatchedParserScope")
    assert "&scope->token.file_mutation" in completed
    assert "SnapshotFileMutationToken(" not in completed
    probe = function("ProbeBaselineProviders")
    assert "record.first_open_sequence" in probe and "&file_mutation" in probe
    assert probe.index("const content::CustomFileMutationToken") < probe.index("g_original_fs_open(")


def test_pack_tokens_are_captured_before_release_and_keep_retention_flags():
    body = function("HookLevelReleasePacks")
    assert body.index("SnapshotPackMutationToken(") < body.index("g_original_level_release_packs(")
    assert body.index("g_original_level_release_packs(") < body.index("RecordPackRelease(")
    assert "&captured_pack_tokens[index]" in body
    assert "owned_release && element_readable && (pack_flags & 0x200U) == 0" in body


def test_reports_and_baseline_ignore_verified_tombstones():
    report = function("WriteContentLedgerReport")
    for guard in ["if (!epoch.id) continue;", "if (!source.add_succeeded) continue;",
                  "if (!file.open_count) continue;", "if (!pack.epoch) continue;",
                  "if (!epoch) continue;"]:
        assert guard in report
    assert "SnapshotRetirementHistory(" in report
    assert "retired.verified_transaction_token" in report
    assert "pack.epoch && pack.epoch <= epoch_id" in function("VerifyFrontendBaseline")
