from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
PRODUCTION = CUSTOM / "CustomMapAssetIsolationFix.cpp"
AUDIO_OWNER = CUSTOM / "AudioProductionOwner.inl"


def _cpp_function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def _byte_array(source: str, name: str) -> list[int]:
    match = re.search(
        rf"static\s+const\s+std::uint8_t\s+{re.escape(name)}\[\]\s*=\s*"
        rf"\{{(?P<body>[\s\S]*?)\}};",
        source,
    )
    assert match is not None, f"missing byte evidence array: {name}"
    return [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group("body"))]


def _assert_relative_callsite(
    source: str, call_rva: str, target_constant: str
) -> None:
    pattern = (
        r"ValidateRelativeCallSite\s*\(\s*"
        r"g_context\.engine_base\s*\+\s*"
        + re.escape(call_rva)
        + r"\s*,\s*g_context\.engine_base\s*\+\s*"
        + re.escape(target_constant)
    )
    assert re.search(pattern, source), (
        f"missing exact call-site validation {call_rva} -> {target_constant}"
    )


def _assert_result_preserved(body: str, original: str, result_type: str) -> None:
    assert body.count(f"{original}(") == 1
    assignment = re.search(
        rf"(?:const\s+)?{re.escape(result_type)}\s+(\w+)\s*=\s*"
        rf"{re.escape(original)}\s*\(",
        body,
    )
    if assignment is not None:
        assert re.search(
            rf"\breturn\s+{re.escape(assignment.group(1))}\s*;", body
        )
        return
    assert re.search(rf"\breturn\s+{re.escape(original)}\s*\(", body)


def test_audio_touch_only_group_is_atomic_independent_and_fail_closed():
    source = PRODUCTION.read_text(encoding="utf-8")
    predicate = _cpp_function_body(source, "bool IsAudioTouchHookStatus")
    minimum = _cpp_function_body(source, "bool IsAudioMinimumHookStatus")
    optional = _cpp_function_body(
        source, "bool IsAudioOptionalTouchHookStatus"
    )
    diagnostic = _cpp_function_body(source, "bool IsAudioDiagnosticHookStatus")
    disable_touch = _cpp_function_body(source, "void DisableAudioTouchHookPlan")
    disable_minimum = _cpp_function_body(
        source, "void DisableAudioMinimumHookPlan"
    )
    disable_optional = _cpp_function_body(
        source, "void DisableAudioOptionalTouchHookPlan"
    )
    reset_owner = _cpp_function_body(
        source[source.rindex("bool ExecuteFrontendResetAtPostC10") :],
        "bool ExecuteFrontendResetAtPostC10",
    )

    exact_statuses = (
        "g_audio_script_load_status",
        "g_audio_fmod_load_banks_status",
        "g_sound_bank_manager_load_status",
        "g_speech_bank_load_status",
        "g_speech_bank_remove_localised_status",
        "g_speech_bank_remove_all_status",
        "g_speech_manager_create_status",
        "g_speech_manager_destroy_status",
    )
    for status in exact_statuses:
        assert predicate.count(status) == 1
    for forbidden in (
        "g_tools_load_audio_banks_status",
        "g_fs_open_native_audio_status",
    ):
        assert forbidden not in predicate
    assert "!IsAudioTouchHookStatus(status)" in diagnostic
    for status in exact_statuses[:3]:
        assert status in minimum
    assert "IsAudioTouchHookStatus(status)" in optional
    assert "!IsAudioMinimumHookStatus(status)" in optional
    assert "IsAudioMinimumHookStatus(route->owner)" in disable_minimum
    assert (
        "IsAudioOptionalTouchHookStatus(route->owner)"
        in disable_optional
    )
    assert "DisableAudioMinimumHookPlan();" in disable_touch
    assert "DisableAudioOptionalTouchHookPlan();" in disable_touch
    assert "g_audio_touch_hooks_available = false" in disable_optional

    plan_start = source.index("bool audio_minimum_plan_ok =")
    plan_end = source.index("g_original_change_active_level", plan_start)
    plan = source[plan_start:plan_end]
    for hook in (
        "&HookAudioScriptLoad",
        "&HookAudioFmodLoadBanks",
        "&HookSoundBankManagerLoad",
        "&HookSpeechBankLoad",
        "&HookSpeechBankRemoveLocalised",
        "&HookSpeechBankRemoveAll",
        "&HookSpeechManagerCreate",
        "&HookSpeechManagerDestroy",
    ):
        assert plan.count(hook) == 1
    assert "if (!audio_minimum_plan_ok)" in plan
    assert "DisableAudioMinimumHookPlan();" in plan
    assert "if (!audio_optional_touch_plan_ok)" in plan
    assert "DisableAudioOptionalTouchHookPlan();" in plan
    assert "g_audio_touch_hooks_available = true;" in plan

    assert "AssessAudioSemanticTouchAtResetBoundary" in reset_owner
    assert "AssessProductionAudioMinimumAtResetBoundary" in reset_owner
    assert "AudioMinimumDisposition::Touched" in reset_owner
    assert "ContentDomainId::Audio" in reset_owner
    assert "AudioMinimumDisposition::Incomplete" in reset_owner
    assert "CandidateObserverCoverage::Incomplete" in reset_owner
    assert "SealCandidateRetainingConsumersAtResetBoundary" in reset_owner
    assert reset_owner.index(
        "AssessProductionAudioMinimumAtResetBoundary"
    ) < reset_owner.index(
        "cold::PrepareColdRebootAtStateFive"
    )
    assert "kAudioRetainedReferenceModelCoverageComplete = false" in source


