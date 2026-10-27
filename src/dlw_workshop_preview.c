#include "dlw_common.h"
#include "dlw_custom_map_loading_core.h"

/*
 * Chrome Engine 6's dynamic UI-image path consumes DDS texture files.  Steam
 * and local Workshop projects provide PNG/JPEG previews, so the loader
 * converts them on its existing worker thread into a small, deterministic,
 * uncompressed 32-bpp BGRA DDS.  The generation-4 header is byte-for-byte
 * compatible with the 51 mipmapped A8R8G8B8 textures found in the supported
 * Dying Light Developer Tools corpus: DDSD_LINEARSIZE names the top-level
 * byte count, depth is zero, and the complete lower-mip payload remains
 * tightly packed.  Generation 4 stores a centered 16:9 cover crop inside the
 * square frontend backing so loading aliases can select the exact 512x288
 * image without stretching.  WIC and COM are resolved dynamically to keep
 * the loader's static import allowlist unchanged.
 */

#define DLUMA_WORKSHOP_PREVIEW_STRIDE \
    (DLUMA_WORKSHOP_PREVIEW_EDGE * 4U)
#define DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES \
    (DLUMA_WORKSHOP_PREVIEW_STRIDE * DLUMA_WORKSHOP_PREVIEW_EDGE)
#define DLUMA_WORKSHOP_PREVIEW_MIP_COUNT 10U
#define DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES \
    ((DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES * 4U - 4U) / 3U)
#define DLUMA_WORKSHOP_PREVIEW_DDS_BYTES \
    (128U + DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES)
#define DLUMA_WORKSHOP_PREVIEW_MAX_SOURCE_BYTES \
    (8UL * 1024UL * 1024UL)

#define DLUMA_DDS_FLAGS_2D_RGBA 0x00001007UL
#define DLUMA_DDS_FLAGS_MIPPED_2D_RGBA 0x000A1007UL
#define DLUMA_DDS_PIXEL_FORMAT_RGBA 0x00000041UL
#define DLUMA_DDS_CAPS_TEXTURE 0x00001000UL
#define DLUMA_DDS_CAPS_MIPPED_TEXTURE 0x00401008UL

#define DLUMA_WIC_SDK_VERSION1 0x0236U
#define DLUMA_WIC_DECODE_METADATA_ON_DEMAND 0
#define DLUMA_WIC_BITMAP_INTERPOLATION_FANT 3
#define DLUMA_WIC_BITMAP_DITHER_NONE 0
#define DLUMA_WIC_BITMAP_PALETTE_CUSTOM 0
#define DLUMA_COINIT_MULTITHREADED 0U
#define DLUMA_RPC_E_CHANGED_MODE ((LONG)0x80010106UL)

typedef struct DlumaWorkshopGuid
{
    DWORD data1;
    WORD data2;
    WORD data3;
    BYTE data4[8];
} DlumaWorkshopGuid;

typedef LONG (WINAPI *DlumaCoInitializeExFn)(void *reserved, DWORD model);
typedef void (WINAPI *DlumaCoUninitializeFn)(void);
typedef LONG (WINAPI *DlumaWicCreateFactoryFn)(
    unsigned int sdk_version, void **factory);
typedef ULONG (__stdcall *DlumaComReleaseFn)(void *object);
typedef LONG (__stdcall *DlumaWicFactoryCreateDecoderFromFilenameFn)(
    void *factory, const WCHAR *filename,
    const DlumaWorkshopGuid *vendor, DWORD access,
    int metadata_cache_option, void **decoder);
typedef LONG (__stdcall *DlumaWicFactoryCreateFormatConverterFn)(
    void *factory, void **converter);
typedef LONG (__stdcall *DlumaWicFactoryCreateBitmapScalerFn)(
    void *factory, void **scaler);
typedef LONG (__stdcall *DlumaWicFactoryCreateBitmapClipperFn)(
    void *factory, void **clipper);
typedef LONG (__stdcall *DlumaWicDecoderGetFrameFn)(
    void *decoder, unsigned int index, void **frame);
typedef LONG (__stdcall *DlumaWicBitmapSourceGetSizeFn)(
    void *source, unsigned int *width, unsigned int *height);
typedef LONG (__stdcall *DlumaWicClipperInitializeFn)(
    void *clipper, void *source, const void *rectangle);
typedef LONG (__stdcall *DlumaWicScalerInitializeFn)(
    void *scaler, void *source, unsigned int width,
    unsigned int height, int interpolation_mode);
