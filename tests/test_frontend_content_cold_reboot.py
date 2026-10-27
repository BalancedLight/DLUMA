from pathlib import Path
import hashlib
import re
import subprocess
import struct

from tools.local_test_paths import DEVTOOLS_ROOT


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"


def _cpp_function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def _cpp_last_function_body(source: str, signature: str) -> str:
    start = source.rindex(signature)
    return _cpp_function_body(source[start:], signature)


def test_native_frontend_content_cold_reboot_harness():
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "tools\\build_frontend_content_cold_reboot_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "frontend-cold-reboot-fake"
                / "frontend_content_cold_reboot_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_one_production_toggle_owns_every_exit_reset_component():
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    ini = (
        ROOT
        / "config"
        / "dluma.ini"
    ).read_text(encoding="utf-8")
    table = ui[
        ui.index("static const DlumaLoaderSettingDefinition g_dluma_loader_settings") :
        ui.index("g_dluma_loader_category_names")
    ]
    assert table.count('L"GameFixes", L"CustomMapAssetIsolation"') == 1
    assert re.search(
        r'L"GameFixes",\s*L"CustomMapAssetIsolation",\s*0', host
    )
    assert ini.count("CustomMapAssetIsolation=0") == 1
    for retired in (
        "ResetSources",
        "ResetGlobalData",
        "ResetAudioBanks",
        "FrontendContentColdReboot",
        "VerifyFrontendBaseline",
        "ExperimentalPlayerPresetRebuild",
    ):
        assert retired not in table
        assert retired not in host
        assert retired not in common
        assert retired not in ini


def test_cold_reboot_core_has_no_independent_component_permissions():
    source = (CUSTOM / "FrontendContentColdReboot.cpp").read_text(
        encoding="utf-8"
    )
    header = (CUSTOM / "FrontendContentColdReboot.h").read_text(
        encoding="utf-8"
    )
    joined = source + header
    assert "master_enabled" in joined
    for forbidden in (
        "reset_sources_enabled",
        "reset_global_data_enabled",
        "reset_audio_enabled",
        "player_reset_enabled",
    ):
        assert forbidden not in joined


