#include "dlw_media_service.h"

#include "../dlw_player_media_adapter.h"

#include <string.h>

#define DLUMA_MEDIA_MAX_BANKS 16U
#define DLUMA_MEDIA_MAX_WAVES 128U
#define DLUMA_MEDIA_MAX_WAVES_PER_OWNER 16U
#define DLUMA_MEDIA_MAX_POSITION_MS 86400000ULL
#define DLUMA_MEDIA_MAX_CAPABILITY_REGISTRIES 256U
#define DLUMA_MEDIA_TOKEN_BANK 4U
#define DLUMA_MEDIA_TOKEN_WAVE 5U
#define DLUMA_MEDIA_TOKEN_MUSIC 6U

typedef struct DlumaMediaBankSlot {
    uint64_t owner_id;
    uint32_t owner_generation;
    uint32_t generation;
    DlumaCompanionArchive* archive;
    DlumaCompanionArchiveAudioManifest manifest;
    DlumaMediaToken adapter_token;
    unsigned char active;
    unsigned char reserved[7];
} DlumaMediaBankSlot;

typedef struct DlumaMediaWaveOwnerSlot {
    uint64_t owner_id;
    uint32_t owner_generation;
    uint32_t generation;
    DlumaMediaToken adapter_token;
    unsigned char active;
    unsigned char reserved[7];
} DlumaMediaWaveOwnerSlot;

typedef struct DlumaMediaMusicOwner {
    uint64_t owner_id;
    uint32_t owner_generation;
    uint32_t generation;
    DlumaMediaToken adapter_token;
    unsigned char active;
    unsigned char reserved[7];
} DlumaMediaMusicOwner;

typedef struct DlumaMediaRegistry {
    DlumaMediaBankSlot banks[DLUMA_MEDIA_MAX_BANKS];
    DlumaMediaWaveOwnerSlot waves[DLUMA_MEDIA_MAX_WAVES];
    DlumaMediaMusicOwner music;
    DlumaCapabilityRegistry* capability_registries[
        DLUMA_MEDIA_MAX_CAPABILITY_REGISTRIES];
    uint32_t capability_registry_count;
    volatile LONG token_generation;
    volatile LONG stopping;
    unsigned char initialized;
    unsigned char retained;
    unsigned char reserved[2];
} DlumaMediaRegistry;

static DlumaMediaRegistry g_dluma_media;

static uint32_t dluma_media_next_generation(void)
{
    uint32_t value = (uint32_t)dluma_atomic_increment(
        &g_dluma_media.token_generation) & 0x00FFFFFFU;
    if (!value)
        value = (uint32_t)dluma_atomic_increment(
            &g_dluma_media.token_generation) & 0x00FFFFFFU;
    return value;
}

static uint64_t dluma_media_token(
    uint32_t kind,
    uint32_t generation,
    uint32_t index)
{
    return ((uint64_t)(kind & 0xFFU) << 56U) |
        ((uint64_t)(generation & 0x00FFFFFFU) << 32U) |
        ((uint64_t)index + 1U);
}

static int dluma_media_decode_token(
    uint64_t token,
    uint32_t kind,
    uint32_t capacity,
    uint32_t* index,
    uint32_t* generation)
{
    uint32_t low = (uint32_t)(token & 0xFFFFFFFFULL);
    if (!index || !generation ||
        (uint32_t)(token >> 56U) != kind || !low ||
        low > capacity)
        return 0;
    *generation = (uint32_t)(
        (token >> 32U) & 0x00FFFFFFULL);
    *index = low - 1U;
    return *generation != 0U;
}

static int dluma_media_copy_view(
    char* output,
    uint32_t capacity,
    DlumaStringView value)
{
    uint64_t index;
    if (!output || !capacity || value.size >= capacity ||
        (!value.data && value.size))
        return 0;
    for (index = 0; index < value.size; ++index)
        if (!value.data[index]) return 0;
    if (value.size)
        memcpy(output, value.data, (size_t)value.size);
    output[value.size] = 0;
    return 1;
}

