from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CUSTOM = ROOT / "src" / "fixes" / "custom_map_asset_isolation"
HEADER = CUSTOM / "PlayerGameplayParamsRestartAdapter.h"
SOURCE = CUSTOM / "PlayerGameplayParamsRestartAdapter.cpp"
HARNESS = ROOT / "tests" / "player_gameplay_params_restart_adapter_fake_harness.cpp"


def _function_definition(source: str, name: str) -> str:
    start = source.index(name)
    opening = source.index("{", start)
    while (terminator := source.find(";", start, opening)) != -1:
        start = source.index(name, terminator + 1)
        opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {name}")


def test_native_player_gameplay_params_restart_adapter_fake_harness():
    subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "tools\\build_player_gameplay_params_restart_adapter_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            str(
                ROOT
                / "build"
                / "player-gameplay-params-restart-fake"
                / "player_gameplay_params_restart_adapter_fake_harness.exe"
            )
        ],
        cwd=ROOT,
        check=True,
    )


def test_adapter_is_registered_as_an_optional_touched_domain():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    assert '#include "PlayerGameplayParamsRestartAdapter.h"' in production
    assert "PlayerGameplayParamsRestartAdapter.cpp" in project
    assert "PlayerGameplayParamsRestartAdapter.h" in project
    assert "MakePlayerGameplayParamsContentDomainAdapter" in production
    assert "production_player_snapshot_operations" in production
    assert "ReadProductionPlayerRestartEnvironment" in production
    assert "SnapshotProductionPlayerState" in production
    assert "production_player_mutation_operations" in production
    assert "ProductionResetGameplayParamsCache" in production
    assert "ProductionInitializePlayerStateVariables" in production
    assert "PrepareProductionPlayerB0SnapshotContext" in production
    assert "PublishProductionPlayerB0ToFrontendBaseline" in production
    assert "AbandonPlayerGameplayParamsBaseline" in production
    assert "disabled_player_mutation_operations" not in production
    assert re.search(
        r"MakePlayerGameplayParamsContentDomainAdapter\([^;]+false\)",
        production,
        re.DOTALL,
    )


def test_production_wiring_uses_only_exact_validated_player_targets():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    assert re.search(
        r"kRemoveCachedPlayerLevelPresetsRva\s*=\s*0x0045AC60U",
        production,
    )
    assert re.search(
        r"kPlayerStateInitializeVariablesRva\s*=\s*0x00BB86E0U",
        production,
    )
    for symbol in (
        "g_player_cache_reset_mutation_target_validated",
        "g_player_initialize_variables_mutation_target_validated",
        "ValidateProductionPlayerMutationTargets",
        "ProductionResetGameplayParamsCache",
        "ProductionInitializePlayerStateVariables",
    ):
        assert symbol in production

    validator = _function_definition(
        production, "bool ValidateProductionPlayerMutationTargets("
    )
    assert "g_gamedll_full_profile_valid" in validator
    assert "kRemoveCachedPlayerLevelPresetsRva" in validator
    assert "kPlayerStateInitializeVariablesRva" in validator
    assert validator.count("BytesEqual(") >= 2
    assert validator.count("ValidateRelativeCallSite(") >= 2
    assert "0x00BB84CDU" in validator
    assert "0x00BB8465U" in validator

    reset = _function_definition(
        production, "void ProductionResetGameplayParamsCache("
    )
    initialize = _function_definition(
        production, "void ProductionInitializePlayerStateVariables("
    )
    assert reset.count("g_original_remove_cached_player_level_presets(") == 1
    assert initialize.count(
        "g_original_player_state_initialize_variables("
    ) == 1
    assert "g_player_cache_reset_mutation_target_validated" in reset
    assert "g_player_initialize_variables_mutation_target_validated" in initialize
    for callback in (reset, initialize):
        assert "GetCurrentThreadId()" in callback or (
            "ProductionPlayerMutation" in callback
        )
        for forbidden in (
            "HookRemoveCachedPlayerLevelPresets(",
            "HookPlayerStateInitializeVariables(",
            "g_original_player_state_clear(",
            "g_original_player_state_remove_cached_presets(",
            "g_original_reload_player_scripts(",
            "g_original_local_client_recreate_representation(",
            "g_original_player_fpp_get_default_fov(",
            "g_original_player_fpp_get_camera_default_fov(",
        ):
            assert forbidden not in callback

    mutation_table = production[
        production.index("production_player_mutation_operations") :
        production.index(
            "InitializePlayerGameplayParamsRestartAdapter(",
            production.index("production_player_mutation_operations"),
        )
    ]
    assert "&ProductionResetGameplayParamsCache" in mutation_table
    assert "&ProductionInitializePlayerStateVariables" in mutation_table
    assert "g_player_cache_reset_mutation_target_validated" in mutation_table
    assert "g_player_initialize_variables_mutation_target_validated" in mutation_table
    assert "production_enabled" in mutation_table