def test_touched_optional_domains_are_promoted_into_atomic_reset_preflight():
    header = (CUSTOM / "ContentDomainRestartRegistry.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    assert "MarkDomainTouched" in header
    assert "touched_mask" in header
    assert "touch_revision" in header
    assert "touch_transaction_state" in header
    assert "prepared_required_mask" in header
    assert re.search(
        r"mandatory_mask\s*\|\s*LoadTouchedMask", source
    )
    assert re.search(
        r"baseline_ready_mask\s*&\s*required_mask\)\s*!=\s*"
        r"required_mask",
        source,
    )
    assert "RequiredMask(registry) != required_mask" in source
    assert (
        "RequiredMask(registry) != registry->prepared_required_mask"
        in source
    )
    assert "CommitTouchTransactionWindow" in source
    assert "CheckTouchTransactionWindow" in source
    assert "prepared_touch_revision" in source


def test_registry_executes_only_mandatory_plus_touched_domains():
    source = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    run_phase = _cpp_function_body(source, "RestartResult RunPhase")
    quiesce = _cpp_function_body(source, "RestartResult RunQuiescePhase")
    prepare = _cpp_function_body(
        source, "RestartResult PrepareRegistryTransaction"
    )
    capture = _cpp_function_body(
        source, "RestartResult CaptureRegistryBaseline"
    )
    begin_retaining = _cpp_function_body(
        source, "bool BeginRetainingObservation"
    )
    finalize = _cpp_function_body(
        source, "bool FinalizeCommittedTouchTransactionWindow"
    )

    for phase in (run_phase, quiesce):
        assert re.search(
            r"effective_dependencies\s*=\s*\n?\s*"
            r"adapter\.dependency_mask\s*&\s*participation_mask",
            phase,
        )
        assert re.search(
            r"effective_dependencies\s*&\s*phase_succeeded",
            phase,
        )
    assert re.search(
        r"RestartPhase::Preflighting,\s*context,\s*"
        r"required_mask,\s*required_mask",
        prepare,
    )
    assert "registry->baseline_ready_mask, required_mask" not in prepare
    assert "registry->last_result = RestartResult::Verified" in capture
    assert "MarkDomainTouched" not in begin_retaining
    assert "dluma_atomic_exchange(&registry->touched_mask, 0)" in finalize


def test_touch_publication_brackets_the_bit_with_transaction_state_checks():
    source = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    mark = _cpp_function_body(source, "bool MarkDomainTouched")
    first_state = mark.index("touch_transaction_state")
    first_publish = mark.index("publish_touch();")
    revision = mark.index("IncrementTouchRevision")
    second_state = mark.index("touch_transaction_state", first_publish)
    assert first_state < first_publish < revision < second_state
    assert mark.count("kTouchTransactionCommitting") >= 2
    assert "kTouchTransactionCommitConflicted" in mark
    assert mark.count("publish_touch();") >= 2


def test_production_registration_makes_content_domains_touch_promoted():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    initialize = _cpp_function_body(
        production, "bool InitializeColdRebootScaffold"
    )
    player_registration = initialize[
        initialize.index("MakePlayerGameplayParamsContentDomainAdapter") :
        initialize.index("cold::ContentDomainAdapter optional_globals")
    ]
    audio_registration = initialize[
        initialize.index("cold::ContentDomainAdapter audio") :
        initialize.index("cold::ContentDomainAdapter verify")
    ]
    verify_registration = initialize[
        initialize.index("cold::ContentDomainAdapter verify") :
        initialize.index("const cold::ColdRebootPlatform platform")
    ]
    assert re.search(
        r"MakePlayerGameplayParamsContentDomainAdapter\([^;]*false\s*\)",
        player_registration,
        re.DOTALL,
    )
    assert re.search(
        r"ContentDomainId::Audio,[^;]*false,",
        audio_registration,
        re.DOTALL,
    )
    verify_definition = verify_registration[
        : verify_registration.index("if (!cold::RegisterAdapter")
    ]
    assert "ContentDomainId::VfsSources" in verify_definition
    assert "ContentDomainId::PlayerGameplayParams" not in verify_definition
    assert "ContentDomainId::Audio" not in verify_definition
    register_calls = list(
        re.finditer(
            r"RegisterAdapter\(&g_cold_reboot_registry,\s*&([a-z_]+)\)",
            verify_registration,
        )
    )
    assert register_calls
    assert register_calls[-1].group(1) == "verify"


def test_native_harness_covers_touched_set_transaction_policy():
    harness = (
        ROOT / "tests" / "frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    for required_case in (
        "TestPlayerUntouchedAudioUntouchedVfsAndVerifyReachVerified",
        "TestPlayerTouchedTransactionOrdersVfsPlayerVerify",
        "TestTouchedUnsupportedDomainsBlockBeforeVfsMutation",
        "TestPlayerTouchedWithoutPlayerBaselineBlocksBeforeVfsMutation",
        "TestAudioTouchedWithReadyBaselineButUnsupportedOwnerBlocksAllMutation",
        "TestPlayerTouchedButPreflightUnsupportedHasNoSourceOnlyFallback",
        "TestParticipationCombinationsPreserveRequiredOrder",
        "TestUntouchedStaticDependenciesDoNotBlockTerminalVerify",
        "TestSuccessfulEpochClearsTouchedMask",
    ):
        assert required_case in harness
    assert "const std::uint32_t expected_reset_order[] = {13U, 23U, 63U}" in harness
    assert "vfs.calls[3] == 0" in harness
    assert "registry.touched_mask == 0" in harness


def test_retain_capable_producers_reserve_before_original_and_commit_conflicts():
    header = (CUSTOM / "ContentDomainRestartRegistry.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    for contract in (
        "RetainingObservationToken",
        "RetainingObservationOrigin",
        "BeginRetainingObservation",
        "EndRetainingObservation",
        "retaining_observation_active_calls",
        "retaining_observation_revision",
    ):
        assert contract in header
    assert "kTouchTransactionCommitConflicted" in source
    assert "FinalizeCommittedTouchTransactionWindow" in source
    assert "ResetOwnedReplay" in source

    retaining_hooks = (
        "HookPlayerStateSetProgressTemplate",
        "HookCachePlayerLevelPreset",
        "HookRebuildPlayerLevelPreset",
        "HookFindPlayerLevelPreset",
        "HookLoadVariablesFromFile",
        "HookPlayerStateInitializeVariables",
        "HookPlayerStateLoad",
        "HookPlayerStateParseAppearances",
        "HookReloadPlayerScripts",
        "HookLocalClientRecreateRepresentation",
        "HookPlayerStateSetVariablesToLevel",
        "HookAudioScriptLoad",
        "HookSoundBankManagerLoad",
        "HookAudioFmodLoadBanks",
        "HookToolsLoadAudioBanks",
    )
    for hook in retaining_hooks:
        body = _cpp_function_body(production, hook)
        scope = "AudioReplayInvocation" if hook in (
            "HookAudioScriptLoad", "HookSoundBankManagerLoad", "HookAudioFmodLoadBanks"
        ) else "RetainingObservationCall"
        assert scope + " retaining(" in body
        assert body.index(scope) < body.index(
            "g_original_"
        )


def test_generic_custom_open_requires_an_exact_retaining_scope_before_candidate_admission():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    ensure_epoch = _cpp_function_body(
        production, "std::uint32_t EnsureEpochForSuccessfulSource"
    )
    open_hook = _cpp_function_body(production, "void* __fastcall HookFsOpen")
    promote = _cpp_function_body(
        production, "bool SealCandidateRetainingConsumersAtResetBoundary"
    )
    reset_owner = _cpp_function_body(
        production[
            production.rindex("bool ExecuteFrontendResetAtPostC10") :
        ],
        "bool ExecuteFrontendResetAtPostC10",
    )
    broad_record = _cpp_function_body(
        production, "bool RecordBroadCandidateOpen"
    )
    promotion = promote
    assert "MarkDomainTouched" not in ensure_epoch
    assert "LatchStartupB0IrreversibleClose" in ensure_epoch
    assert "const bool exact_retaining_scope" in open_hook
    candidate_gate = re.search(
        r"const\s+bool\s+broad_candidate_recorded\s*=\s*"
        r"result\s*&&\s*exact_attribution\s*&&\s*"
        r"attribution\.source_active\s*&&\s*targeted_logical_available\s*&&\s*"
        r"exact_retaining_scope\s*&&\s*RecordBroadCandidateOpen\s*\(",
        open_hook,
        re.DOTALL,
    )
    assert candidate_gate
    assert open_hook.count("RecordBroadCandidateOpen") == 1
    for exact_scope in (
        "g_player_appearance_scope.depth",
        "g_progression_xml_load_scope.active",
        "g_audio_bank_load_scope.depth",
        "material_scope->depth && exact_asset_watch",
    ):
        assert exact_scope in open_hook
    assert "CandidateRetainingObserver::BroadFilesystemOpen" in broad_record
    assert "EvaluateCandidateRetainingConsumersAtBf8" in promote
    assert "MarkDomainTouched" in promote
    assert reset_owner.index(
        "SealCandidateRetainingConsumersAtResetBoundary"
    ) < reset_owner.index(
        "PrepareColdRebootAtStateFive"
    )
    assert "CorrelateCandidateRetainingOwner" in production
    assert "ClassifyCandidateRetainingConsumer" in production
    assert "RestartDomainForPersistentObservation" not in production
    assert "MarkRestartDomainTouchedForPersistentObservation" not in production
    assert "ResolveLiveProductionPlayerProvider" in production
    assert "LookupExactCustomSourceIdentity" in production
    assert "content::CaptureSnapshot" in promotion
    assert "candidate_epoch.id > maximum_epoch" in promotion
    assert "source.loader_owned_custom_content" in promotion
    assert "candidate_epoch.id" in promotion
    assert "EvaluateCandidateRetainingConsumersAtBf8" in promotion
    assert "unresolved_mask" in promotion
    assert "registry_touched_mask_mismatch" in promotion


def test_registry_baseline_requires_resolved_player_and_audio_b0():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    start = production.index("const bool transaction_b0_ready")
    block = production[start : production.index(
        "if (transaction_b0_ready", start
    )]
    assert "domains[core_vfs].ready" in block
    assert "domains[core_player].ready" in block
    assert "domains[core_loader].ready" in block
    assert "domains[core_audio].ready" in block
    assert "baseline_kind !=" in block
    assert "PlayerStartupBaselineKind::Unavailable" in block
    assert "g_audio_minimum_semantic_state.baseline_captured" in block


def test_post_mutation_player_failure_is_reported_on_the_player_component():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    reset_owner = _cpp_function_body(
        production[
            production.rindex("bool ExecuteFrontendResetAtPostC10") :
        ],
        "bool ExecuteFrontendResetAtPostC10",
    )
    failed = reset_owner[reset_owner.index("cold::LatchRestartRequired(") :]
    assert "g_player_restart_adapter.runtime_failure_latched" in failed
    assert "g_player_restart_adapter.mutation_started" in failed
    assert "ComponentRuntimeState::RuntimeFailed" in failed


def test_frontend_verification_rechecks_sealed_player_and_audio_decisions():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    verify = _cpp_function_body(
        production, "cold::DomainResult ColdFrontendVerify"
    )
    player = verify.index("AssessPlayerGameplayParamsTouch(")
    audio = verify.index(
        "AssessProductionAudioMinimumAtResetBoundary("
    )
    source_removed = verify.index("epoch.sources_removed")
    allow_frontend = verify.index("loader_healthy", source_removed)
    assert player < source_removed < allow_frontend
    assert audio < source_removed < allow_frontend
    assert "sealed_player_untouched" in verify
    assert "sealed_audio_untouched" in verify
    assert "sealed_domain_decisions_verified && loader_healthy" in verify


def test_all_mandatory_preflights_precede_mutation_and_failure_latches():
    source = (CUSTOM / "ContentDomainRestartRegistry.cpp").read_text(
        encoding="utf-8"
    )
    prepare_owner = source.index("RestartResult PrepareRegistryTransaction")
    execute_owner = source.index("RestartResult ExecutePreparedRegistryTransaction")
    preflight = source.index("RestartPhase::Preflighting", prepare_owner)
    quiesce = source.index("RestartPhase::Quiescing", preflight)
    mutation_flag = source.index("registry->mutation_started = true", execute_owner)
    reset = source.index("RestartPhase::Mutating", mutation_flag)
    assert preflight < quiesce < mutation_flag < reset
    assert "dluma_atomic_exchange(&registry->runtime_failure_latched, 1)" in source
    assert "dluma_atomic_exchange(&registry->admission_blocked, 1)" in source


def test_post_source_controller_requires_prepared_token_and_no_fallback_run():
    source = (CUSTOM / "FrontendContentColdReboot.cpp").read_text(
        encoding="utf-8"
    )
    enter = _cpp_function_body(source, "ColdRebootOutcome EnterColdRebootBarrier")
    validate = _cpp_function_body(source, "BarrierRejectReason ValidateBarrier")
    assert "preflight_prepared" in validate
    assert "prepared_transaction_generation" in validate
    assert "prepared_epoch != evidence.content_epoch" in validate
    assert "ExecutePreparedRegistryTransaction" in enter
    assert "RunRegistryTransaction" not in enter


def test_vfs_preflight_is_side_effect_free_and_registry_reset_owns_source_removal():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    preflight = _cpp_function_body(
        source, "cold::DomainResult ColdVfsPreflight("
    )
    plan_signature = "bool PrepareFrontendContentResetPlan("
    plan = _cpp_function_body(
        source[source.rindex(plan_signature) :], plan_signature
    )
    for forbidden in (
        "PerformFrontendContentReset(",
        "ConsumeFrontendEpochBinding(",
        "MarkEpochConsumed(",
        "RemoveAndDestroyPathCreatedSource(",
        "g_original_fs_remove_object(",
        "rem_source",
        "deleting_destructor",
    ):
        assert forbidden not in preflight
        assert forbidden not in plan

    reset = _cpp_function_body(
        source, "cold::DomainResult ColdVfsReset("
    )
    assert reset.count("PerformFrontendContentReset(") == 1
    assert len(re.findall(r"(?<!bool )PerformFrontendContentReset\(", source)) == 1


def test_session_on_destroy_gathers_proof_and_only_publishes_post_c10_token():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    on_destroy = _cpp_function_body(
        source, "std::uintptr_t __fastcall HookSessionOnDestroy"
    )
    scope_match = on_destroy.index(
        "MatchColdRebootStateFiveScopeSession(session)"
    )
    scope_claim = on_destroy.index("on_destroy_claimed = true")
    bind = on_destroy.index("BindFrontendEpochAtOnDestroy(")
    outer_original = on_destroy.rindex("g_original_session_on_destroy(session)")
    original_returned = on_destroy.index(
        "lifecycle::MarkOriginalReturned(&g_frontend_lifecycle)",
        outer_original,
    )
    defer = on_destroy.index("DeferFrontendSourceResetAtOnDestroy(")
    publish = on_destroy.index(
        "PublishPendingPostC10PreflightToken(", defer
    )
    assert (
        scope_match < scope_claim < bind < outer_original <
        original_returned < defer < publish
    )
    assert on_destroy.index("const bool proof_complete") < defer
    for forbidden in (
        "ColdRebootSourceResetAuthorized(",
        "ConsumeFrontendEpochBinding(",
        "content::MarkEpochConsumed(",
        "PerformFrontendContentReset(",
        "TryBeginFrontendResetPhase(",
        "lifecycle::ConsumeForReset(",
        "lifecycle::BeginReset(",
        "ExecuteFrontendResetAtPostC10(",
        "AssessPlayerSnapshotTouchAtResetBoundary(",
        "AssessProductionAudioMinimumAtResetBoundary(",
        "PrepareColdRebootAtStateFive(",
        "EnterColdRebootBarrier(",
    ):
        assert forbidden not in on_destroy


def test_state_two_completion_token_is_published_after_original_callback():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    callback = _cpp_function_body(
        source, "void __fastcall HookPleaseWaitFinished"
    )
    original = callback.index("g_original_please_wait_finished(game_di)")
    publish = callback.index("PublishPleaseWaitCompletion(")
    assert original < publish
    assert callback.count("g_original_please_wait_finished(game_di)") == 1
    assert "content::CurrentEpochId(&g_content_ledger)" in callback
    assert "CurrentSerializedGameFrameRouteMatches(" in callback
    assert "RecordC08DestroyOwnerDiagnostics(" in callback


def test_state_two_completion_token_is_bound_to_the_same_content_epoch():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    core_header = (CUSTOM / "StateFiveDestroyOwner.h").read_text(
        encoding="utf-8"
    )
    core = (CUSTOM / "StateFiveDestroyOwner.cpp").read_text(
        encoding="utf-8"
    )
    publish = _cpp_function_body(source, "bool PublishPleaseWaitCompletion")
    store = _cpp_function_body(source, "bool StorePleaseWaitOwnerToken")
    consume = _cpp_function_body(
        source, "statefive::RejectReason ConsumePleaseWaitCompletion"
    )
    c10 = _cpp_function_body(source, "void __fastcall HookDestroyingSessionCallback")

    for field in (
        "game_di",
        "content_epoch",
        "destroy_generation",
        "frame_route_token",
        "frame_authority_sequence",
        "published_qpc",
        "c08_callback_thread_id",
        "exact_state_two",
        "frame_authority_conflict",
        "completed",
        "consumed",
    ):
        assert field in core_header
    assert "token->content_epoch != attempt.content_epoch" in core
    assert "token->game_di != attempt.game_di" in core
    assert "token->destroy_generation <= last_consumed_generation" in core
    assert "token->frame_route_token != attempt.frame_route_token" in core
    assert "token->c08_callback_thread_id !=" not in core
    assert "!game_di || !content_epoch" in publish
    assert "g_cold_reboot_please_wait_token.token = token" in store
    assert "token.consumed = true" in consume
    epoch = c10.index("content::CurrentEpochId(&g_content_ledger)")
    token = c10.index("ConsumePleaseWaitCompletion(")
    assert epoch < token
    assert "evidence.content_epoch = epoch" in c10


def test_state_five_callback_only_arms_exact_scope_before_original():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    callback = _cpp_function_body(
        source, "void __fastcall HookDestroyingSessionCallback"
    )
    arm = callback.index("cold::ArmColdRebootForEpoch(")
    scope = callback.index("BeginColdRebootStateFiveScope(")
    original = callback.index(
        "g_original_destroying_session_callback(game_di)"
    )
    assert scope < arm < original
    assert "(void)cold::ArmColdRebootForEpoch(" not in callback
    assert "armed = cold::ArmColdRebootForEpoch(" in callback
    assert "g_cold_reboot.armed_epoch == epoch" in callback
    assert "g_cold_reboot_state_five_scope.armed = true" in callback
    rejected_cleanup = callback.index(
        "FinishColdRebootStateFiveScope(", arm
    )
    assert arm < rejected_cleanup < original
    assert (
        callback.index("statefive::RejectReason::C10ArmRejected", arm)
        < original
    )
    assert callback.index("scope_began = scope_started_here", scope) < arm
    diagnostics = callback.index("RecordC10DestroyOwnerDiagnostics(")
    assert "scope_began, arm_attempted, armed" in callback[diagnostics:]
    assert callback.index("scope_started_here = false", arm) < original
    assert callback.count(
        "g_original_destroying_session_callback(game_di)"
    ) == 1
    increment = callback.index("++g_destroying_session_callback_depth")
    decrement = callback.index("--g_destroying_session_callback_depth")
    assert increment < scope < original < decrement
    assert "g_cold_reboot_state_five_scope.owner_callback_depth" in callback
    for forbidden in (
        "cold::ColdRebootPreflightEvidence",
        "cold::ColdRebootBarrierEvidence",
        "PrepareColdRebootAtStateFive(",
        "EnterColdRebootBarrier(",
        "PerformFrontendContentReset(",
        "ConsumeFrontendEpochBinding(",
    ):
        assert forbidden not in callback


def test_exact_post_c10_owner_prepares_and_executes_without_bf8():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    reset_owner = _cpp_function_body(
        source[source.rindex("bool ExecuteFrontendResetAtPostC10") :],
        "bool ExecuteFrontendResetAtPostC10",
    )
    advance = _cpp_function_body(
        source, "void __fastcall HookDestroyStateAdvance"
    )
    ready = reset_owner.index("!lifecycle::ReadyForReset(")
    prepare = reset_owner.index("cold::PrepareColdRebootAtStateFive(")
    execute = reset_owner.index("cold::EnterColdRebootBarrier(")
    original = advance.index("g_original_destroy_state_advance(game_di)")
    assert ready < prepare < execute
    assert "FinishColdRebootStateFiveScope(" not in advance
    assert advance.count("g_original_destroy_state_advance(game_di)") == 1
    assert "PerformFrontendContentReset(" not in advance
    assert "PrepareColdRebootAtStateFive(" not in advance
    assert "EnterColdRebootBarrier(" not in advance
    assert (
        "preflight.caller_return_rva =\n"
        "        coldwin::kStateFiveCallbackReturnRva"
        in reset_owner
    )
    assert "ReadGameDiState(" not in reset_owner
    assert "environment.current_session" in reset_owner
    assert "scope.post_c10_token_consumed" in reset_owner
    assert "evidence.source_reset_ready" in reset_owner


def test_post_on_destroy_reset_uses_readiness_not_a_false_pre_removed_source_claim():
    core_header = (CUSTOM / "FrontendContentColdReboot.h").read_text(
        encoding="utf-8"
    )
    host = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    reset_owner = _cpp_function_body(
        host[host.rindex("bool ExecuteFrontendResetAtPostC10") :],
        "bool ExecuteFrontendResetAtPostC10",
    )
    assert "source_reset_ready" in core_header
    assert "source_reset_complete" not in core_header
    assert (
        "content::CountActiveCustomSources(&g_content_ledger) == 0"
        not in reset_owner
    )
    execute = reset_owner.index("cold::EnterColdRebootBarrier(")
    complete = reset_owner.index("FrontendRollbackPhase::ResetComplete")
    assert execute < complete


def test_state_five_scope_is_one_shot_at_post_c10_and_reasons_identity_failures():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    begin = _cpp_function_body(
        source, "bool BeginColdRebootStateFiveScope("
    )
    for required in (
        "!game_di",
        "!outgoing_session",
        "!epoch",
        "!please_wait_generation",
        "!frame_route_token",
        "!c08_callback_thread_id",
        "!c10_callback_thread_id",
        "!g_destroying_session_callback_depth",
        "g_cold_reboot_state_five_scope.active",
        "c10_callback_thread_id != GetCurrentThreadId()",
        "g_dluma.stop_requested",
        "ColdRebootNextProjectStagingStarted()",
    ):
        assert required in begin

    assert "bool on_destroy_claimed" in source
    match = _cpp_function_body(
        source,
        "statefive::RejectReason MatchColdRebootStateFiveScopeSession(",
    )
    for required in (
        "scope.active",
        "scope.on_destroy_claimed",
        "scope.c08_callback_thread_id",
        "scope.c10_callback_thread_id",
        "scope.owner_callback_depth",
        "g_destroying_session_callback_depth",
        "statefive::BindSessionOnDestroyToStateFiveScope(",
        "statefive::RejectReason::C08NotObserved",
        "statefive::RejectReason::SessionScopeConsumed",
        "statefive::RejectReason::C10Shutdown",
    ):
        assert required in match

    owner_core = (CUSTOM / "StateFiveDestroyOwner.cpp").read_text(
        encoding="utf-8"
    )
    for reason in (
        "c08_not_observed",
        "c08_state_not_two",
        "c08_token_game_di_mismatch",
        "c08_token_epoch_mismatch",
        "c08_token_generation_missing",
        "c08_token_stale",
        "c08_token_consumed",
        "c10_hook_profile_invalid",
        "c10_caller_rva_invalid",
        "c10_state_not_five",
        "c10_frame_route_unavailable",
        "c10_frame_route_changed",
        "c10_frame_authority_conflict",
        "c10_session_unreadable",
        "c10_session_missing",
        "c10_next_project_staging",
        "c10_shutdown",
        "c10_scope_already_active",
        "c10_scope_begin_rejected",
        "c10_arm_rejected",
        "c10_scope_matched",
    ):
        assert reason in owner_core

    reset_owner = _cpp_function_body(
        source[source.rindex("bool ExecuteFrontendResetAtPostC10") :],
        "bool ExecuteFrontendResetAtPostC10(",
    )
    for required in (
        "scope.active",
        "scope.deferred_source_reset",
        "scope.barrier_attempted",
        "scope.epoch != epoch",
        "scope.outgoing_session != binding.session",
        "scope.c10_callback_thread_id != current_thread",
        "scope.please_wait_generation",
        "scope.frame_route_token",
        "scope.owner_callback_depth !=",
        "g_destroying_session_callback_depth",
        "scope.armed",
        "scope.on_destroy_claimed",
        "scope.post_c10_token_consumed",
        "token.token_publication_sequence !=",
        "token.source_set_identity !=",
        "token.source_registry_generation !=",
        "environment.current_session",
        "FrontendEpochBindingsEqual(",
        "lifecycle::ReadyForReset(&g_frontend_lifecycle)",
        "ColdRebootNextProjectStagingStarted()",
        "g_dluma.stop_requested",
        "scope.barrier_attempted = true",
    ):
        assert required in reset_owner

    for failure in (
        "post_c10_token_missing",
        "post_c10_token_mismatch",
        "post_c10_token_replayed",
        "post_c10_wrong_thread",
        "post_c10_route_mismatch",
        "post_c10_authority_conflict",
        "post_c10_session_slot_unreadable",
        "post_c10_outgoing_session_still_published",
        "post_c10_unexpected_replacement_session",
        "post_c10_active_level_still_present",
        "post_c10_local_player_still_present",
        "post_c10_live_gameplay_consumer",
        "post_c10_source_registry_changed",
    ):
        assert failure in owner_core

    for gate in (begin, match):
        for forbidden in (
            "PrepareColdRebootAtStateFive(",
            "EnterColdRebootBarrier(",
            "PerformFrontendContentReset(",
            "ConsumeFrontendEpochBinding(",
        ):
            assert forbidden not in gate
    attempted = reset_owner.index("scope.barrier_attempted = true")
    prepare = reset_owner.index("cold::PrepareColdRebootAtStateFive(")
    execute = reset_owner.index("cold::EnterColdRebootBarrier(")
    assert attempted < prepare < execute


def test_destroy_owner_diagnostics_persist_exact_c08_c10_and_session_results():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    for field in (
        "cold_reboot_c08_callback_count",
        "cold_reboot_c08_exact_state_two_count",
        "cold_reboot_c08_last_game_di",
        "cold_reboot_c08_last_state",
        "cold_reboot_c08_last_epoch",
        "cold_reboot_c08_last_destroy_generation",
        "cold_reboot_c08_last_thread",
        "cold_reboot_c08_last_frame_route_token",
        "cold_reboot_c08_last_frame_authority_sequence",
        "cold_reboot_c08_last_frame_authority_conflict",
        "cold_reboot_c08_token_publish_count",
        "cold_reboot_c10_callback_count",
        "cold_reboot_c10_exact_owner_count",
        "cold_reboot_c10_last_game_di",
        "cold_reboot_c10_last_caller_rva",
        "cold_reboot_c10_last_state",
        "cold_reboot_c10_last_epoch",
        "cold_reboot_c10_last_thread",
        "cold_reboot_c10_last_current_frame_thread",
        "cold_reboot_c10_last_frame_route_token",
        "cold_reboot_c10_last_frame_authority_sequence",
        "cold_reboot_c10_last_frame_authority_conflict",
        "cold_reboot_c10_last_outgoing_session",
        "cold_reboot_c10_token_consume_count",
        "cold_reboot_c10_token_consume_failure_count",
        "cold_reboot_c10_scope_begin_count",
        "cold_reboot_c10_arm_success_count",
        "cold_reboot_c10_arm_failure_count",
        "cold_reboot_c10_last_disposition",
        "cold_reboot_c10_last_reject_reason",
        "cold_reboot_c10_worker_migrated",
        "session_on_destroy_last_c10_disposition",
        "session_on_destroy_last_c10_reject_reason",
    ):
        assert field in source
    assert source.count("ReportAppendDestroyOwnerDiagnostics(&builder)") == 2
    assert "ColdRebootDestroyOwnerDiagnosticsStore" in source
    assert "attempt < 8" in _cpp_function_body(
        source, "bool SnapshotDestroyOwnerDiagnostics"
    )

    c10 = _cpp_function_body(
        source, "void __fastcall HookDestroyingSessionCallback"
    )
    original = c10.index(
        "g_original_destroying_session_callback(game_di)"
    )
    persisted = c10.index("RecordC10DestroyOwnerDiagnostics(")
    assert persisted < original
    assert "token.c08_callback_thread_id !=" not in c10
    assert "please_wait_thread" not in c10

    on_destroy = _cpp_function_body(
        source, "std::uintptr_t __fastcall HookSessionOnDestroy"
    )
    assert "RecordSessionOnDestroyC10Diagnostics(c10_match)" in on_destroy
    assert "statefive::RejectReasonName(c10_match)" in on_destroy
    assert "prepared_transaction_generation != 0" not in on_destroy
    assert "not_applicable_no_armed_cold_reboot" not in on_destroy


def test_exact_barrier_observers_delegate_original_once():
    source = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    functions = (
        ("void __fastcall HookPleaseWaitFinished", "g_original_please_wait_finished("),
        (
            "void __fastcall HookDestroyingSessionCallback",
            "g_original_destroying_session_callback(",
        ),
        ("void __fastcall HookDestroyStateAdvance", "g_original_destroy_state_advance("),
        ("void __fastcall HookShowMenuMain", "g_original_show_menu_main("),
    )
    for signature, original in functions:
        body = _cpp_function_body(source, signature)
        assert body.count(original) == 1
    show_menu = _cpp_function_body(
        source, "void __fastcall HookShowMenuMain"
    )
    assert show_menu.rfind("g_original_show_menu_main(") > show_menu.find(
        "BlockRestartRequired"
    )


def test_no_full_gamedi_restart_or_forbidden_player_clear_in_new_core():
    sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (
            CUSTOM / "ContentDomainRestartRegistry.cpp",
            CUSTOM / "FrontendBaseline.cpp",
            CUSTOM / "FrontendContentColdReboot.cpp",
        )
    )
    for forbidden in (
        "GameDI::OnDestroy(",
        "GameDI::OnInitialize(",
        "PlayerState::Clear(",
        "InputsBinder::Reload(",
        "fs::refresh(",
        "fs::shutdown(",
    ):
        assert forbidden not in sources


