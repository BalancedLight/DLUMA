// Included after AudioProductionOwner.inl. These joins use the actual worker
// return, native locks, current registry callback and exact loaded modules.
struct ProductionAudioBuses {
    audio_bus_native::Snapshot baseline={},scratch={};
    audio_dsp_native::Snapshot dsp_baseline={},dsp_scratch={};
    audio_dsp_native::Binding dsp={};
    std::uint64_t baseline_generation=0,restored_transaction=0,worker_at_restore=0;
    volatile LONG read_request=0,read_parked=0,read_failed=0;
    std::uintptr_t read_owner=0;
    std::uint64_t read_owner_generation=0,read_deadline=0;
};
ProductionAudioBuses g_production_audio_buses;
thread_local bool g_audio_bus_snapshot_scope_owned=false;

void HoldAudioBusReadPark(const audio_worker_reset::WorkerFrame& frame) noexcept
{
    auto& s=g_production_audio_buses;
    if(!dluma_atomic_compare_exchange(&s.read_request,0,0)||!frame.exact_native_route||!frame.original_returned||
        frame.current_thread_id!=GetCurrentThreadId()||frame.audio_owner!=s.read_owner)return;
    std::uintptr_t owner=0;std::uint64_t generation=0;
    if(!ReadCurrentAudioOwnerLifetime(&owner,&generation)||owner!=s.read_owner||generation!=s.read_owner_generation)return;
    dluma_atomic_exchange(&s.read_parked,1);
    while(dluma_atomic_compare_exchange(&s.read_request,0,0)){
        if(g_dluma.stop_requested||GetTickCount64()>=s.read_deadline){
            dluma_atomic_exchange(&s.read_failed,1);dluma_atomic_exchange(&s.read_request,0);break;
        }
        Sleep(1);
    }
    dluma_atomic_exchange(&s.read_parked,0);
}

bool ReadProductionAudioBusFence(void*,audio_bus_native::Fence* out,bool mutation) noexcept
{
    if(!out||!g_audio_bus_snapshot_scope_owned||!g_audio_definition_read_owned||
        !g_engine_full_profile_valid||g_dluma.stop_requested||AudioObservationActiveCallCount())return false;
    *out={};if(!ReadCurrentAudioOwnerLifetime(&out->owner,&out->generation))return false;
    out->thread=GetCurrentThreadId();out->definitions_owned=true;
    out->sound_owned=g_audio_property_read_lock_owned||
        dluma_atomic_compare_exchange(&g_audio_source_reader_owner,0,0)==static_cast<LONG>(GetCurrentThreadId());
    const auto& s=g_production_audio_buses;const auto* context=g_production_audio.callback;
    const bool transaction=context&&ProductionAudioContext(context)&&ProductionAudioOwnerMatches();
    const bool command_park=transaction&&dluma_atomic_compare_exchange(&g_audio_worker_parked,0,0)&&
        static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_worker_park_transaction,0,0))==context->transaction_generation&&
        !dluma_atomic_compare_exchange(&g_audio_worker_park_failed,0,0);
    const bool read_park=dluma_atomic_compare_exchange(const_cast<volatile LONG*>(&s.read_request),0,0)&&
        dluma_atomic_compare_exchange(const_cast<volatile LONG*>(&s.read_parked),0,0)&&
        !dluma_atomic_compare_exchange(const_cast<volatile LONG*>(&s.read_failed),0,0)&&
        out->owner==s.read_owner&&out->generation==s.read_owner_generation&&GetTickCount64()<s.read_deadline;
    const bool startup=!context&&!content::CurrentEpochId(&g_content_ledger)&&
        !dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0);
    out->worker_parked=command_park||read_park;out->producers_closed=transaction;
    return out->sound_owned&&out->worker_parked&&(transaction||startup)&&(!mutation||command_park);
}

bool ProductionAudioDspFence(void*,const audio_dsp_native::Identity& expected,bool mutation) noexcept
{
    audio_bus_native::Fence fence={};std::uintptr_t system=0,groups=0;std::uint32_t buses=0,count=0;
    if(!ReadProductionAudioBusFence(nullptr,&fence,mutation)||fence.owner!=expected.owner||fence.generation!=expected.generation||
        !ReadBytes(fence.owner+7536,&system,8)||system!=expected.system||
        !ReadBytes(fence.owner+15252,&buses,4)||buses!=expected.buses||buses>14||
        !ReadBytes(fence.owner+7912,&groups,8)||!ReadBytes(fence.owner+7920,&count,4)||count!=buses||!groups)return false;
    for(unsigned i=0;i<buses;++i){std::uintptr_t actual=0;
        if(!ReadBytes(groups+8ULL*i,&actual,8)||actual!=expected.groups[i])return false;
        for(unsigned slot=0;slot<3;++slot)if(!ReadBytes(fence.owner+25632+112ULL*slot+8ULL*i,&actual,8)||actual!=expected.dsp[i][slot])return false;
    }
    std::uintptr_t master_effect=0;
    return ReadBytes(fence.owner+26264,&master_effect,8)&&master_effect==expected.master_effect;
}

