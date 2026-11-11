// Shared authoritative route mutation and pending page-protection ownership.
bool CaptureRouteProtection(HookRoute* route)noexcept
{
    if(!route||route->protection.pending)return false;MEMORY_BASIC_INFORMATION region={};
    if(VirtualQuery(reinterpret_cast<const void*>(route->address),&region,sizeof(region))!=sizeof(region)||
        region.State!=MEM_COMMIT||!region.Protect||route->address<reinterpret_cast<std::uintptr_t>(region.BaseAddress)||
        route->address-reinterpret_cast<std::uintptr_t>(region.BaseAddress)>region.RegionSize||
        route->byte_count>region.RegionSize-(route->address-reinterpret_cast<std::uintptr_t>(region.BaseAddress)))return false;
    route->protection={region.Protect,true,false};return true;
}
bool AtomicPatchPointer(std::uintptr_t address,
                        const std::uint8_t* expected,
                        const std::uint8_t* desired,detour::ProtectionState* protection) noexcept
{
    return dluma::fixes::custom_map_asset_isolation::detour::AtomicPatch(
        address, sizeof(void*), expected, desired, &ProtectDetourRange,
        &FlushDetourRange,
        [](std::uintptr_t at, std::size_t, const std::uint8_t* from,
           const std::uint8_t* to) noexcept {
            LONGLONG comparand = 0, replacement = 0;
            std::memcpy(&comparand, from, sizeof(comparand));
            std::memcpy(&replacement, to, sizeof(replacement));
            return _InterlockedCompareExchange64(
                reinterpret_cast<volatile LONGLONG*>(at), replacement, comparand) == comparand;
        },protection).Succeeded();
}

bool AtomicPatchDetour(std::uintptr_t address,
                       const std::uint8_t* expected,
                       const std::uint8_t* desired,detour::ProtectionState* protection) noexcept
{
    return dluma::fixes::custom_map_asset_isolation::detour::AtomicPatch(
        address, kAtomicDetourSize, expected, desired, &ProtectDetourRange,
        &FlushDetourRange,
        [](std::uintptr_t at, std::size_t, const std::uint8_t* from,
           const std::uint8_t* to) noexcept {
            alignas(16) LONGLONG comparand[2] = {}, replacement[2] = {};
            std::memcpy(comparand, from, kAtomicDetourSize);
            std::memcpy(replacement, to, kAtomicDetourSize);
            return _InterlockedCompareExchange128(
                reinterpret_cast<volatile LONGLONG*>(at), replacement[1],
                replacement[0], comparand) != 0;
        },protection).Succeeded();
}

bool RouteOriginalRangeMatches(const HookRoute* route) noexcept
{
    if (!route || !route->address) return false;
    const std::size_t size = route->kind == HookRouteKind::FunctionDetour
        ? route->stolen_size : route->byte_count;
    return size >= route->byte_count &&
        size <= kMaximumStolenBytes &&
        BytesEqual(route->address, route->original_bytes, size);
}

bool RouteUntouchedTailMatches(const HookRoute* route) noexcept
{
    if (!route || route->kind != HookRouteKind::FunctionDetour ||
        route->stolen_size <= route->byte_count)
        return true;
    return route->stolen_size <= kMaximumStolenBytes &&
        BytesEqual(
            route->address + route->byte_count,
            route->original_bytes + route->byte_count,
            route->stolen_size - route->byte_count);
}

bool ApplyRoute(HookRoute* route) noexcept
{
    if (!route || !route->enabled || route->installed ||
        !RouteOriginalRangeMatches(route))
        return false;
    const bool write_succeeded = route->kind == HookRouteKind::Pointer
        ? AtomicPatchPointer(route->address, route->original_bytes,
                             route->routing_bytes,&route->protection)
        : AtomicPatchDetour(route->address, route->original_bytes,
                            route->routing_bytes,&route->protection);
    /* A protection/flush failure can be reported after an atomic exchange.
     * Record actual ownership so rollback never overlooks live routing. */
    route->installed = BytesEqual(route->address, route->routing_bytes,
                                  route->byte_count);
    if (route->installed) {
        route->ever_installed = true;
    }
    return write_succeeded && route->installed;
}

bool RestoreRoute(HookRoute* route) noexcept
{
    if (!route) return true;
    if(!route->installed)return !route->protection.pending||
        (RouteOriginalRangeMatches(route)&&detour::RestoreProtection(route->address,route->byte_count,&route->protection,&ProtectDetourRange));
    if (!BytesEqual(route->address, route->routing_bytes,
                    route->byte_count) ||
        !RouteUntouchedTailMatches(route))
        return false;
    const bool write_succeeded = route->kind == HookRouteKind::Pointer
        ? AtomicPatchPointer(route->address, route->routing_bytes,
                             route->original_bytes,&route->protection)
        : AtomicPatchDetour(route->address, route->routing_bytes,
                            route->original_bytes,&route->protection);
    const bool restored = RouteOriginalRangeMatches(route);
    if (restored) route->installed = false;
    return write_succeeded && restored;
}
