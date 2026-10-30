#include "dlw_hud_surface_service.h"
#include "dlw_hud_surface_backend.h"

#include <stddef.h>
#include <string.h>

/* SRWLOCK is one zero-initialized pointer on the supported Windows x64 ABI. */
__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(void* lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(void* lock);

/* Each slot is owned by one mod and one map generation. */
typedef struct DlumaHudSurfaceOwner {
    uint64_t owner_id;
    uint64_t token;
    uint32_t owner_generation;
    uint32_t map_generation;
    uint32_t width;
    uint32_t height;
    uint32_t pending_destroy;
} DlumaHudSurfaceOwner;

static DlumaHudSurfaceOwner g_surfaces[4];
static uint64_t g_next_surface_token;
static void* g_surface_lock;

void dluma_hud_surface_prepare_capabilities(DlumaCapabilityRegistry* registry)
{
    static const char unavailable[] = "HUD surface backend unavailable";
    DlumaStringView status = {unavailable, sizeof(unavailable) - 1U};
    /* Remain gated until both Player profiles are validated in game. */
    if (registry)
        (void)dluma_capability_registry_set(registry, DLUMA_CAPABILITY_UI_SURFACE,
                                            0, 0, status);
}

void dluma_hud_surface_revoke_owner(uint64_t owner_id)
{
    unsigned int i;
    AcquireSRWLockExclusive(&g_surface_lock);
    for (i = 0; i < 4U; ++i) {
        DlumaHudSurfaceOwner* surface = &g_surfaces[i];
        if (surface->owner_id && (!owner_id || surface->owner_id == owner_id)) {
            surface->pending_destroy = 1U;
            if (dluma_hud_backend_destroy(surface->owner_id, surface->map_generation))
                memset(surface, 0, sizeof(*surface));
        }
    }
    ReleaseSRWLockExclusive(&g_surface_lock);
}

void dluma_hud_surface_pump(void)
{
    unsigned int i;
    AcquireSRWLockExclusive(&g_surface_lock);
    dluma_hud_backend_pump();
    for (i = 0; i < 4U; ++i) {
        DlumaHudSurfaceOwner* surface = &g_surfaces[i];
        if (surface->owner_id && surface->pending_destroy &&
            dluma_hud_backend_destroy(surface->owner_id, surface->map_generation))
            memset(surface, 0, sizeof(*surface));
    }
    ReleaseSRWLockExclusive(&g_surface_lock);
}

static DlumaStatus dluma_hud_surface_check(DlumaModOwner* owner, uint64_t token,
                                           DlumaHudSurfaceOwner** output)
{
    unsigned int i;
    if (!owner || !token || !output) return DLUMA_STATUS_INVALID_ARGUMENT;
    for (i = 0; i < 4U; ++i) {
        DlumaHudSurfaceOwner* surface = &g_surfaces[i];
        if (surface->token != token || !surface->owner_id || surface->pending_destroy) continue;
        if (owner->owner_id != surface->owner_id) return DLUMA_STATUS_FOREIGN_HANDLE;
        if (owner->generation != surface->owner_generation ||
            owner->map_generation != surface->map_generation)
            return DLUMA_STATUS_STALE_HANDLE;
        *output = surface;
        return DLUMA_STATUS_OK;
    }
    return DLUMA_STATUS_STALE_HANDLE;
}

static DlumaStatus dluma_hud_surface_invoke_locked(DlumaModOwner* owner, uint32_t operation_id,
                                                   const void* request, void* result)
{
    DlumaStatus status;
    uint64_t token;
    DlumaHudSurfaceOwner* surface = 0;
    int success;
    if (!owner || !request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (operation_id == 13001U) {
        const DlumaHudSurfaceCreateRequest* r = (const DlumaHudSurfaceCreateRequest*)request;
        DlumaTokenResult* out = (DlumaTokenResult*)result;
        if (!r->width || !r->height || r->width > 2048U || r->height > 2048U)
            return DLUMA_STATUS_INVALID_ARGUMENT;
        unsigned int i;
        for (i = 0; i < 4U; ++i) {
            if (g_surfaces[i].owner_id == owner->owner_id) return DLUMA_STATUS_LIMIT_EXCEEDED;
            if (!g_surfaces[i].owner_id && !surface) surface = &g_surfaces[i];
        }
        if (!surface) return DLUMA_STATUS_LIMIT_EXCEEDED;
        if (!dluma_hud_backend_available() ||
            !dluma_hud_backend_create(owner->owner_id, r->width, r->height,
                                      owner->map_generation))
            return DLUMA_STATUS_UNAVAILABLE;
        if (++g_next_surface_token == 0U) ++g_next_surface_token;
        surface->owner_id = owner->owner_id;
        surface->owner_generation = owner->generation;
        surface->map_generation = owner->map_generation;
        surface->width = r->width;
        surface->height = r->height;
        surface->token = g_next_surface_token;
        out->token = surface->token;
        return DLUMA_STATUS_OK;
    }
    /* All remaining requests place their owner-bound token immediately after
     * the common record prefix.  Read through the declared request types. */
    switch (operation_id) {
    case 13002U: token = ((const DlumaHudSurfaceUpdateRequest*)request)->token; break;
    case 13003U: token = ((const DlumaHudSurfaceLayoutRequest*)request)->token; break;
    case 13004U: token = ((const DlumaHudSurfaceVisibilityRequest*)request)->token; break;
    case 13005U: token = ((const DlumaHudSurfaceSuppressRequest*)request)->token; break;
    case 13006U: token = ((const DlumaTokenRequest*)request)->token; break;
    default: return DLUMA_STATUS_UNAVAILABLE;
    }
    status = dluma_hud_surface_check(owner, token, &surface);
    if (status != DLUMA_STATUS_OK) return status;
    switch (operation_id) {
    case 13002U: {
        const DlumaHudSurfaceUpdateRequest* r = (const DlumaHudSurfaceUpdateRequest*)request;
        uint64_t minimum_stride = (uint64_t)surface->width * 4U;
        uint64_t needed = (uint64_t)r->stride * surface->height;
        if (!r->pixels.data || r->stride < minimum_stride ||
            needed > 2048ULL * 2048ULL * 4ULL || r->pixels.size != needed)
            return DLUMA_STATUS_INVALID_ARGUMENT;
        success = dluma_hud_backend_update(owner->owner_id, surface->map_generation,
                                           r->pixels.data, (size_t)needed, r->stride);
        break;
    }
    case 13003U: {
        const DlumaHudSurfaceLayoutRequest* r = (const DlumaHudSurfaceLayoutRequest*)request;
        if (!(r->x >= -4096.0f && r->x <= 4096.0f &&
              r->y >= -4096.0f && r->y <= 4096.0f &&
              r->width > 0.0f && r->width <= 4096.0f &&
              r->height > 0.0f && r->height <= 4096.0f))
            return DLUMA_STATUS_INVALID_ARGUMENT;
        success = dluma_hud_backend_layout(owner->owner_id, surface->map_generation,
                                           r->x, r->y, r->width, r->height);
        break;
    }
    case 13004U: {
        const DlumaHudSurfaceVisibilityRequest* r = (const DlumaHudSurfaceVisibilityRequest*)request;
        if (r->visible > 1U) return DLUMA_STATUS_INVALID_ARGUMENT;
        success = dluma_hud_backend_visibility(owner->owner_id, surface->map_generation,
                                               (int)r->visible);
        break;
    }
    case 13005U: {
        const DlumaHudSurfaceSuppressRequest* r = (const DlumaHudSurfaceSuppressRequest*)request;
        if (r->mask & ~63U) return DLUMA_STATUS_INVALID_ARGUMENT;
        success = dluma_hud_backend_suppress(owner->owner_id, surface->map_generation, r->mask);
        break;
    }
    default:
        surface->pending_destroy = 1U;
        success = dluma_hud_backend_destroy(surface->owner_id, surface->map_generation);
        if (success) memset(surface, 0, sizeof(*surface));
        break;
    }
    if (!success) return DLUMA_STATUS_UNAVAILABLE;
    ((DlumaBoolResult*)result)->value = 1U;
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_hud_surface_invoke(DlumaModOwner* owner, uint32_t operation_id,
                                     const void* request, void* result)
{
    DlumaStatus status;
    AcquireSRWLockExclusive(&g_surface_lock);
    status = dluma_hud_surface_invoke_locked(owner, operation_id, request, result);
    ReleaseSRWLockExclusive(&g_surface_lock);
    return status;
}