def test_player_appearance_provider_admission_supports_full_paths_and_custom_before():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    open_hook = _function_definition(
        production, "void* __fastcall HookFsOpen("
    )
    snapshot = _function_definition(
        production, "bool SnapshotProductionPlayerState("
    )
    adapter = SOURCE.read_text(encoding="utf-8")
    assert open_hook.count("IsPlayerAppearancePerksPath(targeted_logical)") >= 2
    appearance_complete = snapshot[
        snapshot.index("output->appearance_complete") :
        snapshot.index("return semantic_complete")
    ]
    assert "appearance_fingerprint != 0" in appearance_complete
    assert "appearance_provider.exact_source" not in appearance_complete
    assert "ResolveLiveProductionAppearanceProvider(" in snapshot
    assert re.search(
        r"RestartSnapshotComplete\(\s*planned\.before,[\s\S]*?false,",
        adapter,
    )
    assert re.search(
        r"RestartSnapshotComplete\(\s*snapshot,[\s\S]*?true,",
        adapter,
    )


def test_production_player_snapshot_does_not_require_optional_progression_hooks():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    prepare = _function_definition(
        production, "bool PrepareProductionPlayerB0SnapshotContext("
    )
    snapshot = _function_definition(
        production, "bool SnapshotProductionPlayerState("
    )
    initialize = _function_definition(
        production, "bool InitializeColdRebootScaffold("
    )
    assert "g_player_state_inventory_trace_available" in prepare
    assert "g_player_progression_trace_available" not in prepare
    assert "cache_append_count" not in prepare
    assert "definition_vector_count" not in prepare
    assert "CapturePlayerStateRows(" in snapshot
    assert "FindDefinitionVectorInGameplayCache(" in snapshot
    assert "if (!record ||" not in snapshot
    assert "(void)ResolveLiveProductionPlayerProvider(" in snapshot
    assert re.search(
        r"production_player_snapshot_operations\s*=\s*\{[\s\S]*?"
        r"true\s*,\s*true\s*,\s*true\s*,\s*"
        r"&ObserveProductionPostDestroySemanticPass\s*\}",
        initialize,
    )
    assert "ObservePostDestroySemanticPassFn" in (
        CUSTOM / "PlayerGameplayParamsRestartAdapter.h"
    ).read_text(encoding="utf-8")


def test_windows_definition_failure_is_not_labeled_as_row_table_failure():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    snapshot = _function_definition(
        production, "bool SnapshotProductionPlayerState("
    )
    classifier = _function_definition(
        production, "ClassifyProductionWindowsSnapshotFailure("
    )
    reason = _function_definition(
        production, "void BuildPlayerAudioB0PermanentFailureReason("
    )
    assert "WindowsSnapshotReasonDefinitionTraversalIncomplete" in classifier
    assert "RowDefinitionTraversalIncomplete" in classifier
    assert "FirstIncompletePlayerDefinitionRow" in snapshot
    assert "rows_before.row_table_readable" in snapshot
    assert "RowDefinitionTraversalIncomplete" in snapshot
    for field in (
        '":level="',
        '":property="',
        '":property_id="',
        '":level_stride="',
        '":property_stride="',
        '":traversal_reason="',
    ):
        assert field in reason


def test_authenticated_player_replay_does_not_self_invalidate_touch_window():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    retaining = production[
        production.index("class RetainingObservationCall {"):
        production.index("class ActiveConsumerCall {")
    ]
    assert "g_player_reset_replay_depth" not in retaining
    assert "CurrentRegistryReplayPermit" in retaining
    assert "replay_active && observed && observed->domain == domain" in retaining
    assert "BeginRetainingReplayObservation" in retaining
    assert "replay_permit.operations" not in retaining


