// Included after native hook builders and actual source/frame context readers.
namespace craftinggate = dluma::fixes::custom_map_asset_isolation::crafting_gateway;
namespace craftingowner = dluma::fixes::custom_map_asset_isolation::crafting_owner;
namespace craftingsnapshot = dluma::fixes::custom_map_asset_isolation::crafting_snapshot;
namespace blueprintrebuild = dluma::fixes::custom_map_asset_isolation::blueprint_rebuild;
namespace itemaggregate = dluma::fixes::custom_map_asset_isolation::item_aggregate;
craftinggate::Gateway g_crafting_gateway;
craftingowner::Owner g_crafting_owner = {};
blueprintrebuild::Baseline g_crafting_blueprint_baseline = {};
itemaggregate::Baseline g_crafting_aggregate_baseline = {};
HookRoute* g_crafting_routes[craftinggate::kHookCount] = {};
bool g_crafting_hooks_prepared = false;
bool g_crafting_hooks_available = false;
bool g_crafting_startup_qualified = false;
bool g_crafting_baseline_capture_scope = false;
std::uint64_t g_crafting_baseline_generation = 0;

bool ReadProductionCraftingContext(void*, craftingowner::Context* out) noexcept
{
    if (!out || !g_gamedll_full_profile_valid || !g_crafting_startup_qualified ||
        g_dluma.stop_requested) return false;
    *out = {};
    out->thread_id = GetCurrentThreadId();
    out->baseline_generation = g_startup_b0_session.session_generation;
    if (!content::TryGetCurrentEpochId(&g_content_ledger, &out->epoch) ||
        !ReadBytes(g_context.gamedll_base + 0x1D07F88U, &out->game_di, sizeof(out->game_di)) ||
        !out->game_di || !ReadBytes(out->game_di + 0xAA0U, &out->current_manager, sizeof(out->current_manager)))
        return false;
    const cold::RestartContext* callback = nullptr;
    cold::ContentDomainId domain = cold::ContentDomainId::Count;
    cold::RestartPhase phase = cold::RestartPhase::Idle;
    if (cold::SnapshotRegistryCallbackContext(&g_cold_reboot_registry, &callback, &domain, &phase) &&
        callback->content_epoch == out->epoch) out->transaction = callback->transaction_generation;
    if (g_crafting_baseline_capture_scope && !out->epoch &&
        !dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen, 0, 0) &&
        g_startup_filesystem_inventory.complete && g_startup_filesystem_inventory.registry_generation_observed) {
        SerializedFrameRouteEvidence frame = {};
        std::uint64_t source_revision = 0;
        out->initial_pristine = ReadStableSourceRegistryGeneration(&source_revision) &&
            source_revision == g_startup_filesystem_inventory.registry_generation &&
            CurrentSerializedGameFrameRouteMatches(0, out->thread_id, &frame) == statefive::RejectReason::C10ScopeMatched;
    }
    if (out->epoch && out->transaction) {
        curves::CurveGatewayContext current;
        std::memset(&current, 0, sizeof(current));
        if (!ReadProductionCurveContext(nullptr, &current)) return false;
        out->post_c10_closed = current.post_c10_consumers_closed &&
            current.transaction == out->transaction && current.epoch == out->epoch;
    }
    // Writer closure and GameDI generation come from the gateway's actual
    // admitted native scope/metadata lease and constructor observations.
    return true;
}

void ObserveProductionCraftingWrite(void*, craftinggate::Hook) noexcept
{
    ObservePotentialNativeDomainWrite(cold::ContentDomainId::ItemDefinitions);
}

void RecordProductionCraftingFailure(void*, craftinggate::Hook hook, craftingowner::Result result) noexcept
{
    QueueContentDiagnostic(content::LedgerDiagnosticType::DiagnosticObservationLost,
        content::CurrentEpochId(&g_content_ledger), static_cast<std::uint32_t>(hook),
        g_crafting_owner.game_di, g_crafting_owner.current_manager, static_cast<std::uint64_t>(result),
        "crafting-native-owner", "native-owner-observation-failed");
    ObservePotentialNativeDomainWrite(cold::ContentDomainId::ItemDefinitions);
}

void DisableUninstalledCraftingRoutes() noexcept
{
    for (auto* route : g_crafting_routes) {
        if (!route || route->installed) continue;
        route->enabled = false;
        if (route->owner) route->owner->route_count = 0;
        FreeRouteRelay(route);
    }
    g_crafting_hooks_prepared = g_crafting_hooks_available = false;
    g_crafting_startup_qualified = false;
}

void BuildCraftingHookPlan() noexcept
{
    if (!g_gamedll_full_profile_valid) return;
    std::size_t count = 0;
    const auto* catalog = craftinggate::Catalog(&count);
    if (!catalog || count != craftinggate::kHookCount) return;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& d = catalog[i];
        const auto target = g_context.gamedll_base + d.rva;
        auto* status = AddHookStatus(d.name, "gamedll_x64_rwdi.dll", target,
            d.rva, d.prefix, sizeof(d.prefix), false);
        g_crafting_routes[i] = AddFunctionDetour(status, target, d.stolen, 0, 0,
            reinterpret_cast<void*>(craftinggate::Wrapper(d.kind)));
    }
}