def test_audio_observations_remain_provisional_until_exact_retained_owner():
    source = PRODUCTION.read_text(encoding="utf-8")
    audio_data = _cpp_function_body(source, "bool __fastcall HookAudioScriptLoad")
    bank = _cpp_function_body(source, "int __fastcall HookSoundBankManagerLoad")

    assert "PromoteAudioTouchForCustomEpoch" not in source
    assert "MarkDomainTouched" not in audio_data
    assert "MarkDomainTouched" not in bank
    assert "RecordAudioStartupScript(" in audio_data
    assert "PersistentConsumerResult::Parsed" in audio_data
    assert "const bool exact_custom_provider" in bank
    assert "exact project provider retained by audio bank manager" in bank
    assert "ExactProviderUnavailable" in bank
    assert "CacheHitProviderMissing" in bank
    assert "AudioBankProviderOutcome::LoadFailed" in bank
    assert (
        "constexpr bool kAudioRetainedReferenceModelCoverageComplete = false"
        in source
    )
    assert "audio_retained_reference_model_coverage_complete=" in source


def test_exact_full_path_selected_source_probe_is_bounded_and_abi_preserving():
    source = PRODUCTION.read_text(encoding="utf-8")
    header = (CUSTOM / "AudioBankProviderCorrelation.h").read_text(
        encoding="utf-8"
    )
    stub_path = ROOT / "src" / "audio_full_path_provider_probe_masm.asm"
    stub = stub_path.read_text(encoding="utf-8")

    for contract in (
        "kFilesystemFullPathRva = 0x00014D80U",
        "kFilesystemFullPathSelectedSourceJoinRva",
        "0x00014FABU",
        "kFilesystemFullPathProviderProbeRva",
        "0x00014FC0U",
        "kFilesystemFullPathProviderProbeStolenSize = 18U",
    ):
        assert contract in header
    assert "full_path_provider_probe_prefix" in source
    for byte in (
        "0x4D,0x0F,0x45,0xC8",
        "0x48,0x83,0xC9,0xFF",
        "0x33,0xC0,0x48,0x8B,0xFB,0xF2,0xAE,0x49",
        "0x8B,0xF9",
    ):
        assert byte in source
    assert "full_path_selected_source_context" in source
    assert "full_path_function_prologue" in source
    assert "full_path_source_node_call" in source
    assert "kSupportedFilesystemSha256" in source
    assert "g_filesystem_full_profile_valid" in source
    assert "kFsFullPathExport" in source
    assert "g_fs_full_path_audio_provider_status" in source
    assert "&dluma_audio_full_path_provider_probe" in source
    assert "kFilesystemFullPathProviderProbeStolenSize" in source
    assert "route->address + route->stolen_size" in source
    assert "g_dluma_audio_full_path_provider_probe_resume" in source

    assert "mov  rdx, qword ptr [rdi-8]" in stub
    assert "mov  eax, dword ptr gs:[48h]" in stub
    assert "AudioFullPathProbeState" in header
    assert "sizeof(AudioFullPathProbeSlot) == 32U" in header
    assert "offsetof(AudioFullPathProbeState, slots) == 8U" in header
    assert "dluma_audio_full_path_provider_probe_unwind PROC FRAME" in stub
    assert ".pushreg rbp" in stub
    assert ".allocstack 1D0h" in stub
    for register, offset in (
        ("rbx", "1E0h"), ("rsi", "1E8h"),
        ("rdi", "1F0h"), ("r12", "1F8h"),
    ):
        assert f".savereg {register}, {offset}" in stub
    assert ".endprolog" in stub
    assert not re.search(r"(?m)^\s*call\s+", stub)
    assert "cmovne r9, r8" in stub
    assert "or   rcx, -1" in stub
    assert "xor  eax, eax" in stub
    assert "repne scasb" in stub
    assert "mov  rdi, r9" in stub
    assert "jmp  qword ptr [g_dluma_audio_full_path_provider_probe_resume]" in stub
    for forbidden in ("WriteFile", "dluma_log", "malloc", "new "):
        assert forbidden not in stub
    assert "dluma_audio_full_path_provider_observed" not in source
    assert "DrainFullPathProbeForBankLoad(context)" in _cpp_function_body(
        source, "void* __fastcall HookAudioFsOpenNative"
    )
    assert "RouteUntouchedTailMatches" in source

    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    batch = (ROOT / "build_windows_msvc.bat").read_text(encoding="utf-8")
    assert "audio_full_path_provider_probe_masm.asm" in project
    assert "msbuild.exe DLUMA.vcxproj" in batch
    assert "*.asm" not in batch