def test_engine_mutations_are_only_injected_exact_operations():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    joined = source + header
    assert source.count(
        "adapter->mutation_operations.reset_global_cache("
    ) == 1
    assert source.count(
        "adapter->mutation_operations.initialize_variables("
    ) == 1
    assert "PlayerGameplayParamsSnapshotOperations" in header
    assert "PlayerGameplayParamsMutationOperations" in header
    assert "source_registry_generation_observed" in header
    assert "ResetGameplayParamsCacheFn" in header
    assert "InitializePlayerStateVariablesFn" in header
    assert "global_cache_reset_route_validated" in header
    assert "initialize_variables_route_validated" in header
    for forbidden in (
        "PlayerState::Clear(",
        "RemovePrecachedLevelPresets(",
        "ReloadPlayerScripts(",
        "RecreateRepresentation(",
        "reinterpret_cast<Reset",
        "reinterpret_cast<Initialize",
        "fs::refresh(",
        "fs::shutdown(",
    ):
        assert forbidden not in joined


def test_read_only_baseline_contract_is_independent_of_mutation_routes():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    capture = source[
        source.index("DomainResult CapturePlayerGameplayParamsBaseline(") :
        source.index("DomainResult PreflightPlayerGameplayParamsRestart(")
    ]
    preflight = source[
        source.index("DomainResult PreflightPlayerGameplayParamsRestart(") :
        source.index("DomainResult QuiescePlayerGameplayParamsRestart(")
    ]
    assert "EnvironmentOperationsComplete(adapter)" in capture
    assert "StateSnapshotOperationsComplete(adapter)" in capture
    assert capture.index("EnvironmentOperationsComplete(adapter)") < capture.index(
        "live_before_count == 0U"
    )
    assert capture.index("StateSnapshotOperationsComplete(adapter)") > capture.index(
        "live_before_count == 0U"
    )
    assert "snapshot_operations.read_environment" in source
    assert "snapshot_operations.snapshot_state" in capture
    assert "mutation_operations" not in capture
    assert "MutationOperationsComplete(adapter)" in preflight
    assert "MutationRoutesValidated(adapter)" in preflight
    assert "MutationProductionEnabled(adapter)" in preflight
    assert "MutationProductionDisabled" in header
    assert "provider.source_registry_generation_observed" in source


def test_post_destroy_semantic_reads_are_two_pass_and_session_independent():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    windows_header = (
        CUSTOM / "PlayerGameplayParamsWindowsSnapshot.h"
    ).read_text(encoding="utf-8")
    windows_source = (
        CUSTOM / "PlayerGameplayParamsWindowsSnapshot.cpp"
    ).read_text(encoding="utf-8")

    for purpose in (
        "StartupSemanticBaseline",
        "PostDestroySemanticAssessment",
        "RestartMutationPreflight",
        "RestartMutationVerify",
    ):
        assert purpose in header
    assert re.search(
        r"ReadPlayerRestartEnvironmentFn\s*=\s*bool \(\*\)\(\s*"
        r"void\* user,\s*PlayerEnvironmentReadPurpose purpose,",
        header,
    )

    assessment = _function_definition(
        source,
        "PlayerGameplayParamsTouchAssessment "
        "AssessPlayerGameplayParamsTouch(",
    )
    first_environment = assessment.index(
        "CapturePostDestroyEnvironmentPass("
    )
    first_states = assessment.index(
        "CapturePostDestroyStatePass(", first_environment
    )
    second_environment = assessment.index(
        "CapturePostDestroyEnvironmentPass(", first_states
    )
    second_states = assessment.index(
        "CapturePostDestroyStatePass(", second_environment
    )
    assert first_environment < first_states < second_environment < second_states
    assert "StableEnvironmentPair(" in assessment
    assert "SameInventoryOwnerSet(" in assessment
    assert "SnapshotSemanticEqual(" in assessment
    assert "OwnerSetMatchesSemanticBaseline(" in assessment

    callback = _function_definition(
        production, "bool ReadProductionPlayerRestartEnvironment("
    )
    assert "PlayerEnvironmentReadPurpose purpose" in callback
    assert "PostDestroyProcessGlobals" in callback
    assert "read_session_local_restart_owner" in callback
    assert re.search(
        r"read_session_local_restart_owner\s*=\s*"
        r"purpose\s*!=\s*[\s\S]*?PostDestroySemanticAssessment",
        callback,
    )
    assert re.search(
        r"if \(read_session_local_restart_owner &&\s*"
        r"!ReadStableLocalRepresentationState\(",
        callback,
    )
    assert "FrozenPlayerOwnerSetComplete(" in callback
    assert callback.count("ReadGameplayCacheShape(") >= 2
    assert "playerstate::CurrentRevision(&g_player_state_ledger)" in callback
    assert "output->current_thread_id = GetCurrentThreadId();" in callback
    assert "startup_b0_thread_id" not in callback
    assert "prepared_thread_id" not in callback

    assert "PostDestroyProcessGlobals" in windows_header
    assert "SessionAware" in windows_header
    post_destroy_branch = windows_source[
        windows_source.index(
            "PlayerGameplayParamsWindowsEnvironmentReadMode::"
        ) :
        windows_source.index(
            "bool SnapshotPlayerGameplayParamsWindowsState("
        )
    ]
    assert "Never dereference" in post_destroy_branch
    assert "kWindowsSessionActiveLevelOffset" in post_destroy_branch
    assert re.search(
        r"mode\s*==\s*[\s\S]*?SessionAware[\s\S]*?"
        r"kWindowsSessionActiveLevelOffset",
        post_destroy_branch,
    )


