from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = (
    ROOT
    / "src"
    / "fixes"
    / "custom_map_asset_isolation"
    / "CustomMapAssetIsolationFix.cpp"
)
TIMELINE_HEADER = PRODUCTION.with_name(
    "PlayerPresetRowPublicationTimeline.h"
)


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def test_runtime19_exact_windows_writer_targets_and_callsites():
    source = PRODUCTION.read_text(encoding="utf-8")
    for name, value in {
        "kLocalClientCreateRepresentationRva": "0x0040B970U",
        "kCachePlayerLevelPresetRva": "0x0045A920U",
        "kFindPlayerLevelPresetRva": "0x0045AB80U",
        "kRemoveCachedPlayerLevelPresetsRva": "0x0045AC60U",
        "kPlayerDiOnCreateRva": "0x00B06110U",
        "kPlayerStateSetProgressTemplateRva": "0x00BB82D0U",
        "kPlayerStateRemoveCachedPresetsRva": "0x00BB84A0U",
        "kPlayerStateInitializeVariablesRva": "0x00BB86E0U",
        "kPlayerStateSetVariablesToLevelRva": "0x00BBD2A0U",
        "kIgsObjectInitObjectRva": "0x00277B80U",
        "kCreateRepresentationSetProgressTemplateCallRva": "0x0040BC4CU",
        "kCreateRepresentationSetProgressTemplateReturnRva": "0x0040BC51U",
        "kCreateRepresentationInitObjectCallRva": "0x0040BF40U",
        "kCreateRepresentationInitObjectReturnRva": "0x0040BF46U",
        "kSetProgressTemplateInitializeVariablesCallRva": "0x00BB8465U",
        "kInitializeVariablesFindPresetCallRva": "0x00BB873BU",
    }.items():
        assert f"{name} = {value};" in source
    plan = section(
        source,
        "void BuildMaterialAndPlayerObservationPlans()",
        "bool AddVtableHook(",
    )
    for token in (
        "g_gamedll_full_profile_valid",
        "g_engine_full_profile_valid",
        "create_representation_prefix",
        "igs_init_object_prefix",
        "ValidateRelativeCallSite(",
        "ValidateIndirectCallSite(",
        "kIgsObjectInitObjectIatRva",
        "FindIatSlot(",
        "DisablePlayerWriterChainHookPlan()",
        "&HookLocalClientCreateRepresentation",
        "&HookIgsObjectInitObject",
    ):
        assert token in plan
    assert (
        "ObserverRoute::\n                RowPublicationStore"
        in plan
    )
    assert "ExactFailure::\n                CallSiteMismatch" in plan


def test_runtime19_writer_events_are_ordered_around_original_calls():
    source = PRODUCTION.read_text(encoding="utf-8")
    create = section(
        source,
        "void __fastcall HookLocalClientCreateRepresentation(",
        "void __fastcall HookIgsObjectInitObject(",
    )
    assert create.index("CreateRepresentationEntry") < create.index(
        "g_original_local_client_create_representation("
    )
    init_object = section(
        source,
        "void __fastcall HookIgsObjectInitObject(",
        "void __fastcall HookPlayerStateSetProgressTemplate(",
    )
    assert init_object.index(
        "CreateRepresentationBeforeInitObject"
    ) < init_object.index("g_original_igs_object_init_object(")
    set_progress = section(
        source,
        "void __fastcall HookPlayerStateSetProgressTemplate(",
        "void ProductionResetGameplayParamsCache(",
    )
    assert set_progress.index(
        "CreateRepresentationBeforeSetProgressTemplate"
    ) < set_progress.index("SetProgressTemplateEntry") < set_progress.index(
        "g_original_player_state_set_progress_template("
    ) < set_progress.index(
        "returned.subphase = playerpublication::WriterSubphase::\n"
        "        SetProgressTemplateReturn"
    ) < set_progress.index(
        "CreateRepresentationAfterSetProgressTemplate"
    )
    initialize = section(
        source,
        "void __fastcall HookPlayerStateInitializeVariables(",
        "void __fastcall HookPlayerStateClear(",
    )
    assert initialize.index(
        "SetProgressTemplateBeforeInitializeVariables"
    ) < initialize.index("InitializeVariablesEntry") < initialize.index(
        "g_original_player_state_initialize_variables(state)"
    ) < initialize.index(
        "returned.subphase = playerpublication::WriterSubphase::\n"
        "        InitializeVariablesReturn"
    )
    on_create = section(
        source,
        "void __fastcall HookPlayerDiOnCreate(",
        "void __fastcall HookPlayerDiOnDestroy(",
    )
    assert on_create.index("PlayerDiOnCreateEntry") < on_create.index(
        "g_original_player_di_on_create(player_di)"
    ) < on_create.index(
        "returned.subphase = playerpublication::WriterSubphase::\n"
        "            PlayerDiOnCreateReturn"
    )


def test_runtime19_event_fields_and_stable_checkpoint_contract_are_reported():
    header = TIMELINE_HEADER.read_text(encoding="utf-8")
    source = PRODUCTION.read_text(encoding="utf-8")
    for field in (
        "baseline_generation",
        "content_generation",
        "transition_generation",
        "caller_module",
        "caller_rva",
        "return_rva",
        "owner_slot",
        "player_state",
        "owner_generation",
        "gameplay_container",
        "base_template_identity_hash",
        "progression_template_identity_hash",
        "input_identity_hash",
        "before_cache_generation",
        "after_cache_generation",
        "before_cache_fingerprint",
        "after_cache_fingerprint",
        "provider_source_handle",
        "provider_source_ordinal",
        "provider_confidence",
        "first_exact_failure",
    ):
        assert field in header
        assert field in source
    for field in (
        "first_stable_gameplay_attempt_count",
        "first_stable_gameplay_first_failure",
        "first_stable_gameplay_last_failure",
        "first_stable_gameplay_eligible_sequence",
        "first_stable_gameplay_capture_sequence",
    ):
        assert field in source
    report = section(
        source,
        "void ReportAppendPlayerPresetPublicationTimeline(",
        "void ReportAppendPostDestroyPlayerDiagnostics(",
    )
    assert report.index("if (!checkpoint.captured)") < report.index(
        "player_publication_owner checkpoint="
    )
