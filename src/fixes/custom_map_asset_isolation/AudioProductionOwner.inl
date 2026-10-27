// Included once in the native isolation translation unit. These callbacks
// deliberately use its actual registry, hook receipts and native owner gates.
bool PrepareProductionAudioBuses(const cold::RestartContext*) noexcept;
bool RestoreProductionAudioBuses(const cold::RestartContext*) noexcept;
bool VerifyProductionAudioBuses(const cold::RestartContext*) noexcept;
bool ProductionAudioTextEqual(const char* a,const char* b,std::size_t bound) noexcept
{
    for(std::size_t i=0;i<bound;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;
}

struct ProductionAudioOwner {
    cold::RestartContext context = {};
    audio_worker_reset::Plan plan = {};
    audio_settings_native::UserGains user_gains = {};
    audio_sound_properties::NativeBinding properties = {};
    audio_worker_reset::windows::SourceReference sources[65536] = {};
    audio_worker_reset::windows::NativeInventory inventory = {};
    audiosnapshot::AudioBaselineWindowsSnapshot current = {};
    file_lifetime::FileRecord replay_files[audio_baseline_content::kBanks] = {};
    file_lifetime::FileRecord verified_files[audio_baseline_content::kBanks] = {};
    std::uint64_t owner_generation = 0, user_gain_request = 0, deadline = 0;
    const cold::RestartContext* callback = nullptr;
    volatile LONG worker_enabled = 0;
    bool prepared = false, released = false, reset = false, reloaded = false;
    bool verified_files_initialized = false, replay_input_failure = false;
};
ProductionAudioOwner g_production_audio;

bool ProductionAudioContext(const cold::RestartContext* context,bool callback=true) noexcept
{
    const auto phase=g_cold_reboot_registry.phase;
    const bool preparing=phase==cold::RestartPhase::Preflighting || phase==cold::RestartPhase::Quiescing || phase==cold::RestartPhase::Cancelling;
    if (!context || !context->content_epoch || !context->transaction_generation ||
        context->owner_thread_id != GetCurrentThreadId() || g_dluma.stop_requested ||
        (callback && g_production_audio.callback != context) ||
        !ProductionDomainTransactionMatches(context,cold::ContentDomainId::Audio) ||
        (!preparing && (g_cold_reboot_registry.prepared_content_epoch != context->content_epoch ||
        g_cold_reboot_registry.prepared_transaction_generation != context->transaction_generation)) ||
        !dluma_atomic_compare_exchange(&g_cold_reboot_registry.touch_transaction_state,0,0) ||
        dluma_atomic_compare_exchange(&g_cold_reboot_registry.runtime_failure_latched,0,0)) return false;
    const auto& scope=g_cold_reboot_state_five_scope;
    std::uint32_t native_state=0;SerializedFrameRouteEvidence frame={};
    return scope.active && scope.armed && scope.on_destroy_claimed && scope.post_c10_token_consumed &&
        scope.barrier_attempted && !scope.runtime_failed && scope.epoch==context->content_epoch &&
        scope.c10_callback_thread_id==GetCurrentThreadId() && g_project_callbacks_drained &&
        g_project_map_exit_key.project_epoch==context->content_epoch &&
        ReadGameDiState(scope.game_di,&native_state) && native_state==5 &&
        ReadGameDiCurrentSession(scope.game_di)==nullptr && FindLiveCustomPlayerOwner()==nullptr &&
        CurrentSerializedGameFrameRouteMatches(scope.frame_route_token,GetCurrentThreadId(),&frame)==statefive::RejectReason::C10ScopeMatched &&
        !ColdRebootNextProjectStagingStarted();
}

struct ProductionAudioCallback {
    const cold::RestartContext* previous;
    explicit ProductionAudioCallback(const cold::RestartContext* context) noexcept
        : previous(g_production_audio.callback) { g_production_audio.callback = previous ? nullptr : context; }
    ~ProductionAudioCallback() { g_production_audio.callback = previous; }
};

bool ProductionAudioOwnerMatches() noexcept
{
    std::uintptr_t owner=0; std::uint64_t generation=0;
    return ReadCurrentAudioOwnerLifetime(&owner,&generation) &&
        owner == g_production_audio.plan.audio_owner && generation == g_production_audio.owner_generation;
}

bool ReadProductionAudioSpeechFence(void*,audio_speech_consumers::Fence* out) noexcept
{
    if (!out) return false;
    *out = {};
    const bool worker_lock = dluma_atomic_compare_exchange(&g_audio_source_reader_owner,0,0) ==
        static_cast<LONG>(GetCurrentThreadId()) && g_audio_worker_speech_token.active;
    const bool baseline_lock = g_audio_property_read_lock_owned && g_speech_registry_snapshot_token.active;
    if ((!worker_lock && !baseline_lock) || !g_audio_speech_consumer_targets_validated ||
        !ReadCurrentAudioOwnerLifetime(&out->audio_owner,&out->owner_generation) ||
        !ReadBytes(g_context.engine_base+kSpeechManagerGlobalRva,&out->speech_manager,sizeof(out->speech_manager))) return false;
    out->thread = GetCurrentThreadId(); out->sound_lock_owned = true; out->speech_gate_owned = true;
    if (g_production_audio.callback && ProductionAudioContext(g_production_audio.callback)) {
        out->transaction = g_production_audio.callback->transaction_generation;
        out->epoch = g_production_audio.callback->content_epoch;
        out->producers_closed = AudioObservationActiveCallCount() == 0;
        out->post_c10 = true;
    } else if (worker_lock && g_audio_worker_reset_scope_active &&
        dluma_atomic_compare_exchange(&g_production_audio.worker_enabled,0,0) && ProductionAudioOwnerMatches()) {
        out->transaction = g_production_audio.context.transaction_generation;
        out->epoch = g_production_audio.context.content_epoch;
        out->producers_closed = dluma_atomic_compare_exchange(&g_cold_reboot_registry.touch_transaction_state,0,0)==2 &&
            AudioObservationActiveCallCount() == 0;
        out->post_c10 = g_cold_reboot_state_five_scope.active && g_cold_reboot_state_five_scope.post_c10_token_consumed &&
            g_cold_reboot_state_five_scope.epoch == out->epoch && !g_cold_reboot_state_five_scope.runtime_failed;
    }
    return true;
}

bool ProductionAudioWorkerFence(void*,const audio_worker_reset::Plan* plan) noexcept
{
    return plan && plan == &g_audio_worker_command.plan && g_audio_worker_reset_scope_active &&
        dluma_atomic_compare_exchange(&g_production_audio.worker_enabled,0,0) && ProductionAudioOwnerMatches() &&
        plan->epoch == g_cold_reboot_registry.prepared_content_epoch &&
        plan->transaction_generation == g_cold_reboot_registry.prepared_transaction_generation &&
        plan->fence_identity == g_production_audio.owner_generation &&
        g_cold_reboot_registry.phase == cold::RestartPhase::Mutating &&
        dluma_atomic_compare_exchange(&g_cold_reboot_registry.touch_transaction_state,0,0)==2 &&
        !dluma_atomic_compare_exchange(&g_cold_reboot_registry.runtime_failure_latched,0,0) &&
        !g_dluma.stop_requested && GetTickCount64() < plan->deadline_tick &&
        AudioObservationActiveCallCount() == 0 &&
        g_cold_reboot_state_five_scope.post_c10_token_consumed &&
        g_cold_reboot_state_five_scope.epoch == plan->epoch && !g_cold_reboot_state_five_scope.runtime_failed;
}

bool ProductionAudioWorkerSnapshot(void*,const audio_worker_reset::Plan* plan,audio_worker_reset::Snapshot* out) noexcept
{
    if (!out || !ProductionAudioWorkerFence(nullptr,plan)) return false;
    auto& inventory=g_production_audio.inventory;
    inventory.sources=g_production_audio.sources; inventory.source_capacity=65536;
    files::OutstandingWork work = {};
    audio_speech_consumers::Snapshot speech = {};
    const audio_speech_consumers::Access access={g_context.engine_base,&ReadAudioBaselineMemory,
        &ReadProductionAudioSpeechFence,nullptr,g_engine_full_profile_valid};
    if (!audio_worker_reset::windows::ReadNativeInventory(&g_audio_worker_binding,*plan,&inventory) ||
        !files::DrainCompletedControls(&g_native_file_bridge.ledger) ||
        !files::SnapshotOutstandingWork(&g_native_file_bridge.ledger,&work) ||
        work.failure!=files::Failure::None || !work.counts_complete || !work.hooks_complete ||
        !work.admission_closed || work.transaction!=plan->transaction_generation ||
        audio_speech_consumers::Capture(access,&speech)!=audio_speech_consumers::Result::Succeeded ||
        audio_speech_consumers::Empty(speech)!=audio_speech_consumers::Result::Succeeded) return false;
    bool retained = inventory.kept_bank_addresses_match;
    for (std::uint32_t i=0;i<plan->keep_count;++i) {
        file_lifetime::FileRecord file={};
        retained &= filebridge::FileIdentity(&g_native_file_bridge,plan->keep[i].native_file,&file) &&
            file.generation==plan->keep[i].instance_generation && file.life==files::Life::Live;
    }
    *out={};out->fence_identity=plan->fence_identity;
    out->preserved_reference_fingerprint=inventory.kept_source_fingerprint;
    out->pending_bank_reads=inventory.pending_bank_reads+(work.reads_drained?0U:1U);
    out->project_bank_count=inventory.project_banks;
    out->unreleased_source_instances=inventory.unreleased_source_instances;
    out->preserved_bank_identities_match=retained;
    out->complete=inventory.complete;
    return true;
}

const audio_baseline_content::Input* ProductionAudioInput(std::uint32_t operation,const char* path,bool localized) noexcept
{
    char normalized[audiostart::kAudioStartupLogicalPathCapacity]={};
    char copied[audiostart::kAudioStartupLogicalPathCapacity]={};
    if (!path || trace::CopyReadableCString(copied,sizeof(copied),path,sizeof(copied))!=trace::AssetTraceStringCopyResult::Copied) return nullptr;
    const bool script=operation==kAudioScriptLoadRva;
    if (!(script ? content::NormalizeLogicalPath(copied,normalized,sizeof(normalized)) :
          CopyAudioBankStem(normalized,sizeof(normalized),copied))) return nullptr;
    const audio_baseline_content::Input* found=nullptr;
    for (std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i) {
        const auto& input=g_audio_baseline_content.inputs[i];
        if (input.script!=script || (!script && (input.localized!=localized ||
            (input.bank_owner==audiostart::AudioStartupBankOwner::Speech)!=(operation==kSpeechBankLoadRva))) ||
            !content::PathsEqual(input.logical_path,normalized)) continue;
        if (found && (found->source.address!=input.source.address || found->source.generation!=input.source.generation ||
            std::memcmp(found->sha256,input.sha256,32))) return nullptr;
        found=&input;
    }
    return found;
}

AudioPreparedScriptInput* ProductionAudioScriptInput(const char* path) noexcept
{
    for (auto& input:g_audio_prepared_script_inputs)
        if (input.ready && content::PathsEqual(input.logical_path,path)) return &input;
    return nullptr;
}

bool ResolveProductionAudioIdentity(std::uint32_t operation,std::uintptr_t owner,const char* path,bool localized,
    cold::ReplayOperationIdentity* out) noexcept
{
    if (!out) return false; *out={};
    if (!g_production_audio.prepared || !ProductionAudioOwnerMatches() ||
        !ProductionAudioContext(g_production_audio.callback)) return false;
    out->domain=cold::ContentDomainId::Audio;out->operation_id=operation;out->owner=owner;
    if (operation==0x113FB0U || operation==kSpeechBankRemoveAllRva) {
        std::uintptr_t speech=0;
        if (operation==0x113FB0U && owner!=g_context.engine_base+0xAD5308U) return false;
        if (operation==kSpeechBankRemoveAllRva && (!ReadBytes(g_context.engine_base+kSpeechManagerGlobalRva,&speech,8) ||
            !speech || owner!=speech+8)) return false;
        out->identity_kind=cold::ReplayIdentityKind::InMemoryOwner;return true;
    }
    const auto* input=ProductionAudioInput(operation,path,localized);
    if (!input) return false;
    out->identity_kind=cold::ReplayIdentityKind::FileContent;
    file_lifetime::SourceKey source={};
    if (!filebridge::SourceIdentity(&g_native_file_bridge,input->source.address,&source) ||
        source.generation!=input->source.generation) return false;
    if (input->script) {
        const auto* prepared=ProductionAudioScriptInput(input->logical_path);
        file_lifetime::FileRecord current={};
        if (owner!=g_context.engine_base+kAudioScriptLoadRva || !prepared ||
            !filebridge::FileIdentity(&g_native_file_bridge,prepared->handle,&current) ||
            current.generation!=prepared->file.generation || current.source.address!=source.address ||
            current.source.generation!=source.generation) return false;
        std::memcpy(out->content_sha256,prepared->digest.sha256,32);
    } else {
        if (input->bank_pin_index>=g_audio_baseline_content.frozen_count) return false;
        const auto& bank=g_audio_baseline_content.banks[input->bank_pin_index];
        if (!bank.hashed || !speechlease::PinStillNamesContent(bank.pin,bank.identity)) return false;
        const auto required_owner=operation==kAudioFmodLoadBanksRva ? g_production_audio.plan.audio_owner :
            operation==kSoundBankManagerLoadRva ? g_production_audio.plan.bank_manager : input->owner;
        if (owner!=required_owner) return false;
        std::memcpy(out->content_sha256,bank.sha256,32);
    }
    out->provider=source.address;out->provider_generation=source.generation;
    CopyLoaderText(out->logical_path,sizeof(out->logical_path),input->logical_path);
    return true;
}

bool ResolveProductionAudioFactory(void*,const filebridge::FactoryRequest& request,cold::ReplayOperationIdentity* out) noexcept
{
    const auto* invocation=g_audio_replay_invocation;
    const auto* actual=invocation?invocation->actual():nullptr;
    if (!out || !actual || actual->identity_kind!=cold::ReplayIdentityKind::FileContent) return false;
    const bool script=actual->operation_id==kAudioScriptLoadRva;
    const bool speech=actual->operation_id==kSpeechBankLoadRva;
    const std::uint32_t flags=script||speech?0U:audio::kSoundBankNativeOpenFlags;
    if(request.mode)return false;
    switch(request.route){
    case filewin::Route::PublicOpen:case filewin::Route::NativeOpen:
        if(request.flags!=flags||request.access||request.creation||request.unbuffered)return false;break;
    case filewin::Route::DirectoryRead:case filewin::Route::ZipFactory:
        if(!script||request.flags||request.access||request.creation||request.unbuffered)return false;break;
    case filewin::Route::Win32Factory:
        if(request.access!=GENERIC_READ||request.creation!=OPEN_EXISTING||request.flags!=FILE_ATTRIBUTE_NORMAL||
            request.unbuffered||(!script&&!speech))return false;break;
    case filewin::Route::AsyncFactory:
        if(script||speech||request.access||request.creation||request.flags||request.unbuffered)return false;break;
    default:return false;
    }
    filebridge::FactoryPathSnapshot path={};
    if (!filebridge::SnapshotFactoryPath(&g_native_file_bridge,request,&path) || !path.complete) return false;
    char joined[filebridge::kFactoryPathCapacity]={},normalized[filebridge::kFactoryPathCapacity]={};
    if (path.representation==filebridge::PathRepresentation::NarrowString)
        CopyLoaderText(joined,sizeof(joined),path.narrow);
    else {
        if (strlen(path.directory)+strlen(path.filename)+2>=sizeof(joined)) return false;
        CopyLoaderText(joined,sizeof(joined),path.directory);
        if (joined[0]) dluma_str_append(joined,sizeof(joined),"/");
        dluma_str_append(joined,sizeof(joined),path.filename);
    }
    if (actual->operation_id==kAudioScriptLoadRva) {
        if (request.mode || !content::NormalizeLogicalPath(joined,normalized,sizeof(normalized))) return false;
        bool path_match=content::PathsEqual(normalized,actual->logical_path);
        if (!path_match && (request.route==filewin::Route::Win32Factory || request.route==filewin::Route::AsyncFactory)) {
            char root[content::kSourcePathCapacity]={},physical[filebridge::kFactoryPathCapacity]={};
            std::uintptr_t source_vtable=0;
            if (!CopySourcePath(root,sizeof(root),reinterpret_cast<void*>(actual->provider)) ||
                !ReadBytes(actual->provider,&source_vtable,sizeof(source_vtable))) return false;
            if (source_vtable==g_context.filesystem_base+0x3F4A8U) {
                if (strlen(root)+strlen(actual->logical_path)+2>=sizeof(physical)) return false;
                CopyLoaderText(physical,sizeof(physical),root); dluma_str_append(physical,sizeof(physical),"/");
                dluma_str_append(physical,sizeof(physical),actual->logical_path);
            } else if (source_vtable==g_context.filesystem_base+0x44A48U) CopyLoaderText(physical,sizeof(physical),root);
            else return false;
            path_match=content::NormalizePhysicalPath(physical,normalized,sizeof(normalized)) &&
                content::PathsEqual(normalized,joined);
        }
        if (!path_match) return false;
        if (request.explicit_source && (!request.source_identity_observed ||
            request.source_identity.address!=actual->provider || request.source_identity.generation!=actual->provider_generation)) return false;
    } else {
        const auto* input=ProductionAudioInput(actual->operation_id,actual->logical_path,
            [&]() noexcept { for (std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i) {
                const auto& in=g_audio_baseline_content.inputs[i]; if(!in.script && content::PathsEqual(in.logical_path,actual->logical_path)) return in.localized;
            } return false; }());
        if (!input || input->bank_pin_index>=g_audio_baseline_content.frozen_count || request.mode ||
            !content::NormalizePhysicalPath(joined,normalized,sizeof(normalized)) ||
            !content::PathsEqual(normalized,g_audio_baseline_content.banks[input->bank_pin_index].pin.path)) return false;
        // This source is the actual fs::full_path interior observation for
        // this native thread and bank invocation, not inherited permit data.
        const auto thread=GetCurrentThreadId();
        const auto& context=speech?g_speech_bank_provider_context:g_fmod_bank_provider_context;
        if(!context.logical_depth||context.logical_depth>audio::kAudioProviderLogicalDepthCapacity||
            context.hard_overflow_latched||context.integrity_loss_latched||context.generation_exhausted)return false;
        const auto generation=context.active_generation[context.logical_depth-1];
        const audio::AudioBankProviderFrame* current_frame=nullptr;
        for(const auto& frame:context.frames)if(frame.generation==generation&&frame.manager==actual->owner&&
            frame.logical_depth==context.logical_depth&&!frame.source_identity_loss&&!frame.argument_contract_loss&&
            !frame.generation_mismatch_loss)current_frame=&frame;
        if(!generation||!current_frame)return false;
        auto& slot=g_dluma_audio_full_path_provider_probe_state.slots[thread&audio::kAudioFullPathProbeSlotMask];
        std::uintptr_t source=0;
        if (dluma_atomic_compare_exchange(reinterpret_cast<volatile LONG*>(&slot.thread_id),0,0)==static_cast<LONG>(thread) &&
            dluma_atomic_compare_exchange(reinterpret_cast<volatile LONG*>(&slot.armed),0,0)){
            const auto expected_owner=speech?audio::AudioFullPathProbeOwner::Speech:audio::AudioFullPathProbeOwner::Fmod;
            if(dluma_atomic_compare_exchange(reinterpret_cast<volatile LONG*>(&slot.owner),0,0)!=static_cast<LONG>(expected_owner)||
                static_cast<std::uint64_t>(_InterlockedCompareExchange64(&slot.scope_generation,0,0))!=generation)return false;
            if(dluma_atomic_compare_exchange(reinterpret_cast<volatile LONG*>(&slot.observation_count),0,0)==1)
                source=static_cast<std::uintptr_t>(_InterlockedCompareExchange64(&slot.selected_source,0,0));
        }
        if (!source) {
            if(current_frame->resolved_source_count==1)source=current_frame->selected_source;
        }
        file_lifetime::SourceKey key={};
        if (!filebridge::SourceIdentity(&g_native_file_bridge,source,&key) || key.address!=actual->provider ||
            key.generation!=actual->provider_generation) return false;
    }
    *out=*actual;return true;
}

bool SnapshotProductionAudioCurrent() noexcept
{
    const audiosnapshot::AudioBaselineWindowsReadContext access={g_context.engine_base,&ReadAudioBaselineMemory,
        &ReadAudioBaselineFence,&TryEnterAudioBaselineLock,&LeaveAudioBaselineLock,nullptr,
        g_engine_full_profile_valid,g_speech_registry_observation_available};
    return audiosnapshot::SnapshotAudioBaselineWindows(&access,&g_production_audio.current) &&
        g_production_audio.current.raw_snapshot_complete;
}

bool ReadProductionAudioPropertyFence(void*,audio_sound_properties::Fence* out) noexcept
{
    if (!out || !ProductionAudioContext(g_production_audio.callback) || !ProductionAudioOwnerMatches() ||
        dluma_atomic_compare_exchange(&g_audio_source_reader_owner,0,0)!=static_cast<LONG>(GetCurrentThreadId()) ||
        !g_audio_worker_speech_token.active || !audio_worker_reset::ProvidersCanRetire(&g_audio_worker_command) ||
        !dluma_atomic_compare_exchange(&g_audio_worker_parked,0,0) ||
        static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_worker_park_transaction,0,0))!=
            g_production_audio.context.transaction_generation) return false;
    audiosnapshot::AudioBaselineWindowsObservationFence fence={};
    if (!ReadAudioBaselineFence(nullptr,&fence) || fence.active_calls || fence.observation_loss) return false;
    auto& inventory=g_production_audio.inventory;
    inventory.sources=g_production_audio.sources;inventory.source_capacity=65536;
    if (!audio_worker_reset::windows::ReadNativeInventory(&g_audio_worker_binding,g_production_audio.plan,&inventory) ||
        !inventory.complete || inventory.pending_bank_reads || inventory.project_banks || inventory.unreleased_source_instances) return false;
    *out={g_production_audio.plan.audio_owner,g_production_audio.owner_generation,fence.revision,
        GetCurrentThreadId(),true,true,true};return true;
}