typedef LONG (__stdcall *DlumaWicConverterInitializeFn)(
    void *converter, void *source,
    const DlumaWorkshopGuid *destination_format,
    int dither, void *palette, double alpha_threshold,
    int palette_type);
typedef LONG (__stdcall *DlumaWicCopyPixelsFn)(
    void *source, const void *rectangle, unsigned int stride,
    unsigned int buffer_size, BYTE *buffer);

typedef struct DlumaWicRect
{
    LONG x;
    LONG y;
    LONG width;
    LONG height;
} DlumaWicRect;

typedef struct DlumaWorkshopWicApi
{
    HMODULE windows_codecs;
    HMODULE ole32;
    DlumaWicCreateFactoryFn create_factory;
    DlumaCoInitializeExFn co_initialize_ex;
    DlumaCoUninitializeFn co_uninitialize;
    unsigned char attempted;
    unsigned char ready;
} DlumaWorkshopWicApi;

static DlumaWorkshopWicApi g_dluma_workshop_wic;
static volatile LONG g_dluma_workshop_wic_status_logged;
static volatile LONG g_dluma_workshop_dds_failure_log_count;
static volatile LONG g_dluma_workshop_dds_contract_log_once;

static const DlumaWorkshopGuid g_dluma_wic_pixel_format_32bpp_bgra = {
    0x6fddc324UL, 0x4e03U, 0x4bfeU,
    {0xb1U, 0x85U, 0x3dU, 0x77U, 0x76U, 0x8dU, 0xc9U, 0x0fU}
};

static int dluma_workshop_hresult_succeeded(LONG result)
{
    return result >= 0;
}

static void *dluma_workshop_com_method(void *object, unsigned int index)
{
    void **vtable;
    if (!object || !dluma_is_readable(object, sizeof(void *)))
        return 0;
    vtable = *(void ***)object;
    if (!vtable ||
        !dluma_is_readable(vtable + index, sizeof(void *)))
        return 0;
    return vtable[index];
}

static void dluma_workshop_com_release(void *object)
{
    DlumaComReleaseFn release;
    if (!object)
        return;
    release = (DlumaComReleaseFn)
        dluma_workshop_com_method(object, 2U);
    if (release)
        (void)release(object);
}

static int dluma_workshop_resolve_wic(void)
{
    DlumaWorkshopWicApi *api = &g_dluma_workshop_wic;
    if (api->attempted)
        return api->ready != 0;
    api->attempted = 1;
    api->windows_codecs = LoadLibraryW(L"windowscodecs.dll");
    api->ole32 = LoadLibraryW(L"ole32.dll");
    if (api->windows_codecs)
        api->create_factory = (DlumaWicCreateFactoryFn)GetProcAddress(
            api->windows_codecs, "WICCreateImagingFactory_Proxy");
    if (api->ole32)
    {
        api->co_initialize_ex =
            (DlumaCoInitializeExFn)GetProcAddress(
                api->ole32, "CoInitializeEx");
        api->co_uninitialize =
            (DlumaCoUninitializeFn)GetProcAddress(
                api->ole32, "CoUninitialize");
    }
    api->ready = (unsigned char)(
        api->create_factory && api->co_initialize_ex &&
        api->co_uninitialize);
    if (dluma_atomic_compare_exchange(
            &g_dluma_workshop_wic_status_logged, 1, 0) == 0)
        dluma_log_pair(
            api->ready ? "INFO" : "WARN",
            "workshop_preview_decoder",
            api->ready ? "wic_32bpp_bgra_ready"
                       : "wic_dynamic_resolution_failed");
    return api->ready != 0;
}

static int dluma_workshop_file_info(
    const WCHAR *path, ULONGLONG *size_out,
    ULONGLONG *write_time_out)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION information;
    if (size_out) *size_out = 0;
    if (write_time_out) *write_time_out = 0;
    if (!path || !path[0])
        return 0;
    file = CreateFileW(
        path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(file, &information))
    {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    if (size_out)
        *size_out =
            ((ULONGLONG)information.nFileSizeHigh << 32U) |
            information.nFileSizeLow;
    if (write_time_out)
        *write_time_out =
            ((ULONGLONG)information.ftLastWriteTime.dwHighDateTime << 32U) |
            information.ftLastWriteTime.dwLowDateTime;
    return 1;
}

