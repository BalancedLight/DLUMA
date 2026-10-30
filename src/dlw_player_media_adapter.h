#ifndef DLUMA_PLAYER_MEDIA_ADAPTER_H
#define DLUMA_PLAYER_MEDIA_ADAPTER_H

#include "dlw_common.h"
#include "dlw_companion_archive.h"
#include "../sdk/c/dluma_api.h"

#define DLUMA_PLAYER_MEDIA_CAP_VEHICLE_STATE 0x00000001U
#define DLUMA_PLAYER_MEDIA_CAP_CUE_PLAYBACK 0x00000002U
#define DLUMA_PLAYER_MEDIA_CAP_MUSIC_EXCLUSIVE 0x00000004U
#define DLUMA_PLAYER_MEDIA_CAP_PER_WAVE_FILTER 0x00000008U
#define DLUMA_PLAYER_MEDIA_CAP_WAVE_BANK 0x00000010U
#define DLUMA_PLAYER_MEDIA_CAP_MUSIC_FILTER 0x00000020U

#define DLUMA_MEDIA_RESULT_SUCCESS 1
#define DLUMA_MEDIA_RESULT_UNAVAILABLE 0
#define DLUMA_MEDIA_RESULT_INVALID (-1)
#define DLUMA_MEDIA_RESULT_WRONG_THREAD (-2)
#define DLUMA_MEDIA_RESULT_BUSY (-3)
#define DLUMA_MEDIA_RESULT_BUFFER_TOO_SMALL (-4)
#define DLUMA_MEDIA_RESULT_STALE (-5)
#define DLUMA_MEDIA_STRUCT_VERSION 1U
#define DLUMA_MEDIA_ROUTE_MUSIC 1U
#define DLUMA_MEDIA_CUE_FLAG_LOOP 0x00000001U
#define DLUMA_MEDIA_MUSIC_ACQUIRING 1U
#define DLUMA_MEDIA_MUSIC_OWNED 2U
#define DLUMA_MEDIA_MUSIC_FAILED 3U
#define DLUMA_MEDIA_PREPARED_BANK_ALIAS_CAPACITY 64U
#define DLUMA_MEDIA_PREPARED_CUE_CAPACITY 128U
#define DLUMA_MEDIA_PREPARED_FINGERPRINT_CAPACITY 65U

typedef uint64_t DlumaMediaToken;

typedef struct DlumaVehicleAdapterState {
    unsigned int size;
    unsigned int version;
    unsigned int available;
    unsigned int local_player_present;
    unsigned int vehicle_present;
    unsigned int is_local_driver;
    unsigned int is_buggy;
    float velocity_x;
    float velocity_y;
    float velocity_z;
    double speed_mps;
    double speed_mph;
    DlumaMediaToken level_epoch;
} DlumaVehicleAdapterState;

typedef struct DlumaMediaCuePlayRequest {
    unsigned int size;
    unsigned int version;
    DlumaMediaToken start_ms;
    float initial_gain;
    unsigned int route;
    unsigned int flags;
    unsigned int fade_in_ms;
    unsigned int fade_out_ms;
} DlumaMediaCuePlayRequest;

typedef struct DlumaMediaCueState {
    unsigned int size;
    unsigned int version;
    unsigned int playing;
    unsigned int reserved;
    DlumaMediaToken position_ms;
    DlumaMediaToken duration_ms;
} DlumaMediaCueState;

typedef struct DlumaMediaMusicState {
    unsigned int size;
    unsigned int version;
    unsigned int phase;
    unsigned int reserved;
} DlumaMediaMusicState;

/*
 * A bank which another loader subsystem has already validated, mounted, and
 * pinned for process lifetime.  engine_bank_alias deliberately omits the
 * .csb suffix because the exact Player LoadWaveBank implementation appends it.
 */