bool ProductionAudioCanRemoveProperty(void*,const audio_sound_properties::Property& property) noexcept
{
    audio_sound_properties::Fence fence={};
    if (!ReadProductionAudioPropertyFence(nullptr,&fence)) return false;
    for(std::size_t i=0;i<g_production_audio.inventory.source_count;++i)
        if(g_production_audio.inventory.sources[i].property_holder==property.holder)return false;
    // Common sounds remain registered. Native ClearAliases resets all their
    // borrowed slots; the mutation authorizer below verifies that actual reset.
    std::size_t count=0;
    return audio_sound_properties::CaptureCommonBorrowers(g_production_audio.properties.access,
        g_audio_property_scratch,g_audio_common_scratch,65536,&count)==audio_sound_properties::Result::Succeeded;
}

bool AuthorizeProductionAudioProperty(void*,audio_sound_properties::NativeOperation operation,
    std::uintptr_t owner,const char*,const char*) noexcept
{
    audio_sound_properties::Fence fence={};
    if(!ReadProductionAudioPropertyFence(nullptr,&fence))return false;
    if(operation==audio_sound_properties::NativeOperation::RemoveProperty){
        std::size_t count=0;
        if(audio_sound_properties::CaptureCommonBorrowers(g_production_audio.properties.access,
            g_audio_property_scratch,g_audio_common_scratch,65536,&count)!=audio_sound_properties::Result::Succeeded)return false;
        for(std::size_t i=0;i<count;++i)if(g_audio_common_scratch[i].property_holder==owner)return false;
        for(std::size_t i=0;i<g_production_audio.inventory.source_count;++i)
            if(g_production_audio.inventory.sources[i].property_holder==owner)return false;
    }
    return true;
}

