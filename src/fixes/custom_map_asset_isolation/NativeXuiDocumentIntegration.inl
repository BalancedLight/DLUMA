// Shared observer integration. No second object, source or generation ledger.
using XuiCompiledAttemptFn=std::uintptr_t(__fastcall*)(void*,int);
XuiCompiledAttemptFn g_original_xui_compiled_attempt;
HookStatusRecord* g_xui_compiled_attempt_status;
struct XuiDocumentInvocation {
    void* level;std::uintptr_t manager;std::uint64_t manager_generation;
    int screen;const DlumaXuiDocumentSource* source;
    unsigned selected,observed,input_count,complete_inputs;bool invalid;
    std::uint64_t first_input_bytes;char first_input_path[512];
    unsigned first_input_stage;std::uintptr_t first_input_file,first_input_vtable;
    char owner_names[DLUMA_XUI_DOCUMENT_COMPONENTS][128];
    char hud_name[128];
};
thread_local XuiDocumentInvocation* g_xui_document_invocation;
struct DocumentMapBinding : DlumaXuiDocumentBinding {
    char owner_names[DLUMA_XUI_DOCUMENT_COMPONENTS][128];bool owners_valid;
};
std::atomic_flag g_document_binding_lock=ATOMIC_FLAG_INIT;
DocumentMapBinding g_document_binding={};
volatile LONG g_document_diagnostic_claimed;
bool DocumentDiagnosticFailure(unsigned code,unsigned index=UINT32_MAX,std::uintptr_t actual=0,std::uintptr_t expected=0,std::uintptr_t expected_level=0)noexcept;

