#ifndef DLW_HUD_SURFACE_BACKEND_H
#define DLW_HUD_SURFACE_BACKEND_H
#include <stddef.h>
#include <stdint.h>

/* Internal game-frame-only backend. Public owners and handles are validated
 * by the service; these functions retain no caller buffers. */
enum DlumaHudStockMask {
    DLUMA_HUD_STOCK_HEALTH = 1u,
    DLUMA_HUD_STOCK_STAMINA = 2u,
    DLUMA_HUD_STOCK_RADAR = 4u,
    DLUMA_HUD_STOCK_WEAPONS = 8u,
    DLUMA_HUD_STOCK_CROSSHAIR = 16u,
    DLUMA_HUD_STOCK_RELOAD = 32u
};
int dluma_hud_backend_available(void);
int dluma_hud_backend_create(uint64_t owner, uint32_t width, uint32_t height, uint32_t generation);
int dluma_hud_backend_update(uint64_t owner, uint32_t generation, const uint8_t *rgba, size_t bytes, uint32_t stride);
int dluma_hud_backend_layout(uint64_t owner, uint32_t generation, float x, float y, float width, float height);
int dluma_hud_backend_visibility(uint64_t owner, uint32_t generation, int visible);
int dluma_hud_backend_suppress(uint64_t owner, uint32_t generation, uint32_t mask);
/* Returns zero when cleanup must be retried on a game-frame callback. */
int dluma_hud_backend_destroy(uint64_t owner, uint32_t generation);
/* Retries cleanup for failed creates that never returned a service token. */
void dluma_hud_backend_pump(void);
#endif