typedef struct DlumaMediaPreparedBankDescriptor {
    unsigned int size;
    unsigned int version;
    char engine_bank_alias[DLUMA_MEDIA_PREPARED_BANK_ALIAS_CAPACITY];
    char cue_name[DLUMA_MEDIA_PREPARED_CUE_CAPACITY];
    char content_fingerprint[DLUMA_MEDIA_PREPARED_FINGERPRINT_CAPACITY];
    unsigned int source_mounted;
    unsigned int source_locked;
} DlumaMediaPreparedBankDescriptor;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_player_media_adapter_reset(void);
int dluma_player_media_adapter_stop_exit_hook(void);
void dluma_player_media_adapter_bind(
    HMODULE original_gamedll, void* game_script_manager);
void dluma_player_media_adapter_tick(
    void* game_subobject, void* level_interface,
    ULONGLONG now_ms, float delta_seconds);
int dluma_player_media_adapter_publish_frontend_music_manager(
    void* owner_level, void* music_manager);
int dluma_player_media_adapter_clear_frontend_music_manager(
    void* owner_level, void* music_manager);
int dluma_player_media_adapter_publish_frontend_main_menu_module(
    void* main_menu_module);
int dluma_player_media_adapter_clear_frontend_main_menu_module(
    void* main_menu_module);
int dluma_player_media_adapter_get_session_level(
    void** level_interface_out);
/* Read-only session owner query for an exact, serialized frame callback.
 * Does not publish a media owner or require an active music manager. */
int dluma_player_media_adapter_query_session_owner_for_frame(
    void* game_subobject, void* observed_active_level,
    void** level_interface_out);
int dluma_player_media_adapter_query_local_player_for_frame(
    void* game_subobject, void* expected_level, void** player_out);
unsigned int dluma_player_media_adapter_capabilities(void);
int dluma_player_media_adapter_get_vehicle_state(
    DlumaVehicleAdapterState* state);
int dluma_player_media_adapter_open_bank(
    DlumaCompanionArchive* pack,
    const DlumaCompanionArchiveEntry* bank_entry,
    DlumaMediaToken* bank_out);
int dluma_player_media_adapter_open_prepared_bank(
    const DlumaMediaPreparedBankDescriptor* descriptor,
    DlumaMediaToken* bank_out);
int dluma_player_media_adapter_prepare_main_menu_bank(
    const DlumaMediaPreparedBankDescriptor* descriptor,
    void* main_menu_module,
    DlumaMediaToken* bank_out);
int dluma_player_media_adapter_bank_loaded_for_pack(
    const DlumaCompanionArchive* pack);
int dluma_player_media_adapter_probe_duration(
    DlumaMediaToken bank, const char* bank_entry,
    DlumaMediaToken* duration_ms_out);
int dluma_player_media_adapter_play(
    DlumaMediaToken bank, const char* bank_entry,
    const DlumaMediaCuePlayRequest* request,
    DlumaMediaToken* wave_out);
int dluma_player_media_adapter_wave_state(
    DlumaMediaToken wave, DlumaMediaCueState* state);
int dluma_player_media_adapter_seek(
    DlumaMediaToken wave, DlumaMediaToken position_ms);
int dluma_player_media_adapter_gain(
    DlumaMediaToken wave, float gain);
int dluma_player_media_adapter_wave_filter(
    DlumaMediaToken wave, float wet_mix, float cutoff_hz);
int dluma_player_media_adapter_stop_wave(DlumaMediaToken wave);
int dluma_player_media_adapter_stop_wave_fade(
    DlumaMediaToken wave, unsigned int fade_out_ms);
int dluma_player_media_adapter_music_acquire(
    unsigned int fade_out_ms, DlumaMediaToken* ownership_out);
int dluma_player_media_adapter_music_state(
    DlumaMediaToken ownership, DlumaMediaMusicState* state);
int dluma_player_media_adapter_music_maintain(
    DlumaMediaToken ownership);
int dluma_player_media_adapter_music_filter(
    DlumaMediaToken ownership, float wet_mix, float cutoff_hz);
int dluma_player_media_adapter_music_release(
    DlumaMediaToken ownership);
int dluma_player_media_adapter_music_release_after(
    DlumaMediaToken ownership, unsigned int resume_delay_ms);
int dluma_player_media_adapter_force_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