def test_full_path_probe_route_validates_and_restores_complete_stolen_range():
    source = PRODUCTION.read_text(encoding="utf-8")
    original_range = _cpp_function_body(
        source, "bool RouteOriginalRangeMatches"
    )
    untouched_tail = _cpp_function_body(
        source, "bool RouteUntouchedTailMatches"
    )
    apply_route = _cpp_function_body(source, "bool ApplyRoute")
    restore_route = _cpp_function_body(source, "bool RestoreRoute")

    # A function detour overwrites only the atomic 16-byte head, but its
    # complete instruction boundary may be longer. Installation must reject
    # a change anywhere in that complete stolen range before writing memory.
    assert re.search(
        r"route->kind\s*==\s*HookRouteKind::FunctionDetour\s*"
        r"\?\s*route->stolen_size\s*:\s*route->byte_count",
        original_range,
    )
    assert "BytesEqual(route->address, route->original_bytes, size)" in (
        original_range
    )
    full_range_check = apply_route.index("!RouteOriginalRangeMatches(route)")
    first_patch = min(
        apply_route.index("AtomicPatchPointer"),
        apply_route.index("AtomicPatchDetour"),
    )
    assert full_range_check < first_patch

    # Bytes beyond the atomic routing head remain executable in place. A
    # foreign edit to that tail must pin/refuse restoration rather than being
    # silently overwritten as part of restoring the head.
    assert "route->stolen_size <= route->byte_count" in untouched_tail
    assert "route->address + route->byte_count" in untouched_tail
    assert "route->original_bytes + route->byte_count" in untouched_tail
    assert "route->stolen_size - route->byte_count" in untouched_tail
    routed_head_check = restore_route.index(
        "BytesEqual(route->address, route->routing_bytes"
    )
    untouched_tail_check = restore_route.index(
        "!RouteUntouchedTailMatches(route)"
    )
    restore_patch = min(
        restore_route.index("AtomicPatchPointer"),
        restore_route.index("AtomicPatchDetour"),
    )
    assert routed_head_check < restore_patch
    assert untouched_tail_check < restore_patch

    # Successful restoration is not inferred from the patch API result. The
    # complete original stolen range must be observed afterward, and routing
    # ownership is cleared only after that verification succeeds.
    restored_check = restore_route.index(
        "const bool restored = RouteOriginalRangeMatches(route)"
    )
    clear_installed = restore_route.index(
        "if (restored) route->installed = false"
    )
    final_result = restore_route.index("return write_succeeded && restored")
    assert restore_patch < restored_check < clear_installed < final_result


def test_fmod_and_speech_observations_publish_exact_selected_source_only():
    source = PRODUCTION.read_text(encoding="utf-8")
    speech = _cpp_function_body(
        source, "void __fastcall HookSpeechBankLoad"
    )
    fmod = _cpp_function_body(
        source, "int __fastcall HookSoundBankManagerLoad"
    )
    for body, prefix in ((speech, "observation"), (fmod, "startup_observation")):
        assert "SetAudioResolvedSourceObservationRequired" in body
        assert "g_audio_full_path_provider_probe_available" in body
        assert f"{prefix}.provider.source =" in body
        assert "provider_record.selected_source" in body
        assert f"{prefix}.provider.source_registry_generation =" in body
        assert (
            f"{prefix}.provider.source_registry_generation_observed ="
            in body
        )
        assert f"{prefix}.provider.exact_source =" in body
        assert "provider_record.exact_source_identity_available" in body