bool ProductionAudioGainOwner(void*,std::uintptr_t* owner,std::uint64_t* generation) noexcept
{return ReadCurrentAudioOwnerLifetime(owner,generation);}
bool AuthorizeProductionAudioGain(void*,std::uint64_t transaction,std::uint32_t epoch,bool) noexcept
{
    return ProductionAudioContext(g_production_audio.callback) && ProductionAudioOwnerMatches() &&
        g_production_audio.reset && transaction==g_production_audio.context.transaction_generation &&
        epoch==g_production_audio.context.content_epoch &&
        (g_cold_reboot_registry.phase==cold::RestartPhase::Reloading || g_cold_reboot_registry.phase==cold::RestartPhase::Verifying);
}
audio_settings_native::Access ProductionAudioGainAccess() noexcept;
bool SetProductionAudioGain(void*,std::uint64_t transaction,std::uint32_t epoch,std::uintptr_t owner,unsigned bus,float gain) noexcept
{return audio_settings_native::NativeSetMaximum(ProductionAudioGainAccess(),transaction,epoch,owner,bus,gain);}
audio_settings_native::Access ProductionAudioGainAccess() noexcept
{return {g_context.engine_base,&ReadAudioBaselineMemory,&ProductionAudioGainOwner,&AuthorizeProductionAudioGain,
    &SetProductionAudioGain,nullptr,g_engine_full_profile_valid};}

