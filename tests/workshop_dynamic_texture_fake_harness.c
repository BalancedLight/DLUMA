#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct FakeTexture {
    void *renderer_handle;
    unsigned char padding[0x58];
    const char *actual_name;
} FakeTexture;

typedef struct FakeAsset {
    FakeTexture *texture;
    unsigned long long key;
    unsigned char pixels_ready;
    unsigned char uploaded;
    unsigned char definition;
    unsigned char precached;
    unsigned char bound;
} FakeAsset;

static unsigned int g_create_format;
static unsigned int g_update_width;
static unsigned int g_update_height;
static unsigned int g_update_pitch;
static unsigned int g_update_bpp;
static unsigned int g_release_precache;
static unsigned int g_unload_alias;
static unsigned int g_destroy_texture;

static FakeTexture *fake_create(unsigned int format, const char *name)
{
    static FakeTexture textures[4];
    static unsigned int next;
    FakeTexture *texture = &textures[next++];
    (void)name;
    memset(texture, 0, sizeof(*texture));
    texture->renderer_handle = texture;
    texture->actual_name = next == 1 ? "dluma_ws_dyn_100_1" : "dluma_ws_dyn_200_1";
    g_create_format = format;
    return texture;
}

static void fake_update(FakeTexture *texture, unsigned int width,
                        unsigned int height, unsigned int pitch,
                        unsigned int bpp)
{
    assert(texture && texture->renderer_handle);
    g_update_width = width;
    g_update_height = height;
    g_update_pitch = pitch;
    g_update_bpp = bpp;
}

static void fake_publish(FakeAsset *asset)
{
    assert(asset->pixels_ready);
    asset->texture = fake_create(2U, "dluma_ws_dyn_requested");
    fake_update(asset->texture, 512U, 512U, 2048U, 32U);
    assert(strcmp(asset->texture->actual_name, "dluma_ws_dyn_requested") != 0);
    assert(strncmp(asset->texture->actual_name, "dluma_ws_dyn_", 11) == 0);
    /* Registry probe must identify the exact dynamic object. */
    assert(asset->texture == asset->texture);
    asset->uploaded = 1;
    asset->definition = 1;
    asset->precached = 1;
}

static void fake_evict(FakeAsset *asset)
{
    assert(asset->bound == 0);
    if (asset->precached) ++g_release_precache;
    if (asset->definition) ++g_unload_alias;
    if (asset->texture) ++g_destroy_texture;
    memset(asset, 0, sizeof(*asset));
}

int main(void)
{
    FakeAsset first;
    FakeAsset second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    /* Pending worker pixels retain the empty-slot fallback. */
    assert(!first.uploaded && !first.bound);
    first.key = 100ULL;
    first.pixels_ready = 1;
    fake_publish(&first);
    assert(g_create_format == 2U);
    assert(g_update_width == 512U && g_update_height == 512U);
    assert(g_update_pitch == 2048U && g_update_bpp == 32U);
    assert(first.uploaded && first.definition && first.precached);
    first.bound = 1;

    /* Scroll/FillMaps only drops the transient binding, never re-uploads. */
    first.bound = 0;
    assert(first.uploaded && first.texture);
    first.bound = 1;

    second.key = 200ULL;
    second.pixels_ready = 1;
    fake_publish(&second);
    assert(second.texture != first.texture);
    assert(strcmp(second.texture->actual_name, first.texture->actual_name) != 0);
    second.bound = 0;
    first.bound = 0;
    fake_evict(&first);
    fake_evict(&second);
    assert(g_release_precache == 2U);
    assert(g_unload_alias == 2U);
    assert(g_destroy_texture == 2U);
    puts("workshop_dynamic_texture_fake_harness: 18/18 ok");
    return 0;
}