static DlumaStatus dluma_media_status(int result)
{
    switch (result) {
    case DLUMA_MEDIA_RESULT_SUCCESS:
        return DLUMA_STATUS_OK;
    case DLUMA_MEDIA_RESULT_INVALID:
        return DLUMA_STATUS_INVALID_ARGUMENT;
    case DLUMA_MEDIA_RESULT_WRONG_THREAD:
        return DLUMA_STATUS_WRONG_THREAD;
    case DLUMA_MEDIA_RESULT_BUSY:
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    case DLUMA_MEDIA_RESULT_BUFFER_TOO_SMALL:
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    case DLUMA_MEDIA_RESULT_STALE:
        return DLUMA_STATUS_STALE_HANDLE;
    case DLUMA_MEDIA_RESULT_UNAVAILABLE:
    default:
        return DLUMA_STATUS_UNAVAILABLE;
    }
}

static DlumaStatus dluma_media_write_string(
    DlumaStringBuffer* output,
    const char* value)
{
    size_t length;
    if (!output || !value) return DLUMA_STATUS_INVALID_ARGUMENT;
    length = strlen(value);
    output->required = (uint64_t)length + 1U;
    if (!output->data || output->capacity < output->required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    memcpy(output->data, value, length + 1U);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_vehicle_state(
    DlumaVehicleStateResult* result)
{
    DlumaVehicleAdapterState state;
    DlumaStatus status;
    int adapter_result;
    if (!result) return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(&state, 0, sizeof(state));
    state.size = (unsigned int)sizeof(state);
    state.version = DLUMA_MEDIA_STRUCT_VERSION;
    adapter_result = dluma_player_media_adapter_get_vehicle_state(&state);
    status = dluma_media_status(adapter_result);
    if (status != DLUMA_STATUS_OK) return status;
    if (!state.available) return DLUMA_STATUS_UNAVAILABLE;
    result->player_present = state.local_player_present;
    result->vehicle_present = state.vehicle_present;
    result->is_local_driver = state.is_local_driver;
    result->linear_velocity.x = state.velocity_x;
    result->linear_velocity.y = state.velocity_y;
    result->linear_velocity.z = state.velocity_z;
    result->speed_mps = state.speed_mps;
    result->speed_mph = state.speed_mph;
    result->level_epoch = state.level_epoch;
    status = dluma_media_write_string(
        &result->vehicle_type,
        state.vehicle_present
            ? (state.is_buggy ? "buggy" : "vehicle")
            : "none");
    if (status != DLUMA_STATUS_OK) return status;
    return dluma_media_write_string(
        &result->class_name,
        state.vehicle_present
            ? (state.is_buggy ? "Buggy" : "Vehicle")
            : "");
}

static DlumaStatus dluma_media_resolve_bank(
    DlumaModOwner* owner,
    uint64_t token,
    DlumaMediaBankSlot** slot_out,
    uint32_t* index_out)
{
    uint32_t index;
    uint32_t generation;
    DlumaMediaBankSlot* slot;
    if (slot_out) *slot_out = 0;
    if (!owner || !slot_out ||
        !dluma_media_decode_token(
            token, DLUMA_MEDIA_TOKEN_BANK,
            DLUMA_MEDIA_MAX_BANKS, &index, &generation))
        return DLUMA_STATUS_STALE_HANDLE;
    slot = &g_dluma_media.banks[index];
    if (!slot->active || slot->generation != generation)
        return DLUMA_STATUS_STALE_HANDLE;
    if (slot->owner_id != owner->owner_id ||
        slot->owner_generation != owner->generation)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    *slot_out = slot;
    if (index_out) *index_out = index;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_resolve_wave(
    DlumaModOwner* owner,
    uint64_t token,
    DlumaMediaWaveOwnerSlot** slot_out,
    uint32_t* index_out)
{
    uint32_t index;
    uint32_t generation;
    DlumaMediaWaveOwnerSlot* slot;
    if (slot_out) *slot_out = 0;
    if (!owner || !slot_out ||
        !dluma_media_decode_token(
            token, DLUMA_MEDIA_TOKEN_WAVE,
            DLUMA_MEDIA_MAX_WAVES, &index, &generation))
        return DLUMA_STATUS_STALE_HANDLE;
    slot = &g_dluma_media.waves[index];
    if (!slot->active || slot->generation != generation)
        return DLUMA_STATUS_STALE_HANDLE;
    if (slot->owner_id != owner->owner_id ||
        slot->owner_generation != owner->generation)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    *slot_out = slot;
    if (index_out) *index_out = index;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_resolve_music(
    DlumaModOwner* owner,
    uint64_t token,
    DlumaMediaMusicOwner** slot_out)
{
    uint32_t index;
    uint32_t generation;
    DlumaMediaMusicOwner* slot = &g_dluma_media.music;
    if (slot_out) *slot_out = 0;
    if (!owner || !slot_out ||
        !dluma_media_decode_token(
            token, DLUMA_MEDIA_TOKEN_MUSIC, 1U,
            &index, &generation))
        return DLUMA_STATUS_STALE_HANDLE;
    if (!slot->active || slot->generation != generation)
        return DLUMA_STATUS_STALE_HANDLE;
    if (slot->owner_id != owner->owner_id ||
        slot->owner_generation != owner->generation)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    *slot_out = slot;
    return DLUMA_STATUS_OK;
}

static const DlumaCompanionArchiveEntry* dluma_media_bank_entry(
    const DlumaCompanionArchive* archive,
    const char* path)
{
    unsigned int index;
    if (!archive || !path) return 0;
    for (index = 0; index < archive->entry_count; ++index)
        if (archive->entries[index].is_csb &&
            dluma_stricmp(
                archive->entries[index].virtual_path,
                path) == 0)
            return &archive->entries[index];
    return 0;
}

static DlumaStatus dluma_media_bank_open(
    DlumaGameHostContext* host_context,
    DlumaModOwner* owner,
    const DlumaAudioBankOpenRequest* request,
    DlumaTokenResult* result)
{
    DlumaCompanionArchive* archive = 0;
    const DlumaCompanionArchiveAudioManifest* manifest;
    const DlumaCompanionArchiveEntry* entry;
    char bank_id[DLUMA_COMPANION_ARCHIVE_MAX_ID];
    DlumaMediaToken adapter_token = 0;
    DlumaStatus status;
    unsigned int index;
    int adapter_result;
    if (!host_context || !owner || !request || !result ||
        !dluma_media_copy_view(
            bank_id, DLUMA_ARRAY_COUNT(bank_id),
            request->bank_id))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_game_host_context_resolve_companion(
        host_context, request->companion_token, &archive);
    if (status != DLUMA_STATUS_OK) return status;
    manifest = dluma_companion_archive_find_bank(archive, bank_id);
    if (!manifest) return DLUMA_STATUS_NOT_FOUND;
    entry = dluma_media_bank_entry(archive, manifest->bank_path);
    if (!entry) return DLUMA_STATUS_NOT_FOUND;
    for (index = 0; index < DLUMA_MEDIA_MAX_BANKS; ++index) {
        DlumaMediaBankSlot* existing = &g_dluma_media.banks[index];
        if (existing->active &&
            existing->owner_id == owner->owner_id &&
            existing->owner_generation == owner->generation &&
            existing->archive == archive &&
            dluma_stricmp(
                existing->manifest.bank_identifier,
                bank_id) == 0) {
            result->token = dluma_media_token(
                DLUMA_MEDIA_TOKEN_BANK,
                existing->generation, index);
            return DLUMA_STATUS_OK;
        }
    }
    for (index = 0; index < DLUMA_MEDIA_MAX_BANKS; ++index)
        if (!g_dluma_media.banks[index].active) break;
    if (index == DLUMA_MEDIA_MAX_BANKS)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    adapter_result = dluma_player_media_adapter_open_bank(
        archive, entry, &adapter_token);
    status = dluma_media_status(adapter_result);
    if (status != DLUMA_STATUS_OK) return status;
    memset(&g_dluma_media.banks[index], 0,
           sizeof(g_dluma_media.banks[index]));
    g_dluma_media.banks[index].owner_id = owner->owner_id;
    g_dluma_media.banks[index].owner_generation = owner->generation;
    g_dluma_media.banks[index].generation = dluma_media_next_generation();
    g_dluma_media.banks[index].archive = archive;
    g_dluma_media.banks[index].manifest = *manifest;
    g_dluma_media.banks[index].adapter_token = adapter_token;
    g_dluma_media.banks[index].active = 1U;
    result->token = dluma_media_token(
        DLUMA_MEDIA_TOKEN_BANK,
        g_dluma_media.banks[index].generation, index);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_cue_duration(
    DlumaModOwner* owner,
    const DlumaAudioCueRequest* request,
    DlumaU64Result* result)
{
    DlumaMediaBankSlot* bank;
    char cue[DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME];
    DWORD declared_duration = 0;
    DlumaMediaToken probed_duration = 0;
    DlumaStatus status;
    int adapter_result;
    if (!request || !result ||
        !dluma_media_copy_view(
            cue, DLUMA_ARRAY_COUNT(cue), request->cue))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_resolve_bank(
        owner, request->bank_token, &bank, 0);
    if (status != DLUMA_STATUS_OK) return status;
    if (!dluma_companion_archive_manifest_has_cue(
            bank->archive, bank->manifest.bank_identifier,
            cue, &declared_duration))
        return DLUMA_STATUS_NOT_FOUND;
    adapter_result = dluma_player_media_adapter_probe_duration(
        bank->adapter_token, cue, &probed_duration);
    status = dluma_media_status(adapter_result);
    if (status != DLUMA_STATUS_OK) return status;
    if (probed_duration != declared_duration)
        return DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
    result->value = probed_duration;
    return DLUMA_STATUS_OK;
}

static uint32_t dluma_media_owner_wave_count(
    const DlumaModOwner* owner)
{
    uint32_t count = 0;
    uint32_t index;
    for (index = 0; index < DLUMA_MEDIA_MAX_WAVES; ++index)
        if (g_dluma_media.waves[index].active &&
            g_dluma_media.waves[index].owner_id == owner->owner_id &&
            g_dluma_media.waves[index].owner_generation == owner->generation)
            ++count;
    return count;
}

static DlumaStatus dluma_media_cue_play(
    DlumaModOwner* owner,
    const DlumaAudioCuePlayRequest* request,
    DlumaTokenResult* result)
{
    DlumaMediaBankSlot* bank;
    DlumaMediaCuePlayRequest adapter_request;
    DlumaMediaToken adapter_token = 0;
    char cue[DLUMA_COMPANION_ARCHIVE_MAX_CUE_NAME];
    DWORD duration = 0;
    DlumaStatus status;
    uint32_t index;
    int adapter_result;
    if (!request || !result ||
        !dluma_media_copy_view(
            cue, DLUMA_ARRAY_COUNT(cue), request->cue) ||
        request->start_ms > DLUMA_MEDIA_MAX_POSITION_MS ||
        request->initial_gain != request->initial_gain ||
        request->initial_gain < 0.0f ||
        request->initial_gain > 4.0f ||
        request->fade_in_ms > 60000U ||
        request->fade_out_ms > 60000U)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_resolve_bank(
        owner, request->bank_token, &bank, 0);
    if (status != DLUMA_STATUS_OK) return status;
    if (!dluma_companion_archive_manifest_has_cue(
            bank->archive, bank->manifest.bank_identifier,
            cue, &duration))
        return DLUMA_STATUS_NOT_FOUND;
    if (request->start_ms > duration)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (dluma_media_owner_wave_count(owner) >=
        DLUMA_MEDIA_MAX_WAVES_PER_OWNER)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    for (index = 0; index < DLUMA_MEDIA_MAX_WAVES; ++index)
        if (!g_dluma_media.waves[index].active) break;
    if (index == DLUMA_MEDIA_MAX_WAVES)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    memset(&adapter_request, 0, sizeof(adapter_request));
    adapter_request.size = (unsigned int)sizeof(adapter_request);
    adapter_request.version = DLUMA_MEDIA_STRUCT_VERSION;
    adapter_request.start_ms = request->start_ms;
    adapter_request.initial_gain = request->initial_gain;
    adapter_request.route = DLUMA_MEDIA_ROUTE_MUSIC;
    adapter_request.flags = request->loop
        ? DLUMA_MEDIA_CUE_FLAG_LOOP : 0U;
    adapter_request.fade_in_ms = request->fade_in_ms;
    adapter_request.fade_out_ms = request->fade_out_ms;
    adapter_result = dluma_player_media_adapter_play(
        bank->adapter_token, cue, &adapter_request,
        &adapter_token);
    status = dluma_media_status(adapter_result);
    if (status != DLUMA_STATUS_OK) return status;
    memset(&g_dluma_media.waves[index], 0,
           sizeof(g_dluma_media.waves[index]));
    g_dluma_media.waves[index].owner_id = owner->owner_id;
    g_dluma_media.waves[index].owner_generation = owner->generation;
    g_dluma_media.waves[index].generation = dluma_media_next_generation();
    g_dluma_media.waves[index].adapter_token = adapter_token;
    g_dluma_media.waves[index].active = 1U;
    result->token = dluma_media_token(
        DLUMA_MEDIA_TOKEN_WAVE,
        g_dluma_media.waves[index].generation, index);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_cue_state(
    DlumaModOwner* owner,
    uint64_t token,
    DlumaAudioCueStateResult* result)
{
    DlumaMediaWaveOwnerSlot* wave;
    DlumaMediaCueState state;
    DlumaStatus status = dluma_media_resolve_wave(
        owner, token, &wave, 0);
    int adapter_result;
    if (status != DLUMA_STATUS_OK) return status;
    memset(&state, 0, sizeof(state));
    state.size = (unsigned int)sizeof(state);
    state.version = DLUMA_MEDIA_STRUCT_VERSION;
    adapter_result = dluma_player_media_adapter_wave_state(
        wave->adapter_token, &state);
    status = dluma_media_status(adapter_result);
    if (status != DLUMA_STATUS_OK) return status;
    result->playing = state.playing;
    result->position_ms = state.position_ms;
    result->duration_ms = state.duration_ms;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_cue_seek(
    DlumaModOwner* owner,
    const DlumaAudioCueSeekRequest* request,
    DlumaBoolResult* result)
{
    DlumaMediaWaveOwnerSlot* wave;
    DlumaStatus status;
    if (!request || !result ||
        request->position_ms > DLUMA_MEDIA_MAX_POSITION_MS)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_resolve_wave(
        owner, request->cue_token, &wave, 0);
    if (status != DLUMA_STATUS_OK) return status;
    status = dluma_media_status(dluma_player_media_adapter_seek(
        wave->adapter_token, request->position_ms));
    result->value = status == DLUMA_STATUS_OK ? 1U : 0U;
    return status;
}

static DlumaStatus dluma_media_cue_gain(
    DlumaModOwner* owner,
    const DlumaAudioCueGainRequest* request,
    DlumaBoolResult* result)
{
    DlumaMediaWaveOwnerSlot* wave;
    DlumaStatus status;
    if (!request || !result || request->gain != request->gain ||
        request->gain < 0.0f || request->gain > 4.0f)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_resolve_wave(
        owner, request->cue_token, &wave, 0);
    if (status != DLUMA_STATUS_OK) return status;
    status = dluma_media_status(dluma_player_media_adapter_gain(
        wave->adapter_token, request->gain));
    result->value = status == DLUMA_STATUS_OK ? 1U : 0U;
    return status;
}

static DlumaStatus dluma_media_filter_values(
    float wet_mix,
    float cutoff_hz)
{
    if (wet_mix != wet_mix || cutoff_hz != cutoff_hz ||
        wet_mix < 0.0f || wet_mix > 1.0f ||
        cutoff_hz < 20.0f || cutoff_hz > 18000.0f)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_cue_filter(
    DlumaModOwner* owner,
    const DlumaAudioFilterRequest* request,
    DlumaBoolResult* result)
{
    DlumaStatus status;
    if (!request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_filter_values(
        request->wet_mix, request->cutoff_hz);
    if (status != DLUMA_STATUS_OK) return status;
    {
        DlumaMediaWaveOwnerSlot* wave;
        status = dluma_media_resolve_wave(
            owner, request->token, &wave, 0);
        (void)wave;
    }
    if (status != DLUMA_STATUS_OK) return status;
    result->value = 0U;
    return DLUMA_STATUS_UNAVAILABLE;
}

static DlumaStatus dluma_media_cue_stop(
    DlumaModOwner* owner,
    const DlumaAudioCueStopRequest* request,
    DlumaBoolResult* result)
{
    DlumaMediaWaveOwnerSlot* wave;
    uint32_t index;
    DlumaStatus status;
    if (!request || !result || request->fade_out_ms > 60000U)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_resolve_wave(
        owner, request->cue_token, &wave, &index);
    if (status != DLUMA_STATUS_OK) return status;
    status = dluma_media_status(
        dluma_player_media_adapter_stop_wave_fade(
            wave->adapter_token, request->fade_out_ms));
    if (status == DLUMA_STATUS_OK ||
        status == DLUMA_STATUS_STALE_HANDLE) {
        memset(&g_dluma_media.waves[index], 0,
               sizeof(g_dluma_media.waves[index]));
        result->value = 1U;
        return DLUMA_STATUS_OK;
    }
    result->value = 0U;
    return status;
}

static DlumaStatus dluma_media_music_acquire(
    DlumaModOwner* owner,
    const DlumaAudioMusicAcquireRequest* request,
    DlumaTokenResult* result)
{
    DlumaMediaToken adapter_token = 0;
    DlumaStatus status;
    if (!owner || !request || !result ||
        request->fade_out_ms > 60000U)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (g_dluma_media.music.active)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    status = dluma_media_status(
        dluma_player_media_adapter_music_acquire(
            request->fade_out_ms, &adapter_token));
    if (status != DLUMA_STATUS_OK) return status;
    memset(&g_dluma_media.music, 0, sizeof(g_dluma_media.music));
    g_dluma_media.music.owner_id = owner->owner_id;
    g_dluma_media.music.owner_generation = owner->generation;
    g_dluma_media.music.generation = dluma_media_next_generation();
    g_dluma_media.music.adapter_token = adapter_token;
    g_dluma_media.music.active = 1U;
    result->token = dluma_media_token(
        DLUMA_MEDIA_TOKEN_MUSIC,
        g_dluma_media.music.generation, 0U);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_media_music_state(
    DlumaModOwner* owner,
    uint64_t token,
    DlumaAudioMusicStateResult* result)
{
    DlumaMediaMusicOwner* music;
    DlumaMediaMusicState state;
    DlumaStatus status = dluma_media_resolve_music(
        owner, token, &music);
    if (status != DLUMA_STATUS_OK) return status;
    memset(&state, 0, sizeof(state));
    state.size = (unsigned int)sizeof(state);
    state.version = DLUMA_MEDIA_STRUCT_VERSION;
    status = dluma_media_status(
        dluma_player_media_adapter_music_state(
            music->adapter_token, &state));
    if (status == DLUMA_STATUS_OK)
        result->phase = state.phase;
    return status;
}

static DlumaStatus dluma_media_music_filter(
    DlumaModOwner* owner,
    const DlumaAudioFilterRequest* request,
    DlumaBoolResult* result)
{
    DlumaStatus status;
    if (!request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_filter_values(
        request->wet_mix, request->cutoff_hz);
    if (status != DLUMA_STATUS_OK) return status;
    {
        DlumaMediaMusicOwner* music;
        status = dluma_media_resolve_music(
            owner, request->token, &music);
        (void)music;
    }
    if (status != DLUMA_STATUS_OK) return status;
    result->value = 0U;
    return DLUMA_STATUS_UNAVAILABLE;
}

static DlumaStatus dluma_media_music_release(
    DlumaModOwner* owner,
    const DlumaAudioMusicReleaseRequest* request,
    DlumaBoolResult* result)
{
    DlumaMediaMusicOwner* music;
    DlumaStatus status;
    if (!request || !result || request->resume_delay_ms > 60000U)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_media_resolve_music(
        owner, request->token, &music);
    if (status != DLUMA_STATUS_OK) return status;
    status = dluma_media_status(
        dluma_player_media_adapter_music_release_after(
            music->adapter_token,
            request->resume_delay_ms));
    if (status == DLUMA_STATUS_OK ||
        status == DLUMA_STATUS_STALE_HANDLE) {
        memset(&g_dluma_media.music, 0, sizeof(g_dluma_media.music));
        result->value = 1U;
        return DLUMA_STATUS_OK;
    }
    result->value = 0U;
    return status;
}

void dluma_media_service_start(void)
{
    LONG generation = g_dluma_media.token_generation;
    memset(&g_dluma_media, 0, sizeof(g_dluma_media));
    g_dluma_media.token_generation = generation;
    g_dluma_media.initialized = 1U;
    dluma_player_media_adapter_reset();
}

void dluma_media_service_bind(
    HMODULE original_gamedll,
    void* game_script_manager)
{
    uint32_t index;
    dluma_player_media_adapter_bind(
        original_gamedll, game_script_manager);
    for (index = 0;
         index < g_dluma_media.capability_registry_count;
         ++index)
        dluma_media_service_prepare_capabilities(
            g_dluma_media.capability_registries[index]);
}

void dluma_media_service_tick(
    void* game_subobject,
    float delta_seconds)
{
    void* level = 0;
    (void)dluma_player_media_adapter_get_session_level(&level);
    dluma_player_media_adapter_tick(
        game_subobject, level,
        GetTickCount64(), delta_seconds);
}

static void dluma_media_set_capability(
    DlumaCapabilityRegistry* registry,
    uint32_t capability,
    int ready,
    const char* message)
{
    DlumaStringView status;
    status.data = message;
    status.size = (uint64_t)strlen(message);
    (void)dluma_capability_registry_set(
        registry, capability, 1, ready, status);
}

void dluma_media_service_prepare_capabilities(
    DlumaCapabilityRegistry* registry)
{
    unsigned int capabilities =
        dluma_player_media_adapter_capabilities();
    int vehicle = (capabilities &
        DLUMA_PLAYER_MEDIA_CAP_VEHICLE_STATE) != 0;
    int cues = (capabilities &
        (DLUMA_PLAYER_MEDIA_CAP_CUE_PLAYBACK |
         DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK)) ==
        (DLUMA_PLAYER_MEDIA_CAP_CUE_PLAYBACK |
         DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK);
    int music = (capabilities &
        DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE) != 0;
    unsigned int index;
    if (!registry) return;
    for (index = 0;
         index < g_dluma_media.capability_registry_count;
         ++index)
        if (g_dluma_media.capability_registries[index] == registry)
            break;
    if (index == g_dluma_media.capability_registry_count &&
        index < DLUMA_MEDIA_MAX_CAPABILITY_REGISTRIES) {
        g_dluma_media.capability_registries[index] = registry;
        ++g_dluma_media.capability_registry_count;
    }
    dluma_media_set_capability(
        registry, DLUMA_CAPABILITY_PLAYER_VEHICLE,
        vehicle, vehicle ? "ready" :
            "exact-build vehicle binding unavailable");
    dluma_media_set_capability(
        registry, DLUMA_CAPABILITY_AUDIO_STEM,
        cues, cues ? "ready" :
            "exact-build bank and cue bindings unavailable");
    dluma_media_set_capability(
        registry, DLUMA_CAPABILITY_AUDIO_MUSIC_EXCLUSIVE,
        music, music ? "ready" :
            "exact-build music ownership binding unavailable");
    dluma_media_set_capability(
        registry, DLUMA_CAPABILITY_AUDIO_WAVE_FILTER,
        0, "exact-build wave filter binding unavailable");
    dluma_media_set_capability(
        registry, DLUMA_CAPABILITY_AUDIO_MUSIC_FILTER,
        0, "exact-build music filter binding unavailable");
}

DlumaStatus dluma_media_service_invoke(
    DlumaGameHostContext* host_context,
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    void* result)
{
    if (!g_dluma_media.initialized ||
        dluma_atomic_compare_exchange(
            &g_dluma_media.stopping, 0, 0) != 0)
        return DLUMA_STATUS_UNAVAILABLE;
    switch (operation_id) {
    case 3012U:
        return dluma_media_vehicle_state(
            (DlumaVehicleStateResult*)result);
    case 6002U:
        return dluma_media_cue_play(
            owner, (const DlumaAudioCuePlayRequest*)request,
            (DlumaTokenResult*)result);
    case 6003U:
        return dluma_media_cue_stop(
            owner, (const DlumaAudioCueStopRequest*)request,
            (DlumaBoolResult*)result);
    case 6004U:
        return dluma_media_bank_open(
            host_context, owner,
            (const DlumaAudioBankOpenRequest*)request,
            (DlumaTokenResult*)result);
    case 6005U:
        return dluma_media_cue_duration(
            owner, (const DlumaAudioCueRequest*)request,
            (DlumaU64Result*)result);
    case 6006U:
        if (!request) return DLUMA_STATUS_INVALID_ARGUMENT;
        return dluma_media_cue_state(
            owner, ((const DlumaTokenRequest*)request)->token,
            (DlumaAudioCueStateResult*)result);
    case 6007U:
        return dluma_media_cue_seek(
            owner, (const DlumaAudioCueSeekRequest*)request,
            (DlumaBoolResult*)result);
    case 6008U:
        return dluma_media_cue_gain(
            owner, (const DlumaAudioCueGainRequest*)request,
            (DlumaBoolResult*)result);
    case 6009U:
        return dluma_media_cue_filter(
            owner, (const DlumaAudioFilterRequest*)request,
            (DlumaBoolResult*)result);
    case 6010U:
        return dluma_media_music_acquire(
            owner, (const DlumaAudioMusicAcquireRequest*)request,
            (DlumaTokenResult*)result);
    case 6011U:
        if (!request) return DLUMA_STATUS_INVALID_ARGUMENT;
        return dluma_media_music_state(
            owner, ((const DlumaTokenRequest*)request)->token,
            (DlumaAudioMusicStateResult*)result);
    case 6012U:
        return dluma_media_music_filter(
            owner, (const DlumaAudioFilterRequest*)request,
            (DlumaBoolResult*)result);
    case 6013U:
        return dluma_media_music_release(
            owner, (const DlumaAudioMusicReleaseRequest*)request,
            (DlumaBoolResult*)result);
    default:
        return DLUMA_STATUS_UNAVAILABLE;
    }
}

int dluma_media_service_revoke_owner(
    uint64_t owner_id,
    uint32_t owner_generation)
{
    unsigned int index;
    int drained = 1;
    for (index = 0; index < DLUMA_MEDIA_MAX_WAVES; ++index) {
        DlumaMediaWaveOwnerSlot* wave = &g_dluma_media.waves[index];
        if (!wave->active || wave->owner_id != owner_id ||
            (owner_generation &&
             wave->owner_generation != owner_generation))
            continue;
        if (dluma_player_media_adapter_stop_wave(wave->adapter_token) ==
            DLUMA_MEDIA_RESULT_SUCCESS)
            memset(wave, 0, sizeof(*wave));
        else
            drained = 0;
    }
    if (g_dluma_media.music.active &&
        g_dluma_media.music.owner_id == owner_id &&
        (!owner_generation ||
         g_dluma_media.music.owner_generation == owner_generation)) {
        if (dluma_player_media_adapter_music_release(
                g_dluma_media.music.adapter_token) ==
            DLUMA_MEDIA_RESULT_SUCCESS)
            memset(&g_dluma_media.music, 0, sizeof(g_dluma_media.music));
        else
            drained = 0;
    }
    if (drained) {
        for (index = 0; index < DLUMA_MEDIA_MAX_BANKS; ++index) {
            DlumaMediaBankSlot* bank = &g_dluma_media.banks[index];
            if (bank->active && bank->owner_id == owner_id &&
                (!owner_generation ||
                 bank->owner_generation == owner_generation))
                memset(bank, 0, sizeof(*bank));
        }
    } else {
        g_dluma_media.retained = 1U;
    }
    return drained;
}

int dluma_media_service_archive_in_use(
    const DlumaCompanionArchive* archive)
{
    unsigned int index;
    if (!archive) return 0;
    if (dluma_player_media_adapter_bank_loaded_for_pack(archive))
        return 1;
    for (index = 0; index < DLUMA_MEDIA_MAX_BANKS; ++index)
        if (g_dluma_media.banks[index].active &&
            g_dluma_media.banks[index].archive == archive)
            return 1;
    return 0;
}

int dluma_media_service_quiesce_all(void)
{
    dluma_atomic_exchange(&g_dluma_media.stopping, 1);
    if (!dluma_player_media_adapter_force_cleanup()) {
        g_dluma_media.retained = 1U;
        return 0;
    }
    memset(g_dluma_media.waves, 0, sizeof(g_dluma_media.waves));
    memset(g_dluma_media.banks, 0, sizeof(g_dluma_media.banks));
    memset(&g_dluma_media.music, 0, sizeof(g_dluma_media.music));
    return 1;
}

int dluma_media_service_all_drained(void)
{
    unsigned int index;
    if (g_dluma_media.retained) return 0;
    if (g_dluma_media.music.active) return 0;
    for (index = 0; index < DLUMA_MEDIA_MAX_WAVES; ++index)
        if (g_dluma_media.waves[index].active) return 0;
    return 1;
}
