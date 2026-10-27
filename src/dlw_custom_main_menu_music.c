#include "dlw_custom_main_menu_music.h"

#include "dlw_custom_main_menu.h"
#include "dlw_custom_main_menu_runtime.h"
#include "dlw_custom_main_menu_runtime_core.h"
#include "dlw_player_media_adapter.h"

typedef struct DlumaCustomMainMenuMusicState
{
    DlumaMediaToken banks[DLUMA_CUSTOM_MENU_MAX_BANKS];
    DlumaMediaToken lease;
    DlumaMediaToken wave;
    unsigned int bank_count;
    ULONGLONG next_play_retry_ms;
    char normal_cue[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
    char active_cue[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
    unsigned char prepared;
    unsigned char unavailable;
    unsigned char unavailable_logged;
    unsigned char retry_logged;
    unsigned char play_failure_logged;
} DlumaCustomMainMenuMusicState;

static DlumaCustomMainMenuMusicState g_dluma_cmm_music;

static int dluma_cmm_music_text_equal_exact(
    const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right)
        return left == right;
    while (left[index] && right[index] && left[index] == right[index])
        ++index;
    return left[index] == right[index];
}

static const DlumaCustomMainMenuMusicOverride *dluma_cmm_music_override(
    const DlumaCustomMainMenuManifest *manifest,
    DlumaCustomMainMenuMusicTarget target)
{
    unsigned int index;
    if (!manifest || target == DLUMA_CUSTOM_MENU_MUSIC_INVALID)
        return 0;
    for (index = 0; index < manifest->music_override_count; ++index)
        if (manifest->music_overrides[index].target == (unsigned int)target)
            return &manifest->music_overrides[index];
    return 0;
}

static int dluma_cmm_music_cue_bank(
    const char *cue_name, unsigned int *bank_index_out)
{
    unsigned int bank_index;
    if (bank_index_out)
        *bank_index_out = 0;
    if (!cue_name || !cue_name[0])
        return 0;
    for (bank_index = 0; bank_index < g_dluma_cmm_music.bank_count;
         ++bank_index)
    {
        const DlumaCompanionArchiveAudioManifest *catalog =
            dluma_custom_main_menu_selected_bank_catalog_at(bank_index);
        unsigned int cue_index;
        if (!catalog)
            return 0;
        for (cue_index = 0; cue_index < catalog->cue_count; ++cue_index)
        {
            if (dluma_cmm_music_text_equal_exact(
                    catalog->cues[cue_index].name, cue_name))
            {
                if (bank_index_out)
                    *bank_index_out = bank_index;
                return 1;
            }
        }
    }
    return 0;
}

static int dluma_cmm_music_release_override(void)
{
    if (g_dluma_cmm_music.wave)
    {
        int stop_result = dluma_player_media_adapter_stop_wave(
            g_dluma_cmm_music.wave);
        if (stop_result != DLUMA_MEDIA_RESULT_SUCCESS &&
            stop_result != DLUMA_MEDIA_RESULT_STALE)
            return 0;
        g_dluma_cmm_music.wave = 0;
    }
    if (g_dluma_cmm_music.lease)
    {
        int release_result = dluma_player_media_adapter_music_release(
            g_dluma_cmm_music.lease);
        if (release_result != DLUMA_MEDIA_RESULT_SUCCESS &&
            release_result != DLUMA_MEDIA_RESULT_STALE)
            return 0;
        g_dluma_cmm_music.lease = 0;
    }
    g_dluma_cmm_music.active_cue[0] = 0;
    return 1;
}

static int dluma_cmm_music_maintain_override(void)
{
    if (g_dluma_cmm_music.lease &&
        dluma_player_media_adapter_music_maintain(
            g_dluma_cmm_music.lease) == DLUMA_MEDIA_RESULT_SUCCESS)
        return 1;
    (void)dluma_cmm_music_release_override();
    dluma_log_pair(
        "WARN", "custom_main_menu_music_override",
        "exclusive_lease_reassert_failed_normal_music_resumed");
    return 0;
}

void dluma_custom_main_menu_music_reset(void)
{
    memset(&g_dluma_cmm_music, 0, sizeof(g_dluma_cmm_music));
    dluma_str_copy(
        g_dluma_cmm_music.normal_cue,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_music.normal_cue), "dw_menu");
}

