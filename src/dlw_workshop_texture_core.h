#ifndef DLUMA_WORKSHOP_TEXTURE_CORE_H
#define DLUMA_WORKSHOP_TEXTURE_CORE_H

/*
 * Workshop preview progress belongs to an immutable generated file, not to a
 * transient list-row object.  Chrome's ForceLoadTexture can return the global
 * missing texture while its queued load is still pending, so elapsed time is
 * authoritative and high-FPS frame counts are not.
 */
typedef enum DlumaWorkshopTextureBackingStatus
{
    DLUMA_WORKSHOP_TEXTURE_BACKING_UNAVAILABLE = 0,
    DLUMA_WORKSHOP_TEXTURE_BACKING_MISSING_FALLBACK,
    DLUMA_WORKSHOP_TEXTURE_BACKING_REAL
} DlumaWorkshopTextureBackingStatus;

typedef enum DlumaWorkshopImageLibProbeStatus
{
    DLUMA_WORKSHOP_IMAGELIB_UNAVAILABLE = 0,
    DLUMA_WORKSHOP_IMAGELIB_DECODE_FAILED,
    DLUMA_WORKSHOP_IMAGELIB_DECODED
} DlumaWorkshopImageLibProbeStatus;

typedef enum DlumaWorkshopTextureAssetState
{
    DLUMA_WS_ASSET_EMPTY = 0,
    DLUMA_WS_ASSET_VFS_VISIBLE,
    DLUMA_WS_ASSET_IMAGELIB_DECODED,
    DLUMA_WS_ASSET_FILE_BACKING_PENDING,
    DLUMA_WS_ASSET_FILE_BACKING_REAL,
    DLUMA_WS_ASSET_UI_DEFINITION_REGISTERED,
    DLUMA_WS_ASSET_READY,
    DLUMA_WS_ASSET_TERMINAL_DECODE_FAILURE,
    DLUMA_WS_ASSET_TERMINAL_TIMEOUT
} DlumaWorkshopTextureAssetState;

typedef struct DlumaWorkshopTextureAssetCore
{
    unsigned long long identity_hash;
    unsigned long long state_started_ms;
    unsigned long long next_probe_ms;
    unsigned long long backing_deadline_ms;
    unsigned long long last_wanted_ms;
    unsigned int generation;
    unsigned int writer_generation;
    unsigned char state;
    unsigned char imagelib_probed;
    unsigned char backing_control_real;
    unsigned char ui_definition_registered;
    unsigned char precached;
    unsigned char prefetch_requested;
    unsigned char ready;
    unsigned char terminal_rejected;
} DlumaWorkshopTextureAssetCore;

typedef struct DlumaWorkshopRowTextureBindingCore
{
    void *item;
    DlumaWorkshopTextureAssetCore *asset;
    unsigned int generation;
    unsigned char custom_alias_bound;
} DlumaWorkshopRowTextureBindingCore;

#define DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_INTERVAL_MS 100ULL
#define DLUMA_WORKSHOP_TEXTURE_BACKING_TIMEOUT_MS 5000ULL

static void dluma_workshop_texture_asset_reset(
    DlumaWorkshopTextureAssetCore *asset,
    unsigned long long identity_hash,
    unsigned int writer_generation)
{
    if (!asset)
        return;
    memset(asset, 0, sizeof(*asset));
    asset->identity_hash = identity_hash;
    asset->writer_generation = writer_generation;
}

static int dluma_workshop_texture_asset_set_identity(
    DlumaWorkshopTextureAssetCore *asset,
    unsigned long long identity_hash,
    unsigned int writer_generation)
{
    if (!asset)
        return 0;
    if (asset->identity_hash == identity_hash &&
        asset->writer_generation == writer_generation)
        return 0;
    dluma_workshop_texture_asset_reset(
        asset, identity_hash, writer_generation);
    return 1;
}

static void dluma_workshop_texture_asset_mark_wanted(
    DlumaWorkshopTextureAssetCore *asset,
    unsigned int generation,
    unsigned long long now_ms)
{
    if (!asset)
        return;
    asset->generation = generation;
    asset->last_wanted_ms = now_ms;
}

static void dluma_workshop_texture_asset_mark_vfs_visible(
    DlumaWorkshopTextureAssetCore *asset,
    unsigned long long now_ms)
{
    if (!asset || asset->state != DLUMA_WS_ASSET_EMPTY)
        return;
    asset->state = DLUMA_WS_ASSET_VFS_VISIBLE;
    asset->state_started_ms = now_ms;
}

static DlumaWorkshopTextureAssetState
dluma_workshop_texture_asset_record_imagelib(
    DlumaWorkshopTextureAssetCore *asset,
    DlumaWorkshopImageLibProbeStatus probe,
    unsigned long long now_ms)
{
    if (!asset)
        return DLUMA_WS_ASSET_EMPTY;
    if (asset->imagelib_probed)
        return (DlumaWorkshopTextureAssetState)asset->state;
    asset->imagelib_probed = 1;
    if (probe == DLUMA_WORKSHOP_IMAGELIB_DECODE_FAILED)
    {
        asset->state = DLUMA_WS_ASSET_TERMINAL_DECODE_FAILURE;
        asset->terminal_rejected = 1;
        return DLUMA_WS_ASSET_TERMINAL_DECODE_FAILURE;
    }
    if (probe != DLUMA_WORKSHOP_IMAGELIB_DECODED)
        return (DlumaWorkshopTextureAssetState)asset->state;
    asset->state = DLUMA_WS_ASSET_IMAGELIB_DECODED;
    asset->state_started_ms = now_ms;
    asset->next_probe_ms = now_ms;
    asset->backing_deadline_ms =
        now_ms + DLUMA_WORKSHOP_TEXTURE_BACKING_TIMEOUT_MS;
    return DLUMA_WS_ASSET_IMAGELIB_DECODED;
}

