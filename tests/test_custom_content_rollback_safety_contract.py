from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "CustomMapAssetIsolationFix.cpp"
)
REGISTRY_PATH = ROOT / "src" / "fixes" / "FixRegistry.cpp"
EXPORTS_PATH = ROOT / "src" / "dlw_exports.c"
UI_PATH = ROOT / "src" / "dlw_ui.c"
MANUAL_PATH = (
    ROOT / "docs" / "fixes" / "custom_map_asset_isolation" / "MANUAL_TEST.md"
)
REPORT_PATH = (
    ROOT
    / "docs"
    / "fixes"
    / "custom_map_asset_isolation"
    / "INVESTIGATION_REPORT.md"
)


def _strip_cpp_comments_and_literals(source: str) -> str:
    pattern = re.compile(
        r"//[^\r\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.MULTILINE | re.DOTALL,
    )
    return pattern.sub(lambda match: " " * len(match.group(0)), source)


def _balanced_end(source: str, start: int, opener: str, closer: str) -> int:
    assert source[start] == opener
    depth = 0
    for index in range(start, len(source)):
        if source[index] == opener:
            depth += 1
        elif source[index] == closer:
            depth -= 1
            if depth == 0:
                return index + 1
    raise AssertionError(f"unterminated {opener}{closer} block")


def _extract_cpp_function(source: str, function_name: str) -> tuple[str, str]:
    scrubbed = _strip_cpp_comments_and_literals(source)
    pattern = re.compile(rf"\b{re.escape(function_name)}\s*\(")
    for match in pattern.finditer(scrubbed):
        prefix = scrubbed[: match.start()].rstrip()
        if prefix.endswith((".", "->", "::")):
            continue

        open_paren = scrubbed.find("(", match.start())
        close_paren = _balanced_end(scrubbed, open_paren, "(", ")")
        body_start = scrubbed.find("{", close_paren)
        declaration_end = scrubbed.find(";", close_paren)
        if body_start < 0 or (
            declaration_end >= 0 and declaration_end < body_start
        ):
            continue
        suffix = scrubbed[close_paren:body_start]
        if any(token in suffix for token in ("(", ")", "=", ",")):
            continue

        end = _balanced_end(scrubbed, body_start, "{", "}")
        return source[match.start() : end], scrubbed[match.start() : end]
    raise AssertionError(f"missing C++ function definition {function_name}")


def _extract_cpp_struct(source: str, struct_name: str) -> tuple[str, str]:
    scrubbed = _strip_cpp_comments_and_literals(source)
    match = re.search(rf"\bstruct\s+{re.escape(struct_name)}\b", scrubbed)
    assert match is not None, f"missing C++ struct {struct_name}"
    body_start = scrubbed.find("{", match.end())
    assert body_start >= 0, f"missing body for C++ struct {struct_name}"
    end = _balanced_end(scrubbed, body_start, "{", "}")
    return source[match.start() : end], scrubbed[match.start() : end]


def _extract_cpp_calls(source: str, function_name: str) -> list[str]:
    scrubbed = _strip_cpp_comments_and_literals(source)
    calls: list[str] = []
    position = 0
    pattern = re.compile(rf"\b{re.escape(function_name)}\s*\(")
    while match := pattern.search(scrubbed, position):
        open_paren = scrubbed.find("(", match.start())
        end = _balanced_end(scrubbed, open_paren, "(", ")")
        calls.append(scrubbed[match.start() : end])
        position = end
    return calls


def _if_blocks(source: str) -> list[tuple[str, int, int]]:
    """Return (condition, body-start, body-end) for braced and simple ifs."""

    blocks: list[tuple[str, int, int]] = []
    position = 0
    pattern = re.compile(r"\bif\s*\(")
    while match := pattern.search(source, position):
        open_paren = source.find("(", match.start())
        condition_end = _balanced_end(source, open_paren, "(", ")")
        condition = source[open_paren + 1 : condition_end - 1]
        body_start = condition_end
        while body_start < len(source) and source[body_start].isspace():
            body_start += 1
        if body_start < len(source) and source[body_start] == "{":
            body_end = _balanced_end(source, body_start, "{", "}")
        else:
            body_end = source.find(";", body_start)
            assert body_end >= 0, "unterminated simple if statement"
            body_end += 1
        blocks.append((condition, body_start, body_end))
        position = match.end()
    return blocks


def _compact(source: str) -> str:
    return re.sub(r"\s+", "", source)


def _assert_hex_bytes(source: str, values: tuple[int, ...]) -> None:
    pattern = r"\{\s*" + r"\s*,\s*".join(
        rf"0x{value:02X}" for value in values
    ) + r"\s*\}"
    assert re.search(pattern, source, re.IGNORECASE), (
        "missing exact supported-build bytes: "
        + " ".join(f"{value:02X}" for value in values)
    )


def test_reset_detaches_exact_path_created_source_before_deferred_destruction():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, prepare = _extract_cpp_function(
        source, "PrepareFrontendContentResetPlan"
    )
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    _, detach = _extract_cpp_function(source, "DetachPreparedSource")
    _, native_detach = _extract_cpp_function(source, "DetachRetirementSource")
    _, native_destroy = _extract_cpp_function(source, "DestroyRetirementSource")
    _, callbacks = _extract_cpp_function(source, "SourceRetirementCallbacks")

    # Resolve the complete source set while it is still mounted. The loaded
    # project reset transfers ownership into the retirement ledger and must
    # never run a deleting destructor while consumers can still retain it.
    for forbidden in (
        "ValidateExactSourceSnapshot",
        "CopySourcePath",
        "ReadBytes",
        "dluma_is_readable",
        "RemoveAndDestroyPathCreatedSource(",
        "g_source_reset_destructors[index](",
        "g_original_fs_remove_path(",
        "g_original_fs_remove_object(",
    ):
        assert forbidden not in reset
    compact = _compact(reset)
    assert "source.source->" not in compact
    assert "!source.path_created_ownership" in prepare
    assert "!source.exact_handle_available" in prepare
    assert "ResolveSourceDeletingDestructor(source.source)" in prepare
    assert len(_extract_cpp_calls(reset, "DetachPreparedSource")) == 1
    compact_prepare = _compact(prepare)
    resolve = compact_prepare.index("ResolveSourceDeletingDestructor(source.source)")
    prepared = compact_prepare.index("g_prepared_source_reset.prepared=true")
    detach_index = compact.index("DetachPreparedSource(index)")
    assert resolve < prepared
    assert compact.index("!g_prepared_source_reset.prepared") < detach_index
    assert "DetachPreparedSource(" not in prepare
    assert "g_source_retirement_transaction" in detach
    assert "g_source_retirement_epoch" in detach
    assert "g_source_retirement_handles[index]" in detach
    assert len(_extract_cpp_calls(detach, "DetachSource")) == 1
    assert "callbacks.verify=&VerifyRetirementSource" in _compact(callbacks)
    assert "callbacks.detach=&DetachRetirementSource" in _compact(callbacks)
    assert "callbacks.destroy=&DestroyRetirementSource" in _compact(callbacks)

    # Only the exact object-form remover mutates registry membership. Its
    # successful unlink is recorded, and a concurrent generation change
    # makes the transaction uncertain rather than authorizing destruction.
    native_compact = _compact(native_detach)
    unlink = native_compact.index("g_original_fs_remove_object(")
    assert native_compact.index("!PreparedSourceRegistryIdentityStillStable()") < unlink
    assert unlink < native_compact.index("g_source_retirement_unlinked[index]=removed")
    assert "after!=before+1U" in native_compact
    assert "&g_source_registry_active_calls,0,0)!=0" in native_compact
    assert "g_source_reset_destructors[index](" not in native_compact

    destroy_compact = _compact(native_destroy)
    destroy = destroy_compact.index("g_source_reset_destructors[index](")
    for gate in (
        "cold::RestartPhase::RetiringProviders",
        "!g_source_retirement_unlinked[index]",
        "!g_source_reset_destructors[index]",
        "!GlobalSourceCollectionExcludesRetirementTargets(false)",
        "!NativeFileRoutesStillOwned()",
        "!files::ValidateProof(",
    ):
        assert destroy_compact.index(gate) < destroy
    assert destroy < destroy_compact.index("files::CompleteSourceRetirement(")

    # Path removal remains observational. The third exact object call is the
    # deferred retirement detacher; the others are the natural observer and
    # the separately guarded cancelled-presentation cleanup helper.
    assert len(_extract_cpp_calls(source, "g_original_fs_remove_path")) == 1
    assert len(_extract_cpp_calls(source, "g_original_fs_remove_object")) == 3
    _, path_wrapper = _extract_cpp_function(source, "HookFsRemovePath")
    assert len(_extract_cpp_calls(path_wrapper, "g_original_fs_remove_path")) == 1