void dluma_custom_main_menu_music_retry_preparation(void)
{
    g_dluma_cmm_music.prepared = 0;
    memset(g_dluma_cmm_music.banks, 0, sizeof(g_dluma_cmm_music.banks));
}

static int dluma_cmm_music_prepare_state(void *main_menu_module)
{
    const char *fingerprint = dluma_custom_main_menu_selected_fingerprint();
    unsigned int count;
    unsigned int index;
    if (!dluma_custom_main_menu_runtime_audio_ready())
        return DLUMA_CMM_MUSIC_RESOLVE_RETRY;
    if (GetTickCount64() < g_dluma_cmm_music.next_play_retry_ms)
        return DLUMA_CMM_MUSIC_RESOLVE_RETRY;
    if (g_dluma_cmm_music.prepared)
        return DLUMA_CMM_MUSIC_RESOLVE_READY;
    if (g_dluma_cmm_music.unavailable)
        return DLUMA_CMM_MUSIC_RESOLVE_PERMANENT;
    if (!main_menu_module || !dluma_custom_main_menu_selected_ready())
        return DLUMA_CMM_MUSIC_RESOLVE_RETRY;
    count = dluma_custom_main_menu_selected_bank_count();
    if (!count || count > DLUMA_CUSTOM_MENU_MAX_BANKS ||
        !fingerprint || !fingerprint[0])
        goto permanent;
    g_dluma_cmm_music.bank_count = count;
    for (index = 0; index < count; ++index)
    {
        DlumaMediaPreparedBankDescriptor descriptor;
        const DlumaCompanionArchiveAudioManifest *catalog =
            dluma_custom_main_menu_selected_bank_catalog_at(index);
        char source_path[DLUMA_MAX_WPATH * 2U];
        char source_sha256[DLUMA_SHA256_HEX_CAPACITY];
        int result;
        memset(&descriptor, 0, sizeof(descriptor));
        source_path[0] = 0;
        source_sha256[0] = 0;
        descriptor.size = sizeof(descriptor);
        descriptor.version = DLUMA_MEDIA_STRUCT_VERSION;
        /* A returning menu can request music before its sources are mounted. */
        if (!catalog)
            goto retry;
        if (!catalog->valid || !catalog->cue_count)
            goto permanent;
        /* A later bank can still be waiting for its source/audio owner after
         * an earlier bank has been prepared.  Retain that validated token so
         * the bounded retry cannot load the same CSB twice. */
        if (g_dluma_cmm_music.banks[index])
            continue;
        if (!dluma_custom_main_menu_selected_bank_alias_at(
                index, descriptor.engine_bank_alias,
                DLUMA_ARRAY_COUNT(descriptor.engine_bank_alias)) ||
            !dluma_custom_main_menu_selected_bank_source_snapshot_at(
                index, source_path, DLUMA_ARRAY_COUNT(source_path),
                source_sha256, DLUMA_ARRAY_COUNT(source_sha256)))
            goto retry;
        dluma_str_copy(
            descriptor.cue_name, DLUMA_ARRAY_COUNT(descriptor.cue_name),
            catalog->cues[0].name);
        dluma_str_copy(
            descriptor.content_fingerprint,
            DLUMA_ARRAY_COUNT(descriptor.content_fingerprint), source_sha256);
        descriptor.source_mounted = 1U;
        descriptor.source_locked = 1U;
        result = dluma_player_media_adapter_prepare_main_menu_bank(
            &descriptor, main_menu_module, &g_dluma_cmm_music.banks[index]);
        if (result != DLUMA_MEDIA_RESULT_SUCCESS ||
            !g_dluma_cmm_music.banks[index])
        {
            if (result == DLUMA_MEDIA_RESULT_INVALID ||
                result == DLUMA_MEDIA_RESULT_BUFFER_TOO_SMALL)
                goto permanent;
            goto retry;
        }
        dluma_log_pair(
            "INFO", "custom_main_menu_audio_bank_loaded",
            descriptor.engine_bank_alias);
    }
    g_dluma_cmm_music.prepared = 1U;
    g_dluma_cmm_music.unavailable_logged = 0;
    g_dluma_cmm_music.retry_logged = 0;
    return DLUMA_CMM_MUSIC_RESOLVE_READY;

retry:
    if (!g_dluma_cmm_music.retry_logged)
    {
        dluma_log_pair(
            "INFO", "custom_main_menu_audio_group",
            "bank_source_or_audio_owner_not_ready_retrying");
        g_dluma_cmm_music.retry_logged = 1U;
    }
    return DLUMA_CMM_MUSIC_RESOLVE_RETRY;

permanent:
    g_dluma_cmm_music.unavailable = 1U;
    (void)dluma_cmm_music_release_override();
    if (!g_dluma_cmm_music.unavailable_logged)
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_audio_group",
            "bank_load_or_probe_failed_visual_menu_retained");
        g_dluma_cmm_music.unavailable_logged = 1U;
    }
    return DLUMA_CMM_MUSIC_RESOLVE_PERMANENT;
}

