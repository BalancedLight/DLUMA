bool XuiFactoryManager(void* factory,xui_content::Key* key)noexcept
{
    if(!key)return false;*key={};const auto address=reinterpret_cast<std::uintptr_t>(factory);
    if(address<16)return false;xui::ManagerRecord manager={};
    if(!g_xui_manager_observation_ready||!xui::LookupManager(&g_xui_manager_ledger,address-16,&manager))return false;
    *key={manager.manager,manager.generation};return true;
}
bool BeginXuiInstanceWriter(void* factory,xui_content::Key* key,xui_instance::Writer* writer)noexcept
{return !(g_xui_birth_requested&&dluma_atomic_compare_exchange(&g_trace_stopping,0,0))&&
    XuiFactoryManager(factory,key)&&g_xui_instance_observation_ready&&xui_instance::BeginWriter(&g_xui_instances,writer);}
void EndXuiInstanceWriter(xui_instance::Writer* writer)noexcept
{if(writer->active)(void)xui_instance::EndWriter(&g_xui_instances,writer);}
bool XuiBirthObservationEnabled()noexcept
{return g_xui_birth_requested&&g_xui_birth_status&&g_xui_birth_status->installed;}
struct XuiCreationFrame {
    XuiCreationFrame* previous;xui_content::Key manager;std::uintptr_t factory,object;
    std::uint64_t generation;bool tracked,notified,skipped;
};
thread_local XuiCreationFrame* g_xui_creation_frame;
bool ObserveXuiObject(std::uintptr_t object,std::uintptr_t owner,xui_content::Key key,
                      xui_instance::Instance* value,bool provisional)noexcept
{
    if(!value)return false;*value={};value->manager=key;value->object=object;value->owner_context=owner;
    std::uint8_t flags=0;
    if(object>UINTPTR_MAX-65||!ReadBytes(object,&value->vtable,8)||!ReadBytes(object+16,&value->interface_object,8)||!ReadBytes(object+64,&flags,1))
        return xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackRead,key,object);
    if((flags&1)||(provisional&&(flags&3)))return xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackFlags,key,object,0,flags);
    value->life=provisional?xui_instance::Life::Provisional:(flags&3)==2?xui_instance::Life::Retired:xui_instance::Life::Active;
    value->origin=xui_instance::Origin::ExternalUnknown;
    const bool epoch_known=content::TryGetCurrentEpochId(&g_content_ledger,&value->epoch);
    if(g_xui_input_frame_count){const auto* frame=g_xui_input_frames[g_xui_input_frame_count-1];
        value->load_serial=frame->load.serial;value->origin=xui_instance::Origin::UiLoadPending;value->epoch=frame->load.epoch;
        if(frame->instance_input.complete){value->source=frame->instance_input.selected_source;
            std::memcpy(value->sha256,frame->instance_input.sha256,32);value->exact_input=true;}}
    else if(epoch_known&&!value->epoch&&!g_xui_content_owner.baseline_frozen&&
            !dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0))value->origin=xui_instance::Origin::CleanStartupExternal;
    return true;
}
std::uintptr_t __fastcall HookXuiFactoryCreateLegacy(void* factory,void* type,void* argument,void* owner)noexcept
{
    ActiveHookCall call(g_xui_factory_statuses[0]);xui_content::Key key={};xui_instance::Writer writer={};
    const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);
    const auto result=g_original_xui_factory_create(factory,type,argument,owner);
    if(tracked&&result){xui_instance::Instance value={};if(ObserveXuiObject(result,reinterpret_cast<std::uintptr_t>(owner),key,&value,false))
        (void)xui_instance::Created(&g_xui_instances,value);}
    EndXuiInstanceWriter(&writer);return result;
}
std::uintptr_t __fastcall HookXuiFactoryCreate(void* factory,void* type,void* argument,void* owner)
{
    if(!g_xui_birth_requested)return HookXuiFactoryCreateLegacy(factory,type,argument,owner);
    HookStatusRecord* status=g_xui_factory_statuses[0];if(status){dluma_atomic_increment(&status->active_calls);++g_current_thread_hook_call_depth;}
    xui_content::Key key={};xui_instance::Writer writer={};std::uintptr_t result=0;bool returned=false;
    const bool tracked=XuiBirthObservationEnabled()&&!dluma_atomic_compare_exchange(&g_trace_stopping,0,0)&&BeginXuiInstanceWriter(factory,&key,&writer);
    XuiCreationFrame frame={g_xui_creation_frame,key,reinterpret_cast<std::uintptr_t>(factory),0,0,tracked,false,false};
    g_xui_creation_frame=&frame;
    __try{result=g_original_xui_factory_create(factory,type,argument,owner);returned=true;}
    __finally{
        g_xui_creation_frame=frame.previous;
        if(tracked){
            if(frame.generation)(void)xui_instance::FinishBirth(&g_xui_instances,key,frame.object,frame.generation,returned,result);
            else if((!returned||result)&&!frame.skipped)(void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::MissingBirth,key,result);
            EndXuiInstanceWriter(&writer);
        }
        if(status){--g_current_thread_hook_call_depth;dluma_atomic_decrement(&status->active_calls);}
    }
    return result;
}
using XuiCreateNotifyFn=std::uintptr_t(__fastcall*)(void*,void*);
std::uintptr_t XuiFactoryCreateNotify(void* object,void* argument,unsigned slot)
{
    // The unchanged native caller supplies these two arguments. Read its
    // original virtual target before observer work and preserve raw RAX.
    const auto vtable=*reinterpret_cast<std::uintptr_t*>(object);
    auto original=*reinterpret_cast<XuiCreateNotifyFn*>(vtable+slot);
    HookStatusRecord* status=g_xui_birth_status;if(status){dluma_atomic_increment(&status->active_calls);++g_current_thread_hook_call_depth;}
    std::uintptr_t result=0;auto* frame=g_xui_creation_frame;
    __try{
        if(frame&&frame->tracked){
            if(dluma_atomic_compare_exchange(&g_trace_stopping,0,0))frame->skipped=true;
            else if(frame->notified)(void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::DuplicateBirth,frame->manager,reinterpret_cast<std::uintptr_t>(object));
            else {
                frame->notified=true;frame->object=reinterpret_cast<std::uintptr_t>(object);
                std::uintptr_t factory=0,owner=0;xui_instance::Instance value={};
                if(!ReadBytes(frame->object+24,&factory,8)||factory!=frame->factory||!ReadBytes(frame->object+40,&owner,8))
                    (void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackRead,frame->manager,frame->object,frame->factory,factory);
                else if(ObserveXuiObject(frame->object,owner,frame->manager,&value,true))
                    (void)xui_instance::Born(&g_xui_instances,value,&frame->generation);
            }
        }
        result=original(object,argument);
    }__finally{if(status){--g_current_thread_hook_call_depth;dluma_atomic_decrement(&status->active_calls);}}
    return result;
}
std::uintptr_t __fastcall HookXuiCreateRuntime(void* object,void* argument){return XuiFactoryCreateNotify(object,argument,872);}
std::uintptr_t __fastcall HookXuiCreateEditor(void* object,void* argument){return XuiFactoryCreateNotify(object,argument,880);}
struct XuiRetirementFrame {XuiRetirementFrame* previous;xui_content::Key manager;std::uintptr_t object;std::uint64_t generation;};
thread_local XuiRetirementFrame* g_xui_retirement_frame;
struct XuiBulkRetirementFrame {XuiBulkRetirementFrame* previous;xui_content::Key manager;const xui_instance::Writer* writer;};
thread_local XuiBulkRetirementFrame* g_xui_bulk_retirement_frame;
void __fastcall HookXuiFactoryRetire(void* factory,void* object)
{
    HookStatusRecord* status=g_xui_factory_statuses[1];if(status){dluma_atomic_increment(&status->active_calls);++g_current_thread_hook_call_depth;}
    xui_content::Key key={};xui_instance::Writer writer={};const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);bool returned=false;
    const auto address=reinterpret_cast<std::uintptr_t>(object);std::uint8_t before=0;std::uint64_t nested=0,generation=0;bool observed=false;
    const xui_instance::Writer* bulk_writer=nullptr;
    if(tracked&&object){
        if(address>UINTPTR_MAX-65||!ReadBytes(address+64,&before,1))(void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackRead,key,address);
        else {
            if((before&3)==1)for(auto* outer=g_xui_retirement_frame;outer;outer=outer->previous)
                if(outer->generation&&outer->object==address&&outer->manager.manager==key.manager&&outer->manager.generation==key.generation){nested=outer->generation;break;}
            if((before&3)==1&&!nested)for(auto* bulk=g_xui_bulk_retirement_frame;bulk;bulk=bulk->previous)
                if(bulk->writer&&bulk->manager.manager==key.manager&&bulk->manager.generation==key.generation){bulk_writer=bulk->writer;break;}
            observed=xui_instance::RetirementEntry(&g_xui_instances,key,address,before&3,nested,&generation,bulk_writer);
        }
    }
    XuiRetirementFrame frame={g_xui_retirement_frame,key,address,observed&&!(before&3)?generation:0};
    g_xui_retirement_frame=&frame;
    __try{g_original_xui_factory_retire(factory,object);returned=true;}
    __finally{
        g_xui_retirement_frame=frame.previous;
        if(observed&&!(before&3)){std::uint8_t flags=0;
            if(!returned||!ReadBytes(address+64,&flags,1))
                (void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackRead,key,address);
            else if((flags&3)!=2)(void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackFlags,key,address,2,flags&3);
            else (void)xui_instance::Retired(&g_xui_instances,key,address,generation);
        }
        else if(observed&&!returned)(void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackRead,key,address);
        EndXuiInstanceWriter(&writer);if(status){--g_current_thread_hook_call_depth;dluma_atomic_decrement(&status->active_calls);}
    }
}
void __fastcall HookXuiFactoryCollect(void* factory)
{
    HookStatusRecord* status=g_xui_factory_statuses[2];if(status){dluma_atomic_increment(&status->active_calls);++g_current_thread_hook_call_depth;}
    xui_content::Key key={};xui_instance::Writer writer={};const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);
    auto address=reinterpret_cast<std::uintptr_t>(factory);std::uint32_t before=0;const bool read=tracked&&ReadBytes(address+32,&before,4);bool returned=false;
    __try{g_original_xui_factory_collect(factory);returned=true;}
    __finally{
        if(tracked){std::uintptr_t head=0;std::uint32_t after=0;bool empty=returned&&read&&ReadBytes(address+24,&head,8)&&ReadBytes(address+32,&after,4)&&!head&&!after;
            if(g_xui_birth_requested)(void)xui_instance::CollectedNested(&g_xui_instances,key,before,empty,writer);
            else (void)xui_instance::Collected(&g_xui_instances,key,before,empty,writer.revision);}
        EndXuiInstanceWriter(&writer);if(status){--g_current_thread_hook_call_depth;dluma_atomic_decrement(&status->active_calls);}
    }
}
void __fastcall HookXuiFactoryRetireAll(void* factory)
{
    HookStatusRecord* status=g_xui_factory_statuses[3];if(status){dluma_atomic_increment(&status->active_calls);++g_current_thread_hook_call_depth;}
    xui_content::Key key={};xui_instance::Writer writer={};const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);bool returned=false;
    XuiBulkRetirementFrame frame={g_xui_bulk_retirement_frame,key,tracked&&g_xui_birth_requested?&writer:nullptr};g_xui_bulk_retirement_frame=&frame;
    __try{g_original_xui_factory_retire_all(factory);returned=true;}
    __finally{
        g_xui_bulk_retirement_frame=frame.previous;
        if(tracked){std::uintptr_t head=0;std::uint32_t count=0;auto address=reinterpret_cast<std::uintptr_t>(factory);
            if(!returned||!ReadBytes(address+8,&head,8)||!ReadBytes(address+16,&count,4)||head||count)
                (void)xui_instance::MarkLost(&g_xui_instances,xui_instance::Loss::CallbackRead,key,address);
            else (void)xui_instance::AllRetired(&g_xui_instances,key,g_xui_birth_requested?&writer:nullptr);}
        EndXuiInstanceWriter(&writer);if(status){--g_current_thread_hook_call_depth;dluma_atomic_decrement(&status->active_calls);}
    }
}
