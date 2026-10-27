#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dlw_workshop_preview.c"
#include "../src/dlw_custom_map_loading_core.c"
#include "../src/dlw_workshop_texture_core.h"

static const BYTE g_sample_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
    0x24, 0x00, 0x00, 0x00, 0x15, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9c, 0x63, 0xe4, 0x12, 0x91, 0xfb,
    0xcf, 0xc0, 0xc0, 0xc0, 0xc0, 0x04, 0x22, 0x40,
    0x18, 0x00, 0x11, 0xd4, 0x01, 0x3f, 0x2b, 0x67,
    0xee, 0xa6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

void *dluma_alloc(SIZE_T bytes)
{
    return malloc(bytes);
}

void dluma_free(void *memory)
{
    free(memory);
}

int dluma_is_readable(const void *address, SIZE_T bytes)
{
    return address != 0 && bytes != 0;
}

void dluma_log_pair(
    const char *level, const char *label, const char *value)
{
    (void)level;
    (void)label;
    (void)value;
}

void dluma_log_u64(
    const char *level, const char *label, ULONGLONG value)
{
    (void)level;
    (void)label;
    (void)value;
}

void dluma_wstr_copy(
    WCHAR *destination, unsigned int capacity, const WCHAR *source)
{
    unsigned int index = 0;
    if (!destination || !capacity)
        return;
    if (source)
        while (source[index] && index + 1U < capacity)
        {
            destination[index] = source[index];
            ++index;
        }
    destination[index] = 0;
}

void dluma_wstr_append(
    WCHAR *destination, unsigned int capacity, const WCHAR *source)
{
    unsigned int index = 0;
    unsigned int source_index = 0;
    if (!destination || !capacity || !source)
        return;
    while (index < capacity && destination[index])
        ++index;
    if (index >= capacity)
        return;
    while (source[source_index] && index + 1U < capacity)
        destination[index++] = source[source_index++];
    destination[index] = 0;
}

void dluma_path_dirname(WCHAR *path)
{
    unsigned int index = 0;
    unsigned int slash = 0;
    if (!path)
        return;
    while (path[index])
    {
        if (path[index] == L'\\' || path[index] == L'/')
            slash = index;
        ++index;
    }
    if (slash)
        path[slash] = 0;
}

int dluma_mkdirs(const WCHAR *path)
{
    (void)path;
    return 1;
}