int dluma_custom_main_menu_music_prepare(void *main_menu_module)
{
    return dluma_cmm_music_prepare_state(main_menu_module) ==
        DLUMA_CMM_MUSIC_RESOLVE_READY;
}

void dluma_custom_main_menu_music_note_normal_cue(const char *cue_name)
{
    if (!cue_name || !dluma_custom_main_menu_asset_token_valid(cue_name))
        cue_name = "dw_menu";
    dluma_str_copy(
        g_dluma_cmm_music.normal_cue,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_music.normal_cue), cue_name);
}

int dluma_custom_main_menu_music_reconcile(const char *cue_name)
{
    DlumaMediaCuePlayRequest request;
    unsigned int bank_index = 0;
    int transition;
    if (!g_dluma_cmm_music.prepared || g_dluma_cmm_music.unavailable)
        return cue_name && cue_name[0] ? 0 : 1;
    transition = dluma_custom_main_menu_runtime_music_transition(
        g_dluma_cmm_music.active_cue, cue_name,
        g_dluma_cmm_music.normal_cue);
    if (transition == DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION)
        return g_dluma_cmm_music.active_cue[0]
            ? dluma_cmm_music_maintain_override()
            : 1;
    if (transition == DLUMA_CMM_MUSIC_TRANSITION_RELEASE)
        return dluma_cmm_music_release_override();
    if (GetTickCount64() < g_dluma_cmm_music.next_play_retry_ms)
        return 0;
    if (!dluma_cmm_music_cue_bank(cue_name, &bank_index) ||
        bank_index >= g_dluma_cmm_music.bank_count)
        return dluma_cmm_music_release_override() && 0;
    if (g_dluma_cmm_music.lease &&
        !dluma_cmm_music_maintain_override())
        return 0;
    if (transition == DLUMA_CMM_MUSIC_TRANSITION_SWITCH &&
        g_dluma_cmm_music.wave)
    {
        int stop_result = dluma_player_media_adapter_stop_wave(
            g_dluma_cmm_music.wave);
        if (stop_result != DLUMA_MEDIA_RESULT_SUCCESS &&
            stop_result != DLUMA_MEDIA_RESULT_STALE)
            return dluma_cmm_music_release_override() && 0;
        g_dluma_cmm_music.wave = 0;
        g_dluma_cmm_music.active_cue[0] = 0;
    }
    if (!g_dluma_cmm_music.lease)
    {
        if (dluma_player_media_adapter_music_acquire(
                0U, &g_dluma_cmm_music.lease) !=
                DLUMA_MEDIA_RESULT_SUCCESS ||
            !g_dluma_cmm_music.lease)
            return 0;
    }
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = DLUMA_MEDIA_STRUCT_VERSION;
    request.initial_gain = 1.0f;
    request.route = DLUMA_MEDIA_ROUTE_MUSIC;
    request.flags = DLUMA_MEDIA_CUE_FLAG_LOOP;
    request.fade_in_ms = 1000U;
    request.fade_out_ms = 0U;
    if (dluma_player_media_adapter_play(
            g_dluma_cmm_music.banks[bank_index], cue_name,
            &request, &g_dluma_cmm_music.wave) !=
            DLUMA_MEDIA_RESULT_SUCCESS ||
        !g_dluma_cmm_music.wave)
    {
        (void)dluma_cmm_music_release_override();
        dluma_custom_main_menu_music_retry_preparation();
        g_dluma_cmm_music.next_play_retry_ms = GetTickCount64() + 500ULL;
        if (!g_dluma_cmm_music.play_failure_logged) {
            dluma_log_pair("WARN", "custom_main_menu_music_override",
                         "play_failed_normal_music_resumed");
            g_dluma_cmm_music.play_failure_logged = 1U;
        }
        return 0;
    }
    g_dluma_cmm_music.next_play_retry_ms = 0;
    g_dluma_cmm_music.play_failure_logged = 0;
    dluma_str_copy(
        g_dluma_cmm_music.active_cue,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_music.active_cue), cue_name);
    dluma_log_pair("INFO", "custom_main_menu_music_override", cue_name);
    return 1;
}