def test_cancelled_presentation_unlinks_before_its_single_deleting_destructor():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, exact_reset = _extract_cpp_function(
        source, "RemoveAndDestroyPathCreatedSource"
    )
    compact = _compact(exact_reset)
    assert "if(!destructor||!g_original_fs_remove_object(source))returnfalse" in compact
    assert compact.index("g_original_fs_remove_object(source)") < compact.index(
        "destructor(source,1U)"
    )
    assert len(_extract_cpp_calls(exact_reset, "destructor")) == 1


def test_provider_retirement_requires_all_domain_and_native_file_release_proofs():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, preflight = _extract_cpp_function(source, "ColdVfsPreflight")
    _, retire = _extract_cpp_function(source, "ColdVfsRetireProviders")
    compact_preflight = _compact(preflight)
    assert compact_preflight.index("PrepareFrontendContentResetPlan(") < (
        compact_preflight.index("PrepareDetachedSourceOwnership()")
    )
    compact = _compact(retire)
    destroy = compact.index("retirement::DestroyDetachedSource(")
    for gate in (
        "!g_project_callbacks_drained",
        "context->transaction_generation!=g_source_retirement_transaction",
        "context->content_epoch!=g_source_retirement_epoch",
        "cold::RestartPhase::RetiringProviders",
        "g_cold_reboot_registry.runtime_failure_latched",
        "ColdRebootNextProjectStagingStarted()",
        "g_source_retirement_registry_generation!=ReadSourceRegistryGeneration()",
        "state.reset_result!=cold::DomainResult::Succeeded",
        "!GlobalSourceCollectionExcludesRetirementTargets(false)",
        "!NativeFileRoutesStillOwned()",
        "!files::CloseAdmission(",
        "!g_source_native_file_proofs[i].ready",
        "g_source_native_file_proofs[i].source_destroyed",
        "files::ValidateProof(",
    ):
        assert compact.index(gate) < destroy
    # The complete set is checked before the second loop can destroy one
    # provider. Each proof is refreshed after earlier deletions revise the
    # native-file ledger, and admission reopens only after retirement.
    close = compact.index("files::CloseAdmission(")
    first_proof = compact.index("files::SnapshotSource(", close)
    second_loop = compact.index("retirement::SourceRecordsource", first_proof)
    refreshed_proof = compact.index("files::SnapshotSource(", second_loop)
    assert close < first_proof < second_loop < refreshed_proof < destroy
    assert destroy < compact.index("files::ReopenAdmission(")
    assert compact.index("files::ReopenAdmission(") < compact.index(
        "g_source_retirement_complete=true"
    )


def test_on_destroy_owner_binds_exact_outgoing_epoch_session_and_level():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, binding = _extract_cpp_struct(source, "FrontendEpochBinding")
    compact_binding = _compact(binding)
    assert re.search(r"std::uint32_t\s+epoch\s*;", binding)
    assert re.search(r"void\s*\*\s*session\s*;", binding)
    assert re.search(r"void\s*\*\s*outgoing_level\s*;", binding)
    assert "selected_content_name" in compact_binding

    # Generic ChangeActiveLevel is an observation/transition-generation owner,
    # never source-reset authority.  This excludes frontend-to-map,
    # same-project map-to-map, and direct project transitions from rollback.
    _, change = _extract_cpp_function(source, "HookChangeActiveLevel")
    for forbidden in (
        "BindFrontendOutgoingEpoch(",
        "BindFrontendEpochAtOnDestroy(",
        "CreateFrontendEpochBinding(",
        "PerformFrontendContentReset(",
        "g_frontend_unload_epoch =",
    ):
        assert forbidden not in change
    assert "SameProjectTransitionNoSourceReset" not in change

    _, late_observation = _extract_cpp_function(
        source, "dluma_custom_map_asset_trace_active_level_observed"
    )
    compact_late = _compact(late_observation)
    assert "SameProjectTransitionNoSourceReset" in late_observation
    assert "previous.epoch==current_epoch" in compact_late
    assert "content::PathsEqual(normalized_previous,normalized_current)" in compact_late

    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    compact_destroy = _compact(on_destroy)
    bind_calls = _extract_cpp_calls(on_destroy, "BindFrontendEpochAtOnDestroy")
    assert len(bind_calls) == 1
    assert (
        "g_fix_enabled&&state_five_owner&&"
        "BindFrontendEpochAtOnDestroy(" in compact_destroy
    )
    assert all(
        token in bind_calls[0]
        for token in ("session", "active_level", "current", "requested", "&binding")
    )
    assert "ReadFrontendEpochBinding(" not in on_destroy
    assert "ConsumeFrontendEpochBinding(" not in on_destroy
    assert "DeferFrontendSourceResetAtOnDestroy(binding)" in compact_destroy
    assert "g_frontend_unload_epoch=binding.epoch" in compact_destroy
    assert "g_frontend_unload_session=binding.session" in compact_destroy
    assert "g_frontend_unload_level=binding.outgoing_level" in compact_destroy
    assert "CurrentEpochId" not in on_destroy

    # The binding and exact unload coordination context exist before the outer
    # original OnDestroy body can call SessionDI::UnloadLevel.
    bind_index = compact_destroy.index("BindFrontendEpochAtOnDestroy(")
    epoch_index = compact_destroy.index("g_frontend_unload_epoch=binding.epoch")
    session_index = compact_destroy.index("g_frontend_unload_session=binding.session")
    level_index = compact_destroy.index("g_frontend_unload_level=binding.outgoing_level")
    outer_original_index = compact_destroy.rindex(
        "g_original_session_on_destroy(session)"
    )
    assert bind_index < epoch_index < outer_original_index
    assert bind_index < session_index < outer_original_index
    assert bind_index < level_index < outer_original_index

    _, binder = _extract_cpp_function(source, "BindFrontendEpochAtOnDestroy")
    compact_binder = _compact(binder)
    assert "content::SelectFrontendResetEpoch(" in binder
    assert "CreateFrontendEpochBinding(binding)" in compact_binder
    for rejected in (
        "FrontendBindFailure::Shutdown",
        "FrontendBindFailure::NoActiveLevel",
        "FrontendBindFailure::FrontendLevel",
        "FrontendBindFailure::NoActiveEpoch",
        "FrontendBindFailure::ContentNameUnavailable",
        "FrontendBindFailure::AmbiguousEpochs",
        "FrontendBindFailure::NoActiveSources",
        "FrontendBindFailure::AlreadyConsumed",
    ):
        assert rejected in binder
    assert "FrontendBindFailure::ContentNameEpochMismatch" not in binder
    assert "!IsFrontendLevelName(requested_level)" not in compact_binder