def test_runtime16_post_destroy_thread_authority_is_lifecycle_local_and_exact():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    harness = HARNESS.read_text(encoding="utf-8")
    for token in (
        "PlayerThreadAuthorityKind",
        "StartupB0CaptureOwner",
        "C10SessionOwner",
        "C10PostReturnOwner",
        "RestartTransactionOwner",
        "environment_observed_thread",
        "environment_expected_thread",
        "historical_b0_thread_id",
        "c08_callback_thread_id",
        "current_c10_thread_id",
        "current_session_scope_thread_id",
        "post_c10_observed_thread_id",
        "pending_post_c10_token_generation",
        "thread_match",
        "baseline_owner_thread_id",
    ):
        assert token in header
    authority = _function_definition(
        source, "bool PostDestroyThreadAuthorityMatches("
    )
    assert "authority->expected_thread_id != context->owner_thread_id" in authority
    assert "environment.current_thread_id !=" in authority
    assert "authority->exact_scope_active" in authority
    assert "authority->current_c10_thread_id ==" in authority
    assert "authority->current_session_scope_thread_id ==" in authority
    assert "authority->post_c10_observed_thread_id ==" in authority
    assert "authority->pending_post_c10_token_generation != 0" in authority

    boundary = _function_definition(
        production,
        "AssessPlayerSnapshotTouchAtResetBoundary(",
    )
    assert "PlayerThreadAuthorityKind::C10PostReturnOwner" in boundary
    assert "scope.c08_callback_thread_id" in boundary
    assert "scope.c10_callback_thread_id" in boundary
    assert "binding.owner_thread_id" in boundary
    assert "scope.post_c10_token_publication_sequence" in boundary
    assert "scope.post_c10_token_consumed" in boundary
    assert "FrontendEpochBindingsEqual(" in boundary
    assert "baseline_owner_thread_id" in boundary
    assert "&authority" in boundary

    state_callback = _function_definition(
        production, "bool SnapshotProductionPlayerState("
    )
    assert "startup_b0_thread_id" not in state_callback
    assert "prepared_thread_id" not in state_callback
    for report_field in (
        "post_destroy_player_thread_authority_kind",
        "post_destroy_player_historical_b0_thread",
        "post_destroy_player_c08_callback_thread",
        "post_destroy_player_current_c10_thread",
        "post_destroy_player_current_session_scope_thread",
        "post_destroy_player_post_c10_observed_thread",
        "post_destroy_player_pending_post_c10_token_generation",
        "_environment_observed_thread",
        "_environment_expected_thread",
        "_thread_match",
    ):
        assert report_field in production
    assert "TestRuntime16LifecycleLocalThreadAuthority" in harness
    assert "kB0Thread = 29600U" in harness
    assert "kExitThread = 33524U" in harness
    assert "kWindowThread = 11111U" in harness