std::uintptr_t ResolveProductionAudioDspExport(void*,const char* name) noexcept
{return reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(g_production_audio_buses.dsp.module),name));}

bool InitializeProductionAudioDspBinding() noexcept
{
    auto& b=g_production_audio_buses.dsp;
    if(!b.validated){
        HMODULE module=GetModuleHandleW(L"fmodex64.dll");
        if(!module||!ValidateLoadedModuleSha256(module,"1bb15cc4f8062235f128822fc6f29e72a2e1ec6b6e40f41122a372fb9713bb17"))return false;
        b.module=reinterpret_cast<std::uintptr_t>(module);b.exact_image=true;
        b.resolve=&ResolveProductionAudioDspExport;b.fence=&ProductionAudioDspFence;
        if(!audio_dsp_native::Validate(&b))return false;
    }
    std::uintptr_t actual=0;
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"fmodex64.dll"))==b.module&&
        ReadBytes(g_context.engine_base+0x857758,&actual,8)&&actual==b.functions[5]&&
        ReadBytes(g_context.engine_base+0x8576D0,&actual,8)&&actual==b.functions[2];
}

bool WriteProductionAudioBusMemory(void*,std::uintptr_t address,const void* value,std::size_t size) noexcept
{
    audio_bus_native::Fence fence={};if(!ReadProductionAudioBusFence(nullptr,&fence,true))return false;
    __try {std::memcpy(reinterpret_cast<void*>(address),value,size);return !std::memcmp(reinterpret_cast<void*>(address),value,size);}
    __except(1){return false;}
}

audio_bus_native::Access ProductionAudioBusAccess() noexcept
{return {g_context.engine_base,&ReadAudioBaselineMemory,&WriteProductionAudioBusMemory,&ReadProductionAudioBusFence,nullptr,g_engine_full_profile_valid};}

class ProductionAudioBusReadScope {
    bool definitions_=false,sound_=false,request_=false;
public:
    bool ready=false;
    ProductionAudioBusReadScope() noexcept {
        auto& s=g_production_audio_buses;
        if(g_audio_definition_read_owned||g_audio_definition_writer_depth||g_audio_bus_snapshot_scope_owned||
            dluma_atomic_compare_exchange(&s.read_request,0,0)||dluma_atomic_compare_exchange(&s.read_parked,0,0)||
            !TryAcquireSRWLockExclusive(&g_audio_definition_gate))return;
        definitions_=true;g_audio_definition_read_owned=true;g_audio_definition_read_violation=false;
        if(!ReadCurrentAudioOwnerLifetime(&s.read_owner,&s.read_owner_generation))return;
        s.read_deadline=GetTickCount64()+5000;dluma_atomic_exchange(&s.read_failed,0);
        dluma_atomic_exchange(&s.read_request,1);request_=true;
        while(GetTickCount64()<s.read_deadline&&!g_dluma.stop_requested){
            if(dluma_atomic_compare_exchange(&s.read_parked,0,0))break;
            if(dluma_atomic_compare_exchange(&s.read_failed,0,0))return;Sleep(1);
        }
        if(!dluma_atomic_compare_exchange(&s.read_parked,0,0)||!TryEnterAudioBaselineLock(nullptr))return;
        sound_=true;g_audio_property_read_lock_owned=true;g_audio_bus_snapshot_scope_owned=true;ready=true;
    }
    ~ProductionAudioBusReadScope(){
        g_audio_bus_snapshot_scope_owned=false;
        if(sound_){g_audio_property_read_lock_owned=false;LeaveAudioBaselineLock(nullptr);}
        if(request_){auto& s=g_production_audio_buses;dluma_atomic_exchange(&s.read_request,0);
            const auto deadline=GetTickCount64()+1000;
            while(dluma_atomic_compare_exchange(&s.read_parked,0,0)&&GetTickCount64()<deadline)Sleep(1);
        }
        if(definitions_){g_audio_definition_read_owned=false;ReleaseSRWLockExclusive(&g_audio_definition_gate);}
    }
};