def test_on_destroy_publishes_and_c10_executes_at_the_post_return_boundary():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    compact = _compact(on_destroy)

    assert len(_extract_cpp_calls(on_destroy, "PerformFrontendContentReset")) == 0
    assert len(_extract_cpp_calls(on_destroy, "g_original_session_on_destroy")) == 2
    original_index = compact.rindex("g_original_session_on_destroy(session)")
    original_returned = compact.index(
        "lifecycle::MarkOriginalReturned(&g_frontend_lifecycle)",
        original_index,
    )
    proof = compact.index("constboolproof_complete=", original_returned)
    deferred = compact.index(
        "DeferFrontendSourceResetAtOnDestroy(binding)", proof
    )
    publish = compact.index(
        "PublishPendingPostC10PreflightToken(", deferred
    )
    assert original_index < original_returned < proof < deferred < publish

    # The nested arm forwards once and returns before the outer binding/reset
    # code.  Therefore nested OnDestroy cannot consume or reset an epoch.
    depth_guard = compact.index("if(g_frontend_lifecycle.depth)")
    nested_original = compact.index(
        "g_original_session_on_destroy(session)", depth_guard
    )
    nested_return = compact.index("returnnested_result", nested_original)
    bind_index = compact.index("BindFrontendEpochAtOnDestroy(")
    assert depth_guard < nested_original < nested_return < bind_index
    assert "lifecycle::EnterNestedOnDestroy(" in on_destroy
    assert "lifecycle::LeaveNestedOnDestroy(" in on_destroy

    # OnDestroy gathers both natural-teardown proofs and publishes only the
    # bounded pending token. The source remover and semantic assessments
    # remain outside this nested DestroySession boundary.
    defer_guard_prefix = compact[original_returned:deferred]
    for guard in (
        "g_frontend_on_destroy_unload_observed",
        "g_frontend_outgoing_level_release_observed",
        "lifecycle::ReadyForReset(&g_frontend_lifecycle)",
    ):
        assert guard in defer_guard_prefix
    for forbidden in (
        "ConsumeFrontendEpochBinding(",
        "TryBeginFrontendResetPhase(",
        "lifecycle::ConsumeForReset(",
        "lifecycle::BeginReset(",
        "PerformFrontendContentReset(",
        "ExecuteFrontendResetAtPostC10(",
        "AssessPlayerSnapshotTouchAtResetBoundary(",
        "AssessProductionAudioMinimumAtResetBoundary(",
    ):
        assert forbidden not in on_destroy

    # C10 establishes the scope, calls its original exactly once, samples the
    # session slot, consumes the token, then invokes the transaction owner
    # before clearing the scope. ColdVfsReset remains the sole source remover.
    _, c10 = _extract_cpp_function(source, "HookDestroyingSessionCallback")
    compact_c10 = _compact(c10)
    assert "BeginColdRebootStateFiveScope(" in c10
    c10_original = compact_c10.index(
        "g_original_destroying_session_callback(game_di)"
    )
    session_sample = compact_c10.index(
        "ReadGameDiCurrentSession(game_di)", c10_original
    )
    consume_token = compact_c10.index(
        "ConsumePendingPostC10Token(", session_sample
    )
    execute_post_c10 = compact_c10.index(
        "ExecuteFrontendResetAtPostC10(", consume_token
    )
    finish_scope = compact_c10.index(
        "FinishColdRebootStateFiveScope(", execute_post_c10
    )
    assert compact_c10.index("BeginColdRebootStateFiveScope(") < c10_original
    assert c10_original < session_sample < consume_token < execute_post_c10
    assert execute_post_c10 < finish_scope
    assert c10.count("g_original_destroying_session_callback(game_di)") == 1
    for forbidden in (
        "PrepareColdRebootAtStateFive(",
        "EnterColdRebootBarrier(",
        "PrepareFrontendContentResetPlan(",
        "PerformFrontendContentReset(",
    ):
        assert forbidden not in c10

    _, post_destroy = _extract_cpp_function(
        source, "ExecuteFrontendResetAtPostC10"
    )
    compact_post_destroy = _compact(post_destroy)
    prepare_index = compact_post_destroy.index(
        "cold::PrepareColdRebootAtStateFive("
    )
    enter_index = compact_post_destroy.index(
        "cold::EnterColdRebootBarrier("
    )
    assert prepare_index < enter_index
    for gate in (
            "scope.active",
            "scope.epoch!=epoch",
            "scope.outgoing_session!=binding.session",
            "scope.c10_callback_thread_id!=current_thread",
            "!scope.please_wait_generation",
            "!scope.frame_route_token",
            "scope.owner_callback_depth!=",
            "g_destroying_session_callback_depth",
            "!scope.armed",
            "!scope.on_destroy_claimed",
            "scope.deferred_source_reset",
            "scope.post_c10_token_consumed",
            "scope.barrier_attempted",
            "!FrontendEpochBindingsEqual(binding,scope.deferred_binding)",
            "!lifecycle::ReadyForReset(&g_frontend_lifecycle)",
            "token.token_publication_sequence!=",
            "environment.current_session",
            "game_thread!=current_thread",
    ):
        assert gate in compact_post_destroy
    assert (
        "preflight.caller_return_rva="
        "coldwin::kStateFiveCallbackReturnRva"
        in compact_post_destroy
    )
    assert (
        "evidence.caller_return_rva="
        "coldwin::kStateFiveCallbackReturnRva"
        in compact_post_destroy
    )
    assert "ReadGameDiState(" not in post_destroy

    _, bf8 = _extract_cpp_function(source, "HookDestroyStateAdvance")
    compact_bf8 = _compact(bf8)
    assert compact_bf8.count("g_original_destroy_state_advance(game_di)") == 1
    for forbidden in (
        "PrepareColdRebootAtStateFive(",
        "EnterColdRebootBarrier(",
        "PerformFrontendContentReset(",
    ):
        assert forbidden not in bf8

    _, vfs_reset = _extract_cpp_function(source, "ColdVfsReset")
    compact_vfs_reset = _compact(vfs_reset)
    consume = compact_vfs_reset.index("ConsumeFrontendEpochBinding(binding)")
    epoch_consume = compact_vfs_reset.index("content::MarkEpochConsumed(")
    reset_phase = compact_vfs_reset.index("TryBeginFrontendResetPhase(binding.epoch)")
    lifecycle_consume = compact_vfs_reset.index(
        "lifecycle::ConsumeForReset(&g_frontend_lifecycle)"
    )
    lifecycle_begin = compact_vfs_reset.index(
        "lifecycle::BeginReset(&g_frontend_lifecycle)"
    )
    reset_index = compact_vfs_reset.index("PerformFrontendContentReset(")
    assert (
        consume < epoch_consume < reset_phase < lifecycle_consume
        < lifecycle_begin < reset_index
    )
    reset_call = _extract_cpp_calls(vfs_reset, "PerformFrontendContentReset")[0]
    for argument in (
        "binding.epoch",
        "g_frontend_on_destroy_unload_observed",
        "g_frontend_outgoing_level_release_observed",
    ):
        assert argument in reset_call
    assert source.count("PerformFrontendContentReset(") == 3

    _, shutdown = _extract_cpp_function(source, "Shutdown")
    assert "PerformFrontendContentReset(" not in shutdown
    _, change = _extract_cpp_function(source, "HookChangeActiveLevel")
    assert "PerformFrontendContentReset(" not in change