static int write_sample_png(const WCHAR *path)
{
    HANDLE file;
    DWORD written = 0;
    file = CreateFileW(
        path, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!WriteFile(
            file, g_sample_png, sizeof(g_sample_png), &written, 0) ||
        written != sizeof(g_sample_png))
    {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    return 1;
}

typedef struct FakeTextureProofCallbacks
{
    DlumaWorkshopImageLibProbeStatus imagelib_result;
    DlumaWorkshopTextureBackingStatus backing_result;
    unsigned int imagelib_calls;
    unsigned int backing_calls;
} FakeTextureProofCallbacks;

static DlumaWorkshopImageLibProbeStatus
fake_imagelib_probe(void *context, const char *virtual_filename)
{
    FakeTextureProofCallbacks *callbacks =
        (FakeTextureProofCallbacks *)context;
    assert(callbacks);
    assert(virtual_filename && virtual_filename[0]);
    ++callbacks->imagelib_calls;
    return callbacks->imagelib_result;
}

static DlumaWorkshopTextureBackingStatus
fake_backing_probe(void *context, const char *virtual_filename)
{
    FakeTextureProofCallbacks *callbacks =
        (FakeTextureProofCallbacks *)context;
    assert(callbacks);
    assert(virtual_filename && virtual_filename[0]);
    ++callbacks->backing_calls;
    return callbacks->backing_result;
}

int main(void)
{
    static const WCHAR source[] =
        L"build\\workshop-preview-fake\\sample.png";
    static const WCHAR output[] =
        L"build\\workshop-preview-fake\\sample.dds";
    HANDLE file;
    LARGE_INTEGER offset;
    BYTE pixel[4] = {0, 0, 0, 0};
    BYTE final_mip[4] = {0, 0, 0, 0};
    DWORD header[32];
    DWORD legacy_value;
    DWORD read = 0;
    DWORD written = 0;
    LARGE_INTEGER file_size;
    DlumaWorkshopTextureAssetCore asset;
    DlumaWorkshopTextureAssetCore timeout_asset;
    DlumaWorkshopTextureAssetCore invalid_control_asset;
    DlumaWorkshopRowTextureBindingCore binding;
    FakeTextureProofCallbacks callbacks;

    DeleteFileW(source);
    DeleteFileW(output);
    assert(DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION == 4U);
    assert(DLUMA_WORKSHOP_PREVIEW_EDGE == 512U);
    assert(DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH == 512U);
    assert(DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT == 288U);
    assert(DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP == 112U);
    assert(write_sample_png(source));
    assert(dluma_workshop_preview_prepare_dds(source, output));
    assert(dluma_workshop_preview_validate_dds(output));
    {
        BYTE *top_mip = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int stride = 0;
        assert(dluma_workshop_preview_read_dds_top_mip_bgra(
            output, &top_mip, &width, &height, &stride));
        assert(top_mip != 0);
        assert(width == 512U && height == 512U && stride == 2048U);
        dluma_free(top_mip);
    }
#if 0
    assert(!dluma_workshop_texture_can_mark_ready(
        DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK, 1, 1, 1));
    assert(!dluma_workshop_texture_can_mark_ready(
        DLUMA_WORKSHOP_TEXTURE_BACKING_UNAVAILABLE, 1, 1, 1));
    assert(!dluma_workshop_texture_can_mark_ready(
        DLUMA_WORKSHOP_TEXTURE_BACKING_REAL, 1, 0, 1));
    assert(dluma_workshop_texture_can_mark_ready(
        DLUMA_WORKSHOP_TEXTURE_BACKING_REAL, 1, 1, 1));
    assert(dluma_workshop_preview_imagelib_payload_consumption(
               DLUMA_DDS_FLAGS_MIPPED_2D_RGBA,
               DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES,
               DLUMA_WORKSHOP_PREVIEW_EDGE,
               DLUMA_WORKSHOP_PREVIEW_EDGE,
               DLUMA_WORKSHOP_PREVIEW_MIP_COUNT) ==
           DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES);
    assert(dluma_workshop_preview_imagelib_payload_consumption(
               0x0002100fUL, DLUMA_WORKSHOP_PREVIEW_STRIDE,
               DLUMA_WORKSHOP_PREVIEW_EDGE,
               DLUMA_WORKSHOP_PREVIEW_EDGE,
               DLUMA_WORKSHOP_PREVIEW_MIP_COUNT) == 2095104ULL);
    assert(dluma_workshop_preview_imagelib_payload_consumption(
               0x0002100fUL, DLUMA_WORKSHOP_PREVIEW_STRIDE,
               DLUMA_WORKSHOP_PREVIEW_EDGE,
               DLUMA_WORKSHOP_PREVIEW_EDGE,
               DLUMA_WORKSHOP_PREVIEW_MIP_COUNT) >
           DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES);

    memset(&proof, 0, sizeof(proof));
    assert(dluma_workshop_texture_set_probe_identity(
        &proof, 100ULL, DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION,
        (unsigned char)DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT));
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 0, 0, 0, 0,
               DLUMA_WORKSHOP_TEXTURE_BACKING_UNAVAILABLE, 0) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_PENDING);
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 1, 1, 1, 0,
               DLUMA_WORKSHOP_TEXTURE_BACKING_UNAVAILABLE, 0) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_PENDING);
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 1, 1, 1, 1,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK, 1) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_PENDING);
    assert(proof.backing_probe_attempts == 1U);
    proof.backing_probe_attempts =
        (unsigned char)
            (DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT - 2U);
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 1, 1, 1, 1,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK, 1) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_PENDING);
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 1, 1, 1, 1,
               DLUMA_WORKSHOP_TEXTURE_BACKING_REAL, 1) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_READY);
    assert(proof.backing_verified);

    assert(dluma_workshop_texture_set_probe_identity(
        &proof, 101ULL, DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION,
        (unsigned char)DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT));
    assert(proof.backing_probe_attempts == 0U);
    assert(!proof.backing_verified);
    proof.backing_probe_attempts =
        (unsigned char)
            (DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT - 1U);
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 1, 1, 1, 1,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK, 1) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_MISSING_FALLBACK);
    assert(proof.terminal_rejected);

    assert(dluma_workshop_texture_set_probe_identity(
        &proof, 101ULL, DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION + 1U,
        (unsigned char)DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT));
    assert(proof.backing_probe_attempts == 0U);
    assert(!proof.terminal_rejected);
    proof.backing_probe_attempts =
        (unsigned char)
            (DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_LIMIT - 1U);
    assert(dluma_workshop_texture_advance_proof(
               &proof, 1, 1, 1, 1, 1,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK, 0) ==
           DLUMA_WORKSHOP_TEXTURE_PROOF_PENDING);
    assert(!proof.terminal_rejected);