static int dluma_workshop_texture_asset_should_probe_backing(
    const DlumaWorkshopTextureAssetCore *asset,
    unsigned long long now_ms)
{
    if (!asset ||
        (asset->state != DLUMA_WS_ASSET_IMAGELIB_DECODED &&
         asset->state != DLUMA_WS_ASSET_FILE_BACKING_PENDING))
        return 0;
    return now_ms >= asset->next_probe_ms;
}

static DlumaWorkshopTextureAssetState
dluma_workshop_texture_asset_record_backing(
    DlumaWorkshopTextureAssetCore *asset,
    DlumaWorkshopTextureBackingStatus backing,
    int control_probe_real,
    unsigned long long now_ms)
{
    if (!asset)
        return DLUMA_WS_ASSET_EMPTY;
    if (asset->state != DLUMA_WS_ASSET_IMAGELIB_DECODED &&
        asset->state != DLUMA_WS_ASSET_FILE_BACKING_PENDING)
        return (DlumaWorkshopTextureAssetState)asset->state;
    if (control_probe_real)
        asset->backing_control_real = 1;
    if (backing == DLUMA_WORKSHOP_TEXTURE_BACKING_REAL)
    {
        asset->state = DLUMA_WS_ASSET_FILE_BACKING_REAL;
        return DLUMA_WS_ASSET_FILE_BACKING_REAL;
    }
    asset->state = DLUMA_WS_ASSET_FILE_BACKING_PENDING;
    asset->next_probe_ms =
        now_ms + DLUMA_WORKSHOP_TEXTURE_BACKING_PROBE_INTERVAL_MS;
    if (backing == DLUMA_WORKSHOP_TEXTURE_BACKING_UNAVAILABLE ||
        !asset->backing_control_real ||
        now_ms < asset->backing_deadline_ms)
        return DLUMA_WS_ASSET_FILE_BACKING_PENDING;
    asset->state = DLUMA_WS_ASSET_TERMINAL_TIMEOUT;
    asset->terminal_rejected = 1;
    return DLUMA_WS_ASSET_TERMINAL_TIMEOUT;
}

typedef DlumaWorkshopImageLibProbeStatus
(*DlumaWorkshopImageLibProbeCallback)(
    void *context, const char *virtual_filename);
typedef DlumaWorkshopTextureBackingStatus
(*DlumaWorkshopBackingProbeCallback)(
    void *context, const char *virtual_filename);

static DlumaWorkshopTextureAssetState
dluma_workshop_texture_asset_tick_file_proof(
    DlumaWorkshopTextureAssetCore *asset,
    const char *virtual_filename,
    unsigned long long now_ms,
    int control_probe_real,
    void *callback_context,
    DlumaWorkshopImageLibProbeCallback imagelib_probe,
    DlumaWorkshopBackingProbeCallback backing_probe)
{
    DlumaWorkshopTextureAssetState state;
    if (!asset || !virtual_filename || !virtual_filename[0])
        return DLUMA_WS_ASSET_EMPTY;
    state = (DlumaWorkshopTextureAssetState)asset->state;
    if (state == DLUMA_WS_ASSET_VFS_VISIBLE)
    {
        DlumaWorkshopImageLibProbeStatus probe =
            imagelib_probe
                ? imagelib_probe(
                      callback_context, virtual_filename)
                : DLUMA_WORKSHOP_IMAGELIB_UNAVAILABLE;
        state = dluma_workshop_texture_asset_record_imagelib(
            asset, probe, now_ms);
    }
    if ((state == DLUMA_WS_ASSET_IMAGELIB_DECODED ||
         state == DLUMA_WS_ASSET_FILE_BACKING_PENDING) &&
        dluma_workshop_texture_asset_should_probe_backing(
            asset, now_ms))
    {
        DlumaWorkshopTextureBackingStatus backing =
            backing_probe
                ? backing_probe(
                      callback_context, virtual_filename)
                : DLUMA_WORKSHOP_TEXTURE_BACKING_UNAVAILABLE;
        state = dluma_workshop_texture_asset_record_backing(
            asset, backing, control_probe_real, now_ms);
    }
    return state;
}

static unsigned long long
dluma_workshop_texture_asset_backing_elapsed_ms(
    const DlumaWorkshopTextureAssetCore *asset,
    unsigned long long now_ms)
{
    if (!asset || now_ms <= asset->state_started_ms)
        return 0;
    return now_ms - asset->state_started_ms;
}

static void dluma_workshop_texture_asset_mark_ui_definition(
    DlumaWorkshopTextureAssetCore *asset)
{
    if (!asset || asset->state != DLUMA_WS_ASSET_FILE_BACKING_REAL)
        return;
    asset->ui_definition_registered = 1;
    asset->state = DLUMA_WS_ASSET_UI_DEFINITION_REGISTERED;
}

static void dluma_workshop_texture_asset_mark_ready(
    DlumaWorkshopTextureAssetCore *asset)
{
    if (!asset || !asset->ui_definition_registered)
        return;
    asset->ready = 1;
    asset->state = DLUMA_WS_ASSET_READY;
}

static int dluma_workshop_texture_asset_can_bind(
    const DlumaWorkshopTextureAssetCore *asset)
{
    return asset && asset->state == DLUMA_WS_ASSET_READY &&
           asset->ready && asset->ui_definition_registered;
}

static void dluma_workshop_texture_detach_row_binding(
    DlumaWorkshopRowTextureBindingCore *binding)
{
    if (!binding)
        return;
    memset(binding, 0, sizeof(*binding));
}

#endif