def test_audio_raw_snapshot_uses_validated_lock_and_stays_fail_closed():
    source = PRODUCTION.read_text(encoding="utf-8")
    capture = _cpp_function_body(
        source, "StartupAudioCaptureResult CaptureStartupAudioRawInventory"
    )
    assert "kAudioTryEnterCriticalSectionRva = 0x000F4D50U" in source
    assert "kAudioLeaveCriticalSectionRva = 0x000F4D40U" in source
    assert "audio_try_enter_cs" in source
    assert "audio_leave_cs" in source
    assert "SnapshotAudioBaselineWindows" in capture
    assert re.search(
        r"AudioBaselineWindowsReadContext context\s*=\s*\{[\s\S]*?"
        r"true,\s*g_speech_registry_observation_available,\s*\};",
        capture,
    )
    snapshot_header = (
        CUSTOM / "AudioBaselineWindowsSnapshot.h"
    ).read_text(encoding="utf-8")
    assert "bool exact_engine_image_validated;" in snapshot_header
    assert "bool speech_registry_synchronization_proven;" in snapshot_header
    assert capture.count("SnapshotAudioBaselineWindows") == 1
    assert "CaptureSpeechBanks" not in capture
    assert "CaptureAudioStartupObservationSnapshot" in capture
    assert "ReconcileAudioStartupBaseline" in capture
    assert "authoritative_b0_complete" in capture
    assert "SetBaselineAudioSnapshot" not in capture
    assert "StartupAudioCaptureResult::TerminalIncomplete" in capture
    for prohibited in (
        "PlayerState::Clear",
        "UnloadAllBanks(",
        "UnloadLocalisedBanks(",
        "AudioScript::Clear",
        "AudioScript::Reload",
        "ApplyChanges(",
        "ReloadSoundBanks(",
        "fs::rem_source(",
    ):
        assert prohibited not in capture


def test_non_authoritative_audio_capture_is_latched_and_retries_are_bounded():
    source = PRODUCTION.read_text(encoding="utf-8")
    capture = _cpp_function_body(
        source, "StartupAudioCaptureResult CaptureStartupAudioRawInventory"
    )
    pump = _cpp_function_body(
        source, "extern \"C\" void dluma_custom_map_content_game_thread_tick"
    )

    reconcile = capture.index("ReconcileAudioStartupBaseline")
    final_fence = capture.index(
        "AudioBaselineWindowsObservationFence final_fence"
    )
    final_fence_read = capture.index("ReadAudioBaselineFence", final_fence)
    active_calls = capture.index("final_fence.active_calls", final_fence_read)
    revision = capture.index("final_fence.revision", active_calls)
    observation_loss = capture.index("final_fence.observation_loss", revision)
    reconciled_gate = capture.index("if (!reconciled)", observation_loss)
    authority_gate = capture.index(
        "authoritative_b0_complete", reconciled_gate
    )
    observational_return = capture.index(
        "StartupAudioCaptureResult::ObservationalCaptured", authority_gate
    )
    assert (
        reconcile < final_fence < final_fence_read < active_calls < revision <
        observation_loss < reconciled_gate < authority_gate <
        observational_return
    )
    assert "SetBaselineAudioSnapshot" not in capture
    assert "return StartupAudioCaptureResult::Retry" in capture[
        final_fence_read:observation_loss
    ]
    assert "return StartupAudioCaptureResult::TerminalIncomplete" in capture[
        observation_loss:reconciled_gate
    ]
    fence = _cpp_function_body(source, "bool ReadAudioBaselineFence")
    assert "contention_mutations" in fence
    assert "revision_before" in fence
    assert "revision_after" in fence
    assert "revision_before != revision_after" in fence
    assert "if (unstable && output->active_calls == 0)" in fence
    assert "output->observation_loss = loss_observed" in fence

    assert "kAudioStartupCaptureRetryMs = 100U" in source
    assert "g_audio_startup_capture_terminal" in pump
    assert "g_audio_startup_capture_result" in pump
    assert "g_audio_startup_capture_next_retry_ms" in pump
    assert "audio_capture_due" in pump
    assert "audio_capture_now >= audio_capture_next_retry" in pump
    result_latch = pump.index("g_audio_startup_capture_result")
    retry_branch = pump.index(
        "if (raw_audio_capture_due && retry)", result_latch
    )
    retry_deadline = pump.index(
        "audio_capture_now + kAudioStartupCaptureRetryMs", retry_branch
    )
    terminal_else = pump.index("else", retry_deadline)
    terminal_latch = pump.index(
        "dluma_atomic_exchange(&g_audio_startup_capture_terminal, 1)",
        terminal_else,
    )
    record_result = pump.index("RecordStartupB0DomainResult", result_latch)
    assert (
        result_latch < retry_branch < retry_deadline < terminal_else <
        terminal_latch < record_result
    )
    result_tail = pump[record_result:]
    assert (
        "minimum_audio_ready\n"
        "                ? cold::StartupB0DomainCaptureResult::Ready"
        in result_tail
    )
    assert (
        "observational\n"
        "                ? cold::StartupB0DomainCaptureResult::Ready"
        not in result_tail
    )
    assert "observational" in pump[record_result:]
    assert pump.count("CaptureStartupAudioRawInventory()") == 1

    initialize = _cpp_function_body(
        source, "FixStatus Initialize(const RuntimeContext& context"
    )
    assert "g_audio_startup_capture_terminal, 0" in initialize
    assert "StartupAudioCaptureResult::NotAttempted" in initialize
    assert "g_audio_startup_capture_next_retry_ms, 0" in initialize
    assert "g_audio_startup_capture_terminal, 0" not in capture


