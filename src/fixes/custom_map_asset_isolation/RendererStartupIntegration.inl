// Exact Player engine: rbridge resolves CreateRenderer only after its native
// LoadLibrary helper has returned, before constructing the renderer. Keep this
// temporary import route separate from the full isolation hook-plan storage.
constexpr std::uintptr_t kRendererGetProcAddressSlotRva = 0x856398;
constexpr std::uintptr_t kRendererCreateLookupReturnRva = 0x79C011;
using RendererGetProcAddressFn = FARPROC (WINAPI*)(HMODULE,LPCSTR);
RendererGetProcAddressFn g_renderer_startup_get_proc;
HookRoute g_renderer_startup_route = {};
std::uintptr_t g_renderer_startup_engine_base;
volatile LONG g_renderer_startup_phase; // 0 unused, 1 armed, 2 running, 3 done, 4 stopped/failed
volatile LONG g_renderer_startup_active;

void CompleteRendererStartup() noexcept
{
    if (dluma_atomic_compare_exchange(&g_renderer_startup_phase,2,1)!=1) return;
    if (!RestoreRoute(&g_renderer_startup_route)) {
        dluma_atomic_exchange(&g_renderer_startup_phase,4);
        dluma_log_pair("WARN","isolation_renderer_startup","import_restore_failed");
        (void)MakeStatus(FixState::RuntimeFailed);
        return;
    }
    if (!g_dluma.stop_requested) {
        dluma_log_pair("FIX","isolation_renderer_startup","before_native_renderer_creation");
        if(!FinishDeferredRendererObservation()){
            dluma_log_pair("WARN","isolation_renderer_startup","renderer_observation_failed");
            (void)MakeStatus(FixState::RuntimeFailed);
        }
    }
    dluma_atomic_exchange(&g_renderer_startup_phase,3);
}

FARPROC WINAPI HookRendererStartupGetProcAddress(HMODULE module,LPCSTR name)
{
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    dluma_atomic_increment(&g_renderer_startup_active);
    FARPROC result=nullptr;
    __try {
        result=g_renderer_startup_get_proc(module,name);
        const DWORD original_error=GetLastError();
        if (result && !g_dluma.stop_requested &&
            dluma_atomic_compare_exchange(&g_renderer_startup_phase,0,0)==1 &&
            caller==g_renderer_startup_engine_base+kRendererCreateLookupReturnRva &&
            module==GetModuleHandleW(L"rd3d11_x64_rwdi.dll"))
            CompleteRendererStartup();
        SetLastError(original_error);
    } __finally {
        dluma_atomic_decrement(&g_renderer_startup_active);
    }
    return result;
}

FixStatus ArmRendererStartup(const RuntimeContext& context) noexcept
{
    if (dluma_atomic_compare_exchange(&g_renderer_startup_phase,0,0)!=0)
        return g_status;
    if (!SupportedIdentity(context) ||
        !ValidateLoadedModuleSha256(context.engine_module,kSupportedEngineSha256))
        return MakeStatus(FixState::UnsupportedBuild);
    const auto slot=context.engine_base+kRendererGetProcAddressSlotRva;
    std::uintptr_t current=0;
    if (!ReadBytes(slot,&current,sizeof(current)) ||
        current!=reinterpret_cast<std::uintptr_t>(&GetProcAddress) ||
        !PinLoaderIfNeeded(false)) return MakeStatus(FixState::UnsupportedBuild);
    g_renderer_startup_engine_base=context.engine_base;
    g_renderer_startup_get_proc=reinterpret_cast<RendererGetProcAddressFn>(current);
    auto& route=g_renderer_startup_route;
    route.kind=HookRouteKind::Pointer;route.address=slot;
    route.original_target=reinterpret_cast<void*>(current);
    route.hook_target=reinterpret_cast<void*>(&HookRendererStartupGetProcAddress);
    route.byte_count=sizeof(void*);route.stolen_size=sizeof(void*);route.enabled=true;
    std::memcpy(route.original_bytes,&current,sizeof(current));
    std::memcpy(route.routing_bytes,&route.hook_target,sizeof(route.hook_target));
    (void)MakeStatus(FixState::RuntimePartial,slot,kRendererGetProcAddressSlotRva,true);
    dluma_atomic_exchange(&g_renderer_startup_phase,1);
    if (!ApplyRoute(&route)) {
        dluma_atomic_exchange(&g_renderer_startup_phase,4);
        const bool restored=RestoreRoute(&route);
        return MakeStatus(restored?FixState::UnsupportedBuild:FixState::ApplyFailed,
            slot,kRendererGetProcAddressSlotRva,!restored);
    }
    dluma_log_pair("FIX","isolation_renderer_startup","waiting_for_native_renderer_load");
    return g_status;
}

bool StopRendererStartup() noexcept
{
    dluma_atomic_exchange(&g_renderer_startup_phase,4);
    const bool restored=RestoreRoute(&g_renderer_startup_route);
    for (DWORD waited=0;waited<kQuiesceTimeoutMs;++waited) {
        if (!dluma_atomic_compare_exchange(&g_renderer_startup_active,0,0)) return restored;
        Sleep(1);
    }
    (void)PinLoaderIfNeeded(true);
    return false;
}