bool NativeXuiBorrowContext(void** level,std::uintptr_t* manager)noexcept
{
    if(level)*level=nullptr;if(manager)*manager=0;
    if(!level||!manager||!g_initialized||!g_engine_full_profile_valid||!g_gamedll_full_profile_valid||
       !g_xui_manager_observation_ready||!g_xui_instance_observation_ready||!ProductionXuiHooksCurrent()||
       !dluma_atomic_compare_exchange(&g_native_file_bridge.observing,0,0)||
       !g_xui_compiled_attempt_status||!g_xui_compiled_attempt_status->installed)return false;
    std::uintptr_t native=0;void* after=nullptr;xui::ManagerRecord record={};
    return dluma_player_media_adapter_get_session_level(level)&&*level&&
        ReadBytes(reinterpret_cast<std::uintptr_t>(*level)+8,&native,8)&&native&&
        ReadBytes(native+1392,manager,8)&&*manager&&xui::LookupManager(&g_xui_manager_ledger,*manager,&record)&&record.life==xui::ManagerLife::Live&&
        dluma_player_media_adapter_get_session_level(&after)&&after==*level;
}
bool NativeDocumentFrame(void** level,std::uintptr_t* manager)noexcept
{
    return dluma_engine_game_frame_active_caller_matches(&g_dluma.engine,g_context.engine_module,0x251434U)&&NativeXuiBorrowContext(level,manager);
}
bool NativeDocumentEnabled()noexcept
{
#if defined(DLUMA_PRIVATE_XUI_DOCUMENT_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DOCUMENT_DIAGNOSTIC
    WCHAR value[4]={};return g_dluma.config.developer_mode&&GetEnvironmentVariableW(L"DLUMA_XUI_DOCUMENT_DIAGNOSTIC",value,4)==1&&value[0]==L'1';
#else
    return false;
#endif
}
bool NativeXuiObservationRequested()noexcept
{
    if(NativeDocumentEnabled())return true;
#if (defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC) || (defined(DLUMA_PRIVATE_TEXTURE_SWAP_DIAGNOSTIC) && DLUMA_PRIVATE_TEXTURE_SWAP_DIAGNOSTIC) || (defined(DLUMA_PRIVATE_XUI_DOCUMENT_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DOCUMENT_DIAGNOSTIC)
    WCHAR value[4]={};return g_dluma.config.developer_mode&&GetEnvironmentVariableW(L"DLUMA_XUI_OBSERVATION",value,4)==1&&value[0]==L'1';
#else
    return false;
#endif
}
bool NativeDocumentScope(const DlumaXuiDocumentIdentity* identity)noexcept
{
    void* level=nullptr;std::uintptr_t manager=0;
    return identity&&identity->map_generation&&identity->map_generation==dluma_lua_runtime_current_map_generation()&&
        NativeDocumentEnabled()&&NativeDocumentFrame(&level,&manager);
}
bool NativeDocumentCurrent(const DlumaXuiDocumentIdentity* identity)noexcept
{
    void* level=nullptr;std::uintptr_t manager=0;xui_content::Key key={};
    if(!NativeDocumentScope(identity)||!NativeDocumentFrame(&level,&manager)||!ReadXuiManagerKey(manager,&key))return false;
    while(g_document_binding_lock.test_and_set(std::memory_order_acquire))_mm_pause();
    const bool match=dluma_xui_document_binding_matches(&g_document_binding,*identity,manager,key.generation)!=0;
    g_document_binding_lock.clear(std::memory_order_release);return match;
}
bool NativeDocumentBind(const DlumaXuiDocumentIdentity* identity)noexcept
{
    void* level=nullptr;std::uintptr_t manager=0;xui_content::Key key={};
    if(!NativeDocumentScope(identity)||!NativeDocumentFrame(&level,&manager)||!ReadXuiManagerKey(manager,&key))return false;
    while(g_document_binding_lock.test_and_set(std::memory_order_acquire))_mm_pause();
    g_document_binding={};g_document_binding.manager=manager;g_document_binding.manager_generation=key.generation;g_document_binding.identity=*identity;
    dluma_atomic_exchange(&g_document_diagnostic_claimed,0);
    g_document_binding_lock.clear(std::memory_order_release);return true;
}
void NativeDocumentManagerDestroyed(std::uintptr_t manager,std::uint64_t generation)noexcept
{
    std::uint32_t map=0;while(g_document_binding_lock.test_and_set(std::memory_order_acquire))_mm_pause();
    if(g_document_binding.manager==manager&&g_document_binding.manager_generation==generation){map=g_document_binding.identity.map_generation;g_document_binding={};}
    g_document_binding_lock.clear(std::memory_order_release);
    if(map)dluma_xui_document_service_map_destroyed(map);
}
using DocumentGetOwner=void*(__fastcall*)(void*);
using DocumentGetName=const void*(__fastcall*)(void*);
using DocumentScreen=void*(__fastcall*)(void*,int);
using DocumentLookup=int(__fastcall*)(void*,const void*);
using DocumentScreenId=int(__fastcall*)(void*);
using DocumentNativeScreen=const void*(__fastcall*)(const void*);
struct DocumentString {const char* data;std::uint32_t size,capacity;};
bool DocumentDiagnosticFailure(unsigned code,unsigned index,std::uintptr_t actual,std::uintptr_t expected,std::uintptr_t expected_level)noexcept
{
    if(!NativeDocumentEnabled()||dluma_atomic_compare_exchange(&g_document_diagnostic_claimed,1,0))return false;
    std::uint64_t token=0;while(g_document_binding_lock.test_and_set(std::memory_order_acquire))_mm_pause();
    token=g_document_binding.identity.token;g_document_binding_lock.clear(std::memory_order_release);
    dluma_log_u64("WARN","xui_document_guard_token",token);
    dluma_log_u64("WARN","xui_document_guard_code",code);
    dluma_log_u64("WARN","xui_document_guard_index",index);
    dluma_log_u64("WARN","xui_document_guard_actual",actual);
    dluma_log_u64("WARN","xui_document_guard_expected",expected);
    if(expected_level){std::uintptr_t native=0;
        dluma_log_u64("WARN","xui_document_query_level",expected_level);
        if(ReadBytes(expected_level+8,&native,8))dluma_log_u64("WARN","xui_document_query_native_level",native);}
    xui_instance::ReceiverDiagnostics diagnostic={};
    if(xui_instance::CopyReceiverDiagnostics(&g_xui_instances,actual,&diagnostic)){
        dluma_log_u64("WARN","xui_document_instance_generation",diagnostic.generation);
        dluma_log_u64("WARN","xui_document_instance_health",(diagnostic.sealed?1U:0U)|(diagnostic.lost?2U:0U)|(diagnostic.index_lost?4U:0U)|(static_cast<std::uint64_t>(diagnostic.writers)<<8));
        dluma_log_u64("WARN","xui_document_receiver_matches",diagnostic.matches);
        if(diagnostic.first_loss.reason!=xui_instance::Loss::None){
            dluma_log_u64("WARN","xui_document_first_loss",static_cast<unsigned>(diagnostic.first_loss.reason));
            dluma_log_u64("WARN","xui_document_first_loss_generation",diagnostic.first_loss.generation);
            dluma_log_u64("WARN","xui_document_first_loss_object",diagnostic.first_loss.object);
            dluma_log_u64("WARN","xui_document_first_loss_manager",diagnostic.first_loss.manager.manager);
            dluma_log_u64("WARN","xui_document_first_loss_expected",diagnostic.first_loss.expected);
            dluma_log_u64("WARN","xui_document_first_loss_actual",diagnostic.first_loss.actual);
            dluma_log_u64("WARN","xui_document_first_loss_thread",diagnostic.first_loss.thread_id);
            dluma_log_u64("WARN","xui_document_first_loss_stack_count",diagnostic.first_loss.stack_count);
            for(std::uint32_t n=0;n<diagnostic.first_loss.stack_count&&n<16;++n)
                dluma_log_u64("WARN","xui_document_first_loss_stack_pc",diagnostic.first_loss.stack[n]);
        }
        if(diagnostic.matches){
            dluma_log_u64("WARN","xui_document_receiver_life",static_cast<unsigned>(diagnostic.first.life));
            dluma_log_u64("WARN","xui_document_receiver_manager",diagnostic.first.manager.manager);
            dluma_log_u64("WARN","xui_document_receiver_manager_generation",diagnostic.first.manager.generation);
            dluma_log_u64("WARN","xui_document_receiver_instance_generation",diagnostic.first.generation);
            dluma_log_u64("WARN","xui_document_receiver_origin",static_cast<unsigned>(diagnostic.first.origin));
            xui::ManagerRecord recorded={};
            if(xui::LookupManager(&g_xui_manager_ledger,diagnostic.first.manager.manager,&recorded)){
                dluma_log_u64("WARN","xui_document_receiver_manager_level",recorded.level);
                dluma_log_u64("WARN","xui_document_receiver_manager_life",static_cast<unsigned>(recorded.life));}
        }
    }
    return false;
}
bool DocumentHud(std::uintptr_t* hud)noexcept
{
    std::uintptr_t vt=0;
    if(!hud||!ReadBytes(g_context.gamedll_base+0x1D07FD0,hud,8))return DocumentDiagnosticFailure(110);
    if(!*hud)return DocumentDiagnosticFailure(111);
    if(!ReadBytes(*hud,&vt,8))return DocumentDiagnosticFailure(112,UINT32_MAX,*hud);
    return vt==g_context.gamedll_base+0x15BD918||DocumentDiagnosticFailure(113,UINT32_MAX,vt,g_context.gamedll_base+0x15BD918);
}
bool DocumentLocations(DlumaXuiDocumentSnapshot* snapshot,bool validate,const XuiDocumentInvocation* scope)noexcept
{
    std::uintptr_t values=0;std::uint32_t count=0;
    if(!snapshot||!ReadBytes(g_context.gamedll_base+0x1DF5E50,&values,8)||
       !ReadBytes(g_context.gamedll_base+0x1DF5E58,&count,4))return DocumentDiagnosticFailure(150);
    if(count>4096||(count&&!values))return DocumentDiagnosticFailure(151,count,values);
    std::uint64_t hash=1469598103934665603ULL;
    for(std::uint32_t i=0;i<count;++i){std::uintptr_t location=0,receiver=0;
        if(!ReadBytes(values+8ULL*i,&location,8)||!location||!ReadBytes(location+16,&receiver,8))return DocumentDiagnosticFailure(152,i,location);
        hash^=location;hash*=1099511628211ULL;
        if(!validate){if(receiver)snapshot->location_bound[i/64]|=1ULL<<(i%64);}
        else if(snapshot->location_bound[i/64]&(1ULL<<(i%64))){
            DlumaXuiReceiverObservation seen={};
            if(!receiver||!scope||!dluma_xui_observe_borrowed_receiver(reinterpret_cast<void*>(receiver),scope->level,
                reinterpret_cast<void*>(scope->manager),0,&seen)||seen.manager_generation!=scope->manager_generation)return DocumentDiagnosticFailure(153,i,receiver,scope?scope->manager:0);
        }
    }
    if(validate)return (snapshot->location_count==count&&snapshot->location_identity==hash)||DocumentDiagnosticFailure(154,count,hash,snapshot->location_identity);
    snapshot->location_count=count;snapshot->location_identity=hash;return true;
}
bool NativeDocumentSnapshot(DlumaXuiDocumentSnapshot* snapshot)noexcept
{
    if(!snapshot)return DocumentDiagnosticFailure(100);*snapshot={};void* level=nullptr;std::uintptr_t manager=0,hud=0;
    if(!dluma_texture_native_document_reload_ready())return DocumentDiagnosticFailure(101);
    if(!NativeDocumentFrame(&level,&manager))return DocumentDiagnosticFailure(102);
    if(!DocumentHud(&hud))return false;
    auto owner=reinterpret_cast<DocumentGetOwner>(GetProcAddress(g_context.engine_module,"?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ"));
    auto screen_id=reinterpret_cast<DocumentScreenId>(GetProcAddress(g_context.engine_module,"?GetScreenId@IUIScreen@@QEBAHXZ"));
    auto script=reinterpret_cast<DocumentGetName>(GetProcAddress(g_context.engine_module,"?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ"));
    xui_content::Key key={};char owner_names[DLUMA_XUI_DOCUMENT_COMPONENTS][128]={};
    if(!owner||!screen_id||!script||!ReadXuiManagerKey(manager,&key))return DocumentDiagnosticFailure(120);
    for(unsigned i=0;i<DLUMA_XUI_DOCUMENT_COMPONENTS;++i){std::uintptr_t receiver=0;
        if(!ReadBytes(hud+72+8ULL*i,&receiver,8))return DocumentDiagnosticFailure(130,i);
        if(receiver){DlumaXuiReceiverObservation seen={};
            if(!dluma_xui_observe_borrowed_receiver(reinterpret_cast<void*>(receiver),level,reinterpret_cast<void*>(manager),0,&seen))return DocumentDiagnosticFailure(131,i,receiver,manager,reinterpret_cast<std::uintptr_t>(level));
            if(!ReadBytes(receiver,&snapshot->component_classes[i],8))return DocumentDiagnosticFailure(132,i,receiver);
            void* screen=owner(reinterpret_cast<void*>(receiver));if(!screen)return DocumentDiagnosticFailure(133,i,receiver);
            snapshot->component_screens[i]=screen_id(screen);if(snapshot->component_screens[i]<0)return DocumentDiagnosticFailure(134,i,reinterpret_cast<std::uintptr_t>(screen));
            DocumentString name={};if(!ReadBytes(reinterpret_cast<std::uintptr_t>(script(screen)),&name,sizeof(name))||!name.data||!name.size||
                name.size>=sizeof(owner_names[i])||!ReadBytes(reinterpret_cast<std::uintptr_t>(name.data),owner_names[i],name.size))return DocumentDiagnosticFailure(135,i);
        }}
    if(!DocumentLocations(snapshot,false,nullptr))return false;
    while(g_document_binding_lock.test_and_set(std::memory_order_acquire))_mm_pause();
    const bool matched=g_document_binding.manager==manager&&g_document_binding.manager_generation==key.generation&&g_document_binding.identity.token;
    if(matched){std::memcpy(g_document_binding.owner_names,owner_names,sizeof(owner_names));g_document_binding.owners_valid=true;}
    g_document_binding_lock.clear(std::memory_order_release);return matched||DocumentDiagnosticFailure(136);
}
const void* DocumentXmlInput(void* manager,const void* path)noexcept
{
    auto* scope=g_xui_document_invocation;if(!scope||scope->manager!=reinterpret_cast<std::uintptr_t>(manager))return path;
    DocumentString logical={};char copy[32]={};
    if(!ReadBytes(reinterpret_cast<std::uintptr_t>(path),&logical,sizeof(logical))||logical.size>=sizeof(copy)||
       !logical.data||!ReadBytes(reinterpret_cast<std::uintptr_t>(logical.data),copy,logical.size))return path;
    if(dluma_stricmp(copy,"HUD.xui"))return path;
    if(++scope->selected!=1){scope->invalid=true;return path;}
    // The coordinator has already copied HUD.xui into its logical-name field.
    // Only this parser input changes, for the duration of this native call.
    thread_local DocumentString replacement={};replacement={scope->source->path,
        static_cast<std::uint32_t>(std::strlen(scope->source->path)),static_cast<std::uint32_t>(std::strlen(scope->source->path))};
    return &replacement;
}
void DocumentObserveInput(const xui_content::Input& input,unsigned stage,void* native_file,const char* native_path)noexcept
{
    auto* scope=g_xui_document_invocation;if(!scope||!scope->selected)return;
    if(!scope->input_count++){
        scope->first_input_bytes=input.bytes;
        scope->first_input_stage=stage;
        scope->first_input_file=reinterpret_cast<std::uintptr_t>(native_file);
        (void)ReadBytes(scope->first_input_file,&scope->first_input_vtable,sizeof(scope->first_input_vtable));
        (void)trace::CopyReadableCString(scope->first_input_path,sizeof(scope->first_input_path),native_path,sizeof(scope->first_input_path));
    }
    if(!input.complete)return;
    ++scope->complete_inputs;
    if(input.bytes==scope->source->bytes&&!std::memcmp(input.sha256,scope->source->sha256,32))++scope->observed;
}
std::uintptr_t __fastcall HookXuiCompiledAttempt(void* manager,int screen)noexcept
{
    ActiveHookCall call(g_xui_compiled_attempt_status);auto* scope=g_xui_document_invocation;
    if(scope&&scope->manager==reinterpret_cast<std::uintptr_t>(manager)){
        // The file registry retains earlier HUD.xui IDs after native reload.
        // Match THIS attempted registration's file key, not a first-name lookup.
        std::uintptr_t entries=0;std::uint32_t count=0;
        if(!ReadBytes(scope->manager+200,&entries,8)||!ReadBytes(scope->manager+208,&count,4)||count>4096||
            (count&&(!entries||entries>UINTPTR_MAX-80ULL*count))){scope->invalid=true;return g_original_xui_compiled_attempt(manager,screen);}
        for(std::uint32_t i=0;i<count;++i){int id=-1;const auto entry=entries+80ULL*i;
            if(!ReadBytes(entry+48,&id,4)){scope->invalid=true;break;}
            if(id==screen){DocumentString name={};char bytes[8]={};
                if(!ReadBytes(entry+8,&name,sizeof(name))){scope->invalid=true;break;}
                if(name.size==7&&name.data&&ReadBytes(reinterpret_cast<std::uintptr_t>(name.data),bytes,7)&&!dluma_stricmp(bytes,"HUD.xui"))return 0;
                break;
            }
        }
    }
    return g_original_xui_compiled_attempt(manager,screen);
}
bool CallNativeDocumentReload(void* level)noexcept
{
    __try{reinterpret_cast<void(__fastcall*)(void*)>(g_context.gamedll_base+0x3FCEF0)(level);return true;}
    __except(1){return false;}
}
bool NativeDocumentRebuildBody(const DlumaXuiDocumentIdentity* identity,const DlumaXuiDocumentSource* source,const DlumaXuiDocumentSnapshot* before,bool* reserved,bool* texture_reserved,bool* entered)noexcept
{
    if(!identity||!source||!before||!source->handle||g_xui_document_invocation||!NativeDocumentCurrent(identity))return DocumentDiagnosticFailure(200);
    XuiDocumentInvocation scope={};std::uintptr_t hud=0;
    if(!NativeDocumentFrame(&scope.level,&scope.manager))return DocumentDiagnosticFailure(201);
    if(!DocumentHud(&hud))return false;
    xui_content::Key key={};if(!ReadXuiManagerKey(scope.manager,&key))return DocumentDiagnosticFailure(202);scope.manager_generation=key.generation;scope.source=source;
    auto lookup=reinterpret_cast<DocumentLookup>(GetProcAddress(g_context.engine_module,"?GetUIScreenIdByScriptName@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z"));
    auto find=reinterpret_cast<DocumentScreen>(GetProcAddress(g_context.engine_module,"?FindUiScreen@ILevel@@QEBAPEAVIUIElement@@H@Z"));
    auto owner=reinterpret_cast<DocumentGetOwner>(GetProcAddress(g_context.engine_module,"?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ"));
    auto script=reinterpret_cast<DocumentGetName>(GetProcAddress(g_context.engine_module,"?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ"));
    auto screen_id=reinterpret_cast<DocumentScreenId>(GetProcAddress(g_context.engine_module,"?GetScreenId@IUIScreen@@QEBAHXZ"));
    auto native_screen=reinterpret_cast<DocumentNativeScreen>(GetProcAddress(g_context.engine_module,"?ToCUIScreen@IUIScreen@@QEBAPEBVCUIScreen@@XZ"));
    DocumentString logical={"HUD.xui",7,7};if(!lookup||!find||!owner||!script||!screen_id||!native_screen)return DocumentDiagnosticFailure(203);
    scope.screen=lookup(scope.level,&logical);if(scope.screen<0)return DocumentDiagnosticFailure(204);
    while(g_document_binding_lock.test_and_set(std::memory_order_acquire))_mm_pause();
    const bool names_current=g_document_binding.owners_valid&&dluma_xui_document_binding_matches(&g_document_binding,*identity,scope.manager,scope.manager_generation);
    if(names_current)std::memcpy(scope.owner_names,g_document_binding.owner_names,sizeof(scope.owner_names));
    g_document_binding_lock.clear(std::memory_order_release);if(!names_current)return DocumentDiagnosticFailure(208);
    for(unsigned i=0;i<DLUMA_XUI_DOCUMENT_COMPONENTS;++i)if(before->component_classes[i]&&before->component_screens[i]==scope.screen){
        std::memcpy(scope.hud_name,scope.owner_names[i],sizeof(scope.hud_name));break;
    }
    // On rollback the original transient ID may already have been replaced.
    if(!scope.hud_name[0])for(unsigned i=0;i<DLUMA_XUI_DOCUMENT_COMPONENTS;++i)if(before->component_classes[i]&&
        (!dluma_stricmp(scope.owner_names[i],"HUD")||!dluma_stricmp(scope.owner_names[i],"HUD.xui"))){
        std::memcpy(scope.hud_name,scope.owner_names[i],sizeof(scope.hud_name));break;
    }
    if(!scope.hud_name[0])return DocumentDiagnosticFailure(208);
    if(!dluma_texture_native_begin_document_reload())return DocumentDiagnosticFailure(205);
    *texture_reserved=true;
    if(!dluma_hud_visibility_backend_begin_document_reload())return DocumentDiagnosticFailure(206);
    *reserved=true;
    g_xui_document_invocation=&scope;
    *entered=true;
    const bool returned=CallNativeDocumentReload(scope.level);
    g_xui_document_invocation=nullptr;
    void* current_level=nullptr;std::uintptr_t current_manager=0;
    if(!returned)return DocumentDiagnosticFailure(210);
    if(scope.invalid||scope.selected!=1||!scope.observed){
        if(NativeDocumentEnabled()){
            dluma_log_u64("WARN","xui_document_input_count",scope.input_count);
            dluma_log_u64("WARN","xui_document_complete_inputs",scope.complete_inputs);
            dluma_log_u64("WARN","xui_document_first_input_bytes",scope.first_input_bytes);
            dluma_log_u64("WARN","xui_document_expected_input_bytes",scope.source->bytes);
            dluma_log_pair("WARN","xui_document_first_input_path",scope.first_input_path);
            dluma_log_pair("WARN","xui_document_expected_input_path",scope.source->path);
            dluma_log_u64("WARN","xui_document_content_lost",g_xui_content_owner.lost.load()?1U:0U);
            dluma_log_u64("WARN","xui_document_first_input_stage",scope.first_input_stage);
            dluma_log_u64("WARN","xui_document_first_input_file",scope.first_input_file);
            dluma_log_u64("WARN","xui_document_first_input_vtable",scope.first_input_vtable);
            dluma_log_u64("WARN","xui_document_file_ledger_failure",dluma_atomic_compare_exchange(&g_native_file_bridge.ledger.failure,0,0));
            dluma_log_u64("WARN","xui_document_filesystem_base",g_context.filesystem_base);
        }
        return DocumentDiagnosticFailure(211,scope.selected,scope.observed,scope.invalid?1U:0U);
    }
    if(!NativeDocumentCurrent(identity)||!NativeDocumentFrame(&current_level,&current_manager))return DocumentDiagnosticFailure(212);
    if(current_level!=scope.level||current_manager!=scope.manager)return DocumentDiagnosticFailure(213,UINT32_MAX,current_manager,scope.manager);
    if(!DocumentHud(&hud))return false;
    const auto hud_length=std::strlen(scope.hud_name);
    const DocumentString current_name={scope.hud_name,static_cast<std::uint32_t>(hud_length),static_cast<std::uint32_t>(hud_length)};const int current_screen=lookup(scope.level,&current_name);
    void* root=current_screen>=0?find(scope.level,current_screen):nullptr;void* root_owner=nullptr;
    if(!root)return DocumentDiagnosticFailure(215,static_cast<unsigned>(current_screen));
    for(unsigned i=0;i<DLUMA_XUI_DOCUMENT_COMPONENTS;++i)if(before->component_classes[i]){
        std::uintptr_t receiver=0,vt=0;DlumaXuiReceiverObservation seen={};
        if(!ReadBytes(hud+72+8ULL*i,&receiver,8)||!receiver||!ReadBytes(receiver,&vt,8)||vt!=before->component_classes[i]||
           !dluma_xui_observe_borrowed_receiver(reinterpret_cast<void*>(receiver),scope.level,reinterpret_cast<void*>(scope.manager),0,&seen)||seen.manager_generation!=scope.manager_generation)return DocumentDiagnosticFailure(221,i,receiver,scope.manager);
        void* owning_screen=owner(reinterpret_cast<void*>(receiver));
        const auto length=std::strlen(scope.owner_names[i]);const DocumentString expected_name={scope.owner_names[i],static_cast<std::uint32_t>(length),static_cast<std::uint32_t>(length)};
        const int registered=length?lookup(scope.level,&expected_name):-1;DocumentString actual_name={};char actual_bytes[128]={};
        if(!owning_screen||registered<0||screen_id(owning_screen)!=registered||
            !ReadBytes(reinterpret_cast<std::uintptr_t>(script(owning_screen)),&actual_name,sizeof(actual_name))||!actual_name.data||
            actual_name.size>=sizeof(actual_bytes)||!ReadBytes(reinterpret_cast<std::uintptr_t>(actual_name.data),actual_bytes,actual_name.size)||
            dluma_stricmp(actual_bytes,scope.owner_names[i]))return DocumentDiagnosticFailure(222,i,reinterpret_cast<std::uintptr_t>(owning_screen));
        if(registered==current_screen){
            if(root_owner&&owning_screen!=root_owner)return DocumentDiagnosticFailure(222,i,reinterpret_cast<std::uintptr_t>(owning_screen));
            root_owner=owning_screen;
        }
    }
    // A top-level screen need not have an enclosing owner. Resolve its screen
    // through qualified HUD children and verify the exported native/root pair.
    const auto native_root=root_owner?reinterpret_cast<std::uintptr_t>(native_screen(root_owner)):0;
    std::uintptr_t root_interface=0;DocumentString name={};char name_bytes[32]={};
    if(!native_root||native_root>UINTPTR_MAX-16||!ReadBytes(native_root+16,&root_interface,8)||root_interface!=reinterpret_cast<std::uintptr_t>(root)||
       !ReadBytes(reinterpret_cast<std::uintptr_t>(script(root_owner)),&name,sizeof(name))||!name.data||
       name.size>=sizeof(name_bytes)||!ReadBytes(reinterpret_cast<std::uintptr_t>(name.data),name_bytes,name.size)||
       (dluma_stricmp(name_bytes,"HUD.xui")&&dluma_stricmp(name_bytes,"HUD")))return DocumentDiagnosticFailure(214,static_cast<unsigned>(scope.screen),reinterpret_cast<std::uintptr_t>(root_owner),reinterpret_cast<std::uintptr_t>(root));
    DlumaXuiDocumentSnapshot locations=*before;return DocumentLocations(&locations,true,&scope);
}
DlumaXuiDocumentRebuildResult NativeDocumentRebuild(const DlumaXuiDocumentIdentity* identity,const DlumaXuiDocumentSource* source,const DlumaXuiDocumentSnapshot* before)noexcept
{
    if(!g_xui_compiled_attempt_status){(void)DocumentDiagnosticFailure(207);return DLUMA_XUI_DOCUMENT_REBUILD_REFUSED;}
    bool result=false,reserved=false,texture_reserved=false,entered=false;HookStatusRecord* status=g_xui_compiled_attempt_status;
    dluma_atomic_increment(&status->active_calls);++g_current_thread_hook_call_depth;
    __try{
        __try{result=NativeDocumentRebuildBody(identity,source,before,&reserved,&texture_reserved,&entered);}
        __except(1){(void)DocumentDiagnosticFailure(entered?299:209);result=false;}
    }__finally{
        g_xui_document_invocation=nullptr;
        if(reserved)dluma_hud_visibility_backend_end_document_reload();
        if(texture_reserved)dluma_texture_native_end_document_reload();
        --g_current_thread_hook_call_depth;dluma_atomic_decrement(&status->active_calls);
    }
    return result?DLUMA_XUI_DOCUMENT_REBUILD_SUCCEEDED:
        entered?DLUMA_XUI_DOCUMENT_REBUILD_ENTERED_FAILURE:DLUMA_XUI_DOCUMENT_REBUILD_REFUSED;
}