def test_release_packs_marks_only_the_clevel_inside_exact_owned_unload_scope():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    assert re.search(r"thread_local\s+void\s*\*\s*g_frontend_unload_level\s*;", source)
    _, release = _extract_cpp_function(source, "HookLevelReleasePacks")
    compact = _compact(release)

    begin_index = compact.index("lifecycle::BeginOwnedLevelRelease(")
    original_index = compact.index("g_original_level_release_packs(level)")
    complete_index = compact.index("lifecycle::CompleteOwnedLevelRelease(")
    assert begin_index < original_index < complete_index

    # Live evidence proves session+0xB0 is an ILevel pointer while the natural
    # ReleasePacks receiver is a distinct CLevel pointer.  Attribution is by
    # exact 0x431C83 UnloadLevel dynamic scope, never guessed pointer equality.
    assert "level==g_frontend_unload_level" not in compact
    assert "g_frontend_unload_level==level" not in compact
    exact_scope = re.search(
        r"constboolexact_owned_unload_scope=([^;]+);", compact
    )
    assert exact_scope is not None
    for guard in (
        "ContentTrackingLive()",
        "g_frontend_unload_depth",
        "g_frontend_unload_epoch",
        "g_frontend_lifecycle.owned_unload_depth",
        "g_frontend_on_destroy_unload_observed",
        "!g_dluma.stop_requested",
    ):
        assert guard in exact_scope.group(1)
    assert "lifecycle::BeginOwnedLevelRelease(" in release
    assert "lifecycle::CompleteOwnedLevelRelease(" in release

    lifecycle_source = SOURCE_PATH.with_name(
        "FrontendSourceRollbackLifecycle.cpp"
    ).read_text(encoding="utf-8")
    _, observe = _extract_cpp_function(
        lifecycle_source, "BeginOwnedLevelRelease"
    )
    compact_observe = _compact(observe)
    assert "!barrier->owned_unload_depth" in compact_observe
    assert "thread_id!=barrier->owner_thread_id" in compact_observe
    assert "barrier->release_enter_count" in compact_observe
    assert "barrier->unique_receivers" in compact_observe
    assert "barrier->receiver_overflow=true" in compact_observe


def test_unload_proof_accepts_only_exact_on_destroy_return_site_and_identity():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    lifecycle_header = SOURCE_PATH.with_name(
        "FrontendSourceRollbackLifecycle.h"
    ).read_text(encoding="utf-8")
    lifecycle_source = SOURCE_PATH.with_name(
        "FrontendSourceRollbackLifecycle.cpp"
    ).read_text(encoding="utf-8")

    assert re.search(
        r"kOnDestroyUnloadReturnRva\s*=\s*0x0*431C83U", lifecycle_header
    )
    assert re.search(
        r"kAfterFadeUnloadReturnRva\s*=\s*0x0*434DD6U", lifecycle_header
    )
    _, observe = _extract_cpp_function(
        lifecycle_source, "ObserveOnDestroyOwnedUnload"
    )
    compact_observe = _compact(observe)
    for guard in (
        "caller_rva!=kOnDestroyUnloadReturnRva",
        "session!=barrier->session",
        "!active_level",
        "active_level!=barrier->outgoing_level",
        "shutdown_or_quiescing",
        "!barrier->depth",
        "!barrier->epoch",
    ):
        assert guard in compact_observe

    unload, _ = _extract_cpp_function(source, "HookUnloadLevel")
    compact_unload = _compact(unload)
    observe_index = compact_unload.index(
        "lifecycle::ObserveOnDestroyOwnedUnload("
    )
    original_index = compact_unload.index("g_original_unload_level(session)")
    leave_index = compact_unload.index(
        "lifecycle::CompleteOnDestroyOwnedUnload("
    )
    assert observe_index < original_index < leave_index
    assert "caller_rva==kAfterFadeUnloadReturnRva" in compact_unload
    assert "after-fade-rva-0x434DD6-rejected" in unload

    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    compact_reset = _compact(reset)
    assert compact_reset.startswith(
        "PerformFrontendContentReset(std::uint32_tepoch_id,"
        "boolfrontend_on_destroy_unload_observed,"
        "booloutgoing_level_release_observed)noexcept"
    )
    guard_end = compact_reset.index("g_prepared_source_reset.prepared")
    assert "!frontend_on_destroy_unload_observed||" in compact_reset[:guard_end]
    assert "!outgoing_level_release_observed||" in compact_reset[:guard_end]