def test_post_destroy_diagnostics_are_bounded_pod_and_exact():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    for structure in (
        "PlayerPostDestroyEnvironmentCaptureDiagnostics",
        "PlayerPostDestroyRowCaptureDiagnostics",
        "PlayerPostDestroyStateCaptureDiagnostics",
        "PlayerPostDestroyCaptureDiagnostics",
    ):
        assert f"struct {structure}" in header
    assert (
        "states[kPlayerStateInventoryCapacity]" in
        header[header.index("struct PlayerPostDestroyCaptureDiagnostics") :]
    )
    for failure in (
        "EnvironmentCallbackRejected",
        "GameplayContainerUnreadable",
        "GameDiUnreadable",
        "GlobalOwnerSlotsUnreadable",
        "InventoryScanIncomplete",
        "CacheFingerprintUnavailable",
        "ActiveSessionStillPresent",
        "ActiveLevelStillPresent",
        "LocalPlayerStillPresent",
        "LiveGameplayConsumerStillPresent",
        "OwnerSetChangedBetweenPasses",
        "StateSnapshotCallbackRejected",
        "StateSnapshotSemanticIncomplete",
        "StateChangedBetweenPasses",
    ):
        assert failure in header
        assert failure in source
    for forbidden in ("std::vector", "std::string", "new ", "malloc("):
        assert forbidden not in (
            header[
                header.index("struct PlayerPostDestroyCaptureDiagnostics") :
                header.index("void InitializePlayerGameplayParamsRestartAdapter")
            ]
            + source[
                source.index("void ResetPostDestroyDiagnostics") :
                source.index("DomainResult PreflightPlayerGameplayParamsRestart")
            ]
        )


def test_baseline_abandon_is_locked_local_bookkeeping_only():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    assert "bool AbandonPlayerGameplayParamsBaseline(" in header
    abandon = source[
        source.index("bool AbandonPlayerGameplayParamsBaseline(") :
        source.index(
            "bool PlayerGameplayParamsBaselineEvidenceStillCurrent("
        )
    ]
    assert "TryLock phase(&adapter->phase_lock)" in abandon
    assert "if (!phase.locked()) return false;" in abandon
    for refusal in (
        "runtime_failure_latched",
        "transaction_active",
        "preflight_complete",
        "quiesce_complete",
        "mutation_started",
        "mutation_in_progress",
    ):
        assert refusal in abandon
    assert "std::memset(adapter->baseline_states, 0" in abandon
    assert "adapter->baseline_state_count = 0" in abandon
    assert "adapter->baseline_inventory_revision = 0" in abandon
    assert "adapter->baseline_generation = 0" in abandon
    assert "adapter->baseline_captured = false" in abandon
    for forbidden in (
        "snapshot_operations",
        "mutation_operations",
        "operations_user",
        "inventory_lock",
        "inventory_count",
        "adapter->inventory_revision",
        "ClearPreparedTransaction",
        "ReadEnvironment(",
        "snapshot_state(",
        "reset_global_cache(",
        "initialize_variables(",
    ):
        assert forbidden not in abandon


def test_adapter_is_fixed_capacity_no_exception_and_no_hook_side_io():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    joined = source + header
    assert "kPlayerStateInventoryCapacity = 64" in header
    assert "inventory[kPlayerStateInventoryCapacity]" in header
    assert "baseline_states[kPlayerStateInventoryCapacity]" in header
    assert "plan[kPlayerStateInventoryCapacity]" in header
    assert "#define _HAS_EXCEPTIONS 0" in source
    assert joined.count("noexcept") >= 20
    for forbidden in (
        "std::vector",
        "std::string",
        "std::map",
        "std::unordered",
        "new ",
        "delete ",
        "malloc(",
        "realloc(",
        "fopen(",
        "WriteFile(",
        "printf(",
        "throw ",
        "try {",
        "catch (",
    ):
        assert forbidden not in joined


def test_generation_safe_inventory_and_fail_closed_preflight_contract():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    assert "record.generation = ++adapter->next_state_generation" in source
    assert "found->live = false" in source
    assert "record.generation == adapter->plan[planned].generation" in source
    assert "FindLivePlayerStateGeneration" in header
    assert "FindLivePlayerStateGeneration" in source
    assert "coverage_started_before_first_allocation" in header
    assert "complete_destructor_route_validated" in header
    assert "inventory_loss_latched" in header
    assert "inventory_overflow_latched" in header
    assert "CancelPreparedPlayerGameplayParamsRestart" in header
    assert "completed_transaction_generation" in header
    assert "transaction_baseline_generation" in header
    assert "transaction_content_epoch" in header
    assert "current_thread_id" in header
    for required in (
        "LiveGameplayConsumer",
        "SessionStillActive",
        "ShutdownOrQuiescing",
        "StateSnapshotIncomplete",
        "GlobalOwnerMismatch",
        "BaselineStateMissing",
        "InventoryChanged",
    ):
        assert required in header