void PrepareCraftingHookBridge() noexcept
{
    if (!g_gamedll_full_profile_valid || !ExactIsolationStartupOwnerAbsence()) {
        DisableUninstalledCraftingRoutes(); return;
    }
    std::uintptr_t originals[craftinggate::kHookCount] = {};
    for (std::size_t i = 0; i < craftinggate::kHookCount; ++i) {
        const auto* route = g_crafting_routes[i];
        if (!route || !route->enabled || !route->owner || !route->owner->validated || !route->relay) {
            DisableUninstalledCraftingRoutes(); return;
        }
        originals[i] = reinterpret_cast<std::uintptr_t>(route->relay);
    }
    const craftinggate::Callbacks callbacks = {&ReadAudioBaselineMemory,
        &ReadProductionCraftingContext, &ObserveProductionCraftingWrite,
        &RecordProductionCraftingFailure, nullptr};
    if (!craftinggate::Initialize(&g_crafting_gateway, g_context.gamedll_base,
            curves::kPlayerGameDllSha256, callbacks, originals, craftinggate::kHookCount)) {
        DisableUninstalledCraftingRoutes(); return;
    }
    constexpr std::size_t records_bytes = craftingowner::kMaximumManagers * sizeof(craftingowner::Manager);
    constexpr std::size_t spans_bytes = craftingsnapshot::kMaximumSpans * sizeof(craftingsnapshot::Span);
    auto* storage = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,
        records_bytes + spans_bytes + craftingsnapshot::kMaximumBytes,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!storage) { DisableUninstalledCraftingRoutes(); return; }
    craftingsnapshot::Snapshot baseline = {};
    baseline.spans = reinterpret_cast<craftingsnapshot::Span*>(storage + records_bytes);
    baseline.span_capacity = craftingsnapshot::kMaximumSpans;
    baseline.bytes = storage + records_bytes + spans_bytes;
    baseline.capacity = craftingsnapshot::kMaximumBytes;
    craftingowner::Access access = {};
    g_crafting_hooks_prepared = craftinggate::MakeOwnerAccess(&g_crafting_gateway, &access) &&
        craftingowner::Initialize(&g_crafting_owner, access,
            reinterpret_cast<craftingowner::Manager*>(storage), craftingowner::kMaximumManagers, baseline) &&
        craftinggate::Bind(&g_crafting_gateway, &g_crafting_owner);
    if (!g_crafting_hooks_prepared) {
        VirtualFree(storage, 0, MEM_RELEASE); DisableUninstalledCraftingRoutes(); return;
    }
    // The live and held native graphs and their immutable snapshot stay owned
    // for the process lifetime until a complete Item borrower owner can retire them.
    g_crafting_startup_qualified = true;
}

void SealCraftingHookBridge() noexcept
{
    std::uintptr_t game = 1;
    g_crafting_hooks_available = g_crafting_hooks_prepared && ExactIsolationStartupOwnerAbsence() &&
        ReadBytes(g_context.gamedll_base + 0x1D07F88U, &game, sizeof(game)) && !game &&
        !g_crafting_gateway.game_di.load() && craftinggate::VerifyCoverage(&g_crafting_gateway);
    if (!g_crafting_hooks_available) {
        g_crafting_gateway.lost.store(true); g_crafting_owner.lost = true;
    }
    dluma_log_pair("FIX", "isolation_crafting_owner",
        g_crafting_hooks_available ? "startup_observer_sealed" : "unsupported_incomplete_observation");
}

bool CaptureProductionCraftingBaseline(std::uint64_t generation, std::uint32_t thread_id) noexcept
{
    if (!g_crafting_hooks_available || g_crafting_owner.lost || !generation || thread_id != GetCurrentThreadId() ||
        generation != g_startup_b0_session.session_generation || content::CurrentEpochId(&g_content_ledger) ||
        dluma_atomic_compare_exchange(&g_startup_b0_custom_staging_seen, 0, 0)) return false;
    if (g_crafting_baseline_generation) return g_crafting_baseline_generation == generation;
    craftinggate::Lease lease = {};
    if (!craftinggate::Close(&g_crafting_gateway, &lease)) return false;
    g_crafting_baseline_capture_scope = true;
    const bool captured = (g_crafting_owner.baseline_captured ||
        craftingowner::CaptureBaseline(&g_crafting_owner) == craftingowner::Result::Succeeded) &&
        blueprintrebuild::CaptureFrozenBaseline(g_crafting_owner.baseline, &g_crafting_blueprint_baseline) &&
        itemaggregate::CaptureFrozenBaseline(g_crafting_owner.baseline, &g_crafting_aggregate_baseline);
    g_crafting_baseline_capture_scope = false;
    const bool opened = craftinggate::Open(&g_crafting_gateway, lease);
    if (!captured || !opened) return false;
    g_crafting_baseline_generation = generation;
    dluma_log_pair("FIX", "isolation_crafting_baseline", "native_graph_blueprint_and_item_aggregate_inputs_captured");
    return true;
}