def test_frontend_lookup_barrier_is_nonblocking_and_fails_closed_on_early_lookup():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, observe = _extract_cpp_function(source, "ObserveFirstFrontendLookup")
    compact_observe = _compact(observe)
    _, prepare_lookup = _extract_cpp_function(
        source, "PrepareFrontendLookupEvent"
    )
    compact_prepare = _compact(prepare_lookup)
    _, setter = _extract_cpp_function(source, "SetFrontendLookupBarrier")
    compact_setter = _compact(setter)

    # One packed epoch/phase/first-result CAS makes the first lookup
    # authoritative without mixed-generation atomics or blocking the engine.
    assert "g_frontend_lookup_barrier_state" in source
    assert "_InterlockedCompareExchange64(" in prepare_lookup
    assert "kFrontendLookupObservedBit" in prepare_lookup
    assert "kFrontendLookupEarlyBit" in prepare_lookup
    assert "phase==lifecycle::FrontendRollbackPhase::ResetComplete" in compact_prepare
    assert "initial_epoch=epoch" in compact_prepare
    assert "epoch!=initial_epoch" in compact_prepare
    assert "FrontendLookupBarrierEpoch(actual)!=initial_epoch" in compact_prepare
    assert "if(FrontendLookupBarrierEpoch(observed)!=epoch)return;" in compact_setter
    for stale_global in (
        "g_frontend_lookup_phase",
        "g_frontend_lookup_epoch",
        "g_frontend_lookup_latched",
        "g_frontend_lookup_ordering_failed",
    ):
        assert stale_global not in source
    assert "FrontendLookupDisposition::LatchRestartRequired" in observe
    assert "dluma_project_launch_block(" in observe
    assert "SetAggregateState(FixState::RuntimeFailed)" in compact_observe
    assert "FirstFrontendLookupDisposition" in observe
    assert "FrontendLookupBeforeSourceReset" in observe
    assert "FrontendLookupAfterSourceReset" in observe
    assert "FirstFrontendPackOrModuleLookup" in prepare_lookup
    assert "trace::PrepareThenPublish(" in observe

    _, drain = _extract_cpp_function(source, "DrainContentDiagnostics")
    compact_drain = _compact(drain)
    assert "FrontendLookupBeforeSourceReset" in drain
    assert "ComponentRuntimeState::RuntimeFailed" in drain
    assert "SetAggregateState(FixState::RuntimeFailed)" in compact_drain

    _, load_pack = _extract_cpp_function(source, "HookGameLoadPack")
    compact_load_pack = _compact(load_pack)
    lookup_index = compact_load_pack.index("ObserveFirstFrontendLookup(")
    original_index = compact_load_pack.index("g_original_game_load_pack(")
    assert lookup_index < original_index
    assert "IsFrontendPackName(copied)" in compact_load_pack
    assert r"\\data\\menu_main_pc.rpack" in source
    assert "constboolresult=g_original_game_load_pack(" in compact_load_pack
    assert "returnresult;" in compact_load_pack

    _, load_module = _extract_cpp_function(source, "HookLoadModuleBegin")
    compact_load_module = _compact(load_module)
    lookup_index = compact_load_module.index("ObserveFirstFrontendLookup(")
    original_index = compact_load_module.index("g_original_load_module_begin(")
    assert lookup_index < original_index
    assert "exact_boundary&&IsFrontendLevelName(destination)" in compact_load_module

    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    compact_on_destroy = _compact(on_destroy)
    outer_original = compact_on_destroy.rindex(
        "g_original_session_on_destroy(session)"
    )
    original_returned = compact_on_destroy.index(
        "lifecycle::MarkOriginalReturned(&g_frontend_lifecycle)",
        outer_original,
    )
    defer = compact_on_destroy.index(
        "DeferFrontendSourceResetAtOnDestroy(binding)", original_returned
    )
    assert outer_original < original_returned < defer
    assert "PerformFrontendContentReset(" not in on_destroy

    enter_event = compact_on_destroy.index(
        "AssetTraceEventType::SessionOnDestroyEnter"
    )
    active_state = compact_on_destroy.index(
        "SetFrontendLookupBarrier(", enter_event
    )
    assert enter_event < active_state < outer_original

    original_event = compact_on_destroy.index(
        "AssetTraceEventType::SessionOnDestroyOriginalReturned",
        original_returned,
    )
    original_state = compact_on_destroy.index(
        "SetFrontendLookupBarrier(", original_event
    )
    assert original_returned < original_event < original_state < defer

    _, vfs_reset = _extract_cpp_function(source, "ColdVfsReset")
    compact_vfs_reset = _compact(vfs_reset)
    reset_in_progress = compact_vfs_reset.index(
        "TryBeginFrontendResetPhase(binding.epoch)"
    )
    reset_begin_event = compact_vfs_reset.index(
        "AssetTraceEventType::FrontendSourceResetBegin", reset_in_progress
    )
    reset = compact_vfs_reset.index(
        "PerformFrontendContentReset(", reset_begin_event
    )
    reset_end_event = compact_vfs_reset.index(
        "AssetTraceEventType::FrontendSourceResetEnd", reset
    )
    barrier_complete = compact_vfs_reset.index(
        "SetFrontendLookupBarrier(", reset_end_event
    )
    reset_complete = compact_vfs_reset.index(
        "lifecycle::CompleteReset(&g_frontend_lifecycle", barrier_complete
    )
    assert (
        reset_in_progress < reset_begin_event < reset < reset_end_event
        < barrier_complete < reset_complete
    )

    _, try_begin = _extract_cpp_function(
        source, "TryBeginFrontendResetPhase"
    )
    compact_try_begin = _compact(try_begin)
    assert "FrontendRollbackPhase::OriginalReturned" in try_begin
    assert "(observed&kFrontendLookupFlagsMask)!=0" in compact_try_begin
    assert "FrontendRollbackPhase::ResetInProgress" in try_begin
    assert "_InterlockedCompareExchange64(" in try_begin

    _, reset_function = _extract_cpp_function(
        source, "PerformFrontendContentReset"
    )
    assert "FrontendLookupWasEarly(epoch_id)" in reset_function
    assert "remaining-sources-preserved" in source

    early_sample = compact_vfs_reset.index(
        "FrontendLookupWasEarly(binding.epoch)", reset
    )
    assert reset < early_sample < reset_end_event
    assert "CommitFrontendLookupBarrier" not in vfs_reset

    trace_header = SOURCE_PATH.with_name("CustomMapAssetTrace.h").read_text(
        encoding="utf-8"
    )
    trace_source = SOURCE_PATH.with_name("CustomMapAssetTrace.cpp").read_text(
        encoding="utf-8"
    )
    assert "PublishThenCommit" in trace_header
    assert "PrepareThenPublish" in trace_header
    _, publish_commit = _extract_cpp_function(trace_source, "PublishThenCommit")
    compact_publish_commit = _compact(publish_commit)
    append = compact_publish_commit.index("AppendPayloadLocked(payload)")
    commit = compact_publish_commit.index("commit(context)", append)
    unlock = compact_publish_commit.index("UnlockQueue()", commit)
    assert append < commit < unlock
    _, prepare_publish = _extract_cpp_function(
        trace_source, "PrepareThenPublish"
    )
    compact_prepare_publish = _compact(prepare_publish)
    callback = compact_prepare_publish.index("prepare(context,payload)")
    append = compact_prepare_publish.index(
        "AppendPayloadLocked(*payload)", callback
    )
    unlock = compact_prepare_publish.index("UnlockQueue()", append)
    assert callback < append < unlock


def test_frontend_verification_retries_incomplete_evidence_without_false_failure():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, verify = _extract_cpp_function(source, "VerifyFrontendBaseline")
    compact = _compact(verify)

    incomplete = compact.index("if(!verification_snapshot_complete)")
    source_failure = compact.index("ComponentRuntimeState::RuntimeFailed", incomplete)
    assert "active_sources!=UINT32_MAX" in compact
    assert "unknown_files!=UINT32_MAX" in compact
    assert "g_content_snapshot.dropped_mutations==0" in compact
    assert "content::DroppedMutationCount(&g_content_ledger)==0" in compact
    assert "ComponentRuntimeState::RuntimePartial" in compact[incomplete:source_failure]
    assert "g_last_verified_reset_epoch=epoch_id" not in compact[
        incomplete:source_failure
    ]