def test_startup_inventory_bootstrap_requires_complete_exact_evidence():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    exports = (ROOT / "src" / "dlw_exports.c").read_text(encoding="utf-8")
    for field in (
        "supported_image",
        "before_original_game_script_initializer",
        "closed_creator_set_validated",
        "constructor_route_installed",
        "complete_destructor_route_installed",
        "before_global_slots_readable",
        "after_global_slots_readable",
        "before_local_process_global_state",
        "before_character_manager_global_state",
        "after_local_process_global_state",
        "after_character_manager_global_state",
        "constructor_active_calls",
        "complete_destructor_active_calls",
    ):
        assert field in header
        assert field in source or field in production
    assert "BeginPlayerStateInventoryCoverageFromBootstrap" in header
    assert "BeginPlayerStateInventoryCoverageFromBootstrap" in source
    assert "PrecoverageObservationLoss" in header
    assert "ExistingGlobalState" in header

    initializer = exports[
        exports.index("static void* dluma_player_initialize(void)") :
        exports.index("static __int64 dluma_player_shutdown(void)")
    ]
    initialize = initializer.index("dluma::fixes::InitializeAllFixes")
    original = initializer.index("manager = g_original_initialize()")
    assert initialize < original


def test_supported_image_validates_creator_sites_and_proves_startup_closed_set():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    expected_constants = {
        "kGameDiLocalPlayerStateCreateCallRva": "0x003BFECAU",
        "kGameDiCharacterPlayerStateCreateCallRva": "0x003C0045U",
        "kPlayerDiPlayerStateCreateCallRva": "0x00B06262U",
        "kTutorialPlayerStateCreateCallRva": "0x00C167D4U",
        "kPlayerStateRttiFactoryRva": "0x00BBF2A0U",
        "kPlayerStateRttiObjectRva": "0x01D889B0U",
        "kPlayerStateRttiFactorySlot0Rva": "0x017E37F0U",
        "kPlayerStateRttiFactorySlot1Rva": "0x017E37F8U",
    }
    for name, value in expected_constants.items():
        assert re.search(
            rf"constexpr\s+std::uintptr_t\s+{name}\s*=\s*{value};",
            production,
        )
    assert "ValidateKnownPlayerStateCreatorSites" in production
    assert "kPlayerStateRttiObjectRva" in production
    assert "Generic/name-based RTTI creation remains" in production
    # The standalone creator-site check remains non-authoritative.  Startup
    # evidence is initialized fail-closed and promoted only by the independent
    # exact engine-callback proof.
    assert (
        "closed_creator_set_validated =\n        false;"
        in production
    )
    assert '#include "PlayerStateInventoryBootstrapProof.h"' in production
    assert "ProvePlayerStateInventoryBootstrap(" in production
    assert (
        "if (player_inventory_bootstrap_proved)"
        in production
    )
    assert (
        "g_player_inventory_bootstrap_evidence =\n"
        "            player_inventory_proof_result.evidence;"
        in production
    )
    hook_plan = production[
        production.index("bool BuildHookPlan()") :
        production.index("bool PrepareAllRelays()")
    ]
    assert hook_plan.index("ValidateKnownPlayerStateCreatorSites()") < (
        hook_plan.index("BuildMaterialAndPlayerObservationPlans()")
    )


