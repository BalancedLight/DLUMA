// Included inside the isolation implementation's namespace after native route
// builders and the exact source/frame readers have been defined.
bool ReadProductionResourceContext(void*,resourcegate::Context* out) noexcept
{
    if(!out)return false;std::memset(out,0,sizeof(*out));
    if(g_resource_startup_capture_scope){
        SerializedFrameRouteEvidence frame={};
        out->clean_startup=g_engine_full_profile_valid&&g_filesystem_full_profile_valid&&
            !content::CurrentEpochId(&g_content_ledger)&&!dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0)&&
            CurrentSerializedGameFrameRouteMatches(0,GetCurrentThreadId(),&frame)==statefive::RejectReason::C10ScopeMatched;
        return out->clean_startup;
    }
    curves::CurveGatewayContext current;std::memset(&current,0,sizeof(current));if(!ReadProductionCurveContext(nullptr,&current))return false;
    out->authority=current.authority;out->transaction=current.transaction;out->epoch=current.epoch;
    out->post_c10_consumers_closed=current.post_c10_consumers_closed;return true;
}
bool ReadProductionResourceSourceRevision(void*,std::uint64_t* out) noexcept
{ return out&&ReadStableSourceRegistryGeneration(out); }
bool ClassifyProductionResourceSource(void*,files::SourceKey source,retained::Realm* realm,std::uint32_t* epoch) noexcept
{
    if(!realm||!epoch||!source.address||!source.generation)return false;
    *realm=retained::Realm::Unknown;*epoch=0;files::SourceKey actual={};
    if(!filebridge::SourceIdentity(&g_native_file_bridge,source.address,&actual)||actual.generation!=source.generation)return false;
    for(std::size_t i=0;i<g_resource_baseline_source_count;++i)
        if(g_resource_baseline_sources[i].address==source.address&&g_resource_baseline_sources[i].generation==source.generation){
            *realm=retained::Realm::Global;return true;}
    char path[content::kSourcePathCapacity]={};
    if(!CopySourcePath(path,sizeof(path),reinterpret_cast<void*>(source.address)))return false;
    content::CustomSourceIdentitySnapshot project={};
    if(content::LookupExactCustomSourceIdentity(&g_content_ledger,reinterpret_cast<void*>(source.address),path,&project)){
        // Historical path/address rows cannot classify a new native source
        // incarnation. Active custom source publication is required here.
        if(!project.epoch||!project.source_active)return false;*realm=retained::Realm::Project;*epoch=project.epoch;return true;
    }
    std::uint32_t current=0;
    if(content::TryGetCurrentEpochId(&g_content_ledger,&current)&&!current&&
       !dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen,0,0)){
        *realm=retained::Realm::Global;return true;
    }
    return false;
}
bool IsMaterialResourceRoute(resourcegate::Route route) noexcept
{
    switch(route){case resourcegate::Route::MaterialPackLoad:case resourcegate::Route::MaterialReload:
    case resourcegate::Route::MaterialPackDestroy:case resourcegate::Route::MaterialAcquire:
    case resourcegate::Route::MaterialRebind:case resourcegate::Route::ContainerOpen:
    case resourcegate::Route::ContainerFetch:case resourcegate::Route::ContainerLoadData:
    case resourcegate::Route::ContainerDataSize:case resourcegate::Route::MaterialFileRead:
    case resourcegate::Route::MaterialRelease:case resourcegate::Route::MaterialAddRef:
    case resourcegate::Route::MaterialReleaseRef:return true;default:return false;}
}
void ObserveProductionResourceWrite(void*,resourcegate::Route route) noexcept
{
    ObservePotentialNativeDomainWrite(IsMaterialResourceRoute(route)?cold::ContentDomainId::NativeMaterialDefinitions:
        cold::ContentDomainId::NativeRetainedResources);
}
void NoteProductionResourceOpen(void* file,const char* actual_native_path) noexcept
{
    if(!file||!g_native_resources.initialized||!resourcegate::HasActiveInputScope(&g_native_resources.gateway))return;
    resourcegate::InputKey input={};
    (void)resourcegate::ObserveNativeOpenedFile(&g_native_resources.gateway,reinterpret_cast<std::uintptr_t>(file),
        actual_native_path,retained::Realm::Unknown,0,&input);
}
bool CaptureProductionResourceInput(void*,resourcegate::InputKey input,const files::FileRecord& file) noexcept
{
    if(file.kind!=files::FileKind::Zip)return resource_input_capture::Queue(&g_native_resources.capture,input,file);
    archiveinput::Digest digest={};
    if(!archiveinput::Copy(&g_archive_input_cache,file,&digest)||!digest.complete||!digest.cursor_restored||
       digest.result!=archiveinput::Result::Succeeded)return false;
    return resourcegate::CompleteInput(&g_native_resources.gateway,input,digest.sha256,digest.bytes);
}
bool BuildRendererResourceHookPlan() noexcept
{
    if(!g_renderer_full_profile_valid)return false;
    std::size_t count=0;const auto* renderer_hooks=renderer_streaming::Catalog(&count);
    for(std::size_t i=0;i<count;++i){const auto& spec=renderer_hooks[i];const auto address=g_context.renderer_base+spec.rva;
        auto* status=AddHookStatus(spec.name,"rd3d11_x64_rwdi.dll",address,spec.rva,spec.prefix,sizeof(spec.prefix),true);
        auto* route=AddFunctionDetour(status,address,spec.stolen,spec.relative_offset,spec.relative_end,
            reinterpret_cast<void*>(renderer_streaming::Wrapper(spec.route)));
        if(!route||!status->validated)return false;
        route->second_rip_displacement_offset=spec.second_relative_offset;
        route->second_rip_instruction_end_offset=spec.second_relative_end;
        g_renderer_stream_routes[i]=route;
    }
    return true;
}
bool BuildNativeResourceHookPlan() noexcept
{
    if(!g_fix_enabled)return true;
    if(!g_renderer_full_profile_valid){
        // Recheck only the naturally loaded module at this final pre-resource
        // installation seam. Never load a renderer to satisfy qualification.
        g_context.renderer_module=GetModuleHandleW(L"rd3d11_x64_rwdi.dll");
        g_renderer_full_profile_valid=ReadLoadedModuleIdentity(g_context.renderer_module,&g_context.renderer_base,
            &g_context.renderer_timestamp,&g_context.renderer_size_of_image)&&
            g_context.renderer_timestamp==kSupportedRendererTimestamp&&g_context.renderer_size_of_image==kSupportedRendererImageSize&&
            ValidateLoadedModuleSha256(g_context.renderer_module,renderer_streaming::kPlayerRendererSha256);
    }
    if(!g_engine_full_profile_valid||!g_filesystem_full_profile_valid||
        (g_context.renderer_module&&!g_renderer_full_profile_valid)){
        dluma_log_pair("WARN","custom_content_resource_hook_plan",
            !g_engine_full_profile_valid?"engine_profile_unavailable":
            !g_filesystem_full_profile_valid?"filesystem_profile_unavailable":
            !g_context.renderer_module?"renderer_not_loaded_at_installation":
            "renderer_identity_validation_failed");
        return false;
    }
    std::size_t count=0;const auto* catalog=resourcegate::Catalog(&count);
    for(std::size_t i=0;i<count;++i){const auto& spec=catalog[i];HookRoute* existing=nullptr;
        const auto base=spec.pointer_in_filesystem?g_context.filesystem_base:g_context.engine_base;
        const auto address=spec.pointer_slot?base+spec.pointer_slot:g_context.engine_base+spec.engine_rva;
        for(std::size_t j=0;j<g_hook_route_count;++j){auto& route=g_hook_routes[j];
            if(route.enabled&&route.address==address){if(existing||!spec.shared_observer)return false;existing=&route;}}
        if(existing){existing->owner->mandatory=true;g_resource_routes[i]=existing;continue;}
        void* wrapper=reinterpret_cast<void*>(resourcegate::Wrapper(spec.route));
        if(spec.pointer_slot){
            void* original=reinterpret_cast<void*>(spec.engine_rva?g_context.engine_base+spec.engine_rva:
                g_context.filesystem_base+spec.filesystem_rva);
            auto* status=AddHookStatus(spec.name,spec.pointer_in_filesystem?"filesystem_x64_rwdi.dll":"engine_x64_rwdi.dll",
                address,spec.pointer_slot,reinterpret_cast<const std::uint8_t*>(&original),sizeof(original),true);
            g_resource_routes[i]=AddPointerRoute(status,address,original,wrapper);
        }else{
            auto* status=AddHookStatus(spec.name,"engine_x64_rwdi.dll",address,spec.engine_rva,spec.prefix,spec.prefix_size,true);
            g_resource_routes[i]=AddFunctionDetour(status,address,spec.stolen_size,spec.displacement_offset,spec.instruction_end,wrapper);
        }
        if(!g_resource_routes[i]||!g_resource_routes[i]->owner->validated)return false;
    }
    return !g_context.renderer_module||BuildRendererResourceHookPlan();
}
bool PrepareRendererEntryUnwind(HookRoute* route,renderer_streaming::Route entry) noexcept
{
    if(!route||!route->relay_allocation||route->kind!=HookRouteKind::FunctionDetour)return false;
    unsigned char codes[64]={};std::size_t length=0;
    if(!renderer_streaming::CopyEntryUnwind(&g_native_resources.renderer,entry,codes,sizeof(codes),&length))return false;
    auto* bytes=static_cast<unsigned char*>(route->relay_allocation);DWORD old=0;
    if(!VirtualProtect(bytes,0x1000,PAGE_READWRITE,&old))return false;
    std::memcpy(bytes+0x80,codes,length);
    auto* table=reinterpret_cast<DlumaNativeFileRuntimeFunction*>(bytes+0x100);
    *table={0,static_cast<DWORD>(route->stolen_size+14),0x80};
    const bool registered=RtlAddFunctionTable(table,1,reinterpret_cast<std::uintptr_t>(bytes))!=0;
    route->relay_unwind_registered=registered;DWORD ignored=0;
    const bool restored=VirtualProtect(bytes,0x1000,old,&ignored)!=FALSE;
    return registered&&restored;
}
void PrepareNativeResourceBridge() noexcept
{
    g_resource_bridge_prepared=false;
    if(!g_fix_enabled)return;
    resourcegate::Callbacks callbacks={};callbacks.read=&ReadAudioBaselineMemory;callbacks.context=&ReadProductionResourceContext;
    callbacks.potential=&ObserveProductionResourceWrite;callbacks.source_revision=&ReadProductionResourceSourceRevision;
    callbacks.pin_input=&CaptureProductionResourceInput;
    callbacks.full_path=&ObserveProductionXuiFullPath;
    callbacks.classify_source=&ClassifyProductionResourceSource;
    if(!resourceprod::Initialize(&g_native_resources,g_context.engine_base,g_context.filesystem_base,
        retained::windows::kPlayerEngineSha256,filewin::kFilesystemSha256,callbacks,&g_native_file_bridge,
        &g_dluma_audio_full_path_provider_probe_state,reinterpret_cast<std::uintptr_t>(&dluma_audio_full_path_provider_probe),
        g_context.renderer_base,renderer_streaming::kPlayerRendererSha256))return;
    std::size_t count=0;const auto* catalog=resourcegate::Catalog(&count);
    for(std::size_t i=0;i<count;++i){const auto& spec=catalog[i];const auto* route=g_resource_routes[i];
        if(!route||!route->enabled||!route->owner||!route->owner->validated)return;
        const auto original=reinterpret_cast<std::uintptr_t>(route->kind==HookRouteKind::FunctionDetour?route->relay:route->original_target);
        if(!resourcegate::BindOriginal(&g_native_resources.gateway,spec.route,original,reinterpret_cast<std::uintptr_t>(route->hook_target)))return;
        // Preserve the existing monolith observer as the published hook. Its
        // original invokes our gateway, whose original is the native relay.
        if(spec.shared_observer&&route->hook_target!=reinterpret_cast<void*>(resourcegate::Wrapper(spec.route))){
            const auto wrapper=resourcegate::Wrapper(spec.route);
            switch(spec.route){
            case resourcegate::Route::PackRelease:g_original_internal_pack_release=reinterpret_cast<InternalLevelPackReleaseFn>(wrapper);break;
            case resourcegate::Route::DemandEvict:g_original_on_demand_evict=reinterpret_cast<OnDemandEvictFn>(wrapper);break;
            case resourcegate::Route::MaterialPackLoad:g_original_materials_pack_load=reinterpret_cast<MaterialsPackLoadFn>(wrapper);break;
            case resourcegate::Route::MaterialPackDestroy:g_original_materials_pack_destructor=reinterpret_cast<MaterialsPackDestructorFn>(wrapper);break;
            case resourcegate::Route::MaterialReload:g_original_material_reload_local_materials=reinterpret_cast<MaterialReloadLocalMaterialsFn>(wrapper);break;
            case resourcegate::Route::MaterialRebind:g_original_material_existing_rebind=reinterpret_cast<MaterialExistingRebindFn>(wrapper);break;
            default:return;}
        }
    }
    const auto* renderer_hooks=renderer_streaming::Catalog(&count);
    for(std::size_t i=0;g_context.renderer_module&&i<count;++i){auto* route=g_renderer_stream_routes[i];
        if(!route||!route->enabled||!route->owner||!route->owner->validated||!route->relay||
           !PrepareRendererEntryUnwind(route,renderer_hooks[i].route)||
           !renderer_streaming::BindOriginal(&g_native_resources.renderer,renderer_hooks[i].route,
               reinterpret_cast<std::uintptr_t>(route->relay)))return;
    }
    g_resource_bridge_prepared=resourcegate::PublishBridge(&g_native_resources.gateway)&&
        (!g_context.renderer_module||renderer_streaming::Publish(&g_native_resources.renderer));
}
void SealNativeResourceBridge() noexcept
{
    if(!g_fix_enabled)return;
    const bool sealed=resourceprod::SealEngine(&g_native_resources);
    const bool complete=sealed&&g_native_resources.renderer.validated&&resourceprod::Seal(&g_native_resources);
    dluma_log_pair("FIX","isolation_native_resources",complete?"startup_observation_sealed":
        sealed&&!g_native_resources.renderer.validated?"engine_observation_sealed_waiting_for_renderer":"incomplete_native_owner_observation");
}
bool FinishDeferredRendererObservation() noexcept
{
    if(!g_resource_bridge_prepared||!g_native_resources.initialized||g_native_resources.renderer.validated||
        g_dluma.stop_requested||content::CurrentEpochId(&g_content_ledger))return false;
    g_context.renderer_module=GetModuleHandleW(L"rd3d11_x64_rwdi.dll");
    g_renderer_full_profile_valid=ReadLoadedModuleIdentity(g_context.renderer_module,&g_context.renderer_base,
        &g_context.renderer_timestamp,&g_context.renderer_size_of_image)&&
        g_context.renderer_timestamp==kSupportedRendererTimestamp&&g_context.renderer_size_of_image==kSupportedRendererImageSize&&
        ValidateLoadedModuleSha256(g_context.renderer_module,renderer_streaming::kPlayerRendererSha256);
    if(!g_renderer_full_profile_valid||!resourceprod::InitializeRenderer(&g_native_resources,g_context.renderer_base,
        renderer_streaming::kPlayerRendererSha256))return false;
    const auto first_route=g_hook_route_count;
    bool ready=BuildRendererResourceHookPlan();
    std::size_t count=0;const auto* hooks=renderer_streaming::Catalog(&count);
    for(std::size_t i=0;ready&&i<count;++i){auto* route=g_renderer_stream_routes[i];
        ready=route&&PrepareRelay(route)&&PrepareRendererEntryUnwind(route,hooks[i].route)&&
            renderer_streaming::BindOriginal(&g_native_resources.renderer,hooks[i].route,reinterpret_cast<std::uintptr_t>(route->relay));
    }
    ready=ready&&renderer_streaming::Publish(&g_native_resources.renderer);
    for(std::size_t i=first_route;ready&&i<g_hook_route_count;++i)ready=ApplyRoute(&g_hook_routes[i]);
    if(!ready){
        for(std::size_t i=g_hook_route_count;i>first_route;--i){auto* route=&g_hook_routes[i-1];
            if(RestoreRoute(route)&&!route->installed){route->enabled=false;FreeRouteRelay(route);}
        }
        RecomputeInstalledStatuses();return false;
    }
    RecomputeInstalledStatuses();
    const bool complete=resourceprod::Seal(&g_native_resources);
    dluma_log_pair("FIX","isolation_native_resources",complete?"startup_observation_sealed":"renderer_seal_failed");
    dluma_log_u64("FIX","isolation_resource_engine_sealed",g_native_resources.gateway.sealed.load()?1:0);
    dluma_log_u64("FIX","isolation_resource_renderer_sealed",g_native_resources.renderer.sealed?1:0);
    return complete;
}
cold::DomainResult ColdResourcesBaseline(void*,const cold::RestartContext* context) noexcept
{
    if(!context||!context->baseline_generation||context->owner_thread_id!=GetCurrentThreadId()||
       !g_native_resources.available||!g_startup_filesystem_inventory.complete)return cold::DomainResult::Unsupported;
    if(!g_resource_baseline_source_count){
        for(std::size_t i=0;i<g_startup_filesystem_inventory.source_count;++i){files::SourceKey key={};
            if(i>=vfs::kFilesystemSourceInventoryCapacity||!filebridge::SourceIdentity(&g_native_file_bridge,
                g_startup_filesystem_inventory.sources[i].source,&key))return cold::DomainResult::Unsupported;
            g_resource_baseline_sources[i]=key;}
        g_resource_baseline_source_count=g_startup_filesystem_inventory.source_count;
    }
    g_resource_startup_capture_scope=true;bool complete=false;
    __try{complete=resourceprod::CaptureBaseline(&g_native_resources,context->baseline_generation);}
    __finally{g_resource_startup_capture_scope=false;}
    return complete?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;
}
bool ResourceTransactionMatches(const cold::RestartContext* context) noexcept
{
    return context&&context->content_epoch&&context->transaction_generation&&context->owner_thread_id==GetCurrentThreadId()&&
        context->baseline_generation==g_native_resources.baseline_generation&&g_native_resources.available&&
        ProductionDomainTransactionMatches(context,cold::ContentDomainId::NativeRetainedResources);
}
cold::DomainResult ColdResourcesPreflight(void*,const cold::RestartContext* context) noexcept
{return ResourceTransactionMatches(context)&&resourceprod::PreflightResources(&g_native_resources,context->transaction_generation,context->content_epoch)
    ?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;}