def test_frontend_lifecycle_diagnostics_use_bounded_reasons_and_removal_fields():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    reasons, _ = _extract_cpp_function(source, "FrontendBindFailureName")
    for reason in (
        "no_active_level",
        "frontend_level",
        "no_active_epoch",
        "ambiguous_active_epochs",
        "content_name_unavailable",
        "content_name_epoch_mismatch",
        "no_active_sources",
        "shutdown",
        "already_consumed",
    ):
        assert f'"{reason}"' in reasons

    ledger_header = SOURCE_PATH.with_name("CustomContentLedger.h").read_text(
        encoding="utf-8"
    )
    assert "char text0[256];" in ledger_header
    assert "char text1[256];" in ledger_header
    assert "std::uintptr_t object;" in ledger_header
    assert "std::uint64_t value0;" in ledger_header
    assert "std::uint64_t value1;" in ledger_header

    drain, _ = _extract_cpp_function(source, "DrainContentDiagnostics")
    for label in (
        " source=",
        " flags=",
        " add_ordinal=",
        " path=",
        " kind=",
    ):
        assert f'"{label}"' in drain


def test_lazy_epoch_uses_successful_exact_source_provenance_and_exact_loader_exclusion():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    ensure, scrubbed_ensure = _extract_cpp_function(
        source, "EnsureEpochForSuccessfulSource"
    )
    compact_ensure = _compact(scrubbed_ensure)
    assert "!g_update_dlc_depth" in compact_ensure
    assert "IsExactLoaderOwnedSourcePath(normalized_source)" in compact_ensure
    assert "DeriveCustomProjectRoot(normalized_source" in compact_ensure
    assert "if(!exact_source)" in compact_ensure
    assert "content::BeginEpoch(" in ensure
    assert "content_name_available?g_update_content_name" in compact_ensure
    assert 'g_update_content_name : ""' in ensure
    assert "LazyEpochProjectRootConflict" in ensure
    assert "CustomProjectSourceAllowedSameLoaderItem" in ensure

    _, derive = _extract_cpp_function(source, "DeriveCustomProjectRoot")
    assert "g_normalized_loader_project_root" not in derive
    _, loader_exclusion = _extract_cpp_function(
        source, "IsExactLoaderOwnedSourcePath"
    )
    assert "g_normalized_loader_pack_path" in loader_exclusion
    assert "content::PathsEqual" in loader_exclusion

    _, add_path = _extract_cpp_function(source, "HookFsAddPath")
    compact_add = _compact(add_path)
    original = compact_add.index("g_original_fs_add_path(path,flags)")
    ensure_call = compact_add.index("EnsureEpochForSuccessfulSource(")
    record = compact_add.index(
        "RecordSuccessfulSourceAddForEpochDetailed("
    )
    assert original < ensure_call < record
    assert "if(outer_add&&result&&" in compact_add
    assert "object_add_observed" in compact_add
    assert "content::PathsEqual(" in compact_add
    assert "DuplicateActiveKindRecorded" in add_path
    assert "BarrierRejectReason::DuplicateSourceOwnership" in compact_add
    assert "dluma_project_launch_block(" in add_path


def test_null_content_name_binds_unique_complete_epoch_and_original_return_is_unconditional():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    binder, scrubbed_binder = _extract_cpp_function(
        source, "BindFrontendEpochAtOnDestroy"
    )
    compact_binder = _compact(scrubbed_binder)
    assert "content_name_available" in binder
    assert "SelectFrontendResetEpoch(" in binder
    assert "if(!content_name_available)" not in compact_binder
    assert "binding_authority=unique_active_project_root" in binder

    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    compact_destroy = _compact(on_destroy)
    original = compact_destroy.rindex("g_original_session_on_destroy(session)")
    returned_event = compact_destroy.index(
        "SessionOnDestroyOriginalReturned", original
    )
    unbound_return = compact_destroy.index("if(!bound)", returned_event)
    assert original < returned_event < unbound_return
    assert "g_unbound_on_destroy_phase" in on_destroy
    assert "binding.owner_thread_id" in on_destroy


def test_loss_categories_and_multi_receiver_release_are_reported_separately():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    ledger_header = SOURCE_PATH.with_name("CustomContentLedger.h").read_text(
        encoding="utf-8"
    )
    lifecycle_header = SOURCE_PATH.with_name(
        "FrontendSourceRollbackLifecycle.h"
    ).read_text(encoding="utf-8")
    lifecycle_source = SOURCE_PATH.with_name(
        "FrontendSourceRollbackLifecycle.cpp"
    ).read_text(encoding="utf-8")
    for field in (
        "ownership_mutation_loss",
        "diagnostic_observation_loss",
        "irrelevant_without_epoch",
    ):
        assert field in ledger_header
        assert field in source
    for reason in (
        "source_add_contention",
        "source_add_capacity",
        "source_identity_incomplete",
        "epoch_capacity",
        "epoch_conflict",
        "release_capacity",
        "binding_contention",
        "consumption_contention",
    ):
        assert f'"{reason}"' in SOURCE_PATH.with_name(
            "CustomContentLedger.cpp"
        ).read_text(encoding="utf-8")
    assert "kMaximumOwnedReleaseReceivers" in lifecycle_header
    assert "unique_receivers" in lifecycle_header
    assert "release_enter_count" in lifecycle_header
    assert "release_exit_count" in lifecycle_header
    _, nested = _extract_cpp_function(lifecycle_source, "EnterNestedOnDestroy")
    assert "thread_id != barrier->owner_thread_id" in nested
    _, complete = _extract_cpp_function(
        lifecycle_source, "CompleteOnDestroyOwnedUnload"
    )
    assert "release_enter_count != barrier->release_exit_count" in complete
    assert "receiver_overflow" in complete


def test_reset_preflight_is_complete_and_ends_without_engine_mutation():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, preflight = _extract_cpp_function(
        source, "PrepareFrontendContentResetPlan"
    )
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    compact = _compact(preflight)

    epoch_snapshot_index = compact.index("content::SnapshotEpoch(")
    ledger_snapshot_index = compact.index("content::CaptureSnapshot(")
    active_count_index = compact.index(
        "content::CountActiveCustomSourcesThroughEpoch("
    )
    source_snapshot_index = compact.index(
        "content::SnapshotActiveSourcesThroughEpochComplete("
    )

    assert epoch_snapshot_index < ledger_snapshot_index
    assert ledger_snapshot_index < active_count_index
    assert source_snapshot_index > active_count_index
    assert "g_content_snapshot.dropped_mutations!=0" in compact
    assert "source_snapshot.dropped_mutations!=0" in compact
    assert "source_snapshot.count!=expected_source_count" in compact
    assert "source_snapshot.expected_active_count!=expected_source_count" in compact
    assert "content::DroppedMutationCount(" in compact

    # There must be a complete exact-handle preflight pass before the removal
    # pass, so a missing handle cannot produce a partially removed source set.
    exact_handle_index = compact.index("!source.exact_handle_available")
    null_handle_index = compact.index("!source.source")
    ownership_index = compact.index("!source.path_created_ownership")
    destructor_index = compact.index(
        "ResolveSourceDeletingDestructor(source.source)"
    )
    assert source_snapshot_index < exact_handle_index
    assert source_snapshot_index < null_handle_index
    assert source_snapshot_index < ownership_index < destructor_index
    for forbidden in (
        "content::CloseEpochForFrontend(",
        "ConsumeFrontendEpochBinding(",
        "content::MarkEpochConsumed(",
        "RemoveAndDestroyPathCreatedSource(",
        "g_original_fs_remove_object(",
        "g_original_fs_remove_path(",
    ):
        assert forbidden not in preflight
    assert len(_extract_cpp_calls(reset, "DetachPreparedSource")) == 1