bool PrepareProductionAudioRelease(const cold::RestartContext* context) noexcept
{
    auto& state=g_production_audio;
    if(!ProductionAudioContext(context) || !g_audio_worker_binding.validated || !g_audio_replay_native_targets.validated ||
        !g_audio_user_mix_observer_ready || AudioObservationActiveCallCount() ||
        !NativeFileRoutesStillOwned() || !g_audio_property_baseline.complete ||
        g_audio_property_baseline_generation!=context->baseline_generation ||
        !g_audio_speech_consumer_baseline.complete || g_audio_speech_consumer_baseline.cached_speech ||
        audio_speech_consumers::Empty(g_audio_speech_consumer_baseline)!=audio_speech_consumers::Result::Succeeded ||
        !g_audio_baseline_content.inventory_joined || g_audio_baseline_content.baseline_generation!=context->baseline_generation ||
        !audio_baseline_content::AllBankContentHeld(g_audio_baseline_content) ||
        !audiostart::AudioStartupObservationLossEmpty(g_audio_runtime_observation_ledger.loss) ||
        !SnapshotProductionAudioCurrent())return false;
    if(!ClosePreparedAudioScriptInputs())return false;
    state.context=*context;state.plan={};state.deadline=GetTickCount64()+10000;
    state.replay_input_failure=false;state.reset=state.released=state.reloaded=state.prepared=false;
    if(!ReadCurrentAudioOwnerLifetime(&state.plan.audio_owner,&state.owner_generation) ||
        state.plan.audio_owner!=g_audio_property_baseline.fence.audio_owner ||
        state.owner_generation!=g_audio_property_baseline.fence.owner_generation ||
        state.current.speech_manager!=g_audio_baseline_windows_snapshot.speech_manager ||
        audio_settings_native::Capture(ProductionAudioGainAccess(),&state.user_gains)!=audio_settings_native::Result::Succeeded)return false;
    state.plan.bank_manager=state.current.bank_manager;
    state.plan.epoch=context->content_epoch;state.plan.transaction_generation=context->transaction_generation;
    state.plan.fence_identity=state.owner_generation;state.plan.deadline_tick=state.deadline;
    if(!state.verified_files_initialized){
        for(std::uint32_t i=0;i<g_audio_baseline_content.frozen_count;++i)
            state.verified_files[i]=g_audio_baseline_content.banks[i].natural.file;
        state.verified_files_initialized=true;
    }
    std::memset(state.replay_files,0,sizeof(state.replay_files));
    for(std::uint32_t i=0;i<state.current.bank_count;++i){
        const auto& bank=state.current.banks[i];file_lifetime::FileRecord current={};
        if(!filebridge::FileIdentity(&g_native_file_bridge,bank.native_file,&current))return false;
        const auto* baseline=ProductionAudioInput(kSoundBankManagerLoadRva,bank.name,bank.localised);
        bool global=false;
        if(baseline && baseline->bank_pin_index<g_audio_baseline_content.frozen_count){
            const auto& prior=state.verified_files[baseline->bank_pin_index];
            global=prior.file==current.file && prior.generation==current.generation;
            if(global){
                if(state.plan.keep_count==audio_worker_reset::kMaximumKeptBanks)return false;
                auto& keep=state.plan.keep[state.plan.keep_count++];
                CopyLoaderText(keep.name,sizeof(keep.name),bank.name);keep.record=bank.record;keep.native_file=current.file;
                keep.instance_generation=current.generation;keep.provider_identity=baseline->source.address;
                std::memcpy(keep.content_sha256,baseline->sha256,32);
            }
        }
        if(!global){
            bool classified=false;
            for(std::uint32_t j=0;j<g_audio_runtime_observation_ledger.bank_count;++j){
                const auto& actual=g_audio_runtime_observation_ledger.banks[j];
                content::CustomSourceIdentitySnapshot source={};
                if(actual.epoch==context->content_epoch && actual.owner==audiostart::AudioStartupBankOwner::Fmod &&
                    actual.manager==state.plan.bank_manager && actual.native_file==current.file &&
                    actual.native_file_instance_generation==current.generation && actual.provider.exact_source &&
                    actual.observation_complete && actual.provider_outcome==audiostart::AudioStartupProviderOutcome::NaturalOpenResolvedPath &&
                    content::LookupExactCustomSourceIdentity(&g_content_ledger,reinterpret_cast<void*>(actual.provider.source),
                        actual.provider.path,&source) && source.epoch==context->content_epoch && source.source_active)classified=true;
            }
            if(!classified)return false;
        }
    }
    auto operations=audio_worker_reset::windows::MakeOperations(&g_audio_worker_binding);
    if(!operations.try_lock || !operations.try_lock(operations.user))return false;
    state.inventory.sources=state.sources;state.inventory.source_capacity=65536;
    audio_speech_consumers::Snapshot speech={};
    const audio_speech_consumers::Access speech_access={g_context.engine_base,&ReadAudioBaselineMemory,
        &ReadProductionAudioSpeechFence,nullptr,g_engine_full_profile_valid};
    const bool captured=audio_worker_reset::windows::ReadNativeInventory(&g_audio_worker_binding,state.plan,&state.inventory) &&
        state.inventory.complete && state.inventory.kept_bank_addresses_match &&
        audio_speech_consumers::Capture(speech_access,&speech)==audio_speech_consumers::Result::Succeeded &&
        audio_speech_consumers::Empty(speech)==audio_speech_consumers::Result::Succeeded;
    operations.unlock(operations.user);
    if(!captured)return false;
    state.plan.preserved_reference_fingerprint=state.inventory.kept_source_fingerprint;
    if(!TryEnterAudioBaselineLock(nullptr))return false;
    g_audio_property_read_lock_owned=true;
    const audio_sound_properties::Access property_read={nullptr,&ReadAudioBaselineMemory,&ReadAudioSoundPropertyFence,g_context.engine_base};
    std::size_t common_count=0;
    bool properties_ready=audio_sound_properties::CaptureReadOnly(property_read,&g_audio_property_scratch)==audio_sound_properties::Result::Succeeded &&
        audio_sound_properties::CaptureCommonBorrowers(property_read,g_audio_property_scratch,g_audio_common_scratch,
            65536,&common_count)==audio_sound_properties::Result::Succeeded &&
        !g_audio_property_baseline.common_banks_count && !g_audio_property_scratch.common_banks_count &&
        common_count==g_audio_common_baseline_count;
    for(std::size_t i=0;i<common_count && properties_ready;++i){bool found=false;
        for(std::size_t j=0;j<g_audio_common_baseline_count;++j)found|=g_audio_common_scratch[i].object==g_audio_common_baseline[j].object;
        properties_ready&=found;
    }
    for(std::size_t i=0;i<g_audio_property_baseline.property_count && properties_ready;++i){const auto& expected=g_audio_property_baseline.properties[i];bool found=false;
        for(std::size_t j=0;j<g_audio_property_scratch.property_count;++j){const auto& actual=g_audio_property_scratch.properties[j];
            found|=actual.holder==expected.holder && ProductionAudioTextEqual(actual.name,expected.name,sizeof(actual.name));}
        properties_ready&=found;
    }
    g_audio_property_read_lock_owned=false;LeaveAudioBaselineLock(nullptr);
    if(!properties_ready)return false;
    state.plan.complete_content_provenance=true;state.plan.all_retaining_owners_classified=true;
    std::uint32_t script=0;
    for(std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i){const auto& input=g_audio_baseline_content.inputs[i];
        file_lifetime::SourceKey current={};
        if(!filebridge::SourceIdentity(&g_native_file_bridge,input.source.address,&current) || current.generation!=input.source.generation)return false;
        if(input.script && (script==audiostart::kAudioStartupScriptCapacity ||
            !PrepareAudioScriptReplayInput(input,state.deadline,&g_audio_prepared_script_inputs[script++])))return false;
    }
    state.properties.access={nullptr,&ReadAudioBaselineMemory,&ReadProductionAudioPropertyFence,g_context.engine_base};
    state.properties.authorize=&AuthorizeProductionAudioProperty;state.properties.can_remove_extra=&ProductionAudioCanRemoveProperty;
    if(!audio_sound_properties::ValidateNativeBinding(&state.properties,
        "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c"))return false;
    if(!PrepareProductionAudioBuses(context))return false;
    state.prepared=true;return true;
}