cold::DomainResult ColdResourcesQuiesce(void*,const cold::RestartContext* context) noexcept
{return ResourceTransactionMatches(context)?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;}
cold::DomainResult ColdResourcesReset(void*,const cold::RestartContext* context) noexcept
{return ResourceTransactionMatches(context)&&g_cold_reboot_registry.phase==cold::RestartPhase::Mutating&&
    resourceprod::ResetResources(&g_native_resources,context->transaction_generation,context->content_epoch)
    ?cold::DomainResult::Succeeded:cold::DomainResult::Failed;}
cold::DomainResult ColdResourcesRestored(void*,const cold::RestartContext* context) noexcept
{return ResourceTransactionMatches(context)&&resourceprod::VerifyResources(&g_native_resources,context->transaction_generation,context->content_epoch)
    ?cold::DomainResult::Succeeded:cold::DomainResult::Failed;}
cold::DomainResult ColdMaterialsBaseline(void*,const cold::RestartContext* context) noexcept
{
    if(!context||!context->baseline_generation||context->owner_thread_id!=GetCurrentThreadId()||!g_native_resources.available)
        return cold::DomainResult::Unsupported;
    // The combined native reload806CC0 loads the replacement BEFORE releasing
    // the old pack. It is intentionally absent from registry Reset callbacks.
    // Material input observation remains live while its split owner is qualified.
    std::uintptr_t manager=0;
    if(!ReadBytes(g_context.engine_base+0xACD118,&manager,sizeof(manager))||!manager)return cold::DomainResult::Unsupported;
    g_resource_startup_capture_scope=true;bool complete=false;
    __try{complete=resourceprod::CaptureMaterialBaseline(&g_native_resources,manager,context->baseline_generation);}
    __finally{g_resource_startup_capture_scope=false;}
    return complete?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;
}
bool MaterialTransactionMatches(const cold::RestartContext* context) noexcept
{
    return context&&context->content_epoch&&context->transaction_generation&&context->owner_thread_id==GetCurrentThreadId()&&
        context->baseline_generation==g_native_resources.material.baseline.generation&&g_native_resources.available&&
        ProductionDomainTransactionMatches(context,cold::ContentDomainId::NativeMaterialDefinitions);
}
cold::DomainResult ColdMaterialsPreflight(void*,const cold::RestartContext* context) noexcept
{return MaterialTransactionMatches(context)&&resourceprod::PreflightMaterials(&g_native_resources,context->transaction_generation,context->content_epoch)
    ?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;}
cold::DomainResult ColdMaterialsQuiesce(void*,const cold::RestartContext* context) noexcept
{return MaterialTransactionMatches(context)?cold::DomainResult::Succeeded:cold::DomainResult::Unsupported;}
cold::DomainResult ColdMaterialsReset(void*,const cold::RestartContext* context) noexcept
{return MaterialTransactionMatches(context)&&g_cold_reboot_registry.phase==cold::RestartPhase::Mutating&&
    resourceprod::ResetMaterials(&g_native_resources,context->transaction_generation,context->content_epoch)
    ?cold::DomainResult::Succeeded:cold::DomainResult::Failed;}
cold::DomainResult ColdMaterialsRestored(void*,const cold::RestartContext* context) noexcept
{return MaterialTransactionMatches(context)&&resourceprod::VerifyMaterials(&g_native_resources,context->transaction_generation,context->content_epoch)
    ?cold::DomainResult::Succeeded:cold::DomainResult::Failed;}