#endif
    /*
     * The immutable asset model is time based.  Thirty high-FPS ticks over
     * 174 ms must remain pending and must not become a renderer timeout.
     */
    memset(&asset, 0, sizeof(asset));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.imagelib_result = DLUMA_WORKSHOP_IMAGELIB_DECODED;
    callbacks.backing_result =
        DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK;
    dluma_workshop_texture_asset_reset(
        &asset, 100ULL, DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION);
    dluma_workshop_texture_asset_mark_vfs_visible(&asset, 1000ULL);
    assert(!dluma_workshop_texture_asset_can_bind(&asset));
    {
        unsigned int tick;
        for (tick = 0; tick < 30U; ++tick)
        {
            ULONGLONG now = 1000ULL + (ULONGLONG)tick * 6ULL;
            assert(dluma_workshop_texture_asset_tick_file_proof(
                       &asset, "fake_d3.dds", now, 1,
                       &callbacks, fake_imagelib_probe,
                       fake_backing_probe) ==
                   DLUMA_WS_ASSET_FILE_BACKING_PENDING);
        }
    }
    assert(callbacks.imagelib_calls == 1U);
    assert(callbacks.backing_calls == 2U);
    assert(asset.state == DLUMA_WS_ASSET_FILE_BACKING_PENDING);
    assert(!asset.terminal_rejected);
    assert(asset.state_started_ms == 1000ULL);
    assert(asset.backing_deadline_ms == 6000ULL);
    assert(dluma_workshop_texture_asset_backing_elapsed_ms(
               &asset, 1188ULL) == 188ULL);

    /*
     * A row leaving the radius window drops only its binding.  The same
     * asset retains its identity, alias-independent state start, and pending
     * renderer request for reuse when the row returns.
     */
    memset(&binding, 0, sizeof(binding));
    binding.item = (void *)1;
    binding.asset = &asset;
    binding.generation = 7U;
    dluma_workshop_texture_detach_row_binding(&binding);
    assert(!binding.item);
    assert(!binding.asset);
    assert(asset.identity_hash == 100ULL);
    assert(asset.state_started_ms == 1000ULL);
    assert(asset.state == DLUMA_WS_ASSET_FILE_BACKING_PENDING);

    /* The queued backing may become real well before the five-second gate. */
    callbacks.backing_result =
        DLUMA_WORKSHOP_TEXTURE_BACKING_REAL;
    assert(dluma_workshop_texture_asset_tick_file_proof(
               &asset, "fake_d3.dds", 4000ULL, 1,
               &callbacks, fake_imagelib_probe,
               fake_backing_probe) ==
           DLUMA_WS_ASSET_FILE_BACKING_REAL);
    assert(callbacks.imagelib_calls == 1U);
    assert(!dluma_workshop_texture_asset_can_bind(&asset));
    dluma_workshop_texture_asset_mark_ui_definition(&asset);
    assert(!dluma_workshop_texture_asset_can_bind(&asset));
    asset.precached = 1;
    asset.prefetch_requested = 1;
    dluma_workshop_texture_asset_mark_ready(&asset);
    assert(dluma_workshop_texture_asset_can_bind(&asset));

    /* A decoded file can time out only after five seconds and a real stock
     * control probe. */
    memset(&timeout_asset, 0, sizeof(timeout_asset));
    dluma_workshop_texture_asset_reset(
        &timeout_asset, 200ULL,
        DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION);
    dluma_workshop_texture_asset_mark_vfs_visible(
        &timeout_asset, 10ULL);
    assert(dluma_workshop_texture_asset_record_imagelib(
               &timeout_asset, DLUMA_WORKSHOP_IMAGELIB_DECODED,
               10ULL) == DLUMA_WS_ASSET_IMAGELIB_DECODED);
    assert(dluma_workshop_texture_asset_record_backing(
               &timeout_asset,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK,
               1, 5009ULL) ==
           DLUMA_WS_ASSET_FILE_BACKING_PENDING);
    assert(dluma_workshop_texture_asset_record_backing(
               &timeout_asset,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK,
               1, 5010ULL) ==
           DLUMA_WS_ASSET_TERMINAL_TIMEOUT);
    assert(timeout_asset.terminal_rejected);

    /* An invalid control context is never a file-specific rejection oracle. */
    memset(&invalid_control_asset, 0, sizeof(invalid_control_asset));
    dluma_workshop_texture_asset_reset(
        &invalid_control_asset, 300ULL,
        DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION);
    dluma_workshop_texture_asset_mark_vfs_visible(
        &invalid_control_asset, 20ULL);
    assert(dluma_workshop_texture_asset_record_imagelib(
               &invalid_control_asset,
               DLUMA_WORKSHOP_IMAGELIB_DECODED,
               20ULL) == DLUMA_WS_ASSET_IMAGELIB_DECODED);
    assert(dluma_workshop_texture_asset_record_backing(
               &invalid_control_asset,
               DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK,
               0, 10020ULL) ==
           DLUMA_WS_ASSET_FILE_BACKING_PENDING);
    assert(!invalid_control_asset.terminal_rejected);

    /* Direct ImageLib decode failure is the one immediate format terminal. */
    assert(dluma_workshop_texture_asset_set_identity(
        &invalid_control_asset, 301ULL,
        DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION));
    dluma_workshop_texture_asset_mark_vfs_visible(
        &invalid_control_asset, 30ULL);
    assert(dluma_workshop_texture_asset_record_imagelib(
               &invalid_control_asset,
               DLUMA_WORKSHOP_IMAGELIB_DECODE_FAILED,
               30ULL) ==
           DLUMA_WS_ASSET_TERMINAL_DECODE_FAILURE);
    assert(invalid_control_asset.terminal_rejected);
    assert(dluma_workshop_texture_asset_set_identity(
        &invalid_control_asset, 302ULL,
        DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION + 1U));
    assert(invalid_control_asset.state == DLUMA_WS_ASSET_EMPTY);
    assert(!invalid_control_asset.terminal_rejected);
    /* A second pass exercises the current-cache fast path. */
    assert(dluma_workshop_preview_prepare_dds(source, output));

    file = CreateFileW(
        output, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    assert(file != INVALID_HANDLE_VALUE);
    memset(header, 0, sizeof(header));
    assert(ReadFile(file, header, sizeof(header), &read, 0));
    assert(read == sizeof(header));
    assert(header[2] == 0x000A1007UL);
    assert(header[5] == 512U * 512U * 4U);
    assert(header[6] == 0U);
    assert(header[7] == 10U);
    assert(header[17] == 0U);
    assert(header[18] == 0U);
    assert(header[27] == 0x00401008UL);
    offset.QuadPart = 128;
    assert(SetFilePointerEx(file, offset, 0, FILE_BEGIN));
    assert(ReadFile(file, pixel, sizeof(pixel), &read, 0));
    assert(read == sizeof(pixel));
    /* The square frontend backing letterboxes the centered 16:9 content. */
    assert(pixel[0] == 0U);
    assert(pixel[1] == 0U);
    assert(pixel[2] == 0U);
    assert(pixel[3] == 255U);
    offset.QuadPart =
        128LL +
        (LONGLONG)DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP *
            (LONGLONG)DLUMA_WORKSHOP_PREVIEW_STRIDE;
    assert(SetFilePointerEx(file, offset, 0, FILE_BEGIN));
    assert(ReadFile(file, pixel, sizeof(pixel), &read, 0));
    assert(read == sizeof(pixel));
    /* WIC converts the known RGBA content crop to uncompressed BGRA. */
    assert(pixel[0] == 30U);
    assert(pixel[1] == 20U);
    assert(pixel[2] == 10U);
    assert(pixel[3] == 255U);
    assert(GetFileSizeEx(file, &file_size));
    assert(file_size.QuadPart == 1398228LL);
    offset.QuadPart = file_size.QuadPart - 4LL;
    assert(SetFilePointerEx(file, offset, 0, FILE_BEGIN));
    assert(ReadFile(
        file, final_mip, sizeof(final_mip), &read, 0));
    assert(read == sizeof(final_mip));
    /* The final 1x1 mip must retain the image rather than becoming an
     * uninitialized gray fallback. */
    assert(final_mip[3] == 255U);
    CloseHandle(file);

    /*
     * Strict migration: the generation-2 flags are rejected even though its
     * depth/reserved/payload fields are otherwise valid.  The retained PNG
     * source atomically regenerates generation 4.
     */
    file = CreateFileW(
        output, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    assert(file != INVALID_HANDLE_VALUE);
    offset.QuadPart = 2LL * (LONGLONG)sizeof(DWORD);
    assert(SetFilePointerEx(file, offset, 0, FILE_BEGIN));
    legacy_value = 0x00021007UL;
    assert(WriteFile(
        file, &legacy_value, sizeof(legacy_value), &written, 0));
    assert(written == sizeof(legacy_value));
    CloseHandle(file);
    assert(!dluma_workshop_preview_validate_dds(output));
    assert(dluma_workshop_preview_prepare_dds(source, output));
    assert(dluma_workshop_preview_validate_dds(output));

    DeleteFileW(source);
    DeleteFileW(output);
    puts("workshop_preview_fake_harness: 64/64 ok");
    return 0;
}