/*
 * Model the byte count consumed by the exact ImageLib DDS reader paths.  If
 * DDSD_PITCH is present, ImageLib reuses the one declared pitch for every row
 * of every mip.  Without it, the tightly packed BGRA payload is consumed
 * exactly once.
 */
static ULONGLONG dluma_workshop_preview_imagelib_payload_consumption(
    DWORD flags, DWORD pitch_or_linear_size,
    DWORD width, DWORD height, DWORD mip_count)
{
    ULONGLONG consumed = 0;
    DWORD level;
    for (level = 0; level < mip_count; ++level)
    {
        if (flags & 0x00000008UL)
            consumed += (ULONGLONG)pitch_or_linear_size * height;
        else
            consumed += (ULONGLONG)width * height * 4U;
        if (width > 1U)
            width >>= 1U;
        if (height > 1U)
            height >>= 1U;
    }
    return consumed;
}

int dluma_workshop_preview_validate_dds(const WCHAR *path)
{
    HANDLE file;
    DWORD header[32];
    DWORD read = 0;
    ULONGLONG size = 0;
    unsigned int index;
    if (!path || !path[0] ||
        !dluma_workshop_file_info(path, &size, 0) ||
        size != DLUMA_WORKSHOP_PREVIEW_DDS_BYTES)
        return 0;
    file = CreateFileW(
        path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    memset(header, 0, sizeof(header));
    if (!ReadFile(file, header, sizeof(header), &read, 0) ||
        read != sizeof(header))
    {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    if (header[0] != 0x20534444UL ||
        header[1] != 124U ||
        header[2] != DLUMA_DDS_FLAGS_MIPPED_2D_RGBA ||
        header[3] != DLUMA_WORKSHOP_PREVIEW_EDGE ||
        header[4] != DLUMA_WORKSHOP_PREVIEW_EDGE ||
        header[5] != DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES ||
        header[6] != 0U ||
        header[7] != DLUMA_WORKSHOP_PREVIEW_MIP_COUNT ||
        header[19] != 32U ||
        header[20] != DLUMA_DDS_PIXEL_FORMAT_RGBA ||
        header[21] != 0U ||
        header[22] != 32U ||
        header[23] != 0x00ff0000UL ||
        header[24] != 0x0000ff00UL ||
        header[25] != 0x000000ffUL ||
        header[26] != 0xff000000UL ||
        header[27] != DLUMA_DDS_CAPS_MIPPED_TEXTURE)
        return 0;
    for (index = 8U; index <= 18U; ++index)
        if (header[index] != 0U)
            return 0;
    for (index = 28U; index <= 31U; ++index)
        if (header[index] != 0U)
            return 0;
    return dluma_workshop_preview_imagelib_payload_consumption(
               header[2], header[5], header[4], header[3],
               header[7]) ==
           DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES;
}

/* Worker-only CPU publication input for the renderer-backed Workshop route.
 * Generation-3 DDS is retained as a deterministic cache format; only its
 * validated top mip is copied to the game thread for CreateDynamicTexture. */
int dluma_workshop_preview_read_dds_top_mip_bgra(
    const WCHAR *dds_path, BYTE **pixels_out,
    unsigned int *width_out, unsigned int *height_out,
    unsigned int *stride_out)
{
    HANDLE file;
    BYTE *pixels;
    BYTE header[128];
    DWORD read = 0;
    if (!pixels_out || !width_out || !height_out || !stride_out ||
        !dds_path || !dds_path[0] ||
        !dluma_workshop_preview_validate_dds(dds_path))
        return 0;
    *pixels_out = 0;
    *width_out = 0;
    *height_out = 0;
    *stride_out = 0;
    file = CreateFileW(dds_path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    pixels = (BYTE *)dluma_alloc(DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES);
    if (!pixels ||
        !ReadFile(file, header, sizeof(header), &read, 0) ||
        read != sizeof(header) ||
        !ReadFile(file, pixels, DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES,
                  &read, 0) ||
        read != DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES)
    {
        if (pixels)
            dluma_free(pixels);
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    *pixels_out = pixels;
    *width_out = DLUMA_WORKSHOP_PREVIEW_EDGE;
    *height_out = DLUMA_WORKSHOP_PREVIEW_EDGE;
    *stride_out = DLUMA_WORKSHOP_PREVIEW_EDGE * 4U;
    return 1;
}

static void dluma_workshop_build_bgra_mips(BYTE *pixels)
{
    unsigned int source_width = DLUMA_WORKSHOP_PREVIEW_EDGE;
    unsigned int source_height = DLUMA_WORKSHOP_PREVIEW_EDGE;
    unsigned int source_offset = 0;
    unsigned int destination_offset =
        DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES;
    unsigned int level;
    for (level = 1U;
         level < DLUMA_WORKSHOP_PREVIEW_MIP_COUNT;
         ++level)
    {
        unsigned int destination_width =
            source_width > 1U ? source_width >> 1U : 1U;
        unsigned int destination_height =
            source_height > 1U ? source_height >> 1U : 1U;
        unsigned int y;
        for (y = 0; y < destination_height; ++y)
        {
            unsigned int x;
            for (x = 0; x < destination_width; ++x)
            {
                unsigned int source_x = x << 1U;
                unsigned int source_y = y << 1U;
                unsigned int source_x1 =
                    source_x + 1U < source_width
                        ? source_x + 1U : source_x;
                unsigned int source_y1 =
                    source_y + 1U < source_height
                        ? source_y + 1U : source_y;
                unsigned int indices[4];
                unsigned int channel;
                BYTE *destination =
                    pixels + destination_offset +
                    (y * destination_width + x) * 4U;
                indices[0] = source_offset +
                    (source_y * source_width + source_x) * 4U;
                indices[1] = source_offset +
                    (source_y * source_width + source_x1) * 4U;
                indices[2] = source_offset +
                    (source_y1 * source_width + source_x) * 4U;
                indices[3] = source_offset +
                    (source_y1 * source_width + source_x1) * 4U;
                for (channel = 0; channel < 4U; ++channel)
                {
                    unsigned int sum =
                        pixels[indices[0] + channel] +
                        pixels[indices[1] + channel] +
                        pixels[indices[2] + channel] +
                        pixels[indices[3] + channel];
                    destination[channel] = (BYTE)((sum + 2U) >> 2U);
                }
            }
        }
        source_offset = destination_offset;
        source_width = destination_width;
        source_height = destination_height;
        destination_offset +=
            destination_width * destination_height * 4U;
    }
}

static int dluma_workshop_write_dds_atomic(
    const WCHAR *output_path, const BYTE *pixels)
{
    DWORD header[32];
    DWORD written = 0;
    HANDLE file;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    if (!output_path || !output_path[0] || !pixels)
        return 0;
    dluma_wstr_copy(directory, DLUMA_ARRAY_COUNT(directory), output_path);
    dluma_path_dirname(directory);
    if (!dluma_mkdirs(directory))
        return 0;
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary), output_path);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L".tmp");
    memset(header, 0, sizeof(header));
    header[0] = 0x20534444UL; /* "DDS " */
    header[1] = 124U;
    header[2] = DLUMA_DDS_FLAGS_MIPPED_2D_RGBA;
    header[3] = DLUMA_WORKSHOP_PREVIEW_EDGE;
    header[4] = DLUMA_WORKSHOP_PREVIEW_EDGE;
    header[5] = DLUMA_WORKSHOP_PREVIEW_PIXEL_BYTES;
    header[6] = 0U;
    header[7] = DLUMA_WORKSHOP_PREVIEW_MIP_COUNT;
    /* header[8..18] remain zero, matching stock ImageLib-authored DDS. */
    header[19] = 32U;
    header[20] = DLUMA_DDS_PIXEL_FORMAT_RGBA;
    header[21] = 0U;
    header[22] = 32U;
    header[23] = 0x00ff0000UL;
    header[24] = 0x0000ff00UL;
    header[25] = 0x000000ffUL;
    header[26] = 0xff000000UL;
    header[27] = DLUMA_DDS_CAPS_MIPPED_TEXTURE;
    /* header[28..31] remain zero. */
    file = CreateFileW(
        temporary, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!WriteFile(file, header, sizeof(header), &written, 0) ||
        written != sizeof(header) ||
        !WriteFile(
            file, pixels, DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES,
            &written, 0) ||
        written != DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES ||
        !FlushFileBuffers(file))
    {
        CloseHandle(file);
        DeleteFileW(temporary);
        return 0;
    }
    CloseHandle(file);
    if (!MoveFileExW(
            temporary, output_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary);
        return 0;
    }
    return dluma_workshop_preview_validate_dds(output_path);
}

static int dluma_workshop_decode_to_bgra(
    const WCHAR *source_path, BYTE *pixels)
{
    DlumaWorkshopWicApi *api = &g_dluma_workshop_wic;
    void *factory = 0;
    void *decoder = 0;
    void *frame = 0;
    void *clipper = 0;
    void *scaler = 0;
    void *converter = 0;
    DlumaWicFactoryCreateDecoderFromFilenameFn create_decoder;
    DlumaWicFactoryCreateFormatConverterFn create_converter;
    DlumaWicFactoryCreateBitmapScalerFn create_scaler;
    DlumaWicFactoryCreateBitmapClipperFn create_clipper;
    DlumaWicDecoderGetFrameFn get_frame;
    DlumaWicBitmapSourceGetSizeFn get_size;
    DlumaWicClipperInitializeFn initialize_clipper;
    DlumaWicScalerInitializeFn initialize_scaler;
    DlumaWicConverterInitializeFn initialize_converter;
    DlumaWicCopyPixelsFn copy_pixels;
    DlumaCustomMapLoadingCrop crop;
    DlumaWicRect source_rectangle;
    unsigned int source_width = 0;
    unsigned int source_height = 0;
    unsigned int pixel_index;
    LONG co_result;
    int uninitialize = 0;
    int succeeded = 0;
    if (!source_path || !source_path[0] || !pixels ||
        !dluma_workshop_resolve_wic())
        return 0;
    co_result = api->co_initialize_ex(0, DLUMA_COINIT_MULTITHREADED);
    if (dluma_workshop_hresult_succeeded(co_result))
        uninitialize = 1;
    else if (co_result != DLUMA_RPC_E_CHANGED_MODE)
        goto done;
    if (!dluma_workshop_hresult_succeeded(
            api->create_factory(DLUMA_WIC_SDK_VERSION1, &factory)) ||
        !factory)
        goto done;
    create_decoder =
        (DlumaWicFactoryCreateDecoderFromFilenameFn)
            dluma_workshop_com_method(factory, 3U);
    create_converter =
        (DlumaWicFactoryCreateFormatConverterFn)
            dluma_workshop_com_method(factory, 10U);
    create_scaler =
        (DlumaWicFactoryCreateBitmapScalerFn)
            dluma_workshop_com_method(factory, 11U);
    create_clipper =
        (DlumaWicFactoryCreateBitmapClipperFn)
            dluma_workshop_com_method(factory, 12U);
    if (!create_decoder || !create_converter || !create_scaler ||
        !create_clipper ||
        !dluma_workshop_hresult_succeeded(create_decoder(
            factory, source_path, 0, GENERIC_READ,
            DLUMA_WIC_DECODE_METADATA_ON_DEMAND, &decoder)) ||
        !decoder)
        goto done;
    get_frame = (DlumaWicDecoderGetFrameFn)
        dluma_workshop_com_method(decoder, 13U);
    if (!get_frame ||
        !dluma_workshop_hresult_succeeded(
            get_frame(decoder, 0U, &frame)) ||
        !frame ||
        !dluma_workshop_hresult_succeeded(
            create_clipper(factory, &clipper)) ||
        !clipper ||
        !dluma_workshop_hresult_succeeded(
            create_scaler(factory, &scaler)) ||
        !scaler)
        goto done;
    get_size = (DlumaWicBitmapSourceGetSizeFn)
        dluma_workshop_com_method(frame, 3U);
    initialize_clipper = (DlumaWicClipperInitializeFn)
        dluma_workshop_com_method(clipper, 8U);
    initialize_scaler = (DlumaWicScalerInitializeFn)
        dluma_workshop_com_method(scaler, 8U);
    if (!get_size || !initialize_clipper || !initialize_scaler ||
        !dluma_workshop_hresult_succeeded(
            get_size(frame, &source_width, &source_height)) ||
        !source_width || !source_height ||
        !dluma_custom_map_loading_cover_crop(
            source_width, source_height,
            DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH,
            DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT, &crop) ||
        crop.x > 0x7FFFFFFFUL || crop.y > 0x7FFFFFFFUL ||
        crop.width > 0x7FFFFFFFUL ||
        crop.height > 0x7FFFFFFFUL)
        goto done;
    source_rectangle.x = (LONG)crop.x;
    source_rectangle.y = (LONG)crop.y;
    source_rectangle.width = (LONG)crop.width;
    source_rectangle.height = (LONG)crop.height;
    if (!dluma_workshop_hresult_succeeded(initialize_clipper(
            clipper, frame, &source_rectangle)) ||
        !dluma_workshop_hresult_succeeded(initialize_scaler(
            scaler, clipper,
            DLUMA_WORKSHOP_PREVIEW_CONTENT_WIDTH,
            DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT,
            DLUMA_WIC_BITMAP_INTERPOLATION_FANT)) ||
        !dluma_workshop_hresult_succeeded(
            create_converter(factory, &converter)) ||
        !converter)
        goto done;
    initialize_converter = (DlumaWicConverterInitializeFn)
        dluma_workshop_com_method(converter, 8U);
    copy_pixels = (DlumaWicCopyPixelsFn)
        dluma_workshop_com_method(converter, 7U);
    for (pixel_index = 0;
         pixel_index <
             DLUMA_WORKSHOP_PREVIEW_EDGE *
                 DLUMA_WORKSHOP_PREVIEW_EDGE;
         ++pixel_index)
    {
        pixels[pixel_index * 4U + 0U] = 0U;
        pixels[pixel_index * 4U + 1U] = 0U;
        pixels[pixel_index * 4U + 2U] = 0U;
        pixels[pixel_index * 4U + 3U] = 0xFFU;
    }
    if (!initialize_converter || !copy_pixels ||
        !dluma_workshop_hresult_succeeded(initialize_converter(
            converter, scaler, &g_dluma_wic_pixel_format_32bpp_bgra,
            DLUMA_WIC_BITMAP_DITHER_NONE, 0, 0.0,
            DLUMA_WIC_BITMAP_PALETTE_CUSTOM)) ||
        !dluma_workshop_hresult_succeeded(copy_pixels(
            converter, 0, DLUMA_WORKSHOP_PREVIEW_STRIDE,
            DLUMA_WORKSHOP_PREVIEW_STRIDE *
                DLUMA_WORKSHOP_PREVIEW_CONTENT_HEIGHT,
            pixels +
                DLUMA_WORKSHOP_PREVIEW_CONTENT_TOP *
                    DLUMA_WORKSHOP_PREVIEW_STRIDE)))
        goto done;
    succeeded = 1;
done:
    dluma_workshop_com_release(converter);
    dluma_workshop_com_release(scaler);
    dluma_workshop_com_release(clipper);
    dluma_workshop_com_release(frame);
    dluma_workshop_com_release(decoder);
    dluma_workshop_com_release(factory);
    if (uninitialize)
        api->co_uninitialize();
    return succeeded;
}

int dluma_workshop_preview_prepare_dds(
    const WCHAR *source_path, const WCHAR *output_path)
{
    ULONGLONG source_size = 0;
    ULONGLONG source_time = 0;
    ULONGLONG output_time = 0;
    BYTE *pixels;
    int converted;
    if (dluma_atomic_compare_exchange(
            &g_dluma_workshop_dds_contract_log_once, 1, 0) == 0)
    {
        dluma_log_pair(
            "INFO", "workshop_preview_dds_contract",
            "stock_imagelib_a8r8g8b8_mipped");
        dluma_log_u64(
            "INFO", "workshop_preview_dds_generation",
            DLUMA_WORKSHOP_PREVIEW_DDS_GENERATION);
    }
    if (!source_path || !source_path[0] ||
        !output_path || !output_path[0] ||
        !dluma_workshop_file_info(
            source_path, &source_size, &source_time) ||
        source_size < 8U ||
        source_size > DLUMA_WORKSHOP_PREVIEW_MAX_SOURCE_BYTES)
        return 0;
    if (dluma_workshop_preview_validate_dds(output_path) &&
        dluma_workshop_file_info(output_path, 0, &output_time) &&
        output_time >= source_time)
        return 1;
    pixels = (BYTE*)dluma_alloc(DLUMA_WORKSHOP_PREVIEW_MIP_PIXEL_BYTES);
    if (!pixels)
        return 0;
    converted = dluma_workshop_decode_to_bgra(source_path, pixels);
    if (converted)
    {
        dluma_workshop_build_bgra_mips(pixels);
        converted =
            dluma_workshop_write_dds_atomic(output_path, pixels);
    }
    dluma_free(pixels);
    if (!converted &&
        dluma_atomic_increment(
            &g_dluma_workshop_dds_failure_log_count) <= 16)
        dluma_log_pair(
            "WARN", "workshop_preview_dds_conversion",
            "failed_using_existing_or_fallback");
    return converted;
}