bool AddProductionAudioReplayOperation(cold::ReplayOperationIdentity* operations,std::size_t* count,
    const audio_baseline_content::Input* input,std::uint32_t operation,std::uint32_t parent) noexcept
{
    if(!operations||!count||*count==cold::kMaximumReplayOperations)return false;
    auto& out=operations[(*count)++];out={};out.domain=cold::ContentDomainId::Audio;
    out.ordinal=static_cast<std::uint32_t>(*count);out.parent_ordinal=parent;out.operation_id=operation;
    if(!input){out.identity_kind=cold::ReplayIdentityKind::InMemoryOwner;
        out.owner=operation==0x113FB0U?g_context.engine_base+0xAD5308U:g_production_audio.current.speech_manager+8;
        return out.owner!=0;
    }
    out.identity_kind=cold::ReplayIdentityKind::FileContent;
    out.owner=input->script?g_context.engine_base+kAudioScriptLoadRva:
        operation==kAudioFmodLoadBanksRva?g_production_audio.plan.audio_owner:input->owner;
    out.provider=input->source.address;out.provider_generation=input->source.generation;
    std::memcpy(out.content_sha256,input->sha256,32);
    CopyLoaderText(out.logical_path,sizeof(out.logical_path),input->logical_path);return true;
}

bool BuildProductionAudioReplayOperations(const audio_baseline_content::Input* input,std::uint32_t clear,
    cold::ReplayOperationIdentity* operations,std::size_t* count) noexcept
{
    *count=0;
    if(!input)return AddProductionAudioReplayOperation(operations,count,nullptr,clear,0);
    const auto add_bank=[&](const audio_baseline_content::Input& bank,std::uint32_t parent) noexcept {
        if(bank.bank_owner==audiostart::AudioStartupBankOwner::Speech)
            return AddProductionAudioReplayOperation(operations,count,&bank,static_cast<std::uint32_t>(kSpeechBankLoadRva),parent);
        if(!AddProductionAudioReplayOperation(operations,count,&bank,static_cast<std::uint32_t>(kAudioFmodLoadBanksRva),parent))return false;
        const auto wrapper=static_cast<std::uint32_t>(*count);
        return AddProductionAudioReplayOperation(operations,count,&bank,static_cast<std::uint32_t>(kSoundBankManagerLoadRva),wrapper);
    };
    if(!input->script)return add_bank(*input,0);
    if(!AddProductionAudioReplayOperation(operations,count,input,static_cast<std::uint32_t>(kAudioScriptLoadRva),0))return false;
    for(std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i){const auto& child=g_audio_baseline_content.inputs[i];
        if(child.parent_sequence==input->enter_sequence && !add_bank(child,1))return false;
    }
    return true;
}

