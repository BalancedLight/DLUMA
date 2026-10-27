from pathlib import Path
import subprocess
from test_content_ledger_observer_tokens import function

ROOT = Path(__file__).resolve().parents[1]


def test_native_file_lifetimes_and_completion_probes():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_native_file_lifetime_fake_tests.bat"],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    result = subprocess.run(
        [str(ROOT / "build/native-file-lifetime-fake/native_file_lifetime_fake_harness.exe")],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert result.stdout.count("PASS ") == 19


def test_file_hook_plan_is_complete_and_reuses_existing_open_routes():
    plan = function("BuildNativeFileHookPlan")
    assert "if (route.hook_target != hook || existing) return false" in plan
    assert "g_native_file_statuses[index] = existing->owner" in plan
    assert "HookRouteKind::InteriorEnvelope" in plan
    assert "filewin::EntryRelocationFor" in plan
    seal = function("SealNativeFileBridge")
    assert "g_native_file_statuses[i]->installed" in seal
    assert seal.index("ExistingSource(") < seal.index("filebridge::Seal(")
    assert "CompleteHookInstallation" not in plan


def test_file_bridge_is_prepared_before_planning_and_not_reset_afterward():
    source = (ROOT / "src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp").read_text()
    plan = source.index("if (!BuildHookPlan() || !PrepareAllRelays())")
    prepared = source.index("    PrepareNativeFileBridge();")
    assert prepared < plan
    assert source.count("    PrepareNativeFileBridge();") == 1
    assert plan < source.index("    PrepareNativeResourceBridge();", plan)


def test_factory_admission_precedes_original_and_speech_pin_sees_observed_file():
    for name, original in [("HookFsOpen", "g_original_fs_open"),
                           ("HookAudioFsOpenNative", "g_original_fs_open_native")]:
        body = function(name)
        assert body.index("!native_file_call.admitted()") < body.index(original + "(")
        assert body.index(original + "(") < body.index("native_file_call.returned(result)")
    native = function("HookAudioFsOpenNative")
    assert native.index("native_file_call.returned(result)") < native.index("CaptureAudioStartupNativePin(")
    publication = function("HookFsAddObject")
    assert publication.index("filebridge::SourcePublication(") < publication.index("g_original_fs_add_object(")


def test_provider_deletion_requires_actual_lifetimes_and_global_exclusion():
    retire = function("ColdVfsRetireProviders")
    assert retire.index("files::CloseAdmission(") < retire.index("files::SnapshotSource(")
    assert retire.index("files::SnapshotSource(") < retire.index("retirement::DestroyDetachedSource(")
    assert retire.index("retirement::DestroyDetachedSource(") < retire.index("files::ReopenAdmission(")
    assert "proof.all_participating_consumers_released = true" not in retire
    assert "files::ValidateProof(" in retire
    destroy = function("DestroyRetirementSource")
    assert destroy.index("files::ValidateProof(") < destroy.index("g_source_reset_destructors[index](")
    assert destroy.index("g_source_reset_destructors[index](") < destroy.index("files::CompleteSourceRetirement(")
    assert "GlobalSourceCollectionExcludesRetirementTargets(false)" in destroy
    exclusion = function("GlobalSourceCollectionExcludesRetirementTargets")
    assert "g_source_retirement_process_sources.complete" in exclusion
    assert "generation != g_source_retirement_process_generation" in exclusion
    assert "owner.source_object == g_source_reset_snapshot[n].source" in exclusion


def test_native_relays_keep_registered_unwind_and_atomic_envelopes():
    interior = function("PrepareNativeFileInteriorRelay")
    assert "RtlAddFunctionTable" in interior
    assert "prepared.original_envelope" in interior
    assert "prepared.patched_envelope" in interior
    entry = function("PrepareNativeFileEntryUnwind")
    assert "CopyEntryUnwind" in entry and "RtlAddFunctionTable" in entry
    free = function("FreeRouteRelay")
    assert free.index("RtlDeleteFunctionTable") < free.index("VirtualFree(")