def test_reset_checks_stop_and_lifecycle_before_closing_the_epoch():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, preflight = _extract_cpp_function(
        source, "PrepareFrontendContentResetPlan"
    )
    _, vfs_reset = _extract_cpp_function(source, "ColdVfsReset")
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    compact = _compact(reset)
    compact_vfs_reset = _compact(vfs_reset)
    close_index = compact.index("content::CloseEpochForFrontend(")
    compact_preflight = _compact(preflight)
    for gate in (
        "g_dluma.stop_requested",
        "ContentTrackingLive()",
        "g_frontend_reset_stopping",
        "lifecycle::ReadyForReset(&g_frontend_lifecycle)",
    ):
        assert gate in compact_preflight
    gate_index = compact_vfs_reset.index("FrontendResetGatereset_gate")
    marker_index = compact_vfs_reset.index(
        "cold::MarkRegistryMutationStarted("
    )
    perform_index = compact_vfs_reset.index("PerformFrontendContentReset(")
    for gate in (
        "g_dluma.stop_requested",
        "ContentTrackingLive()",
        "g_frontend_reset_stopping",
        "PreparedSourceRegistryIdentityStillStable()",
    ):
        assert gate_index < compact_vfs_reset.index(gate) < marker_index
    assert marker_index < perform_index
    assert compact.index("g_frontend_reset_active") < close_index
    assert "g_original_fs_remove_object(" not in reset
    assert "g_original_fs_remove_path(" not in reset


def test_supported_filesystem_source_mutators_validate_one_registry_srw_lock():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    raw_validation, validation = _extract_cpp_function(
        source, "ValidateFsSourceRegistrySynchronization"
    )
    compact_validation = _compact(validation).lower()

    windows_view = (
        ROOT / "src" / "fixes" / "custom_map_asset_isolation"
        / "FilesystemSourceWindowsView.h"
    ).read_text(encoding="utf-8")
    assert re.search(
        r"kFilesystemSourcesSrwLockRva\s*=\s*0x0*53AD0U",
        windows_view,
        re.IGNORECASE,
    )
    assert "kFsSourceRegistryLockRva" in source

    # fs::add_source(CFSSource*): acquire and release the source registry lock.
    expected = {
        0x000139BA: (0x48, 0x8D, 0x0D, 0x0F, 0x01, 0x04, 0x00),
        0x000139C1: (0xFF, 0x15, 0x21, 0xA8, 0x02, 0x00),
        0x00013B3B: (0x48, 0x8D, 0x0D, 0x8E, 0xFF, 0x03, 0x00),
        0x00013B42: (0xFF, 0x15, 0xA8, 0xA6, 0x02, 0x00),
        # fs::rem_source(path): natural path removal uses the same lock.
        0x00013E96: (0x48, 0x8D, 0x0D, 0x33, 0xFC, 0x03, 0x00),
        0x00013EA5: (0xFF, 0x15, 0x3D, 0xA3, 0x02, 0x00),
        0x00013FE8: (0x48, 0x8D, 0x0D, 0xE1, 0xFA, 0x03, 0x00),
        0x00013FEF: (0xFF, 0x15, 0xFB, 0xA1, 0x02, 0x00),
        # fs::rem_source(CFSSource*): pointer-only membership comparison under
        # that lock, followed by the not-found release path.
        0x00014229: (0x48, 0x8D, 0x0D, 0xA0, 0xF8, 0x03, 0x00),
        0x00014230: (0xFF, 0x15, 0xB2, 0x9F, 0x02, 0x00),
        0x00014250: (0x49, 0x3B, 0x5B, 0xF8),
        0x0001425E: (0x48, 0x8D, 0x0D, 0x6B, 0xF8, 0x03, 0x00),
        0x00014265: (0xFF, 0x15, 0x85, 0x9F, 0x02, 0x00),
    }
    for rva, byte_values in expected.items():
        assert re.search(
            rf"0x0*{rva:x}u", compact_validation, re.IGNORECASE
        ), f"missing exact validation RVA 0x{rva:X}"
        _assert_hex_bytes(raw_validation, byte_values)

    _, plan = _extract_cpp_function(source, "BuildHookPlan")
    assert re.search(
        r"mandatory_ok\s*&=\s*ValidateFsSourceRegistrySynchronization\s*\(\s*\)",
        plan,
    )


def test_runtime_aggregate_status_is_refreshed_in_the_fix_registry():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    registry = REGISTRY_PATH.read_text(encoding="utf-8")
    assert "FixStatus CurrentStatus()" in source
    assert "status.state = AggregateState()" in source
    assert "custom_map_asset_isolation::CurrentStatus()" in registry
    _, verify = _extract_cpp_function(source, "VerifyFrontendBaseline")
    assert "SetAggregateState(" in verify
    compact_verify = _compact(verify)
    assert "verified_epoch.outgoing_level_release_observed" in compact_verify
    assert "outgoing_level_release_observed&&packs_ok" in compact_verify
    assert re.search(
        r"level_resources\s*=\s*level_resources_ok\s*\?",
        verify,
    )
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    assert "SetAggregateState(" in reset


def test_workshop_roots_are_discovered_before_content_hooks_snapshot_them():
    exports = EXPORTS_PATH.read_text(encoding="utf-8")
    _, initialize = _extract_cpp_function(exports, "dluma_player_initialize")
    compact = _compact(initialize)
    discovery = compact.index("dluma_discover_workshop_roots()")
    fixes = compact.index("dluma::fixes::InitializeAllFixes(runtime_context)")
    original = compact.index("g_original_initialize()")
    assert discovery < fixes < original


def test_failed_install_with_retained_routes_is_not_reported_unsupported():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, initialize = _extract_cpp_function(source, "Initialize")
    compact = _compact(initialize)
    failure = compact.index("if(!InstallMandatoryRoutes())")
    tail = compact[failure:]
    assert "retained_routing=AnyInstalledRoutes()" in tail
    assert "g_initialized=retained_routing" in tail
    assert "retained_routing?FixState::ApplyFailed" in tail
    assert "PinLoaderIfNeeded(true)" in tail


def test_atomic_detours_install_one_unified_reset_and_evidence_plan():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, plan = _extract_cpp_function(source, "BuildHookPlan")
    assert "ValidateAtomicRouteMutationSafety()" in plan
    assert "AddRelativeCallRoute(" not in source
    assert "std::memcpy(reinterpret_cast<void*>(address), bytes, size)" not in source
    assert "_InterlockedCompareExchange128(" in source
    _, initialize = _extract_cpp_function(source, "Initialize")
    compact = _compact(initialize)
    assert "options.trace_enabled" not in compact
    watch_validation = compact.index(
        "if(enabled&&!g_watch_list.count)"
    )
    plan_call = compact.index("BuildHookPlan()")
    pin_call = compact.index("PinLoaderIfNeeded(false)")
    install_call = compact.index("InstallMandatoryRoutes()")
    assert watch_validation < plan_call < pin_call < install_call
    assert "if(!BuildHookPlan()||!PrepareAllRelays())" in compact


