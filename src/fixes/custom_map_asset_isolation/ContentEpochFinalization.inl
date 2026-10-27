// Terminal bookkeeping runs inside the registry's committing producer fence.
// Native owners have already reset, retired providers, reloaded and verified.
volatile LONGLONG g_epoch_observer_quiescence_generation=0;

bool FinalizationNativeOwnersQuiet(const cold::RestartContext* context,
    const cold::RegistryFinalizationToken* token,std::uint32_t* callbacks,
    std::uint32_t* references)noexcept
{
    if(callbacks)*callbacks=UINT32_MAX;
    if(references)*references=UINT32_MAX;
    if(!callbacks||!references||!cold::RegistryFinalizationIsCurrent(&g_cold_reboot_registry,context,token) ||
       g_dluma.stop_requested || !ContentTrackingLive() || !g_project_callbacks_drained ||
       g_project_map_exit_key.project_epoch!=context->content_epoch ||
       !g_source_retirement_complete || g_source_retirement_epoch!=context->content_epoch ||
       g_source_retirement_transaction!=context->transaction_generation ||
       !NativeFileRoutesStillOwned() || !GlobalSourceCollectionExcludesRetirementTargets(false))return false;
    const auto& scope=g_cold_reboot_state_five_scope;
    SerializedFrameRouteEvidence frame={};std::uint32_t state=0;
    if(!scope.active||!scope.armed||!scope.on_destroy_claimed||!scope.post_c10_token_consumed||scope.runtime_failed||
       scope.epoch!=context->content_epoch||scope.c10_callback_thread_id!=GetCurrentThreadId()||
       !ReadGameDiState(scope.game_di,&state)||state!=5U||ReadGameDiCurrentSession(scope.game_di)||
       FindLiveCustomPlayerOwner()||ColdRebootNextProjectStagingStarted()||
       CurrentSerializedGameFrameRouteMatches(scope.frame_route_token,GetCurrentThreadId(),&frame)!=statefive::RejectReason::C10ScopeMatched)
        return false;
    // Only this validated cleanup owner's already-entered wrapper hierarchy
    // may remain. Other threads/callbacks are never subtracted as "expected".
    const auto hooks=TotalHookActiveCalls();
    const auto consumer=dluma_atomic_compare_exchange(&g_consumer_active_calls,0,0);
    if(consumer<0||hooks<g_current_thread_hook_call_depth||
       static_cast<std::uint32_t>(consumer)<g_current_thread_consumer_call_depth)return false;
    files::OutstandingWork work={};
    retirement::RetirementSummary providers={};
    if(!files::SnapshotOutstandingWork(&g_native_file_bridge.ledger,&work)||
       !work.hooks_complete||!work.counts_complete||!work.admission_closed||
       work.transaction!=context->transaction_generation||work.failure!=files::Failure::None||
       retirement::SnapshotRetirementSummary(&g_source_retirement_ledger,&providers)!=retirement::RetirementResult::Succeeded||
       providers.observation_lost||!CloseProductionXuiInputs())return false;
    const auto inputs=g_native_resources.initialized?resource_input_capture::Outstanding(&g_native_resources.capture):0;
    const std::uint64_t pending=static_cast<std::uint64_t>(hooks-g_current_thread_hook_call_depth)+
        static_cast<std::uint32_t>(consumer)-g_current_thread_consumer_call_depth+
        AudioObservationActiveCallCount()+work.active_factories+work.active_destructors+
        work.pending_completion_events+work.pending_control_references+inputs+
        g_native_resources.gateway.active_calls.load()+g_native_resources.gateway.pending_calls.load();
    const std::uint64_t retained=static_cast<std::uint64_t>(providers.active_consumers)+providers.mounted+
        providers.detached+providers.quarantined+providers.in_progress;
    *callbacks=pending>UINT32_MAX?UINT32_MAX:static_cast<std::uint32_t>(pending);
    *references=retained>UINT32_MAX?UINT32_MAX:static_cast<std::uint32_t>(retained);
    return !pending&&!retained&&work.reads_drained&&filebridge::Idle(&g_native_file_bridge);
}

bool FinalizeVerifiedContentEpoch(void*,const cold::RestartContext* context,
    const cold::RegistryFinalizationToken* token)noexcept
{
    if(!cold::RegistryFinalizationIsCurrent(&g_cold_reboot_registry,context,token)||
       !files::CloseAdmission(&g_native_file_bridge.ledger,context->transaction_generation))return false;
    content::VerifiedEpochRetirement receipt={};
    if(!FinalizationNativeOwnersQuiet(context,token,&receipt.pending_observer_callbacks,&receipt.external_active_references))return false;
    for(std::size_t i=0;i<g_source_retirement_count;++i){
        retirement::SourceRecord source={};
        if(retirement::SnapshotSource(&g_source_retirement_ledger,g_source_retirement_handles[i],&source)!=retirement::RetirementResult::Succeeded||
           source.phase!=retirement::SourcePhase::Destroyed||source.active_consumers||source.late_consumer_latched||
           source.detach_transaction_generation!=context->transaction_generation)return false;
    }
    // These flags previously stopped at the VFS-only result. A terminal token
    // exists only after the production frontend's restored-or-untouched checks.
    if(!content::MarkEpochComponentResults(&g_content_ledger,context->content_epoch,true,true,true,true,
            content::NextSequence(&g_content_ledger))||
       !content::CaptureEpochRetirementCandidate(&g_content_ledger,context->content_epoch,&receipt))return false;
    if(!FinalizationNativeOwnersQuiet(context,token,&receipt.pending_observer_callbacks,&receipt.external_active_references))return false;
    receipt.verified_transaction_token=context->transaction_generation;
    const auto quiescence=_InterlockedIncrement64(&g_epoch_observer_quiescence_generation);
    if(quiescence<=0)return false;
    receipt.observer_quiescence_token=static_cast<std::uint64_t>(quiescence);
    receipt.transaction_verified=true;
    receipt.observer_admission_closed=true;
    receipt.quarantined=false;
    if(content::CheckEpochRetirement(&g_content_ledger,&receipt)!=content::EpochRetirementResult::Ready)return false;
    if(provenance::CheckCandidateEpochRetirement(&g_candidate_retaining_consumer_ledger,
        &g_cold_reboot_registry,context,token)!=provenance::CandidateLedgerResult::Succeeded)return false;
    const auto audio_revision=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_runtime_observation_ledger.revision,0,0));
    if(!AudioRetireVerifiedEpoch(receipt,audio_revision,context,token))return false;
    if(provenance::RetireCandidateEpoch(&g_candidate_retaining_consumer_ledger,
        &g_cold_reboot_registry,context,token)!=provenance::CandidateLedgerResult::Succeeded)return false;
    if(!cold::RegistryFinalizationIsCurrent(&g_cold_reboot_registry,context,token)||
       content::RetireVerifiedEpoch(&g_content_ledger,&receipt,content::NextSequence(&g_content_ledger),
           &g_last_epoch_retirement)!=content::EpochRetirementResult::Retired)return false;
    MarkContentDirty();
    // The registry fence still rejects ordinary file factories while this
    // separate file gate opens. Its final CAS detects a racing new producer.
    return files::ReopenAdmission(&g_native_file_bridge.ledger,context->transaction_generation) &&
        cold::RegistryFinalizationIsCurrent(&g_cold_reboot_registry,context,token);
}