int dluma_custom_main_menu_music_release_screen_override(void)
{
    return dluma_cmm_music_release_override();
}

int dluma_custom_main_menu_music_shutdown(void)
{
    return dluma_cmm_music_release_override();
}

int dluma_custom_main_menu_music_retire_generation(void)
{
    int released = dluma_cmm_music_release_override();
    memset(&g_dluma_cmm_music, 0, sizeof(g_dluma_cmm_music));
    dluma_str_copy(
        g_dluma_cmm_music.normal_cue,
        DLUMA_ARRAY_COUNT(g_dluma_cmm_music.normal_cue), "dw_menu");
    dluma_log_pair(
        released ? "INFO" : "WARN",
        "custom_main_menu_music_generation",
        released
            ? "prepared_bank_tokens_retired"
            : "prepared_bank_tokens_retired_after_release_failure");
    return released;
}

static int dluma_cmm_music_resolve_cue_state(
    DlumaCustomMainMenuMusicTarget target,
    void *main_menu_module,
    const char **cue_name_out)
{
    const DlumaCustomMainMenuManifest *manifest =
        dluma_custom_main_menu_selected_manifest();
    const DlumaCustomMainMenuMusicOverride *music_override =
        dluma_cmm_music_override(manifest, target);
    unsigned int bank_index = 0;
    int prepare_state;
    if (cue_name_out)
        *cue_name_out = 0;
    if (!cue_name_out || !music_override || !music_override->name[0])
        return DLUMA_CMM_MUSIC_RESOLVE_PERMANENT;
    prepare_state = dluma_cmm_music_prepare_state(main_menu_module);
    if (prepare_state != DLUMA_CMM_MUSIC_RESOLVE_READY)
        return prepare_state;
    if (!dluma_cmm_music_cue_bank(music_override->name, &bank_index))
        return DLUMA_CMM_MUSIC_RESOLVE_PERMANENT;
    *cue_name_out = music_override->name;
    return DLUMA_CMM_MUSIC_RESOLVE_READY;
}

static int dluma_cmm_music_resolve_cue(
    DlumaCustomMainMenuMusicTarget target,
    void *main_menu_module,
    const char **cue_name_out)
{
    return dluma_cmm_music_resolve_cue_state(
               target, main_menu_module, cue_name_out) ==
        DLUMA_CMM_MUSIC_RESOLVE_READY;
}

int dluma_custom_main_menu_music_resolve_main_cue(
    void *main_menu_module,
    const char **cue_name_out)
{
    return dluma_cmm_music_resolve_cue(
        DLUMA_CUSTOM_MENU_MUSIC_MAIN, main_menu_module, cue_name_out);
}

int dluma_custom_main_menu_music_resolve_main_cue_state(
    void *main_menu_module,
    const char **cue_name_out)
{
    return dluma_cmm_music_resolve_cue_state(
        DLUMA_CUSTOM_MENU_MUSIC_MAIN, main_menu_module, cue_name_out);
}

int dluma_custom_main_menu_music_resolve_credits_cue(
    void *main_menu_module,
    const char **cue_name_out)
{
    return dluma_cmm_music_resolve_cue(
        DLUMA_CUSTOM_MENU_MUSIC_CREDITS,
        main_menu_module, cue_name_out);
}