def test_localized_evidence_is_never_inferred_from_zero_initialization():
    source = PRODUCTION.read_text(encoding="utf-8")
    reconciler = (
        CUSTOM / "AudioStartupBaselineReconciler.cpp"
    ).read_text(encoding="utf-8")
    ledger_header = (
        CUSTOM / "AudioStartupObservationLedger.h"
    ).read_text(encoding="utf-8")
    native_open_hook = _cpp_function_body(
        source, "void* __fastcall HookAudioFsOpenNative"
    )
    speech_hook = _cpp_function_body(
        source, "void __fastcall HookSpeechBankLoad"
    )
    fmod_hook = _cpp_function_body(
        source, "int __fastcall HookSoundBankManagerLoad"
    )

    assert "bool localized_observed;" in ledger_header
    assert "localized_observed" not in native_open_hook
    assert "observation.localized = localised" in speech_hook
    assert "observation.localized_observed = true" in speech_hook
    assert "provider_record.native_open_count != 0U" in speech_hook
    assert "startup_observation.localized = localised" in fmod_hook
    assert "startup_observation.localized_observed = true" in fmod_hook
    assert reconciler.count("if (exact_observation.localized_observed)") == 1
    assert reconciler.count("if (cache.localized_observed)") == 1
    assert reconciler.count(
        "if (observations.banks[found].localized_observed)"
    ) == 1
    assert "AudioReconcileUnresolvedFmodLocalizedEvidence" in reconciler
    assert "AudioReconcileUnresolvedSpeechLocalizedEvidence" in reconciler


def test_audio_report_exposes_observational_and_authority_limits():
    source = PRODUCTION.read_text(encoding="utf-8")
    for field in (
        "audio_raw_snapshot_reason_mask",
        "audio_raw_snapshot_unresolved_fields",
        "audio_sound_lock_snapshot_complete",
        "audio_speech_owner_synchronization_proven",
        "audio_speech_mutation_surface_complete",
        "audio_speech_observation_gate_available",
        "audio_speech_gate_integrity_failed",
        "audio_speech_gate_admission_state",
        "audio_speech_gate_revision",
        "audio_speech_gate_completed_mutation_count",
        "audio_speech_snapshot_revision",
        "audio_speech_snapshot_completed_mutation_count",
        "audio_speech_snapshot_observed_entry_count",
        "audio_full_path_exact_source_probe_available",
        "audio_speech_localized_provider_correlation",
        "audio_natural_main_menu_procedure_observer",
        "audio_startup_capture_terminal",
        "audio_startup_capture_result",
        "audio_startup_coverage_before_audio_singletons",
        "audio_reconcile_reason_mask",
        "audio_reconcile_unresolved_fields",
        "audio_observational_inventory_reconciled",
        "audio_provider_path_coverage_complete",
        "audio_exact_source_coverage_complete",
        "audio_fmod_localized_evidence_complete",
        "audio_speech_localized_evidence_complete",
        "audio_authoritative_b0_complete",
        "audio_reset_owner_proven",
        "audio_reset_ready",
    ):
        assert field in source
    assert "speech_mutator_synchronization_unproven" in source
    assert '"installed_current_registry_only"' in source
    assert "audio_natural_main_menu_procedure_owner" not in source


def test_exact_speech_mutation_observers_call_only_their_original_once():
    source = PRODUCTION.read_text(encoding="utf-8")
    void_hooks = (
        ("void __fastcall HookSpeechBankLoad", "g_original_speech_bank_load"),
        (
            "void __fastcall HookSpeechBankRemoveLocalised",
            "g_original_speech_bank_remove_localised",
        ),
        (
            "void __fastcall HookSpeechBankRemoveAll",
            "g_original_speech_bank_remove_all",
        ),
    )
    for signature, original in void_hooks:
        body = _cpp_function_body(source, signature)
        assert body.count(f"{original}(") == 1
        assert source.count(f"{original}(") == 1
        assert "BeginSpeechRegistryMutation" in body
        assert "EndSpeechRegistryMutation" in body
        assert body.index("BeginSpeechRegistryMutation") < body.index(
            f"{original}("
        ) < body.index("EndSpeechRegistryMutation")
        assert "dluma_log" not in body
        assert "dluma_write" not in body

    returning_hooks = (
        (
            "bool __fastcall HookSpeechManagerCreate",
            "g_original_speech_manager_create",
        ),
        (
            "bool __fastcall HookSpeechManagerDestroy",
            "g_original_speech_manager_destroy",
        ),
    )
    for signature, original in returning_hooks:
        body = _cpp_function_body(source, signature)
        assert source.count(f"{original}(") == 1
        _assert_result_preserved(body, original, "bool")
        assert "BeginSpeechRegistryMutation" in body
        assert "EndSpeechRegistryMutation" in body
        assert body.index("BeginSpeechRegistryMutation") < body.index(
            f"{original}("
        ) < body.index("EndSpeechRegistryMutation")


