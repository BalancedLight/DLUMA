#include <stdio.h>
#include <string.h>

#include "../src/game/dlw_media_service.c"

#define CHECK(value) do { if (!(value)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); return 0; \
} } while (0)

static DlumaCompanionArchive g_archive;
static DlumaGameHostContext g_host_context;
static DlumaMediaToken g_next_wave = 1000U;
static int g_stop_result = DLUMA_MEDIA_RESULT_SUCCESS;
static int g_music_release_result = DLUMA_MEDIA_RESULT_SUCCESS;
static int g_force_cleanup = 1;
static int g_vehicle_available = 1;
static unsigned int g_stop_calls;
static unsigned int g_music_release_calls;
static unsigned int g_archive_mount_calls;

int dluma_stricmp(const char* left, const char* right)
{
    return _stricmp(left, right);
}

DlumaStatus dluma_capability_registry_set(
    DlumaCapabilityRegistry* registry, uint32_t capability_id,
    int configured, int ready, DlumaStringView status)
{
    size_t size;
    if (!registry || capability_id > DLUMA_CAPABILITY_REGISTRY_MAX_ID)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    registry->entries[capability_id].configured = configured ? 1U : 0U;
    registry->entries[capability_id].ready = ready ? 1U : 0U;
    size = status.size < DLUMA_CAPABILITY_STATUS_MAX_BYTES
        ? (size_t)status.size : DLUMA_CAPABILITY_STATUS_MAX_BYTES;
    if (size) memcpy(registry->entries[capability_id].status, status.data, size);
    registry->entries[capability_id].status[size] = 0;
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_game_host_context_resolve_companion(
    DlumaGameHostContext* context, uint64_t token,
    DlumaCompanionArchive** archive_out)
{
    (void)context;
    if (!archive_out || token != 77U) return DLUMA_STATUS_STALE_HANDLE;
    *archive_out = &g_archive;
    return DLUMA_STATUS_OK;
}

int dluma_companion_archive_mount(DlumaCompanionArchive* archive)
{
    ++g_archive_mount_calls;
    archive->mounted = 1U;
    return 1;
}

const DlumaCompanionArchiveAudioManifest* dluma_companion_archive_find_bank(
    const DlumaCompanionArchive* archive, const char* bank_identifier)
{
    if (archive == &g_archive && strcmp(bank_identifier, "media") == 0)
        return &g_archive.audio_catalog.banks[0];
    return 0;
}

int dluma_companion_archive_manifest_has_cue(
    const DlumaCompanionArchive* archive, const char* bank_identifier,
    const char* cue_name, DWORD* duration_ms_out)
{
    if (archive != &g_archive || strcmp(bank_identifier, "media") != 0 ||
        strcmp(cue_name, "Main") != 0)
        return 0;
    if (duration_ms_out) *duration_ms_out = 1000U;
    return 1;
}

void dluma_player_media_adapter_reset(void) {}
void dluma_player_media_adapter_bind(HMODULE module, void* manager)
{ (void)module; (void)manager; }
void dluma_player_media_adapter_tick(
    void* game, void* level, ULONGLONG now, float delta)
{ (void)game; (void)level; (void)now; (void)delta; }
int dluma_player_media_adapter_get_session_level(void** level)
{ if (level) *level = 0; return DLUMA_MEDIA_RESULT_UNAVAILABLE; }
unsigned int dluma_player_media_adapter_capabilities(void)
{
    return DLUMA_PLAYER_MEDIA_CAP_VEHICLE_STATE |
        DLUMA_PLAYER_MEDIA_CAP_CUE_PLAYBACK |
        DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK |
        DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE;
}
int dluma_player_media_adapter_get_vehicle_state(DlumaVehicleAdapterState* state)
{
    state->available = g_vehicle_available ? 1U : 0U;
    state->local_player_present = 1U;
    state->vehicle_present = 1U; state->is_local_driver = 1U;
    state->is_buggy = 1U; state->velocity_x = 3.0f;
    state->velocity_y = 4.0f; state->speed_mps = 5.0;
    state->speed_mph = 11.1847; state->level_epoch = 9U;
    return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_open_bank(
    DlumaCompanionArchive* archive, const DlumaCompanionArchiveEntry* entry,
    DlumaMediaToken* token)
{
    (void)archive; (void)entry; *token = 100U;
    return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_bank_loaded_for_pack(
    const DlumaCompanionArchive* archive)
{ (void)archive; return 0; }
int dluma_player_media_adapter_probe_duration(
    DlumaMediaToken bank, const char* cue, DlumaMediaToken* duration)
{
    if (bank != 100U || strcmp(cue, "Main") != 0) return DLUMA_MEDIA_RESULT_INVALID;
    *duration = 1000U; return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_play(
    DlumaMediaToken bank, const char* cue,
    const DlumaMediaCuePlayRequest* request, DlumaMediaToken* wave)
{
    if (bank != 100U || strcmp(cue, "Main") != 0 ||
        request->route != DLUMA_MEDIA_ROUTE_MUSIC)
        return DLUMA_MEDIA_RESULT_INVALID;
    *wave = ++g_next_wave; return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_wave_state(
    DlumaMediaToken wave, DlumaMediaCueState* state)
{
    (void)wave; state->playing = 1U; state->position_ms = 250U;
    state->duration_ms = 1000U; return DLUMA_MEDIA_RESULT_SUCCESS;
}
int dluma_player_media_adapter_seek(DlumaMediaToken wave, DlumaMediaToken position)
{ (void)wave; (void)position; return DLUMA_MEDIA_RESULT_SUCCESS; }
int dluma_player_media_adapter_gain(DlumaMediaToken wave, float gain)
{ (void)wave; (void)gain; return DLUMA_MEDIA_RESULT_SUCCESS; }
int dluma_player_media_adapter_wave_filter(
    DlumaMediaToken wave, float wet, float cutoff)
{ (void)wave; (void)wet; (void)cutoff; return DLUMA_MEDIA_RESULT_SUCCESS; }
int dluma_player_media_adapter_stop_wave(DlumaMediaToken wave)
{ (void)wave; ++g_stop_calls; return g_stop_result; }
int dluma_player_media_adapter_stop_wave_fade(
    DlumaMediaToken wave, unsigned int fade)
{ (void)wave; (void)fade; ++g_stop_calls; return g_stop_result; }
int dluma_player_media_adapter_music_acquire(
    unsigned int fade, DlumaMediaToken* token)
{ (void)fade; *token = 500U; return DLUMA_MEDIA_RESULT_SUCCESS; }
int dluma_player_media_adapter_music_state(
    DlumaMediaToken token, DlumaMediaMusicState* state)
{ (void)token; state->phase = DLUMA_MEDIA_MUSIC_OWNED; return DLUMA_MEDIA_RESULT_SUCCESS; }
int dluma_player_media_adapter_music_filter(
    DlumaMediaToken token, float wet, float cutoff)
{ (void)token; (void)wet; (void)cutoff; return DLUMA_MEDIA_RESULT_SUCCESS; }
int dluma_player_media_adapter_music_release(DlumaMediaToken token)
{ (void)token; ++g_music_release_calls; return g_music_release_result; }
int dluma_player_media_adapter_music_release_after(
    DlumaMediaToken token, unsigned int delay)
{ (void)token; (void)delay; ++g_music_release_calls; return g_music_release_result; }
int dluma_player_media_adapter_force_cleanup(void) { return g_force_cleanup; }

static DlumaStringView view(const char* text)
{
    DlumaStringView result = {text, (uint64_t)strlen(text)};
    return result;
}

static DlumaModOwner owner(uint64_t id, uint32_t generation)
{
    DlumaModOwner value;
    memset(&value, 0, sizeof(value));
    value.owner_id = id; value.generation = generation;
    value.state = DLUMA_MOD_OWNER_ACTIVE;
    return value;
}

static uint64_t open_bank(DlumaModOwner* mod_owner)
{
    DlumaAudioBankOpenRequest request;
    DlumaTokenResult result;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result));
    request.companion_token = 77U; request.bank_id = view("media");
    if (dluma_media_service_invoke(
            &g_host_context, mod_owner, 6004U, &request, &result) !=
        DLUMA_STATUS_OK) return 0;
    return result.token;
}

static uint64_t play_cue(DlumaModOwner* mod_owner, uint64_t bank)
{
    DlumaAudioCuePlayRequest request;
    DlumaTokenResult result;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result));
    request.bank_token = bank; request.cue = view("Main");
    request.initial_gain = 1.0f; request.loop = 1U;
    if (dluma_media_service_invoke(0, mod_owner, 6002U, &request, &result) !=
        DLUMA_STATUS_OK) return 0;
    return result.token;
}

static void reset_fixture(void)
{
    memset(&g_archive, 0, sizeof(g_archive));
    strcpy_s(g_archive.entries[0].virtual_path,
        sizeof(g_archive.entries[0].virtual_path), "Media.csb");
    g_archive.entries[0].is_csb = 1U; g_archive.entry_count = 1U;
    strcpy_s(g_archive.audio_catalog.banks[0].bank_identifier,
        sizeof(g_archive.audio_catalog.banks[0].bank_identifier), "media");
    strcpy_s(g_archive.audio_catalog.banks[0].bank_path,
        sizeof(g_archive.audio_catalog.banks[0].bank_path), "Media.csb");
    g_archive.audio_catalog.banks[0].valid = 1U;
    g_archive.audio_catalog.bank_count = 1U;
    g_stop_result = DLUMA_MEDIA_RESULT_SUCCESS;
    g_music_release_result = DLUMA_MEDIA_RESULT_SUCCESS;
    g_force_cleanup = 1; g_stop_calls = 0U; g_music_release_calls = 0U;
    g_archive_mount_calls = 0U;
    g_vehicle_available = 1;
    dluma_media_service_start();
}

static int test_vehicle_and_capabilities(void)
{
    DlumaVehicleStateResult result;
    DlumaCapabilityRegistry registry;
    char type[16], class_name[16];
    reset_fixture(); memset(&result, 0, sizeof(result)); memset(&registry, 0, sizeof(registry));
    result.vehicle_type.data = type; result.vehicle_type.capacity = sizeof(type);
    result.class_name.data = class_name; result.class_name.capacity = sizeof(class_name);
    CHECK(dluma_media_service_invoke(0, 0, 3012U, 0, &result) == DLUMA_STATUS_OK);
    CHECK(result.is_local_driver && result.speed_mps == 5.0 && strcmp(type, "buggy") == 0);
    g_vehicle_available = 0;
    CHECK(dluma_media_service_invoke(
        0, 0, 3012U, 0, &result) == DLUMA_STATUS_UNAVAILABLE);
    dluma_media_service_prepare_capabilities(&registry);
    CHECK(registry.entries[DLUMA_CAPABILITY_PLAYER_VEHICLE].ready == 1U);
    CHECK(registry.entries[DLUMA_CAPABILITY_AUDIO_WAVE_FILTER].ready == 0U);
    return 1;
}

static int test_cue_lifecycle_and_ownership(void)
{
    DlumaModOwner first = owner(1U, 1U), foreign = owner(2U, 1U);
    DlumaAudioCueRequest duration_request;
    DlumaU64Result duration_result;
    DlumaTokenRequest state_request;
    DlumaAudioCueStateResult state_result;
    DlumaAudioCueSeekRequest seek_request;
    DlumaAudioCueGainRequest gain_request;
    DlumaAudioFilterRequest filter_request;
    DlumaAudioCueStopRequest stop_request;
    DlumaBoolResult bool_result;
    uint64_t bank, wave;
    reset_fixture(); bank = open_bank(&first); wave = play_cue(&first, bank);
    CHECK(bank && wave);
    CHECK(g_archive_mount_calls == 0U);
    CHECK(!g_archive.mounted);
    memset(&duration_request, 0, sizeof(duration_request)); memset(&duration_result, 0, sizeof(duration_result));
    duration_request.bank_token = bank; duration_request.cue = view("Main");
    CHECK(dluma_media_service_invoke(0, &first, 6005U, &duration_request, &duration_result) == DLUMA_STATUS_OK);
    CHECK(duration_result.value == 1000U);
    memset(&state_request, 0, sizeof(state_request)); memset(&state_result, 0, sizeof(state_result));
    state_request.token = wave;
    CHECK(dluma_media_service_invoke(0, &foreign, 6006U, &state_request, &state_result) == DLUMA_STATUS_FOREIGN_HANDLE);
    CHECK(dluma_media_service_invoke(0, &first, 6006U, &state_request, &state_result) == DLUMA_STATUS_OK);
    memset(&seek_request, 0, sizeof(seek_request)); seek_request.cue_token = wave; seek_request.position_ms = 500U;
    CHECK(dluma_media_service_invoke(0, &first, 6007U, &seek_request, &bool_result) == DLUMA_STATUS_OK);
    memset(&gain_request, 0, sizeof(gain_request)); gain_request.cue_token = wave; gain_request.gain = 4.0f;
    CHECK(dluma_media_service_invoke(0, &first, 6008U, &gain_request, &bool_result) == DLUMA_STATUS_OK);
    gain_request.gain = 4.1f;
    CHECK(dluma_media_service_invoke(0, &first, 6008U, &gain_request, &bool_result) == DLUMA_STATUS_INVALID_ARGUMENT);
    memset(&filter_request, 0, sizeof(filter_request)); filter_request.token = wave; filter_request.wet_mix = 0.5f; filter_request.cutoff_hz = 900.0f;
    CHECK(dluma_media_service_invoke(0, &first, 6009U, &filter_request, &bool_result) == DLUMA_STATUS_UNAVAILABLE);
    memset(&stop_request, 0, sizeof(stop_request)); stop_request.cue_token = wave; stop_request.fade_out_ms = 60000U;
    CHECK(dluma_media_service_invoke(0, &first, 6003U, &stop_request, &bool_result) == DLUMA_STATUS_OK);
    CHECK(dluma_media_service_invoke(0, &first, 6006U, &state_request, &state_result) == DLUMA_STATUS_STALE_HANDLE);
    return 1;
}

static int test_music_contention_and_revoke(void)
{
    DlumaModOwner first = owner(1U, 3U), second = owner(2U, 1U);
    DlumaAudioMusicAcquireRequest request;
    DlumaTokenResult result;
    DlumaTokenRequest state_request;
    DlumaAudioMusicStateResult state_result;
    DlumaAudioFilterRequest filter;
    DlumaBoolResult bool_result;
    uint64_t token;
    reset_fixture(); memset(&request, 0, sizeof(request)); request.fade_out_ms = 1500U;
    memset(&result, 0, sizeof(result));
    CHECK(dluma_media_service_invoke(0, &first, 6010U, &request, &result) == DLUMA_STATUS_OK);
    token = result.token; CHECK(token != 0U);
    CHECK(dluma_media_service_invoke(0, &second, 6010U, &request, &result) == DLUMA_STATUS_LIMIT_EXCEEDED);
    memset(&state_request, 0, sizeof(state_request)); state_request.token = token;
    CHECK(dluma_media_service_invoke(0, &first, 6011U, &state_request, &state_result) == DLUMA_STATUS_OK);
    CHECK(state_result.phase == DLUMA_MEDIA_MUSIC_OWNED);
    memset(&filter, 0, sizeof(filter)); filter.token = token; filter.wet_mix = 1.0f; filter.cutoff_hz = 20.0f;
    CHECK(dluma_media_service_invoke(0, &first, 6012U, &filter, &bool_result) == DLUMA_STATUS_UNAVAILABLE);
    CHECK(dluma_media_service_revoke_owner(first.owner_id, first.generation));
    CHECK(g_music_release_calls == 1U);
    CHECK(dluma_media_service_invoke(0, &first, 6011U, &state_request, &state_result) == DLUMA_STATUS_STALE_HANDLE);
    return 1;
}

static int test_generation_wildcard_and_failed_drain(void)
{
    DlumaModOwner first = owner(9U, 8U);
    uint64_t bank, wave;
    reset_fixture(); bank = open_bank(&first); wave = play_cue(&first, bank);
    CHECK(bank && wave); g_stop_result = DLUMA_MEDIA_RESULT_UNAVAILABLE;
    CHECK(!dluma_media_service_revoke_owner(first.owner_id, 0U));
    CHECK(!dluma_media_service_all_drained());
    g_force_cleanup = 0;
    CHECK(!dluma_media_service_quiesce_all());
    CHECK(!dluma_media_service_all_drained());
    reset_fixture(); CHECK(dluma_media_service_all_drained());
    return 1;
}

int main(void)
{
    unsigned int passed = 0U;
    if (!test_vehicle_and_capabilities()) return 1;
    ++passed; puts("[PASS] generic vehicle state and capability gates");
    if (!test_cue_lifecycle_and_ownership()) return 1;
    ++passed; puts("[PASS] cue lifecycle, bounds, filters, and owner tokens");
    if (!test_music_contention_and_revoke()) return 1;
    ++passed; puts("[PASS] exclusive music contention and owner revocation");
    if (!test_generation_wildcard_and_failed_drain()) return 1;
    ++passed; puts("[PASS] wildcard revocation and retained failed drain");
    printf("media service fake cases passed: %u\n", passed);
    return 0;
}