def test_minimal_playerstate_inventory_routes_are_an_independent_optional_group():
    production = (CUSTOM / "CustomMapAssetIsolationFix.cpp").read_text(
        encoding="utf-8"
    )
    progression = production[
        production.index("bool IsPlayerProgressionHookStatus(") :
        production.index("void DisablePlayerProgressionHookPlan()")
    ]
    inventory = production[
        production.index("bool IsPlayerStateInventoryHookStatus(") :
        production.index("void DisablePlayerStateInventoryHookPlan()")
    ]
    install = production[
        production.index("void InstallOptionalRoutes()") :
        production.index("void AppendIdentity(")
    ]
    prepare = production[
        production.index("bool PrepareAllRelays()") :
        production.index("bool InstallMandatoryRoutes()")
    ]
    build = production[
        production.index("void BuildMaterialAndPlayerObservationPlans()") :
        production.index("bool AddVtableHook(")
    ]
    inventory_plan = build[
        build.index("bool player_inventory_plan_ok") :
        build.index("bool player_plan_ok")
    ]
    progression_plan = build[
        build.index("bool player_plan_ok") :
        build.index(
            "const std::uintptr_t igs_init_object_iat",
            build.index("bool player_plan_ok"),
        )
    ]

    inventory_statuses = (
        "g_player_state_constructor_status",
        "g_player_state_complete_destructor_status",
        "g_player_di_on_create_status",
        "g_player_di_on_destroy_status",
        "g_tutorial_player_di_on_create_status",
    )
    for status in inventory_statuses:
        assert status in inventory
        assert status not in progression
        assert status in inventory_plan
        assert status not in progression_plan

    assert "bool player_inventory_failed = false;" in install
    assert "IsPlayerStateInventoryHookStatus(route->owner)" in install
    assert "DisablePlayerStateInventoryHookPlan();" in install
    assert "IsPlayerStateInventoryHookStatus(route->owner)" in prepare
    assert "DisablePlayerStateInventoryHookPlan();" in prepare
    assert "g_player_state_inventory_trace_available" in install
    progression_availability = install[
        install.index("g_player_progression_trace_available =") :
        install.index("g_default_levels_trace_available =")
    ]
    for status in inventory_statuses:
        assert status not in progression_availability
    assert (
        "player_state_inventory_observer=classwide-routes-installed"
        in production
    )
    assert (
        "player_state_preinstall_completeness=engine-callback-proof"
        in production
    )
    assert "player_state_preinstall_completeness=unresolved" in production


def test_required_property_set_and_preservation_invariants_are_explicit():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    assert "kRequiredPlayerPropertyCount = 15" in header
    for property_id in (
        103,
        209,
        496,
        625,
        702,
        706,
        707,
        713,
        714,
        715,
        779,
        823,
        780,
        817,
        818,
    ):
        assert re.search(rf"=\s*{property_id};", header)
    for field in (
        "progression_selector_fingerprint",
        "class_selector_fingerprint",
        "earned_experience",
        "current_level",
        "player_variables",
        "skill_membership_fingerprint",
        "regular_buff_fingerprint",
        "timed_buff_fingerprint",
        "inventory_fingerprint",
        "equipment_fingerprint",
        "quest_fingerprint",
    ):
        assert field in header
        assert field in source


def test_reset_and_all_reinitializations_share_one_synchronous_callback():
    source = SOURCE.read_text(encoding="utf-8")
    reset = source[source.index("DomainResult ResetPlayerGameplayParams(") :]
    reset = reset[: reset.index("DomainResult ReloadPlayerGameplayParams(")]
    reset_call = reset.index(
        "adapter->mutation_operations.reset_global_cache("
    )
    initialize_loop = reset.index(
        "for (std::uint32_t index = 0; index < adapter->plan_count; ++index)",
        reset_call,
    )
    initialize_call = reset.index(
        "adapter->mutation_operations.initialize_variables(",
        initialize_loop,
    )
    first_post_reset_read = reset.index(
        "PlayerRestartEnvironment after_environment", initialize_call
    )
    first_post_reset_snapshot = reset.index(
        "PlayerStateRestartSnapshot snapshot", first_post_reset_read
    )
    assert reset_call < initialize_loop < initialize_call
    assert initialize_call < first_post_reset_read < first_post_reset_snapshot
    assert "transaction_global_reset_calls != 1U" in reset
    assert "transaction_initialize_calls != adapter->plan_count" in reset
    assert "mutation_started" in reset


def test_postconditions_require_explicit_membership_base_providers_and_invariants():
    source = SOURCE.read_text(encoding="utf-8")
    assert "after_environment.cache_generation <=" in source
    assert "actual.cache_generation != new_cache_generation" in source
    assert (
        "actual.cache_membership_evidence !=\n"
        "                 PlayerCacheMembershipEvidence::ProvenMember"
    ) in source
    assert "!actual.cache_observer_available" in source
    assert "!actual.cache_inventory_complete" in source
    assert "!actual.cache_generation_observed" in source
    assert "ProviderEqual(expected.provider, actual.provider)" in source
    assert "ProviderEqual(expected->provider, actual->provider)" in source
    assert "PreservationEqual(planned->before, snapshot)" in source
    assert "baseline.appearance_fingerprint != after.appearance_fingerprint" in source