def test_exact_barrier_constants_are_documented_in_source_contract():
    source = (CUSTOM / "FrontendContentColdReboot.h").read_text(
        encoding="utf-8"
    )
    assert "exact_state_five_owner" in source
    assert "please_wait_active" in source
    assert "pipeline_wait_returned" in source
    assert "session_destroyed" in source
    assert "source_reset_ready" in source
    assert "next_project_staging_started" in source
    assert "game_thread_id" in source
    assert "shutdown_requested" in source


def test_baseline_requires_known_fov_properties_and_no_returned_frontend_capture():
    header = (CUSTOM / "FrontendBaseline.h").read_text(encoding="utf-8")
    source = (CUSTOM / "FrontendBaseline.cpp").read_text(encoding="utf-8")
    assert "kBaselineRequiredPlayerPropertyCount = 15" in header
    for property_id in (
        103,
        209,
        496,
        702,
        706,
        707,
        713,
        714,
        715,
        823,
    ):
        assert f"{property_id}U" in source
    for property_name in (
        "kMoveSprintSpeedProperty",
        "kCameraDefaultFovProperty",
        "kCameraCarFovProperty",
        "kFovSprintModifierProperty",
        "kFovAmortizerModifierProperty",
    ):
        assert property_name in source
    assert "admission->returned_frontend" in source
    assert "admission->custom_epoch_count" in source
    assert "admission->active_custom_source_count" in source
    assert "admission->local_project_lua_count" in source