def test_shutdown_quiesces_observers_before_route_restoration():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, shutdown = _extract_cpp_function(source, "Shutdown")
    compact = _compact(shutdown)
    active = compact.index("WaitForActiveCalls()")
    consumers = compact.index("WaitForConsumerCalls()")
    restore = compact.index("RestoreInstalledRoutes(false)")
    assert active < restore
    assert consumers < restore
    restored = re.search(r"constboolrestored=([^;]+);", compact)
    assert restored is not None
    gates = restored.group(1).split("&&")
    assert gates[-1] == "RestoreInstalledRoutes(false)"
    for required in (
        "quiesced",
        "consumer_quiesced",
        "resource_worker_stopped",
        "xui_inputs_closed",
        "!g_native_resources.gateway.active_calls.load()",
        "!g_native_resources.gateway.pending_calls.load()",
        "!g_native_resources.renderer.active.load()",
        "!g_native_resources.renderer.pending.load()",
    ):
        assert required in gates
    assert "quiesced&&consumer_quiesced&&CloseProductionXuiInputs()" in compact
    assert compact.index("resourceprod::StopInputCapture(") < restore


def test_production_reset_adapters_are_explicitly_unavailable():
    source = SOURCE_PATH.read_text(encoding="utf-8")
    _, initialize = _extract_cpp_function(source, "Initialize")
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    assert "content::Initialize(&g_reset_registry)" in initialize
    assert "content::SealRegistry(&g_reset_registry)" in initialize
    assert "content::RegisterAdapter(" not in initialize
    assert "const audio::CustomAudioResetOperations unsupported_audio = {}" in initialize
    assert "audio::ResetBeforeSourceRemoval(" not in reset
    assert len(_extract_cpp_calls(reset, "DetachPreparedSource")) == 1
    assert "g_source_reset_destructors[index](" not in reset


def test_source_rollback_contract_fingerprint_tracks_post_destroy_transaction_owner():
    """Semantic fingerprint of the proven post-OnDestroy source reset."""
    source = SOURCE_PATH.read_text(encoding="utf-8")
    raw_helper, _ = _extract_cpp_function(
        source, "RemoveAndDestroyPathCreatedSource"
    )
    _, prepare = _extract_cpp_function(
        source, "PrepareFrontendContentResetPlan"
    )
    _, reset = _extract_cpp_function(source, "PerformFrontendContentReset")
    _, on_destroy = _extract_cpp_function(source, "HookSessionOnDestroy")
    _, c10 = _extract_cpp_function(source, "HookDestroyingSessionCallback")
    _, bf8 = _extract_cpp_function(source, "HookDestroyStateAdvance")
    _, post_destroy = _extract_cpp_function(
        source, "ExecuteFrontendResetAtPostC10"
    )
    _, vfs_reset = _extract_cpp_function(source, "ColdVfsReset")
    raw_unload, unload = _extract_cpp_function(source, "HookUnloadLevel")
    _, detach = _extract_cpp_function(source, "DetachPreparedSource")
    _, retire = _extract_cpp_function(source, "ColdVfsRetireProviders")

    proof_surface = prepare + reset
    fingerprint = {
        "cancelled_cleanup_unlink_call_sites": source.count(
            "RemoveAndDestroyPathCreatedSource("
        )
        - 1,
        "loaded_project_detach_dispatches": len(
            _extract_cpp_calls(reset, "DetachPreparedSource")
        ),
        "retirement_owns_detach": len(
            _extract_cpp_calls(detach, "DetachSource")
        ) == 1,
        "retirement_owns_destroy": len(
            _extract_cpp_calls(retire, "DestroyDetachedSource")
        ) == 1,
        "loaded_project_never_destroys_directly": (
            "RemoveAndDestroyPathCreatedSource(" not in reset
            and "g_source_reset_destructors[index](" not in reset
        ),
        "exact_unlink_dispatches": len(
            _extract_cpp_calls(raw_helper, "g_original_fs_remove_object")
        ),
        "proof_predicates": tuple(
            predicate
            for predicate in (
                "frontend_on_destroy_unload_observed",
                "outgoing_level_release_observed",
                "epoch.outgoing_level_release_observed",
                "source.exact_handle_available",
                "source.path_created_ownership",
                "deleting_destructor_preflight_complete",
                "g_content_snapshot.dropped_mutations == 0",
            )
                if predicate in proof_surface
            ),
        "original_before_defer": on_destroy.index(
            "g_original_session_on_destroy(session)"
        ) < on_destroy.index("DeferFrontendSourceResetAtOnDestroy(binding)"),
        "ondestroy_direct_reset_is_isolated": (
            "PerformFrontendContentReset(" not in on_destroy
            and "ConsumeFrontendEpochBinding(" not in on_destroy
        ),
        "post_c10_token_published": (
            on_destroy.index("g_original_session_on_destroy(session)")
            < on_destroy.index("DeferFrontendSourceResetAtOnDestroy(binding)")
            < on_destroy.index("PublishPendingPostC10PreflightToken(")
        ),
        "c10_owns_post_return_transaction": (
            "BeginColdRebootStateFiveScope(" in c10
            and c10.index("g_original_destroying_session_callback(game_di)")
            < c10.index("ConsumePendingPostC10Token(")
            < c10.index("ExecuteFrontendResetAtPostC10(")
            < c10.index(
                "FinishColdRebootStateFiveScope(",
                c10.index("ExecuteFrontendResetAtPostC10("),
            )
            and "PerformFrontendContentReset(" not in c10
        ),
        "post_c10_owns_transaction": (
            post_destroy.index("cold::PrepareColdRebootAtStateFive(")
            < post_destroy.index("cold::EnterColdRebootBarrier(")
        ),
        "bf8_is_observer_only": (
            bf8.count("g_original_destroy_state_advance(game_di)") == 1
            and "PrepareColdRebootAtStateFive(" not in bf8
            and "EnterColdRebootBarrier(" not in bf8
        ),
        "vfs_owns_reset_call": (
            len(_extract_cpp_calls(vfs_reset, "PerformFrontendContentReset"))
            == 1
        ),
        "owned_unload_authority": "kSessionOnDestroyUnloadReturnRva" in unload,
        "after_fade_rejected": (
            "after-fade-rva-0x434DD6-rejected" in raw_unload
        ),
    }
    assert fingerprint == {
        "cancelled_cleanup_unlink_call_sites": 1,
        "loaded_project_detach_dispatches": 1,
        "retirement_owns_detach": True,
        "retirement_owns_destroy": True,
        "loaded_project_never_destroys_directly": True,
        "exact_unlink_dispatches": 1,
        "proof_predicates": (
            "frontend_on_destroy_unload_observed",
            "outgoing_level_release_observed",
            "epoch.outgoing_level_release_observed",
            "source.exact_handle_available",
            "source.path_created_ownership",
            "deleting_destructor_preflight_complete",
            "g_content_snapshot.dropped_mutations == 0",
        ),
        "original_before_defer": True,
        "ondestroy_direct_reset_is_isolated": True,
        "post_c10_token_published": True,
        "c10_owns_post_return_transaction": True,
        "post_c10_owns_transaction": True,
        "bf8_is_observer_only": True,
        "vfs_owns_reset_call": True,
        "owned_unload_authority": True,
        "after_fade_rejected": True,
    }
