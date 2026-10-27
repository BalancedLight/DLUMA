// Actual XUI load scopes, selected native providers and content bytes. These
// observations grant ended-manager closure only; surviving managers require a
// separate selective compiled-table and retained-client restoration owner.
struct XuiInputFrame {
    xui_content::Load load;
    resource_provider::Receipt selection;
    std::uint64_t seen_generations[128];std::size_t seen_count;
    xui_content::Input instance_input;
    bool incomplete_input,project_input;
};
__declspec(thread) XuiInputFrame* g_xui_input_frames[32]={};
__declspec(thread) std::uint32_t g_xui_input_frame_count=0;
struct XuiFullPathFrame {
    resource_provider::Scope scope;
    XuiInputFrame* target;
    bool resource_owned;
};
__declspec(thread) XuiFullPathFrame g_xui_full_path_frames[32]={};
__declspec(thread) std::uint32_t g_xui_full_path_depth=0;
struct XuiNativeInputPair {resource_input::Pin pin;resource_input::Job job;};
xui_content::InputLeasePool g_xui_input_leases;
bool CloseXuiInputPair(void*,void* pair)noexcept
{auto* input=static_cast<XuiNativeInputPair*>(pair);return resource_input::Close(&input->pin,&input->job);}
void FreeXuiInputPair(void*,void* pair)noexcept {HeapFree(GetProcessHeap(),0,pair);}
bool CloseProductionXuiInputs()noexcept
{return xui_content::DrainInputLeases(&g_xui_input_leases,&CloseXuiInputPair,&FreeXuiInputPair,nullptr);}
bool ProductionXuiHooksCurrent()noexcept
{
    const HookStatusRecord* statuses[]={g_xui_manager_constructor_status,g_xui_manager_destructor_status,
        g_xui_xml_load_status,g_xui_binary_pack_load_status,g_xui_binary_file_load_status,
        g_xui_factory_statuses[0],g_xui_factory_statuses[1],g_xui_factory_statuses[2],g_xui_factory_statuses[3]};
    for(const auto* status:statuses){
        if(!status||!status->validated||!status->installed)return false;
        const HookRoute* found=nullptr;
        for(std::size_t i=0;i<g_hook_route_count;++i)if(g_hook_routes[i].owner==status){if(found)return false;found=&g_hook_routes[i];}
        unsigned char bytes[kMaximumStolenBytes]={};
        if(!found||!found->enabled||!found->installed||!found->relay||!found->byte_count||
            found->byte_count>sizeof(bytes)||!ReadBytes(found->address,bytes,found->byte_count)||
            std::memcmp(bytes,found->routing_bytes,found->byte_count))return false;
    }
    return true;
}