def test_fake_harness_covers_success_and_mandatory_negative_cases():
    harness = HARNESS.read_text(encoding="utf-8")
    for case in (
        "TestSnapshotOnlyBaselineDoesNotRequireMutationOperations",
        "TestProductionDisabledMutationRunsNoCallbacks",
        "TestSnapshotStableWindowMismatchIsRejectedAndRetryable",
        "TestDynamicPlayerDiStateParticipatesInCompleteBaseline",
        "TestSemanticBaselineRejectsMissingDataButAcceptsRestartOnlyGaps",
        "TestRuntime12SemanticMembershipEvidenceSplit",
        "TestInventoryRevisionChangeDuringBaselineIsRejectedAndRetryable",
        "TestVerifiedRestartIsOneSynchronousMutation",
        "TestRegistryBackedTouchedPlayerTransactionReachesVerified",
        "TestPlayerTouchAssessmentUsesDirectB1VsB0WithoutProgressionHooks",
        "TestRuntime14PostDestroySemanticCaptureContract",
        "TestRuntime16LifecycleLocalThreadAuthority",
        "TestIncompleteCurrentPlayerSnapshotIsNotUntouched",
        "TestPointerReuseGetsNewGeneration",
        "TestExactStartupBootstrapAuthorizesCoverage",
        "TestIncompleteStartupBootstrapFailsClosed",
        "TestCoverageAndInventoryFailuresArePreMutation",
        "TestInventoryOverflowIsBoundedAndLatched",
        "TestLiveConsumerAndIncompleteStateFailPreflight",
        "TestRequiredPropertySetIsExact",
        "TestInventoryChangeAfterQuiescePreventsMutation",
        "TestPreparedTransactionCanCancelOrBeSuperseded",
        "TestCompletedGenerationCannotMutateAgain",
        "TestThreadAndSelectorIdentityArePreflightRequirements",
        "TestPostMutationProviderAndInvariantFailuresLatch",
        "TestPhaseLockContentionIsSideEffectFree",
        "TestReadOnlyBaselineCanBeAbandonedBeforePreflight",
        "TestBaselineAbandonRefusesActiveOrFailedPhases",
        "TestSecondResetInvocationDoesNotRepeatOperations",
    ):
        assert case in harness


def test_phase_lock_contention_does_not_mutate_transaction_state():
    source = SOURCE.read_text(encoding="utf-8")
    assert source.count(
        "if (!phase.locked()) return DomainResult::Failed;"
    ) == 6
    cancel = source[
        source.index("bool CancelPreparedPlayerGameplayParamsRestart(") :
        source.index("DomainResult CapturePlayerGameplayParamsBaseline(")
    ]
    assert "if (!phase.locked()) return false;" in cancel
    assert "SetFailure(" not in cancel
    assert "Fail(" not in cancel


def test_prepared_cancellation_is_bookkeeping_only_and_registry_paired():
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    cancel = source[
        source.index("bool CancelPreparedPlayerGameplayParamsRestart(") :
        source.index("DomainResult CapturePlayerGameplayParamsBaseline(")
    ]
    assert "ContextMatches(adapter, context)" in cancel
    assert "adapter->mutation_started" in cancel
    assert "ClearPreparedTransaction(adapter)" in cancel
    assert "ReadEnvironment(" not in cancel
    assert "snapshot_state" not in cancel
    assert "CancelPreparedRegistryTransaction" in header
    assert "no engine reads" in header
    assert (
        "definition.cancel_quiesce =\n"
        "        &CancelQuiescedPlayerGameplayParamsRestart;"
    ) in source


def test_live_generation_lookup_is_locked_read_only_and_reuse_safe():
    source = SOURCE.read_text(encoding="utf-8")
    lookup = source[
        source.index("std::uint64_t FindLivePlayerStateGeneration(") :
        source.index("void MarkPlayerStateInventoryLoss(")
    ]
    assert "TryLock lock(&adapter->inventory_lock)" in lookup
    assert "record.live" in lookup
    assert "record.pointer != player_state" in lookup
    assert "generation = record.generation" in lookup
    assert "inventory_loss_latched" not in lookup
    assert "inventory_overflow_latched" not in lookup
    for mutator in (
        "SetFailure(",
        "MarkPlayerStateInventoryLoss(",
        "inventory_revision",
        "observation_sequence",
        "= true",
        "= false",
    ):
        assert mutator not in lookup
