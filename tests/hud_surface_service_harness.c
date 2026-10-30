#include "../src/game/dlw_hud_surface_service.h"
#include "../src/game/dlw_hud_surface_backend.h"
#include <stdio.h>
#include <string.h>

static unsigned creates, updates, destroys;
static int allow_destroy = 1;
int dluma_hud_backend_available(void) { return 1; }
void dluma_hud_backend_pump(void) { }
int dluma_hud_backend_create(uint64_t o, uint32_t w, uint32_t h, uint32_t g)
{ (void)o; (void)w; (void)h; (void)g; ++creates; return 1; }
int dluma_hud_backend_update(uint64_t o, uint32_t g, const uint8_t* p, size_t n, uint32_t s)
{ (void)o; (void)g; (void)p; (void)n; (void)s; ++updates; return 1; }
int dluma_hud_backend_layout(uint64_t o, uint32_t g, float x, float y, float w, float h)
{ (void)o; (void)g; (void)x; (void)y; (void)w; (void)h; return 1; }
int dluma_hud_backend_visibility(uint64_t o, uint32_t g, int v)
{ (void)o; (void)g; (void)v; return 1; }
int dluma_hud_backend_suppress(uint64_t o, uint32_t g, uint32_t m)
{ (void)o; (void)g; (void)m; return 1; }
int dluma_hud_backend_destroy(uint64_t o, uint32_t g)
{ (void)o; (void)g; if (!allow_destroy) return 0; ++destroys; return 1; }

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "HUD contract failed at %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    DlumaModOwner owner = {0}, foreign = {0};
    DlumaHudSurfaceCreateRequest create = {0};
    DlumaHudSurfaceUpdateRequest update = {0};
    DlumaTokenRequest release = {0};
    DlumaTokenResult token = {0};
    DlumaBoolResult result = {0};
    unsigned char pixels[16] = {0, 0, 255, 0};
    unsigned i;
    owner.owner_id = 1; owner.generation = 2; owner.map_generation = 3;
    foreign.owner_id = 2; foreign.generation = 2; foreign.map_generation = 3;
    create.width = 2; create.height = 2;
    CHECK(dluma_hud_surface_invoke(&owner, 13001, &create, &token) == DLUMA_STATUS_OK);
    CHECK(token.token != 0 && creates == 1);
    update.token = token.token; update.stride = 8; update.pixels.data = pixels; update.pixels.size = sizeof(pixels);
    CHECK(dluma_hud_surface_invoke(&foreign, 13002, &update, &result) == DLUMA_STATUS_FOREIGN_HANDLE);
    update.pixels.size = sizeof(pixels) - 1;
    CHECK(dluma_hud_surface_invoke(&owner, 13002, &update, &result) == DLUMA_STATUS_INVALID_ARGUMENT);
    update.pixels.size = sizeof(pixels);
    CHECK(dluma_hud_surface_invoke(&owner, 13002, &update, &result) == DLUMA_STATUS_OK && updates == 1);
    owner.map_generation++;
    CHECK(dluma_hud_surface_invoke(&owner, 13002, &update, &result) == DLUMA_STATUS_STALE_HANDLE);
    allow_destroy = 0;
    dluma_hud_surface_revoke_owner(owner.owner_id);
    CHECK(destroys == 0);
    CHECK(dluma_hud_surface_invoke(&owner, 13002, &update, &result) == DLUMA_STATUS_STALE_HANDLE);
    allow_destroy = 1;
    dluma_hud_surface_pump();
    CHECK(destroys == 1);
    owner.map_generation--;
    for (i = 0; i < 4; ++i) {
        owner.owner_id = i + 1;
        CHECK(dluma_hud_surface_invoke(&owner, 13001, &create, &token) == DLUMA_STATUS_OK);
    }
    owner.owner_id = 5;
    CHECK(dluma_hud_surface_invoke(&owner, 13001, &create, &token) == DLUMA_STATUS_LIMIT_EXCEEDED);
    release.token = token.token;
    owner.owner_id = 4;
    CHECK(dluma_hud_surface_invoke(&owner, 13006, &release, &result) == DLUMA_STATUS_OK);
    dluma_hud_surface_revoke_owner(0);
    CHECK(destroys == 5);
    puts("HUD surface ownership and pixel bounds passed");
    return 0;
}
