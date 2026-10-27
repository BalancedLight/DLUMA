#include "dlw_steam_presence_core.h"

#include <string.h>

static void dluma_steam_presence_copy_status(
    char output[DLUMA_STEAM_STATUS_CAPACITY], const char* input)
{
    unsigned int i = 0;
    if (!output) return;
    if (input) {
        while (i + 1U < DLUMA_STEAM_STATUS_CAPACITY && input[i]) {
            output[i] = input[i];
            ++i;
        }
    }
    output[i] = 0;
}

static int dluma_steam_presence_status_equal(
    const char* left, const char* right)
{
    unsigned int i;
    if (!left || !right) return left == right;
    for (i = 0; i < DLUMA_STEAM_STATUS_CAPACITY; ++i) {
        if (left[i] != right[i]) return 0;
        if (!left[i]) return 1;
    }
    return 0;
}

DlumaSteamPresenceModuleAssessment dluma_steam_presence_assess_module(
    const DlumaSteamPresenceModuleEvidence* evidence)
{
    if (!evidence) return DLUMA_STEAM_MODULE_REJECTED;
    if (evidence->host_profile == DLUMA_STEAM_HOST_RETAIL_UNSUPPORTED ||
        evidence->host_profile == DLUMA_STEAM_HOST_UNSUPPORTED)
        return DLUMA_STEAM_MODULE_UNSUPPORTED;
    if (evidence->host_profile != DLUMA_STEAM_HOST_PLAYER)
        return DLUMA_STEAM_MODULE_REJECTED;
    if (!evidence->module_present)
        return DLUMA_STEAM_MODULE_RETRY;
    if (!evidence->canonical_colocation ||
        !evidence->hash_match ||
        !evidence->pe_identity_match ||
        !evidence->accessor_export_present ||
        !evidence->setter_export_present ||
        !evidence->export_bytes_match ||
        !evidence->export_pointer_ownership)
        return DLUMA_STEAM_MODULE_REJECTED;
    if (!evidence->accessor_succeeded)
        return DLUMA_STEAM_MODULE_RETRY;
    return DLUMA_STEAM_MODULE_READY;
}

void dluma_steam_presence_core_initialize(
    DlumaSteamPresenceCore* state, int enabled)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->enabled = enabled ? 1U : 0U;
    state->retry_delay_ms = DLUMA_STEAM_RETRY_INITIAL_MS;
}

void dluma_steam_presence_core_set_enabled(
    DlumaSteamPresenceCore* state, int enabled)
{
    if (!state) return;
    state->enabled = enabled ? 1U : 0U;
    state->next_retry_ms = 0;
    state->retry_delay_ms = DLUMA_STEAM_RETRY_INITIAL_MS;
}

void dluma_steam_presence_core_observe(
    DlumaSteamPresenceCore* state,
    const DlumaPresenceSnapshot* snapshot)
{
    char formatted[DLUMA_STEAM_STATUS_CAPACITY];
    if (!state) return;
    formatted[0] = 0;
    if (!snapshot || !dluma_presence_format_steam_status(
            snapshot, formatted, sizeof(formatted))) {
        state->observed_valid = 0;
        state->observed_status[0] = 0;
        return;
    }
    if (!state->observed_valid ||
        !dluma_steam_presence_status_equal(
            state->observed_status, formatted)) {
        dluma_steam_presence_copy_status(
            state->observed_status, formatted);
        state->observed_valid = 1;
    }
}

DlumaSteamPresenceAction dluma_steam_presence_core_next_action(
    const DlumaSteamPresenceCore* state,
    unsigned long long now_ms)
{
    DlumaSteamPresenceAction action;
    memset(&action, 0, sizeof(action));
    if (!state || (state->next_retry_ms && now_ms < state->next_retry_ms))
        return action;
    if (!state->enabled || !state->observed_valid) {
        if (state->owns_status)
            action.kind = DLUMA_STEAM_ACTION_REMOVE_STATUS;
        return action;
    }
    if (state->published_valid &&
        dluma_steam_presence_status_equal(
            state->published_status, state->observed_status))
        return action;
    action.kind = DLUMA_STEAM_ACTION_SET_STATUS;
    dluma_steam_presence_copy_status(
        action.value, state->observed_status);
    return action;
}

void dluma_steam_presence_core_complete(
    DlumaSteamPresenceCore* state,
    const DlumaSteamPresenceAction* action,
    int succeeded,
    unsigned long long now_ms)
{
    unsigned long long next_delay;
    if (!state || !action || action->kind == DLUMA_STEAM_ACTION_NONE)
        return;
    if (succeeded) {
        if (action->kind == DLUMA_STEAM_ACTION_SET_STATUS) {
            dluma_steam_presence_copy_status(
                state->published_status, action->value);
            state->published_valid = 1;
            state->owns_status = 1;
        } else if (action->kind == DLUMA_STEAM_ACTION_REMOVE_STATUS) {
            state->published_status[0] = 0;
            state->published_valid = 0;
            state->owns_status = 0;
        }
        state->next_retry_ms = 0;
        state->retry_delay_ms = DLUMA_STEAM_RETRY_INITIAL_MS;
        return;
    }
    if (!state->retry_delay_ms)
        state->retry_delay_ms = DLUMA_STEAM_RETRY_INITIAL_MS;
    state->next_retry_ms = now_ms + state->retry_delay_ms;
    next_delay = state->retry_delay_ms * 2ULL;
    if (next_delay < state->retry_delay_ms ||
        next_delay > DLUMA_STEAM_RETRY_MAX_MS)
        next_delay = DLUMA_STEAM_RETRY_MAX_MS;
    state->retry_delay_ms = next_delay;
}
