bool XuiFactoryManager(void* factory,xui_content::Key* key)noexcept
{
    if(!key)return false;*key={};const auto address=reinterpret_cast<std::uintptr_t>(factory);
    if(address<16)return false;xui::ManagerRecord manager={};
    if(!g_xui_manager_observation_ready||!xui::LookupManager(&g_xui_manager_ledger,address-16,&manager))return false;
    *key={manager.manager,manager.generation};return true;
}
bool BeginXuiInstanceWriter(void* factory,xui_content::Key* key,xui_instance::Writer* writer)noexcept
{
    return XuiFactoryManager(factory,key)&&g_xui_instance_observation_ready&&
        xui_instance::BeginWriter(&g_xui_instances,writer);
}
void EndXuiInstanceWriter(xui_instance::Writer* writer)noexcept
{if(writer->active)(void)xui_instance::EndWriter(&g_xui_instances,writer);}
std::uintptr_t __fastcall HookXuiFactoryCreate(void* factory,void* type,void* argument,void* owner)noexcept
{
    ActiveHookCall call(g_xui_factory_statuses[0]);xui_content::Key key={};xui_instance::Writer writer={};
    const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);
    // This shared native factory is also used by SDK/global widgets. Preserve
    // its original call; unclassified external creation is retained separately.
    const auto result=g_original_xui_factory_create(factory,type,argument,owner);
    if(tracked&&result){xui_instance::Instance value={};value.manager=key;value.object=result;
        value.owner_context=reinterpret_cast<std::uintptr_t>(owner);std::uint8_t flags=0;
        const bool read=result<=UINTPTR_MAX-65&&ReadBytes(result,&value.vtable,8)&&
            ReadBytes(result+16,&value.interface_object,8)&&ReadBytes(result+64,&flags,1);
        value.life=(flags&3)==2?xui_instance::Life::Retired:xui_instance::Life::Active;
        value.origin=xui_instance::Origin::ExternalUnknown;
        const bool epoch_known=content::TryGetCurrentEpochId(&g_content_ledger,&value.epoch);
        if(g_xui_input_frame_count){const auto* frame=g_xui_input_frames[g_xui_input_frame_count-1];
            value.load_serial=frame->load.serial;value.origin=xui_instance::Origin::UiLoadPending;
            value.epoch=frame->load.epoch;
            if(frame->instance_input.complete){value.source=frame->instance_input.selected_source;
                std::memcpy(value.sha256,frame->instance_input.sha256,sizeof(value.sha256));value.exact_input=true;}
        }else if(epoch_known&&!value.epoch&&!g_xui_content_owner.baseline_frozen&&
            !dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0))value.origin=xui_instance::Origin::CleanStartupExternal;
        if(!read||(flags&1)||!xui_instance::Created(&g_xui_instances,value))g_xui_instances.lost=true;
    }
    EndXuiInstanceWriter(&writer);return result;
}
void __fastcall HookXuiFactoryRetire(void* factory,void* object)noexcept
{
    ActiveHookCall call(g_xui_factory_statuses[1]);xui_content::Key key={};xui_instance::Writer writer={};
    const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);
    g_original_xui_factory_retire(factory,object);
    if(tracked&&object){std::uint8_t flags=0;const auto address=reinterpret_cast<std::uintptr_t>(object);
        if(address>UINTPTR_MAX-65||!ReadBytes(address+64,&flags,1)||(flags&3)!=2||
            !xui_instance::Retired(&g_xui_instances,key,address))g_xui_instances.lost=true;}
    EndXuiInstanceWriter(&writer);
}
void __fastcall HookXuiFactoryCollect(void* factory)noexcept
{
    ActiveHookCall call(g_xui_factory_statuses[2]);xui_content::Key key={};xui_instance::Writer writer={};
    const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);const auto address=reinterpret_cast<std::uintptr_t>(factory);
    std::uint32_t before=0;const bool read=tracked&&ReadBytes(address+32,&before,4);
    g_original_xui_factory_collect(factory);
    if(tracked){std::uintptr_t head=0;std::uint32_t after=0;
        const bool empty=read&&ReadBytes(address+24,&head,8)&&ReadBytes(address+32,&after,4)&&!head&&!after;
        (void)xui_instance::Collected(&g_xui_instances,key,before,empty,writer.revision);}
    EndXuiInstanceWriter(&writer);
}
void __fastcall HookXuiFactoryRetireAll(void* factory)noexcept
{
    ActiveHookCall call(g_xui_factory_statuses[3]);xui_content::Key key={};xui_instance::Writer writer={};
    const bool tracked=BeginXuiInstanceWriter(factory,&key,&writer);
    g_original_xui_factory_retire_all(factory);
    if(tracked){std::uintptr_t head=0;std::uint32_t count=0;const auto address=reinterpret_cast<std::uintptr_t>(factory);
        if(!ReadBytes(address+8,&head,8)||!ReadBytes(address+16,&count,4)||head||count||
            !xui_instance::AllRetired(&g_xui_instances,key))g_xui_instances.lost=true;}
    EndXuiInstanceWriter(&writer);
}