resource_provider::Access ProductionXuiProviderAccess()noexcept
{
    return {&g_dluma_audio_full_path_provider_probe_state,&g_native_file_bridge,
        &ReadProductionPlayerMemory,&ReadProductionResourceSourceRevision,nullptr};
}
bool ReadXuiManagerKey(std::uintptr_t manager,xui_content::Key* out)noexcept
{
    if(!out)return false;*out={};
    xui::ManagerRecord records[xui::kMaximumManagers]={};std::size_t count=0;std::uint64_t revision=0;
    if(!g_xui_manager_observation_ready||!xui::CopyManagerInventory(&g_xui_manager_ledger,records,
        xui::kMaximumManagers,&count,&revision))return false;
    for(std::size_t i=0;i<count;++i)if(records[i].manager==manager){*out={manager,records[i].generation};return true;}
    return false;
}
class XuiContentLoadScope {
public:
    XuiInputFrame frame={};bool tracked=false,finished=false;
    XuiContentLoadScope(void* manager,xui_content::Route route)noexcept
    {
        ++g_xui_content_load_depth;
        std::uint32_t epoch=0;xui_content::Key key={};
        const bool known_epoch=content::TryGetCurrentEpochId(&g_content_ledger,&epoch);
        const bool clean=known_epoch&&!epoch&&!g_xui_content_owner.baseline_frozen&&
            !dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0);
        if(g_xui_input_frame_count<32&&known_epoch&&ReadXuiManagerKey(reinterpret_cast<std::uintptr_t>(manager),&key))
            tracked=xui_content::Begin(&g_xui_content_owner,key,epoch,route,clean,&frame.load);
        if(tracked)g_xui_input_frames[g_xui_input_frame_count++]=&frame;
        else {g_xui_content_owner.lost.store(true);ObservePotentialNativeDomainWrite(cold::ContentDomainId::NativeFrontendEntries);}
    }
    void Finish(bool no_input_is_native_noop=false)noexcept
    {
        if(finished)return;finished=true;
        if(tracked){bool affected=false;
            if(!g_xui_input_frame_count||g_xui_input_frames[g_xui_input_frame_count-1]!=&frame)
                g_xui_content_owner.lost.store(true);
            else g_xui_input_frames[--g_xui_input_frame_count]=nullptr;
            const bool complete=xui_content::End(&g_xui_content_owner,&frame.load,no_input_is_native_noop,&affected);
            const auto origin=!complete||frame.incomplete_input||!frame.load.input_count?xui_instance::Origin::UiLoadUnknown:
                frame.project_input?xui_instance::Origin::UiLoadProject:
                affected?xui_instance::Origin::UiLoadUnknown:xui_instance::Origin::UiLoadGlobal;
            (void)xui_instance::FinishLoad(&g_xui_instances,frame.load.serial,origin);
            if(!complete||affected)
                ObservePotentialNativeDomainWrite(cold::ContentDomainId::NativeFrontendEntries);
        }
    }
    ~XuiContentLoadScope(){Finish();--g_xui_content_load_depth;}
};
void ObserveProductionXuiFullPath(void*,resourcegate::EventPhase phase,bool resource_owned,
    std::uintptr_t returned,const resource_provider::Receipt* resource_receipt)noexcept
{
    if(phase==resourcegate::EventPhase::Enter){
        if(g_xui_full_path_depth>=32){++g_xui_full_path_depth;if(g_xui_input_frame_count)g_xui_content_owner.lost.store(true);return;}
        auto& frame=g_xui_full_path_frames[g_xui_full_path_depth++];frame={};frame.resource_owned=resource_owned;
        if(g_xui_input_frame_count){frame.target=g_xui_input_frames[g_xui_input_frame_count-1];
            frame.target->selection={};
            if(!resource_owned){
                // Audio can already own this same full_path invocation under
                // an enclosing bank load. Do not poison its single probe slot
                // by attempting to arm a second observer for the UI dependency.
                auto& slot=g_dluma_audio_full_path_provider_probe_state.slots[
                    GetCurrentThreadId()&audio::kAudioFullPathProbeSlotMask];
                const auto armed=dluma_atomic_compare_exchange(reinterpret_cast<volatile LONG*>(&slot.armed),0,0);
                if(!armed)(void)resource_provider::Begin(ProductionXuiProviderAccess(),frame.target->load.serial,&frame.scope);
            }}
        return;
    }
    if(!g_xui_full_path_depth)return;
    if(--g_xui_full_path_depth>=32)return;
    auto& frame=g_xui_full_path_frames[g_xui_full_path_depth];
    if(frame.target&&phase==resourcegate::EventPhase::Returned){
        if(frame.resource_owned){if(resource_receipt)frame.target->selection=*resource_receipt;}
        else (void)resource_provider::Finish(ProductionXuiProviderAccess(),&frame.scope,returned,&frame.target->selection);
    }
    resource_provider::Cancel(ProductionXuiProviderAccess(),&frame.scope);frame={};
}
bool DigestProductionXuiInput(const files::FileRecord& file,std::uint64_t serial,xui_content::Input* input)noexcept
{
    if(!input)return false;
    if(file.kind==files::FileKind::Zip){archiveinput::Digest digest={};
        if(!g_archive_input_observer_ready||!archiveinput::Copy(&g_archive_input_cache,file,&digest)||
            !digest.complete||!digest.cursor_restored||digest.result!=archiveinput::Result::Succeeded)return false;
        input->bytes=digest.bytes;std::memcpy(input->sha256,digest.sha256,sizeof(input->sha256));return true;
    }
    // Independent handles retain the actual opened object. The bounded reader
    // never seeks the native handle or re-resolves a name through VFS.
    auto* pair=static_cast<XuiNativeInputPair*>(HeapAlloc(GetProcessHeap(),0,sizeof(XuiNativeInputPair)));
    if(!pair)return false;std::memset(pair,0,sizeof(*pair));xui_content::InputLeaseToken token={};
    if(!xui_content::TrackInputLease(&g_xui_input_leases,pair,&token)){HeapFree(GetProcessHeap(),0,pair);return false;}
    bool complete=resource_input::Capture(&g_native_file_bridge,file,serial,&pair->pin)&&
        resource_input::Begin(pair->pin,64ULL*1024ULL*1024ULL,&pair->job);
    while(complete&&pair->job.state==resource_input::State::Reading)(void)resource_input::Step(pair->pin,&pair->job);
    complete=complete&&pair->job.state==resource_input::State::Complete;
    if(complete){input->bytes=pair->job.bytes;std::memcpy(input->sha256,pair->job.sha256,sizeof(input->sha256));}
    const bool closed=xui_content::CompleteInputLease(&g_xui_input_leases,&token,&CloseXuiInputPair,&FreeXuiInputPair,nullptr);
    return complete&&closed;
}
void NoteProductionXuiOpen(void* native_file,const char* native_path,void* explicit_source)noexcept
{
    if(!native_file||!g_xui_input_frame_count)return;
    const auto saved_error=GetLastError();
    xui_content::Input input={};auto* frame=g_xui_input_frames[g_xui_input_frame_count-1];
    bool exact=filebridge::FileIdentity(&g_native_file_bridge,reinterpret_cast<std::uintptr_t>(native_file),&input.file);
    if(exact){input.selected_source=input.file.source;
        if(explicit_source){files::SourceKey selected={};
            exact=filebridge::SourceIdentity(&g_native_file_bridge,reinterpret_cast<std::uintptr_t>(explicit_source),&selected)&&
                (!input.selected_source.address||(input.selected_source.address==selected.address&&
                    input.selected_source.generation==selected.generation));
            if(exact)input.selected_source=selected;
        }
        if(exact&&!input.selected_source.address&&resource_provider::Matches(ProductionXuiProviderAccess(),frame->selection,
            frame->selection.load_serial,native_path))input.selected_source=frame->selection.source;
        retained::Realm realm=retained::Realm::Unknown;
        exact=exact&&ClassifyProductionResourceSource(nullptr,input.selected_source,&realm,&input.source_epoch);
        input.realm=realm==retained::Realm::Global?xui_content::Realm::Global:
            realm==retained::Realm::Project?xui_content::Realm::Project:xui_content::Realm::Unknown;
        exact=exact&&trace::CopyReadableCString(input.path,sizeof(input.path),native_path,sizeof(input.path))==
            trace::AssetTraceStringCopyResult::Copied;
        if(exact)exact=DigestProductionXuiInput(input.file,frame->load.serial,&input);
        if(exact){files::FileRecord after={};files::SourceKey selected={};
            exact=filebridge::FileIdentity(&g_native_file_bridge,input.file.file,&after)&&
                after.file==input.file.file&&after.generation==input.file.generation&&after.root==input.file.root&&
                after.kind==input.file.kind&&after.life==files::Life::Live&&
                after.source.address==input.file.source.address&&after.source.generation==input.file.source.generation&&
                filebridge::SourceIdentity(&g_native_file_bridge,input.selected_source.address,&selected)&&
                selected.generation==input.selected_source.generation;
        }
    }
    input.complete=exact;
    // An inner resource/XML load is also a consumed input of its enclosing UI
    // load. Copies remain values; the native file may close immediately after.
    for(std::uint32_t i=0;i<g_xui_input_frame_count;++i){auto* observing=g_xui_input_frames[i];bool seen=false;
        if(input.file.generation)for(std::size_t j=0;j<observing->seen_count;++j)
            if(observing->seen_generations[j]==input.file.generation){seen=true;break;}
        if(seen)continue;
        observing->instance_input=input;
        if(!input.complete)observing->incomplete_input=true;
        if(input.complete&&input.realm==xui_content::Realm::Project)observing->project_input=true;
        if(input.file.generation){if(observing->seen_count==128)g_xui_content_owner.lost.store(true);
            else observing->seen_generations[observing->seen_count++]=input.file.generation;}
        (void)xui_content::Observe(&g_xui_content_owner,&observing->load,input);
    }
    frame->selection={};SetLastError(saved_error);
}
cold::DomainResult ColdXuiBaseline(void*,const cold::RestartContext* context)noexcept
{
    if(!context||!context->baseline_generation||context->owner_thread_id!=GetCurrentThreadId()||
        !g_xui_manager_observation_ready||!g_xui_instance_observation_ready||g_xui_instances.lost.load()||
        !g_engine_full_profile_valid||!g_filesystem_full_profile_valid||
        g_xui_manager_ledger.lost.load()||!g_archive_input_observer_ready||!ProductionXuiHooksCurrent()||
        !CloseProductionXuiInputs())return cold::DomainResult::Unsupported;
    if(g_xui_content_owner.baseline_frozen)return g_xui_content_owner.baseline_generation==context->baseline_generation&&
        !g_xui_content_owner.lost.load()?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;
    SerializedFrameRouteEvidence frame={};
    if(content::CurrentEpochId(&g_content_ledger)||dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0)||
        CurrentSerializedGameFrameRouteMatches(0,GetCurrentThreadId(),&frame)!=statefive::RejectReason::C10ScopeMatched)
        return cold::DomainResult::Unsupported;
    return xui_content::CaptureBaseline(&g_xui_content_owner,context->baseline_generation)?
        cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;
}
cold::DomainResult ColdXuiEndedOwners(void*,const cold::RestartContext* context)noexcept
{
    return context&&context->owner_thread_id==GetCurrentThreadId()&&g_xui_manager_observation_ready&&
        !g_xui_manager_ledger.lost.load()&&ProductionXuiHooksCurrent()&&
        ProductionDomainTransactionMatches(context,cold::ContentDomainId::NativeFrontendEntries)&&CloseProductionXuiInputs()&&
        xui_instance::ProjectInputInstancesClosed(&g_xui_instances)&&
        xui_content::VerifyEndedOwners(&g_xui_content_owner,context->baseline_generation,context->content_epoch)?
        cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;
}