bool CaptureProductionAudioBusesBaseline(std::uint64_t generation) noexcept
{
    auto& s=g_production_audio_buses;
    if(s.baseline_generation)return s.baseline_generation==generation&&s.baseline.complete&&s.dsp_baseline.complete;
    if(!generation||content::CurrentEpochId(&g_content_ledger)||
        dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0)||!InitializeProductionAudioDspBinding())return false;
    ProductionAudioBusReadScope scope;if(!scope.ready)return false;
    if(audio_bus_native::Capture(ProductionAudioBusAccess(),&s.scratch)!=audio_bus_native::Result::Succeeded||
        audio_dsp_native::Capture(s.dsp,s.scratch.backend,&s.dsp_scratch)!=audio_dsp_native::Result::Succeeded||
        g_audio_definition_read_violation)return false;
    s.baseline=s.scratch;s.dsp_baseline=s.dsp_scratch;s.baseline_generation=generation;return true;
}

bool PrepareProductionAudioBuses(const cold::RestartContext* context) noexcept
{
    auto& s=g_production_audio_buses;
    if(!ProductionAudioContext(context)||s.baseline_generation!=context->baseline_generation||
        !s.baseline.complete||!s.dsp_baseline.complete||!audio_bus_native::ReplayProgramSupported(s.baseline)||!InitializeProductionAudioDspBinding())return false;
    ProductionAudioBusReadScope scope;if(!scope.ready)return false;
    return audio_bus_native::Capture(ProductionAudioBusAccess(),&s.scratch)==audio_bus_native::Result::Succeeded&&
        audio_bus_native::ReplayProgramUnchanged(ProductionAudioBusAccess(),s.baseline)&&
        audio_bus_native::CanRestore(s.baseline,s.scratch)&&
        audio_dsp_native::Capture(s.dsp,s.scratch.backend,&s.dsp_scratch)==audio_dsp_native::Result::Succeeded&&
        audio_dsp_native::CanRestore(s.dsp_baseline,s.dsp_scratch)&&!g_audio_definition_read_violation;
}

bool RestoreProductionAudioBuses(const cold::RestartContext* context) noexcept
{
    auto& s=g_production_audio_buses;
    if(!ProductionAudioContext(context)||s.baseline_generation!=context->baseline_generation||
        g_audio_definition_read_owned||g_audio_definition_writer_depth||!TryAcquireSRWLockExclusive(&g_audio_definition_gate))return false;
    g_audio_definition_read_owned=true;g_audio_definition_read_violation=false;g_audio_bus_snapshot_scope_owned=true;
    const bool restored=audio_bus_native::Restore(ProductionAudioBusAccess(),s.baseline,&s.scratch)==audio_bus_native::Result::Succeeded&&
        audio_dsp_native::Restore(s.dsp,s.dsp_baseline,&s.scratch.backend)==audio_dsp_native::Result::Succeeded&&!g_audio_definition_read_violation;
    if(restored){s.restored_transaction=context->transaction_generation;
        s.worker_at_restore=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_worker_original_completions,0,0));}
    g_audio_bus_snapshot_scope_owned=false;g_audio_definition_read_owned=false;ReleaseSRWLockExclusive(&g_audio_definition_gate);
    return restored;
}

bool VerifyProductionAudioBuses(const cold::RestartContext* context) noexcept
{
    auto& s=g_production_audio_buses;
    if(!ProductionAudioContext(context)||s.restored_transaction!=context->transaction_generation||
        !InitializeProductionAudioDspBinding()||
        static_cast<std::uint64_t>(_InterlockedCompareExchange64(&g_audio_worker_original_completions,0,0))<=s.worker_at_restore)return false;
    ProductionAudioBusReadScope scope;if(!scope.ready)return false;
    return audio_bus_native::Capture(ProductionAudioBusAccess(),&s.scratch)==audio_bus_native::Result::Succeeded&&
        audio_dsp_native::Capture(s.dsp,s.scratch.backend,&s.dsp_scratch)==audio_dsp_native::Result::Succeeded&&
        audio_bus_native::Same(s.baseline,s.scratch,g_production_audio.user_gains.maximum)&&
        audio_dsp_native::Same(s.dsp_baseline,s.dsp_scratch,g_production_audio.user_gains.maximum)&&
        !g_audio_definition_read_violation;
}