def test_player_baseline_schema_carries_authoritative_owner_cache_and_provider_identity():
    header = (CUSTOM / "FrontendBaseline.h").read_text(encoding="utf-8")
    source = (CUSTOM / "FrontendBaseline.cpp").read_text(encoding="utf-8")
    for field in (
        "std::uintptr_t state;",
        "std::uint64_t state_generation;",
        "BaselinePlayerOwnerKind owner_kind;",
        "std::uintptr_t owner_object;",
        "std::uintptr_t player_variables;",
        "progression_selector_fingerprint",
        "class_selector_fingerprint",
        "std::uintptr_t definition_vector;",
        "std::uint64_t earned_experience;",
        "std::uint64_t cache_generation;",
        "bool member_of_live_cache;",
        "BaselinePlayerProviderIdentity provider;",
        "skill_membership_fingerprint",
        "regular_buff_fingerprint",
        "timed_buff_fingerprint",
        "inventory_fingerprint",
        "equipment_fingerprint",
        "quest_fingerprint",
        "appearance_fingerprint",
        "BaselinePlayerProviderIdentity appearance_provider;",
    ):
        assert field in header
    for provider_field in (
        "std::uintptr_t source;",
            "normalized_path_fingerprint",
            "source_registry_generation",
            "source_registry_generation_observed",
            "content_epoch",
            "source_record_index",
            "source_inventory_ordinal",
        "bool observed;",
        "bool exact_source;",
        "bool base_provider;",
    ):
        assert provider_field in header
    assert "CompletePlayerState(*state)" in source
    assert "CompletePlayerProvider(property->provider)" in source
    assert "actual_row.cache_generation" in source
    assert "right.gameplay_cache_generation" in source
    assert "EqualPlayerProvider(expected_row.provider" in source