def test_audio_script_execute_is_a_return_preserving_observer_only():
    source = PRODUCTION.read_text(encoding="utf-8")
    body = _cpp_function_body(
        source, "int __fastcall HookToolsAudioScriptExecute"
    )
    assert source.count("g_original_tools_audio_script_execute(") == 1
    _assert_result_preserved(
        body, "g_original_tools_audio_script_execute", "int"
    )
    assert "AudioScriptExecuteEnter" in body
    assert "AudioScriptExecuteExit" in body
    assert body.index("AudioScriptExecuteEnter") < body.index(
        "g_original_tools_audio_script_execute("
    ) < body.index("AudioScriptExecuteExit")
    assert body.index("g_original_tools_audio_script_execute(") < body.index(
        "RecordAudioStartupMutationFence"
    ) < body.index("AudioScriptExecuteExit")
    assert "RecordAudioLayerObservation" not in body
    assert "MarkRestartDomainTouched" not in body
    for forbidden in (
        "AudioScript::Clear",
        "AudioScript::Reload",
        "UnloadAllBanks(",
        "ReloadSoundBanks(",
        "SetBaselineAudioSnapshot(",
    ):
        assert forbidden not in body


def test_speech_observer_plan_has_exact_rvas_prefixes_and_callsite_evidence():
    source = PRODUCTION.read_text(encoding="utf-8")
    plan = _cpp_function_body(source, "bool BuildHookPlan() noexcept")
    constants = {
        "kSpeechBankLoadRva": "0x0032A7F0U",
        "kSpeechBankRemoveLocalisedRva": "0x0032ACF0U",
        "kSpeechBankRemoveAllRva": "0x0032AF60U",
        "kSpeechManagerCreateRva": "0x0032B220U",
        "kSpeechManagerDestroyRva": "0x0032B300U",
        "kToolsAudioScriptExecuteRva": "0x00294E10U",
        "kToolsAudioScriptExecuteSlot": "0x08B8U",
    }
    for name, value in constants.items():
        assert re.search(
            rf"constexpr\s+std::uintptr_t\s+{name}\s*=\s*{value}\s*;",
            source,
        )

    expected_prefixes = {
        "speech_load_prefix": (
            "48 85 D2 0F 84 C4 04 00 00 48 8B C4 48 89 48 08 "
            "55 53 57 41 54 48 8D A8 18 FE FF FF 48 81 EC C8"
        ),
        "speech_localised_clear_prefix": (
            "48 89 74 24 20 57 48 83 EC 20 48 8B 79 30 8B 51 "
            "38 48 8B F1 48 8D 04 D7 48 3B F8 0F 84 E2 00 00"
        ),
        "speech_full_clear_prefix": (
            "48 89 5C 24 10 48 89 6C 24 18 57 48 83 EC 20 33 "
            "ED 48 8B F9 8B DD 39 59 08 76 2D 48 89 74 24 30"
        ),
        "speech_create_prefix": (
            "48 83 EC 58 48 83 3D 0C 1B 7A 00 00 0F 84 93 00 "
            "00 00 B9 47 4F 4C 43 48 89 7C 24 50 FF 15 26 C2"
        ),
        "speech_destroy_prefix": (
            "40 53 48 83 EC 50 48 8B 1D 2B 1A 7A 00 48 85 DB "
            "0F 85 8A 00 00 00 B9 47 4F 4C 43 48 89 7C 24 60"
        ),
        "tools_execute_prefix": (
            "48 8B 02 48 8D 0D 57 25 5D 00 48 85 C0 48 0F 45 "
            "C8 E9 0A F3 E7 FF"
        ),
    }
    for name, expected in expected_prefixes.items():
        assert _byte_array(plan, name) == [
            int(value, 16) for value in expected.split()
        ]

    callsites = (
        ("0x0011196BU", "kSpeechBankLoadRva"),
        ("0x00111B05U", "kSpeechBankLoadRva"),
        ("0x00111ED0U", "kSpeechBankLoadRva"),
        ("0x0025618DU", "kSpeechBankRemoveLocalisedRva"),
        ("0x00294904U", "kSpeechBankRemoveLocalisedRva"),
        ("0x00111F66U", "kSpeechBankRemoveAllRva"),
        ("0x00112036U", "kSpeechBankRemoveAllRva"),
        ("0x0032B4D1U", "kSpeechBankRemoveAllRva"),
        ("0x0032B55EU", "kSpeechBankRemoveAllRva"),
        ("0x0024E913U", "kSpeechManagerCreateRva"),
        ("0x0024A9E8U", "kSpeechManagerDestroyRva"),
    )
    for call_rva, target in callsites:
        _assert_relative_callsite(plan, call_rva, target)

    surrounding_evidence = (
        "D7 48 83 C1 08 45 33 C0",
        "49 8B CF FF 15 BF 4E 74",
        "D6 49 8D 4F 08 45 33 C0",
        "49 8B CE FF 15 25 4D 74",
        "48 0F 45 D1 48 8D 4D 08",
        "48 8B 4C 24 20 FF 15 58",
        "85 C9 74 09 48 83 C1 08",
        "48 8B 0D DF 2B 87 00 48",
        "48 8B 0D 68 44 83 00 48",
        "85 DB 74 1E 48 8D 4B 08",
        "48 8D 4B 70 E8 3C B0 21",
        "48 8D 4B 70 E8 6C AF 21",
        "20 48 8B E9 48 83 C1 08",
        "48 8D 4D 70 E8 D1 1A 00",
        "DE B2 52 00 48 8D 4D 08",
        "48 8B 4D 38 44 89 65 40",
        "03 00 00 E8 8D 84 FF FF",
        "C6 05 C6 E7 87 00 01 FF",
        "5A ED FF E8 D8 5E EC FF",
        "48 8B 0D 54 FE 87 00 48",
    )
    all_arrays = [
        [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
        for body in re.findall(
            r"static\s+const\s+std::uint8_t\s+\w+\[\]\s*=\s*"
            r"\{([\s\S]*?)\};",
            plan,
        )
    ]
    for evidence in surrounding_evidence:
        expected = [int(value, 16) for value in evidence.split()]
        assert expected in all_arrays, f"missing surrounding bytes: {evidence}"


def test_speech_operation_detection_routes_are_minimum_atomic_and_fail_closed():
    source = PRODUCTION.read_text(encoding="utf-8")
    plan = _cpp_function_body(source, "bool BuildHookPlan() noexcept")
    is_audio = _cpp_function_body(source, "bool IsAudioHookStatus")
    is_touch = _cpp_function_body(source, "bool IsAudioTouchHookStatus")
    disable = _cpp_function_body(
        source, "void DisableAudioOptionalTouchHookPlan"
    )
    status_names = (
        "g_speech_bank_load_status",
        "g_speech_bank_remove_localised_status",
        "g_speech_bank_remove_all_status",
        "g_speech_manager_create_status",
        "g_speech_manager_destroy_status",
    )
    for status in status_names:
        assert status in is_audio
        assert status in is_touch
        declaration = re.search(
            rf"{re.escape(status)}\s*=\s*AddHookStatus\s*\("
            rf"[\s\S]*?,\s*false\s*\);",
            plan,
        )
        assert declaration is not None, f"{status} must remain optional"
        assert f"{status}->validated" in plan

    assert "if (!audio_optional_touch_plan_ok)" in plan
    assert "DisableAudioOptionalTouchHookPlan();" in plan
    assert "g_audio_touch_hooks_available = false" in disable
    assert "g_speech_registry_observation_available = false" in disable
    assert "DisableAudioMinimumHookPlan" not in disable
    assert "DisableMandatoryHookPlan" not in disable
    assert "PerformFrontendContentReset" not in disable

    detour_contracts = (
        ("g_speech_bank_load_status", "16, 5, 9", "HookSpeechBankLoad"),
        (
            "g_speech_bank_remove_localised_status",
            "17, 0, 0",
            "HookSpeechBankRemoveLocalised",
        ),
        (
            "g_speech_bank_remove_all_status",
            "17, 0, 0",
            "HookSpeechBankRemoveAll",
        ),
        (
            "g_speech_manager_create_status",
            "18, 7, 12",
            "HookSpeechManagerCreate",
        ),
        (
            "g_speech_manager_destroy_status",
            "16, 9, 13",
            "HookSpeechManagerDestroy",
        ),
    )
    compact = re.sub(r"\s+", "", plan)
    for status, relocation, hook in detour_contracts:
        arguments = relocation.replace(" ", "")
        assert (
            f"AddFunctionDetour({status},g_context.engine_base+"
            in compact
        )
        route_start = compact.index(f"AddFunctionDetour({status},")
        route = compact[route_start : route_start + 300]
        assert arguments in route
        assert f"reinterpret_cast<void*>(&{hook})" in route
    create_route = compact[
        compact.index("AddFunctionDetour(g_speech_manager_create_status,") :
    ]
    assert ",14,18)" in create_route[:350]


def test_composite_audio_snapshot_gate_is_observational_and_non_mutating():
    source = PRODUCTION.read_text(encoding="utf-8")
    enter = _cpp_function_body(source, "bool TryEnterAudioBaselineLock")
    leave = _cpp_function_body(source, "void LeaveAudioBaselineLock")
    capture = _cpp_function_body(
        source, "StartupAudioCaptureResult CaptureStartupAudioRawInventory"
    )
    initialize = _cpp_function_body(
        source, "FixStatus Initialize(const RuntimeContext& context"
    )

    assert enter.count("try_enter()") == 1
    assert "TryAcquireSpeechRegistrySnapshot" in enter
    assert enter.index("try_enter()") < enter.index(
        "TryAcquireSpeechRegistrySnapshot"
    )
    assert "leave()" in enter[
        enter.index("TryAcquireSpeechRegistrySnapshot") :
    ]
    assert (
        "AbandonSpeechRegistrySnapshot" in leave
        or "CompleteSpeechRegistrySnapshot" in leave
    )
    assert leave.rindex("leave()") > max(
        leave.find("AbandonSpeechRegistrySnapshot"),
        leave.find("CompleteSpeechRegistrySnapshot"),
    )
    assert "g_speech_registry_observation_available" in capture
    assert "InitializeSpeechRegistryObservationGate" in initialize

    for body in (enter, leave, capture):
        for forbidden in (
            "UnloadAllBanks(",
            "UnloadLocalisedBanks(",
            "AudioScript::Clear",
            "AudioScript::Reload",
            "ReloadSoundBanks(",
            "SetBaselineAudioSnapshot(",
            "PlayerState::Clear",
            "fs::rem_source(",
        ):
            assert forbidden not in body


def test_production_audio_and_speech_replay_requires_admitted_transaction_authority():
    source = PRODUCTION.read_text(encoding="utf-8")
    owner = AUDIO_OWNER.read_text(encoding="utf-8")
    for symbol in (
        "UnloadAllBanks(",
        "UnloadLocalisedBanks(",
        "ReloadSoundBanks(",
        "ReplayAudio",
        "SetBaselineAudioSnapshot(",
    ):
        assert symbol not in source + owner

    # Native originals remain confined to their observed wrappers. The
    # production replay path invokes validated entries under a registry
    # permit, so it cannot bypass the wrapper's actual identity admission.
    allowed_original_calls = {
        "g_original_speech_bank_load": "void __fastcall HookSpeechBankLoad",
        "g_original_speech_bank_remove_localised": (
            "void __fastcall HookSpeechBankRemoveLocalised"
        ),
        "g_original_speech_bank_remove_all": (
            "void __fastcall HookSpeechBankRemoveAll"
        ),
        "g_original_speech_manager_create": (
            "bool __fastcall HookSpeechManagerCreate"
        ),
        "g_original_speech_manager_destroy": (
            "bool __fastcall HookSpeechManagerDestroy"
        ),
        "g_original_audio_script_clear": "void __fastcall HookAudioScriptClear",
        "g_original_audio_script_reload": "std::uintptr_t __fastcall HookAudioScriptReload",
    }
    for original, signature in allowed_original_calls.items():
        assert (source + owner).count(f"{original}(") == 1
        assert f"{original}(" in _cpp_function_body(source, signature)

    clear = _cpp_function_body(source, "void __fastcall HookAudioScriptClear")
    compact_clear = re.sub(r"\s+", "", clear)
    assert "AudioReplayInvocationretaining(" in compact_clear
    assert "!retaining.admitted()" in compact_clear
    assert "!definition_scope.admitted()" in compact_clear
    assert "(!native_reload&&!retaining.actual())" in compact_clear
    assert "g_audio_reload_owner_depth" in clear
    assert "_ReturnAddress()" in clear
    denied = compact_clear.index("!retaining.admitted()")
    denial_return = compact_clear.index("return;", denied)
    assert denied < denial_return < compact_clear.index("g_original_audio_script_clear()")
    assert "runtime_failure_latched,1" in compact_clear[denied:denial_return]

    reload = _cpp_function_body(source, "std::uintptr_t __fastcall HookAudioScriptReload")
    assert "if (!retaining.admitted()) return 0" in reload
    assert "if (!definition_scope.admitted()) return 0" in reload
    assert reload.index("++g_audio_reload_owner_depth") < reload.index(
        "g_original_audio_script_reload()"
    ) < reload.index("--g_audio_reload_owner_depth")
    assert "return result;" in reload

    invocation = _cpp_function_body(source, "const cold::ReplayOperationIdentity* Observe(")
    assert "cold::CurrentRegistryReplayPermit(" in invocation
    assert "if (!replay_) return nullptr" in invocation
    assert invocation.index("ResolveProductionAudioIdentity(") < invocation.index("return &actual_")
    replay = _cpp_function_body(owner, "bool InvokeProductionAudioReplay(")
    native_call = replay.index("audio_baseline_replay::InvokeNativeEntry(")
    for gate in (
        "!ProductionAudioContext(context)",
        "!ProductionAudioOwnerMatches()",
        "!g_production_audio.released",
        "BuildProductionAudioReplayOperations(",
        "cold::BeginRegistryReplayPermit(",
    ):
        assert replay.index(gate) < native_call
    assert native_call < replay.index("cold::EndRegistryReplayPermit(")
    assert "return invoked && observed" in replay
    assert (source + owner).count("audio_baseline_replay::InvokeNativeEntry(") == 1