bool InvokeProductionAudioReplay(const cold::RestartContext* context,const audio_baseline_content::Input* input,
    std::uint32_t clear=0) noexcept
{
    if(!ProductionAudioContext(context)||!ProductionAudioOwnerMatches()||g_audio_replay_invocation ||
        g_production_audio.replay_input_failure || !g_production_audio.released)return false;
    cold::ReplayOperationIdentity operations[cold::kMaximumReplayOperations]={};std::size_t count=0;
    if(!BuildProductionAudioReplayOperations(input,clear,operations,&count))return false;
    audio_baseline_replay::Entry entry={};
    if(!input){entry.kind=clear==0x113FB0U?audio_baseline_replay::Kind::ClearScripts:audio_baseline_replay::Kind::ClearSpeech;
        entry.owner_identity=operations[0].owner;
    } else {
        entry.kind=input->script?audio_baseline_replay::Kind::Script:
            input->bank_owner==audiostart::AudioStartupBankOwner::Speech?audio_baseline_replay::Kind::SpeechBank:audio_baseline_replay::Kind::FmodBank;
        entry.owner_identity=operations[0].owner;entry.provider_identity=input->source.address;entry.localized=input->localized;
        entry.source_content_complete=true;std::memcpy(entry.content_sha256,input->sha256,32);
        CopyLoaderText(entry.logical_path,sizeof(entry.logical_path),input->logical_path);
        if(entry.kind==audio_baseline_replay::Kind::SpeechBank){
            if(strlen(entry.logical_path)+4>=sizeof(entry.logical_path))return false;
            dluma_str_append(entry.logical_path,sizeof(entry.logical_path),".spb");
        }
    }
    cold::RegistryReplayPermitToken permit={};
    if(!cold::BeginRegistryReplayPermit(&g_cold_reboot_registry,context,operations,count,&permit))return false;
    g_audio_replay_observed_ordinal=0;
    const bool invoked=audio_baseline_replay::InvokeNativeEntry(&g_audio_replay_native_targets,entry);
    const bool observed=cold::EndRegistryReplayPermit(&g_cold_reboot_registry,&permit,invoked &&
        !g_production_audio.replay_input_failure && !g_audio_replay_invocation);
    return invoked && observed;
}

void ObserveProductionAudioReplayNativeFile(const audio::AudioNativeOpenAttempt& attempt,
    const audio::AudioBankProviderCorrelationContext* context,void* native_file,std::uint64_t source_generation) noexcept
{
    const auto* current=g_audio_replay_invocation?g_audio_replay_invocation->actual():nullptr;
    if(!current)return;
    const bool speech=current->operation_id==kSpeechBankLoadRva;
    const auto* input=ProductionAudioInput(current->operation_id,current->logical_path,
        context && attempt.frame_index<audio::kAudioProviderFrameCapacity ? context->frames[attempt.frame_index].localised:false);
    std::uint64_t after_generation=0;
    if(!input || !native_file || !context || attempt.armed || !attempt.exact_source || !attempt.callsite_matched ||
        !attempt.argument_contract_matched || !attempt.path_copy_complete || !source_generation ||
        !ReadStableSourceRegistryGeneration(&after_generation) || source_generation!=after_generation ||
        input->bank_pin_index>=g_audio_baseline_content.frozen_count || attempt.selected_source!=input->source.address ||
        attempt.provider_instance_generation!=input->source.generation){g_production_audio.replay_input_failure=true;return;}
    speechlease::NaturalOpen open={};open.engine_base=g_context.engine_base;open.filesystem_base=g_context.filesystem_base;
    open.native_file=reinterpret_cast<std::uintptr_t>(native_file);open.caller_return=g_context.engine_base+
        (speech?audio::kSpeechBankNativeOpenReturnRva:audio::kSoundBankManagerNativeOpenReturnRva);
    open.speech_scope_generation=attempt.scope_generation;open.provider_identity=attempt.selected_source;
    open.provider_generation=attempt.provider_instance_generation;open.owner_thread_id=GetCurrentThreadId();
    open.exact_player_images=g_engine_full_profile_valid && g_filesystem_full_profile_valid;
    CopyLoaderText(open.resolved_path,sizeof(open.resolved_path),attempt.provider_path);
    speechlease::Pin pin={};file_lifetime::FileRecord actual={};
    const bool pinned=speechlease::CaptureNaturalBankPin(&pin,open,
        speech?speechlease::NativeBankFile::Speech:speechlease::NativeBankFile::Fmod,&ReadAudioBaselineMemory,nullptr);
    const bool matched=pinned && speechlease::PinStillNamesContent(pin,g_audio_baseline_content.banks[input->bank_pin_index].identity) &&
        filebridge::FileIdentity(&g_native_file_bridge,open.native_file,&actual) &&
        actual.generation==attempt.native_file_instance_generation;
    const bool closed=speechlease::ReleasePin(&pin);
    if(matched && closed)g_production_audio.replay_files[input->bank_pin_index]=actual;
    else g_production_audio.replay_input_failure=true;
}

bool VerifyProductionAudioMembership() noexcept
{
    auto& state=g_production_audio;
    if(state.replay_input_failure || !SnapshotProductionAudioCurrent() || !ProductionAudioOwnerMatches() ||
        state.current.bank_count!=g_audio_baseline_windows_snapshot.bank_count ||
        state.current.speech_bank_count!=g_audio_baseline_windows_snapshot.speech_bank_count ||
        state.current.speech_bank_manifest_fingerprint!=g_audio_baseline_windows_snapshot.speech_bank_manifest_fingerprint ||
        state.current.script_manifest_fingerprint!=g_audio_baseline_windows_snapshot.script_manifest_fingerprint)return false;
    audiosemanticsnapshot::AudioScriptRegistrySemanticSnapshot script={};
    if(!CaptureAudioScriptSemanticSnapshot(&script) ||
        !audiominimum::AudioMinimumScriptSnapshotsEqual(script,g_audio_minimum_semantic_state.baseline.script))return false;
    for(std::uint32_t i=0;i<state.current.bank_count;++i){const auto& bank=state.current.banks[i];
        const auto* input=ProductionAudioInput(kSoundBankManagerLoadRva,bank.name,bank.localised);file_lifetime::FileRecord actual={};
        if(!input || !filebridge::FileIdentity(&g_native_file_bridge,bank.native_file,&actual))return false;
        const auto& replay=state.replay_files[input->bank_pin_index];const auto& prior=state.verified_files[input->bank_pin_index];
        if(!((replay.file==actual.file && replay.generation==actual.generation) ||
             (prior.file==actual.file && prior.generation==actual.generation)))return false;
    }
    for(std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i){const auto& input=g_audio_baseline_content.inputs[i];
        if(!input.script && input.bank_owner==audiostart::AudioStartupBankOwner::Speech &&
            !state.replay_files[input.bank_pin_index].generation)return false;
    }
    return true;
}