def test_player_baseline_projection_is_atomic_and_complete():
    header = (CUSTOM / "FrontendBaseline.h").read_text(encoding="utf-8")
    source = (CUSTOM / "FrontendBaseline.cpp").read_text(encoding="utf-8")
    setter = _cpp_function_body(source, "bool SetBaselinePlayerSnapshot")
    assert "bool SetBaselinePlayerSnapshot(" in header
    for validation in (
        "CompleteRequiredPropertyConfiguration",
        "CompletePlayerState",
        "CompletePlayerProvider",
        "state.rows[row].cache_generation != cache_generation",
        "property.cache_generation != cache_generation",
        "property_count !=",
        "state_count * kBaselineRequiredPlayerPropertyCount",
        "baseline->player_inventory_complete",
    ):
        assert validation in setter
    first_write = setter.index("std::memmove(baseline->player_states")
    assert first_write > setter.index("if (!found) return false")
    assert setter.index("baseline->gameplay_cache_generation =") > first_write
    assert setter.index("baseline->player_state_count =") > first_write
    assert setter.index("baseline->player_property_count =") > first_write
    assert setter.index("baseline->player_inventory_complete = true") > first_write
    harness = (
        ROOT / "tests" / "frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    assert "baseline.player_states[0].state == 0xDEADU" in harness
    assert "baseline.player_properties[0].property_id == 0xBEEFU" in harness


def test_audio_baseline_projection_validates_every_record_before_publication():
    header = (CUSTOM / "FrontendBaseline.h").read_text(encoding="utf-8")
    source = (CUSTOM / "FrontendBaseline.cpp").read_text(encoding="utf-8")
    setter = _cpp_function_body(source, "bool SetBaselineAudioSnapshot")
    assert "bool SetBaselineAudioSnapshot(" in header
    first_write = setter.index("std::memmove(baseline->audio_scripts")
    for validation in (
        "CompleteAudioSettings(*settings)",
        "CompleteAudioScript(scripts[index])",
        "CompleteAudioBank(banks[index])",
        "CompleteSpeechBank(speech_banks[index])",
        "scripts[prior].load_order",
        "banks[prior].manager_record",
        "speech_banks[prior].bank_record",
    ):
        assert validation in setter
        assert setter.index(validation) < first_write
    for publication in (
        "baseline->audio_settings = *settings",
        "baseline->audio_script_count = script_count",
        "baseline->audio_bank_count = bank_count",
        "baseline->speech_bank_count = speech_bank_count",
        "baseline->audio_provider_coverage_complete = true",
        "baseline->audio_inventory_complete = true",
    ):
        assert setter.index(publication) > first_write
    assert "baseline->audio_replay_owner_complete = false" in setter

    harness = (
        ROOT / "tests" / "frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    assert "TestAudioSnapshotRejectsInvalidInputsAtomically" in harness
    assert "TestAudioSnapshotDuplicateLogicalPathRequiresDistinctOrder" in harness
    assert "accepted.audio_scripts[0].load_order == 1U" in harness
    assert "accepted.audio_scripts[1].load_order == 2U" in harness
    assert "scripts[1].load_order = scripts[0].load_order" in harness
    assert "AudioSnapshotStorageEmpty(baseline)" in harness
    assert "!baseline.audio_replay_owner_complete" in harness


def test_production_player_b0_registers_snapshot_and_exact_mutation_callbacks():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    initialize = _cpp_function_body(
        production, "bool InitializeColdRebootScaffold"
    )
    tick = _cpp_function_body(
        production, 'extern "C" void dluma_custom_map_content_game_thread_tick'
    )
    publish = _cpp_last_function_body(
        production, "bool PublishProductionPlayerB0ToFrontendBaseline"
    )

    assert "&ReadProductionPlayerRestartEnvironment" in initialize
    assert "&SnapshotProductionPlayerState" in initialize
    assert re.search(
        r"PlayerGameplayParamsSnapshotOperations\s+"
        r"production_player_snapshot_operations\s*=\s*\{[^}]*"
        r"ReadProductionPlayerRestartEnvironment[^}]*"
        r"SnapshotProductionPlayerState[^}]*true[^}]*true[^}]*\}",
        initialize,
        re.DOTALL,
    )
    assert re.search(
        r"PlayerGameplayParamsMutationOperations\s+"
        r"production_player_mutation_operations\s*=\s*\{\s*\}\s*;",
        initialize,
        re.DOTALL,
    )
    assert re.search(
        r"production_player_mutation_operations\.reset_global_cache\s*=\s*"
        r"&ProductionResetGameplayParamsCache\s*;",
        initialize,
    )
    assert re.search(
        r"production_player_mutation_operations\.initialize_variables\s*=\s*"
        r"&ProductionInitializePlayerStateVariables\s*;",
        initialize,
    )
    assert re.search(
        r"production_player_mutation_operations\.global_cache_reset_route_validated"
        r"\s*=\s*g_player_cache_reset_mutation_target_validated\s*;",
        initialize,
    )
    assert re.search(
        r"production_player_mutation_operations\.initialize_variables_route_validated"
        r"\s*=\s*g_player_initialize_variables_mutation_target_validated\s*;",
        initialize,
    )
    assert re.search(
        r"production_player_mutation_operations\.production_enabled\s*=\s*"
        r"false\s*;",
        initialize,
    )
    assert "first production slice is intentionally VFS-only" in initialize
    adapter_init = initialize.index(
        "playerrestart::InitializePlayerGameplayParamsRestartAdapter("
    )
    assert initialize.index("production_player_snapshot_operations") < adapter_init
    assert initialize.index("production_player_mutation_operations") < adapter_init
    assert "&production_player_mutation_operations" in initialize[adapter_init:]

    prepare = tick.index("PrepareProductionPlayerB0SnapshotContext()")
    capture = tick.index(
        "playerrestart::CapturePlayerGameplayParamsBaseline("
    )
    projection = tick.index("PublishProductionPlayerB0ToFrontendBaseline()")
    abandon = tick.index("AbandonPlayerGameplayParamsBaseline(")
    record_result = tick.index("cold::RecordStartupB0DomainResult(", capture)
    assert prepare < capture < projection < abandon < record_result
    assert "(void)PublishProductionPlayerB0ToFrontendBaseline();" in tick
    assert "!PublishProductionPlayerB0ToFrontendBaseline()" not in tick
    assert "best-effort" in tick[capture:record_result]
    assert "ProductionPlayerPublishedEvidenceStable(" in tick[projection:record_result]
    assert "player_result = cold::DomainResult::Failed" in tick[projection:record_result]

    assert "cold::SetBaselinePlayerSnapshot(" in publish
    assert "cold::AddBaselinePlayerState(" not in publish
    assert "cold::AddBaselinePlayerProperty(" not in publish
    assert publish.count("ProductionPlayerPublishedEvidenceStable(context)") >= 3
    assert "cold::AbandonBaselinePlayerSnapshot(" in tick[projection:record_result]
    assert "PlayerGameplayParamsBaselineEvidenceStillCurrent(" in production


def test_runtime14_player_post_destroy_diagnostics_reach_both_reports():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    report = _cpp_function_body(
        production, "void ReportAppendPostDestroyPlayerDiagnostics"
    )
    assert production.count(
        "ReportAppendPostDestroyPlayerDiagnostics(&builder);"
    ) == 2
    assert '"post_destroy_player_pass1"' in report
    assert '"post_destroy_player_pass2"' in report
    for suffix in (
        "_environment_callback_returned=",
        "_exact_failure=",
        "_owner_count=",
        "_inventory_revision=",
        "_cache_fingerprint=",
        "_local_process_global=",
        "_character_manager_global=",
        "_active_session=",
        "_active_level=",
        "_local_player_di=",
        "_live_gameplay_consumer=",
    ):
        assert suffix in report
    for field in (
        "post_destroy_player_capture_attempt_count=",
        "post_destroy_player_environment_pair_stable=",
        "post_destroy_player_inventory_pair_stable=",
        "post_destroy_player_semantic_pair_stable=",
        "post_destroy_player_owner_set_matches_baseline_pass1=",
        "post_destroy_player_owner_set_matches_baseline_pass2=",
        "post_destroy_player_first_failed_owner_index=",
        "post_destroy_player_first_failed_owner_kind=",
        "post_destroy_player_first_failed_state=",
        "post_destroy_player_first_failed_state_generation=",
        "post_destroy_player_first_failed_state_stage=",
        "post_destroy_player_first_failed_row=",
        "post_destroy_player_first_failed_property_id=",
        "post_destroy_player_first_failed_semantic_reason_mask=",
        "post_destroy_player_assessment=",
        "post_destroy_player_failure=",
        "post_destroy_player_exact_failure=",
    ):
        assert field in report
    assert "PlayerPostDestroyRowCaptureDiagnostics" in report
    assert "raw_failure_reason" in report
    assert "definition_traversal_failure" in report
    assert "semantic_definition_fingerprint" in report


def test_production_player_b0_uses_no_hot_get_fvalue_route_and_bounds_direct_reads():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    plan = _cpp_function_body(production, "bool BuildHookPlan")
    disabled_hot_group = _cpp_function_body(plan, "if (false && g_fix_enabled)")
    snapshot = _cpp_last_function_body(
        production, "bool SnapshotProductionPlayerState"
    )
    forwarding_hook = _cpp_function_body(
        production, "float __fastcall HookPlayerVariablesGetFValue"
    )

    assert "PlayerVariables::GetFValue filtered FOV observer" in disabled_hot_group
    assert "reinterpret_cast<void*>(&HookPlayerVariablesGetFValue)" in disabled_hot_group
    assert "if (false && g_fix_enabled)" in plan
    assert "kEnableDefaultLevelsFovObservers = false" in production
    assert plan.count("reinterpret_cast<void*>(&HookPlayerVariablesGetFValue)") == 1

    direct_call = "g_original_player_variables_get_fvalue("
    assert snapshot.count(direct_call) == 2
    assert forwarding_hook.count(direct_call) == 1
    assert production.count(direct_call) == 3
    assert "static constexpr std::uint32_t required[]" in snapshot
    assert "DLUMA_ARRAY_COUNT(required)" in snapshot
    assert "CaptureRequiredPropertyProviderTypes(" in snapshot
    assert "property_provider_types[index]" in snapshot
    assert snapshot.index("CaptureRequiredPropertyProviderTypes(") < snapshot.index(
        direct_call
    )
    assert snapshot.index(direct_call) < snapshot.rindex(
        "ProductionPlayerEvidenceStable(*context)"
    )
    assert "output->properties[index].value_bits != value_bits" in snapshot


def test_production_player_b0_validates_exact_quest_and_gameplay_cache_shapes():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    quest = _cpp_function_body(production, "bool ReadQuestRuntimeShape")
    quest_empty = _cpp_function_body(
        production, "bool CaptureQuestRuntimeEmpty"
    )
    cache = _cpp_function_body(production, "bool ReadGameplayCacheShape")
    prepare = _cpp_last_function_body(
        production, "bool PrepareProductionPlayerB0SnapshotContext"
    )
    snapshot = _cpp_last_function_body(
        production, "bool SnapshotProductionPlayerState"
    )

    for exact_constant in (
        "kQuestManagerGlobalRva = 0x01E0F990U",
        "kQuestManagerInitGuardRva = 0x01E0FB98U",
        "kQuestManagerVtableRva = 0x01819B68U",
        "kQuestTreeVtableRva = 0x0181C1D8U",
        "kQuestProfileTreeCount = 26U",
        "kGameplayCacheLogicalTypeCount = 8U",
        "kGameplayCacheDescriptorStride = 0x18U",
        "kGameplayCacheRecordStride = 0x40U",
    ):
        assert exact_constant in production

    assert quest.count("CheckedPlayerAddressAdd(") >= 3
    assert "g_context.gamedll_base, kQuestManagerGlobalRva" in quest
    assert "g_context.gamedll_base, kQuestManagerVtableRva" in quest
    assert "g_context.gamedll_base, kQuestTreeVtableRva" in quest
    assert "g_context.gamedll_base, kQuestManagerInitGuardRva" in quest
    assert "output->manager_vtable != expected_manager_vtable" in quest
    assert "tree.vtable != expected_tree_vtable" in quest
    assert "CheckedPlayerIndexedAddress(" in quest
    assert "output->profile_trees, index, 8U" in quest
    assert "ReadPlayerBytesAt(tree.tree, 0x20U" in quest
    assert "ReadPlayerBytesAt(tree.tree, 0x108U" in quest
    assert "std::memcmp(&before, &after, sizeof(before))" in quest_empty
    assert "quest_runtime_absent_v1" in quest_empty
    assert "quest_runtime_uninitialized_v1" in quest_empty
    assert "*fingerprint != 0" in quest_empty

    outer_read = cache.index("kGameplayParamsContainerGlobalRva")
    descriptor_read = cache.index(
        "ReadPlayerBytesAt(output->outer, 0, &output->descriptors"
    )
    assert outer_read < descriptor_read
    assert "output->count != kGameplayCacheLogicalTypeCount" in cache
    assert "output->capacity != kGameplayCacheLogicalTypeCount" in cache
    assert "kGameplayCacheDescriptorStride" in cache
    assert "kGameplayCacheRecordStride" in cache

    for absence_tag in (
        "playerdi_inventory_owner_absent_v1",
        "playerdi_equipment_owner_absent_v1",
    ):
        assert absence_tag not in production
    assert "context.inventory_absence_fingerprint = 0" in prepare
    assert "context.equipment_absence_fingerprint = 0" in prepare
    assert "CaptureQuestRuntimeEmpty(" in prepare
    assert "context->inventory_absence_fingerprint" in snapshot
    assert "context->equipment_absence_fingerprint" in snapshot
    assert "context->quest_absence_fingerprint" in snapshot
    assert "output->player_semantic_snapshot_complete" in snapshot
    assert "output->player_restart_snapshot_complete = false" in snapshot


def test_production_player_b0_correlates_exact_startup_provider_and_rechecks_stability():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    resolve = _cpp_function_body(
        production, "bool ResolveProductionPlayerProvider"
    )
    stable = _cpp_function_body(
        production, "bool ProductionPlayerEvidenceStable"
    )
    prepare = _cpp_last_function_body(
        production, "bool PrepareProductionPlayerB0SnapshotContext"
    )
    environment = _cpp_last_function_body(
        production, "bool ReadProductionPlayerRestartEnvironment"
    )
    state = _cpp_last_function_body(
        production, "bool SnapshotProductionPlayerState"
    )

    for exact_observation_gate in (
        "observed.request_observed",
        "observed.open_succeeded",
        "observed.exact_source",
        "observed.source",
        "observed.content_epoch != 0",
        "g_startup_filesystem_inventory.complete",
    ):
        assert exact_observation_gate in resolve
    assert "source.source != observed.source" in resolve
    assert "NormalizePhysicalPath(" in resolve
    assert "AsciiEqualsInsensitive(normalized, provider)" in resolve
    assert "matched_index != UINT32_MAX" in resolve
    for accepted_kind in (
        "BaselineSourceKind::Base",
        "BaselineSourceKind::Dlc",
        "BaselineSourceKind::LocaleSpeech",
    ):
        assert accepted_kind in resolve
    assert "output->source_record_index = matched_index" in resolve
    assert (
        "output->source_inventory_ordinal = matched_inventory_ordinal"
        in resolve
    )
    assert "output->source_registry_generation" in resolve
    assert "output->source_registry_generation_observed = true" in resolve
    assert "observed.source_registry_generation_observed" in resolve
    assert "g_startup_filesystem_inventory.registry_generation" in resolve
    assert "output->content_epoch = 0" in resolve
    assert "output->exact_source = true" in resolve
    assert "output->base_provider = true" in resolve

    assert "g_source_registry_active_calls" in stable
    assert "ProductionPlayerMutationScopesIdle()" in stable

    assert "context.observation_revision = context.observations.revision" in prepare
    assert "context.source_registry_generation = ReadSourceRegistryGeneration()" in prepare
    assert prepare.count("ReadGameplayCacheShape(") >= 2
    assert "GameplayCacheShapeEqual(cache_before, cache_after)" in prepare
    assert "ProductionPlayerEvidenceStable(context)" in prepare

    assert environment.count("ReadGameplayCacheShape(") >= 2
    assert "GameplayCacheShapeEqual(cache_before, cache_after)" in environment
    assert "CaptureQuestRuntimeEmpty(" not in environment
    assert "playerstate::CurrentRevision(&g_player_state_ledger)" in environment
    assert "g_production_player_live_snapshot.revision" in environment
    assert environment.count("ProductionPlayerEvidenceStable(*context)") >= 2
    assert state.count("SnapshotPlayerGameplayParamsWindowsState(") >= 2
    assert state.count("CapturePlayerStateRows(") >= 2
    assert "playerstate::CurrentRevision(&g_player_state_ledger)" in state
    assert "g_production_player_live_snapshot.revision" in state
    assert "SameProductionStateEvidence(" in state
    assert "ProductionPlayerEvidenceStable(*context)" in state
    assert "output->player_semantic_snapshot_complete" in state
    assert "output->player_restart_snapshot_complete = false" in state


def test_production_partial_b0_is_truthful_noncomparable_and_cannot_arm():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    tick = _cpp_function_body(
        production, "extern \"C\" void dluma_custom_map_content_game_thread_tick"
    )
    vfs_baseline = _cpp_function_body(
        production, "cold::DomainResult ColdVfsBaseline"
    )
    report = _cpp_function_body(
        production, "bool WriteFrontendStartupBaselineReport"
    )
    harness = (
        ROOT / "tests" / "frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")

    assert "TryGetCurrentEpochId" in tick
    assert "counted_sources != UINT32_MAX" in tick
    assert "source_query_succeeded ? counted_sources : 0" in tick
    assert "sample.transient_blocker = cold::StartupB0Blocker::LedgerBusy" in tick
    assert "g_dluma.lua_discovery.local_count" in tick
    assert "dluma_content_pack_snapshot" in tick
    assert "dluma_ui_main_menu_redirect_is_applied" in tick
    assert "loader.loader_pak_state_known = true;" in tick
    assert "loader.loader_xui_state_known = true;" in tick
    assert "CaptureStartupFilesystemInventory" in tick
    assert "PopulateBaselineSourceInventory" in tick
    assert "CommitStartupFilesystemInventory" in tick
    assert "source_inventory_complete" in tick
    assert "after_capture.startup_b0_full_b0_captured" in tick
    assert "AuthorizeStartupB0RegistryBaseline" in tick
    assert re.search(
        r"MarkBaselineCaptured\(\s*&g_content_ledger,\s*authorized,",
        tick,
    )
    for unresolved_domain in (
        "StartupB0Domain::Audio",
        "StartupB0Domain::GlobalData",
        "StartupB0Domain::FrontendResources",
    ):
        assert unresolved_domain in tick
    assert "loader_pak_present = true" not in tick
    assert "g_startup_vfs_inventory_ready" in vfs_baseline
    assert "source_inventory_complete" in vfs_baseline
    assert "loader_pak_identity" in vfs_baseline
    assert "CountActiveCustomSources" not in vfs_baseline

    for key in (
        "capture_scope=",
        "full_b0_captured=",
        "full_b0_comparable=",
        "cold_reboot_baseline_authorized=",
        "observed_active_custom_sources=",
        "observed_global_lua_fingerprint=",
        "observed_approved_global_lua_count=",
        "observed_local_project_lua_count=",
        "loader_pak_state_known=",
        "loader_pak_identity=",
        "loader_xui_state_known=",
        "player_b0_snapshot_complete=",
        "source_b0_unresolved_reason=",
        "source_registry_reported_count=",
        "source_registry_observed_count=",
        "source_registry_inventory_failure=",
        "player_b0_unresolved_reason=",
        "audio_b0_unresolved_reason=",
        "global_data_b0_unresolved_reason=",
        "frontend_b0_unresolved_reason=",
        "loader_b0_unresolved_reason=",
    ):
        assert key in report
    assert "TestIncompleteMandatoryBaselineCannotArmOrMutate" in harness
    assert "TestPartialBaselineEvidenceRemainsNonComparable" in harness


def test_startup_vfs_inventory_is_synchronized_and_install_root_bounded():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    validator = _cpp_function_body(
        production, "bool ValidateFsSourceRegistrySynchronization"
    )
    capture = _cpp_function_body(
        production, "StartupVfsInventoryCaptureResult CaptureStartupFilesystemInventory"
    )
    commit = _cpp_function_body(
        production, "bool CommitStartupFilesystemInventory"
    )
    classify = _cpp_function_body(
        production, "cold::BaselineSourceKind ClassifyBaselineSourcePath"
    )
    plan = _cpp_function_body(production, "bool BuildHookPlan")
    assert "kFsSourceRegistryHeadRva" in validator
    assert "kFsSourceRegistryTailRva" in validator
    assert "kFsSourceRegistryCountRva" in validator
    assert "kFsSourceRegistryFallbackRva" in validator
    assert "g_fs_source_registry_layout_validated" in validator
    assert "ValidateFsSourceRegistrySynchronization" in plan
    windows_view = (CUSTOM / "FilesystemSourceWindowsView.cpp").read_text(
        encoding="utf-8"
    )
    windows_header = (CUSTOM / "FilesystemSourceWindowsView.h").read_text(
        encoding="utf-8"
    )
    assert "CaptureLiveFilesystemSourceRegistryView" in capture
    assert "AcquireSRWLockShared" in windows_view
    assert "ReleaseSRWLockShared" in windows_view
    assert "kFilesystemSourcesSrwLockRva = 0x00053AD0U" in windows_header
    assert "supported_pe_identity_validated" in windows_view
    assert "registry_layout_validated" in windows_view
    assert "source_path_reader_validated" in windows_view
    assert "g_startup_vfs_capture_invalidated" in capture
    assert "g_startup_vfs_registry_generation" in capture
    assert "g_startup_vfs_capture_in_progress" in commit
    assert "g_source_registry_active_calls" in commit
    assert '"workshop"' in classify
    assert "BaselinePathWithinChild" in classify
    assert "inside_approved_base_tree" in classify
    assert "IsExactCustomMainMenuSourcePath" in classify
    assert "cold::BaselineSourceKind::ApprovedGlobal" in classify
    assert classify.index("IsExactCustomMainMenuSourcePath") < classify.index(
        "g_normalized_workshop_root_count"
    )
    for broad_substring in (
        '"\\\\steamapps\\\\common\\\\dying light"',
        '"\\\\devtools"',
        '"\\\\dw\\\\"',
    ):
        assert broad_substring not in classify


def test_loader_owned_b0_state_queries_report_real_internal_state():
    common = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    pack = (ROOT / "src" / "dlw_pack.c").read_text(encoding="utf-8")
    ui = (ROOT / "src" / "dlw_ui.c").read_text(encoding="utf-8")
    assert "int dluma_content_pack_snapshot(" in common
    assert "int dluma_content_pack_snapshot(" in pack
    assert "g_dluma_pack_mounted && g_dluma_pack_path[0]" in pack
    assert "int dluma_ui_main_menu_redirect_is_applied(" in common
    assert "int dluma_ui_main_menu_redirect_is_applied(" in ui
    assert "g_dluma_main_menu_redirect_applied != 0" in ui


def test_transition_quarantine_preserves_state_five_cleanup_authority():
    core_header = (
        CUSTOM / "FrontendContentColdReboot.h"
    ).read_text(encoding="utf-8")
    core = (
        CUSTOM / "FrontendContentColdReboot.cpp"
    ).read_text(encoding="utf-8")
    production = (
        CUSTOM / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    harness = (
        ROOT / "tests/frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")

    for field in (
        "active_epoch_quarantined",
        "transition_attestation_failure",
        "new_project_admission_blocked",
        "cleanup_still_authorized",
        "quarantined_epoch",
    ):
        assert field in core_header
    assert "QuarantineActiveEpochForCleanup" in production
    assert "QuarantineActiveEpochForCleanup" in core
    assert "reboot->restart_required" in core
    assert "reboot->active_epoch_quarantined" in core
    assert "ArmColdRebootForEpoch(" in production
    assert "&g_cold_reboot, epoch" in production
    assert "dluma_project_launch_note_quarantined_cleanup_result" in production
    assert "TestTransitionQuarantineRemainsCleanupCapable" in harness
    assert "TestSourceContradictionRequiresOneFailurePresentation" in (
        harness
    )


def test_blocked_no_level_menu_requests_failure_panel_without_automatic_close():
    core_header = (
        CUSTOM / "FrontendContentColdReboot.h"
    ).read_text(encoding="utf-8")
    production = (
        CUSTOM / "CustomMapAssetIsolationFix.cpp"
    ).read_text(encoding="utf-8")
    shutdown = (ROOT / "src/dlw_shutdown.c").read_text(encoding="utf-8")
    common = (ROOT / "src/dlw_common.h").read_text(encoding="utf-8")

    for field in (
        "no_level_failure_presentation_scheduled",
        "no_level_failure_presentation_count",
        "no_level_failure_presentation_reason",
        "no_level_failure_presentation_thread",
        "no_level_failure_presentation_show_menu_attempt",
        "failure_panel_delivered",
    ):
        assert field in core_header
        assert field in production
    assert "ClaimNoLevelFailurePresentation" in production
    show_menu = _cpp_function_body(production, "void __fastcall HookShowMenuMain(")
    assert "dluma_frontend_error_present(" in show_menu
    assert "dluma_shutdown_schedule_orderly_close" not in show_menu
    admission = _cpp_function_body(production, 'extern "C" int dluma_custom_map_project_admission_evidence(')
    assert admission.index("dluma_frontend_error_blocks_project_launch()") < admission.index("if (!g_dluma.config.custom_map_asset_isolation_enabled)")
    assert "int dluma_shutdown_schedule_orderly_close" in common
    assert "DLUMA_ORDERLY_CLOSE_IDLE" in shutdown
    assert "dluma_atomic_compare_exchange(" in shutdown
    assert "DLUMA_ORDERLY_CLOSE_DELIVERY_MESSAGE" in shutdown
    assert "DLUMA_ORDERLY_CLOSE_TAG_MAGIC" in shutdown
    assert "dluma_orderly_close_token_publish(" in shutdown
    assert "dluma_orderly_close_token_mark_delivered(" in shutdown
    assert "dluma_orderly_close_token_consume(" in shutdown
    assert "observed != dluma_game_window_proc" in shutdown
    assert "process_id != GetCurrentProcessId()" in shutdown
    assert "TerminateProcess" not in production
    assert "TerminateProcess" not in shutdown

    window_proc = _cpp_function_body(
        shutdown, "static LRESULT CALLBACK dluma_game_window_proc"
    )
    assert "message == DLUMA_ORDERLY_CLOSE_DELIVERY_MESSAGE" in window_proc
    assert "message == WM_CLOSE" in window_proc
    assert "SC_CLOSE" in window_proc
    assert "CallWindowProcW(original" in window_proc


def test_registry_quiesces_dependents_first_and_cancels_in_inverse_order():
    source = (
        CUSTOM / "ContentDomainRestartRegistry.cpp"
    ).read_text(encoding="utf-8")
    quiesce = _cpp_function_body(source, "RestartResult RunQuiescePhase")
    cancel_source = source[source.rindex("DomainResult CancelQuiescedAdapters") :]
    cancel = _cpp_function_body(
        cancel_source, "DomainResult CancelQuiescedAdapters"
    )
    harness = (
        ROOT / "tests" / "frontend_content_cold_reboot_fake_harness.cpp"
    ).read_text(encoding="utf-8")
    assert "ordered = registry->order_count" in quiesce
    assert "ordered != 0; --ordered" in quiesce
    assert "orphaned_quiesce" in quiesce
    assert "CancelQuiescedAdapters" in quiesce
    assert "ordered = 0; ordered < registry->order_count" in cancel
    assert "TestDependencyPhaseOrdering" in harness
    assert (
        "TestPreparedCancellationUnwindsReverseQuiesceAndFailureLatches"
        in harness
    )


def _pe_image(path: Path):
    data = path.read_bytes()
    nt = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, nt + 6)[0]
    optional_size = struct.unpack_from("<H", data, nt + 20)[0]
    optional = nt + 24
    image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    table = optional + optional_size
    sections = []
    for index in range(section_count):
        entry = table + index * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from(
            "<IIII", data, entry + 8
        )
        sections.append((rva, max(virtual_size, raw_size), raw))

    def read(rva: int, size: int):
        for start, span, raw in sections:
            if start <= rva and rva + size <= start + span:
                offset = raw + rva - start
                return data[offset : offset + size]
        raise AssertionError(f"unmapped RVA 0x{rva:X}")

    return image_base, read


def test_exact_supported_windows_cold_reboot_profile_matches_player_image():
    pe = DEVTOOLS_ROOT / "gamedll_x64_rwdi.dll"
    if not pe.exists():
        return
    data = pe.read_bytes()
    nt = struct.unpack_from("<I", data, 0x3C)[0]
    optional = nt + 24
    assert struct.unpack_from("<I", data, nt + 8)[0] == 0x623397AA
    assert struct.unpack_from("<I", data, optional + 56)[0] == 0x02082000
    assert hashlib.sha256(data).hexdigest().upper() == (
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
    )
    image_base, read = _pe_image(pe)
    prefixes = {
        0x003D0760: "48 8B C4 48 89 58 18 55 48 8D 68 A1 48 81 EC 90",
        0x003D0B20: "48 83 EC 28 83 B9 FC 08 00 00 02 75 09 48 8B 01",
        0x003D0B40: "40 53 48 83 EC 20 83 B9 FC 08 00 00 05 48 8B D9",
        0x003D0B80: "8B 91 FC 08 00 00 48 8B 01 83 FA 07 7D 09 FF C2",
        0x003C29E0: "40 53 48 83 EC 20 48 83 B9 08 05 00 00 00 48 8B",
        0x011BD2A0: "40 56 57 48 83 EC 68 80 B9 B8 09 00 00 00 8B F2",
    }
    for rva, expected in prefixes.items():
        raw = bytes.fromhex(expected)
        assert read(rva, len(raw)) == raw
    vtable = 0x0157D5F8
    slots = {
        0x0898: 0x003C29E0,
        0x0BF0: 0x003D0760,
        0x0BF8: 0x003D0B80,
        0x0C08: 0x003D0B20,
        0x0C10: 0x003D0B40,
    }
    for slot, target in slots.items():
        assert struct.unpack("<Q", read(vtable + slot, 8))[0] == image_base + target
    instruction = read(0x003D09F6, 5)
    assert instruction[0] == 0xE8
    displacement = struct.unpack_from("<i", instruction, 1)[0]
    assert 0x003D09FB + displacement == 0x011BD2A0

    def relative_call_target(call_rva: int):
        call = read(call_rva, 5)
        assert call[0] == 0xE8
        return call_rva + 5 + struct.unpack_from("<i", call, 1)[0]

    def indirect_call_iat(call_rva: int):
        call = read(call_rva, 6)
        assert call[:2] == b"\xFF\x15"
        return call_rva + 6 + struct.unpack_from("<i", call, 2)[0]

    assert indirect_call_iat(0x003D0910) == 0x013CDE50
    assert relative_call_target(0x003D0925) == 0x011E1A30
    assert relative_call_target(0x003D092D) == 0x003DDF40
    assert indirect_call_iat(0x003D095D) == 0x013CDE58
    assert read(0x003D0969, 7) == bytes.fromhex(
        "41 FF 93 10 0C 00 00"
    )

    assert read(0x003C2A14, 29) == bytes.fromhex(
        "48 8B 8B 08 05 00 00 33 D2 48 8B 01 44 8D 42 0B "
        "FF 90 C8 07 00 00 48 8B BB 10 05 00 00"
    )
    assert read(0x003C2A24, 6) == bytes.fromhex("FF 90 C8 07 00 00")
    assert read(0x003C2A2A, 7) == bytes.fromhex(
        "48 8B BB 10 05 00 00"
    )
    assert read(0x003C2A73, 20) == bytes.fromhex(
        "48 89 BB 08 05 00 00 48 8B 7C 24 30 "
        "B0 01 48 83 C4 20 5B C3"
    )

    assert read(0x003D0B46, 49) == bytes.fromhex(
        "83 B9 FC 08 00 00 05 48 8B D9 75 1F "
        "48 8B 01 FF 90 98 08 00 00 48 8B 03 48 8B CB "
        "C7 83 F8 08 00 00 01 00 00 00 "
        "FF 90 F8 0B 00 00 48 83 C4 20 5B C3"
    )

    extents = {
        0x003C29F3: "32 C0 48 83 C4 20 5B C3",
        0x003C2A7A: (
            "48 8B 7C 24 30 B0 01 48 83 C4 20 5B C3 "
            "CC CC CC CC CC CC CC CC CC"
        ),
        0x003C2A90: "48 8B 81 10 05 00 00 48 85 C0",
        0x003D0AAC: (
            "48 8B B4 24 A0 00 00 00 48 8B BC 24 A8 00 00 00 "
            "48 8B 9C 24 B0 00 00 00 48 81 C4 90 00 00 00 5D C3"
        ),
        0x003D0B19: "CC CC CC CC CC CC CC",
        0x003D0B36: "48 83 C4 28 C3 CC CC CC CC CC",
        0x003D0B71: (
            "48 83 C4 20 5B C3 CC CC CC CC CC CC CC CC CC"
        ),
        0x003D0B97: "48 FF A0 00 0C 00 00 CC CC",
        0x003D0BA0: (
            "40 53 48 83 EC 20 48 8B D9 C7 81 FC 08 00 00 00 "
            "00 00 00"
        ),
        0x011BD6C0: "48 83 C4 68 5F 5E C3 90",
        0x011BD6F0: (
            "48 89 5C 24 08 48 89 6C 24 10 "
            "48 89 74 24 18 57"
        ),
    }
    for rva, expected in extents.items():
        raw = bytes.fromhex(expected)
        assert read(rva, len(raw)) == raw


def test_windows_profile_contract_requires_exact_state_five_teardown_topology():
    header = (CUSTOM / "FrontendColdRebootWindowsProfile.h").read_text(
        encoding="utf-8"
    )
    source = (CUSTOM / "FrontendColdRebootWindowsProfile.cpp").read_text(
        encoding="utf-8"
    )
    required_header = (
        "kSupportedSha256",
        "kStateFiveHideAllCallRva = 0x003D0910U",
        "kStateFiveScheduleDeactivateCallRva = 0x003D0925U",
        "kStateFiveWaitCallRva = 0x003D095DU",
        "kDestroySessionOwnerCallRva = 0x003C2A24U",
        "kDestroySessionOwnerReturnRva = 0x003C2A2AU",
        "kDestroySessionOwnerSlot = 0x07C8U",
        "kDestroySessionCurrentSessionClearRva = 0x003C2A73U",
        "kDestroyGuardWriteRva = 0x003D0B61U",
        "kAdvanceCallRva = 0x003D0B6BU",
        "kAdvanceReturnRva = 0x003D0B71U",
        "ProfileDestroySessionOwnerContext",
        "ProfileFunctionExtents",
        "ProfileComplete = (1U << 9) - 1U",
    )
    for token in required_header:
        assert token in header

    for call in (
        "IndirectImportCall(*input, kStateFiveHideAllCallRva",
        "RelativeCall(*input, kStateFiveScheduleDeactivateCallRva",
        "RelativeCall(*input, kStateFivePredicateCallRva",
        "IndirectImportCall(*input, kStateFiveWaitCallRva",
        "VtableCall(*input, kDestroySessionOwnerCallRva",
        "Equal(*input, kDestroySessionCurrentSessionClearRva",
        "Equal(*input, kAdvanceDestroyStateNextFunctionRva",
        "Equal(*input, kDestroySessionNextFunctionRva",
        "Equal(*input, kShowMenuMainNextFunctionRva",
    ):
        assert call in source
    assert "input->full_sha256_valid" in source


def test_loader_state_zero_exact_identity_is_retryable_and_not_ready():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    assert re.search(
        r"const bool loader_ready = loader_pack_snapshot_matches &&\s*"
        r"loader_xui_operational && !local_project_lua_count &&\s*"
        r"g_frontend_startup_baseline\.loader\.loader_pak_identity;",
        production,
    )
    assert re.search(
        r"loader_ready \? cold::StartupB0DomainCaptureResult::Ready\s*"
        r": cold::StartupB0DomainCaptureResult::Incomplete",
        production,
    )
    assert "loader_ready ? cold::StartupB0Blocker::None" in production
    assert "loader_ready\n                ? g_frontend_startup_baseline.loader.loader_pak_identity\n                : 0" in production