bool ProductionAudioReplayReadsDrained() noexcept
{
    files::OutstandingWork work={};
    return files::DrainCompletedControls(&g_native_file_bridge.ledger) &&
        files::SnapshotOutstandingWork(&g_native_file_bridge.ledger,&work) &&
        work.failure==files::Failure::None && work.hooks_complete && work.counts_complete && work.reads_drained;
}

bool VerifyProductionAudioFinalConsumers() noexcept
{
    auto& state=g_production_audio;
    if(!VerifyProductionAudioMembership() || !ProductionAudioReplayReadsDrained())return false;
    audio_worker_reset::Plan verify=state.plan;verify.keep_count=0;
    for(std::uint32_t i=0;i<state.current.bank_count;++i){const auto& bank=state.current.banks[i];
        const auto* input=ProductionAudioInput(kSoundBankManagerLoadRva,bank.name,bank.localised);file_lifetime::FileRecord file={};
        if(!input || !filebridge::FileIdentity(&g_native_file_bridge,bank.native_file,&file))return false;
        auto& keep=verify.keep[verify.keep_count++];keep={};keep.record=bank.record;keep.native_file=file.file;
        keep.instance_generation=file.generation;keep.provider_identity=input->source.address;
        CopyLoaderText(keep.name,sizeof(keep.name),bank.name);std::memcpy(keep.content_sha256,input->sha256,32);
    }
    auto operations=audio_worker_reset::windows::MakeOperations(&g_audio_worker_binding);
    if(!operations.try_lock(operations.user))return false;
    auto& inventory=state.inventory;inventory.sources=state.sources;inventory.source_capacity=65536;
    audio_speech_consumers::Snapshot speech={};
    const audio_speech_consumers::Access access={g_context.engine_base,&ReadAudioBaselineMemory,
        &ReadProductionAudioSpeechFence,nullptr,g_engine_full_profile_valid};
    std::uintptr_t voices=0;std::uint32_t deferred=0,pending=0;
    const bool result=audio_worker_reset::windows::ReadNativeInventory(&g_audio_worker_binding,verify,&inventory) &&
        inventory.complete && inventory.kept_bank_addresses_match && !inventory.pending_bank_reads &&
        !inventory.project_banks && !inventory.unreleased_source_instances &&
        ReadBytes(verify.audio_owner+audio_worker_reset::windows::kActiveVoiceHeadOffset,&voices,sizeof(voices)) && !voices &&
        ReadBytes(verify.audio_owner+audio_worker_reset::windows::kDeferredVoiceCountOffset,&deferred,sizeof(deferred)) && !deferred &&
        ReadBytes(verify.audio_owner+audio_worker_reset::windows::kPendingUnloadOffset,&pending,sizeof(pending)) && !pending &&
        audio_speech_consumers::Capture(access,&speech)==audio_speech_consumers::Result::Succeeded &&
        audio_speech_consumers::Empty(speech)==audio_speech_consumers::Result::Succeeded && !speech.cached_speech;
    operations.unlock(operations.user);return result;
}

cold::DomainResult ColdAudioPreflight(void*,const cold::RestartContext* context) noexcept
{
    ProductionAudioCallback callback(context);
    if(!PrepareProductionAudioRelease(context)) {
        g_production_audio.prepared=false;(void)ClosePreparedAudioScriptInputs();
        return cold::DomainResult::Unsupported;
    }
    cold::ReplayOperationIdentity expected[cold::kMaximumReplayOperations]={};std::size_t count=0;
    for(std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i){const auto& input=g_audio_baseline_content.inputs[i];
        if(!input.parent_sequence && !BuildProductionAudioReplayOperations(&input,0,expected,&count)){
            g_production_audio.prepared=false;(void)ClosePreparedAudioScriptInputs();return cold::DomainResult::Unsupported;
        }
    }
    return cold::DomainResult::Succeeded;
}

cold::DomainResult ColdAudioQuiesce(void*,const cold::RestartContext* context) noexcept
{
    ProductionAudioCallback callback(context);
    return ProductionAudioContext(context) && g_production_audio.prepared && ProductionAudioOwnerMatches() &&
        g_production_audio.context.transaction_generation==context->transaction_generation &&
        g_production_audio.context.content_epoch==context->content_epoch && !AudioObservationActiveCallCount()
        ? cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;
}

cold::DomainResult ColdAudioCancel(void*,const cold::RestartContext* context) noexcept
{
    ProductionAudioCallback callback(context);
    if(!context || g_production_audio.context.transaction_generation!=context->transaction_generation ||
        g_production_audio.released || g_production_audio.reset)return cold::DomainResult::Failed;
    g_production_audio.prepared=false;
    return ClosePreparedAudioScriptInputs()?cold::DomainResult::Succeeded:cold::DomainResult::Failed;
}

cold::DomainResult ColdAudioReset(void*,const cold::RestartContext* context) noexcept
{
    ProductionAudioCallback callback(context);auto& state=g_production_audio;
    if(!ProductionAudioContext(context)||!state.prepared||!ProductionAudioOwnerMatches()||state.reset||
        g_source_retirement_epoch!=context->content_epoch||g_source_retirement_transaction!=context->transaction_generation||
        g_source_retirement_complete)return cold::DomainResult::Failed;
    for(std::size_t i=0;i<g_source_retirement_count;++i)if(!g_source_retirement_unlinked[i])return cold::DomainResult::Failed;
    auto operations=audio_worker_reset::windows::MakeOperations(&g_audio_worker_binding);
    if(!operations.native_targets_validated || !files::CloseAdmission(&g_native_file_bridge.ledger,context->transaction_generation))return cold::DomainResult::Failed;
    if(audio_worker_reset::Poll(&g_audio_worker_command,GetTickCount64())!=audio_worker_reset::Phase::Idle &&
        !audio_worker_reset::ResetCompletedCommand(&g_audio_worker_command))return cold::DomainResult::Failed;
    audio_worker_reset::Initialize(&g_audio_worker_command,&operations);
    state.deadline=GetTickCount64()+10000;state.plan.deadline_tick=state.deadline;
    if(!cold::MarkRegistryMutationStarted(&g_cold_reboot_registry,context))return cold::DomainResult::Failed;
    dluma_atomic_exchange(&state.worker_enabled,1);
    if(!audio_worker_reset::Enqueue(&g_audio_worker_command,state.plan)||
        !RequestAudioWorkerPark(context->transaction_generation,state.deadline)){
        dluma_atomic_exchange(&state.worker_enabled,0);return cold::DomainResult::Failed;
    }
    bool parked=false;
    while(GetTickCount64()<state.deadline && ProductionAudioContext(context)){
        const auto phase=audio_worker_reset::Poll(&g_audio_worker_command,GetTickCount64());
        if(phase==audio_worker_reset::Phase::Failed || phase==audio_worker_reset::Phase::Cancelled)break;
        if(phase==audio_worker_reset::Phase::Released && dluma_atomic_compare_exchange(&g_audio_worker_parked,0,0)){
            parked=true;break;
        }
        Sleep(1);
    }
    if(!parked){
        audio_worker_reset::RequestCancellation(&g_audio_worker_command);ReleaseAudioWorkerPark(context->transaction_generation);
        dluma_atomic_exchange(&state.worker_enabled,0);return cold::DomainResult::Failed;
    }
    state.released=true;
    bool restored=false;
    if(operations.try_lock(operations.user)){
        audio_speech_consumers::Snapshot speech={};
        const audio_speech_consumers::Access access={g_context.engine_base,&ReadAudioBaselineMemory,
            &ReadProductionAudioSpeechFence,nullptr,g_engine_full_profile_valid};
        restored=audio_speech_consumers::ClearCached(access,&speech)==audio_speech_consumers::Result::Succeeded &&
            audio_sound_properties::Restore(state.properties.access,g_audio_property_baseline,&g_audio_property_scratch,
                audio_sound_properties::MakeNativeRestoreOperations(&state.properties))==audio_sound_properties::Result::Succeeded &&
            RestoreProductionAudioBuses(context);
        operations.unlock(operations.user);
    }
    ReleaseAudioWorkerPark(context->transaction_generation);dluma_atomic_exchange(&state.worker_enabled,0);
    if(!restored || dluma_atomic_compare_exchange(&g_audio_worker_park_failed,0,0) ||
        !InvokeProductionAudioReplay(context,nullptr,0x113FB0U) ||
        (state.current.speech_manager && !InvokeProductionAudioReplay(context,nullptr,static_cast<std::uint32_t>(kSpeechBankRemoveAllRva))))return cold::DomainResult::Failed;
    state.reset=true;
    return files::ReopenAdmission(&g_native_file_bridge.ledger,context->transaction_generation)
        ? cold::DomainResult::Succeeded:cold::DomainResult::Failed;
}

cold::DomainResult ColdAudioReload(void*,const cold::RestartContext* context) noexcept
{
    ProductionAudioCallback callback(context);auto& state=g_production_audio;
    if(!ProductionAudioContext(context)||!state.prepared||!state.reset||state.reloaded || !ProductionAudioOwnerMatches() ||
        !g_source_retirement_complete || g_source_retirement_epoch!=context->content_epoch ||
        g_source_retirement_transaction!=context->transaction_generation ||
        dluma_atomic_compare_exchange(&g_audio_worker_parked,0,0) ||
        !audio_baseline_content::AllBankContentHeld(g_audio_baseline_content))return cold::DomainResult::Failed;
    if(!files::CloseAdmission(&g_native_file_bridge.ledger,context->transaction_generation))return cold::DomainResult::Failed;
    for(std::uint32_t i=0;i<g_audio_baseline_content.input_count;++i){const auto& input=g_audio_baseline_content.inputs[i];
        if(!input.parent_sequence && !InvokeProductionAudioReplay(context,&input))return cold::DomainResult::Failed;
    }
    if(!VerifyProductionAudioMembership())return cold::DomainResult::Failed;
    const auto completed=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_worker_original_completions,0,0));
    const auto result=audio_settings_native::Apply(&g_audio_user_gain_state,ProductionAudioGainAccess(),state.user_gains,
        context->transaction_generation,context->content_epoch,completed,&state.user_gain_request);
    state.reloaded=result==audio_settings_native::Result::Pending;
    if(!state.reloaded&&state.user_gain_request){
        audio_settings_native::Cancel(&g_audio_user_gain_state,state.user_gain_request);state.user_gain_request=0;
    }
    return state.reloaded && files::ReopenAdmission(&g_native_file_bridge.ledger,context->transaction_generation)
        ? cold::DomainResult::Succeeded:cold::DomainResult::Failed;
}

cold::DomainResult ColdAudioVerify(void*,const cold::RestartContext* context) noexcept
{
    ProductionAudioCallback callback(context);auto& state=g_production_audio;
    if(!ProductionAudioContext(context)||!state.reloaded||!state.user_gain_request)return cold::DomainResult::Failed;
    const auto deadline=GetTickCount64()+5000;bool gains=false;
    while(GetTickCount64()<deadline && ProductionAudioContext(context)){
        const auto completed=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_worker_original_completions,0,0));
        const auto result=audio_settings_native::Verify(&g_audio_user_gain_state,ProductionAudioGainAccess(),state.user_gain_request,completed);
        if(result==audio_settings_native::Result::Succeeded && ProductionAudioReplayReadsDrained()){gains=true;break;}
        if(result==audio_settings_native::Result::Succeeded){Sleep(1);continue;}
        if(result!=audio_settings_native::Result::Pending && result!=audio_settings_native::Result::Busy)break;
        Sleep(1);
    }
    bool properties=false;
    if(gains && TryEnterAudioBaselineLock(nullptr)){
        g_audio_property_read_lock_owned=true;
        const audio_sound_properties::Access access={nullptr,&ReadAudioBaselineMemory,&ReadAudioSoundPropertyFence,g_context.engine_base};
        std::size_t common_count=0;
        properties=audio_sound_properties::CaptureReadOnly(access,&g_audio_property_scratch)==audio_sound_properties::Result::Succeeded &&
            audio_sound_properties::SameValues(g_audio_property_baseline,g_audio_property_scratch,true) &&
            audio_sound_properties::CaptureCommonBorrowers(access,g_audio_property_scratch,g_audio_common_scratch,65536,&common_count)==audio_sound_properties::Result::Succeeded &&
            !g_audio_property_scratch.common_banks_count && common_count==g_audio_common_baseline_count;
        for(std::size_t i=0;i<common_count && properties;++i){bool found=false;
            for(std::size_t j=0;j<g_audio_common_baseline_count;++j)found|=g_audio_common_scratch[i].object==g_audio_common_baseline[j].object &&
                g_audio_common_scratch[i].property_holder==g_audio_common_baseline[j].property_holder;
            properties&=found;
        }
        g_audio_property_read_lock_owned=false;LeaveAudioBaselineLock(nullptr);
    }
    const bool verified=gains && properties && VerifyProductionAudioFinalConsumers() && VerifyProductionAudioBuses(context) &&
        ProductionAudioContext(context) && !state.replay_input_failure;
    audio_settings_native::Cancel(&g_audio_user_gain_state,state.user_gain_request);state.user_gain_request=0;
    if(!verified)return cold::DomainResult::Failed;
    for(std::uint32_t i=0;i<g_audio_baseline_content.frozen_count;++i)
        if(state.replay_files[i].generation)state.verified_files[i]=state.replay_files[i];
    state.prepared=false;
    return ClosePreparedAudioScriptInputs()?cold::DomainResult::Succeeded:cold::DomainResult::Failed;
}
