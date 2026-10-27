#include "dlw_editor_exit_intent.h"

static unsigned int dluma_editor_exit_reason_priority(unsigned int reason)
{
    switch (reason) {
    case DLUMA_EDITOR_EXIT_REASON_WINDOW_DESTROYED: return 1U;
    case DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END: return 4U;
    case DLUMA_EDITOR_EXIT_REASON_ALT_F4: return 3U;
    case DLUMA_EDITOR_EXIT_REASON_SYSTEM_MENU_CLOSE: return 2U;
    case DLUMA_EDITOR_EXIT_REASON_WINDOW_CLOSE: return 1U;
    default: return 0U;
    }
}

void dluma_editor_exit_intent_initialize(
    DlumaEditorExitIntentSnapshot* snapshot,
    unsigned int observer_active)
{
    if (!snapshot) return;
    snapshot->observer_active = observer_active ? 1U : 0U;
    snapshot->state = DLUMA_EDITOR_EXIT_INTENT_NONE;
    snapshot->reason = DLUMA_EDITOR_EXIT_REASON_NONE;
    snapshot->reserved = 0U;
    snapshot->generation = 0ULL;
    snapshot->observed_ms = 0ULL;
}

unsigned long long dluma_editor_exit_intent_begin(
    DlumaEditorExitIntentSnapshot* snapshot,
    DlumaEditorExitIntentReason reason,
    unsigned long long now_ms)
{
    if (!snapshot || !snapshot->observer_active)
        return 0ULL;
    if (snapshot->state == DLUMA_EDITOR_EXIT_INTENT_COMMITTED)
        return snapshot->generation;
    if (snapshot->state == DLUMA_EDITOR_EXIT_INTENT_NONE) {
        ++snapshot->generation;
        if (!snapshot->generation) ++snapshot->generation;
        snapshot->state = DLUMA_EDITOR_EXIT_INTENT_TENTATIVE;
        snapshot->reason = (unsigned int)reason;
        snapshot->observed_ms = now_ms;
    } else if (dluma_editor_exit_reason_priority((unsigned int)reason) >
               dluma_editor_exit_reason_priority(snapshot->reason)) {
        snapshot->reason = (unsigned int)reason;
    }
    return snapshot->generation;
}

void dluma_editor_exit_intent_resolve(
    DlumaEditorExitIntentSnapshot* snapshot,
    unsigned long long generation,
    int accepted)
{
    if (!snapshot ||
        snapshot->state != DLUMA_EDITOR_EXIT_INTENT_TENTATIVE ||
        snapshot->generation != generation)
        return;
    if (accepted) {
        snapshot->state = DLUMA_EDITOR_EXIT_INTENT_COMMITTED;
        return;
    }
    snapshot->state = DLUMA_EDITOR_EXIT_INTENT_NONE;
    snapshot->reason = DLUMA_EDITOR_EXIT_REASON_NONE;
    snapshot->observed_ms = 0ULL;
}

void dluma_editor_exit_intent_commit(
    DlumaEditorExitIntentSnapshot* snapshot,
    DlumaEditorExitIntentReason reason,
    unsigned long long now_ms)
{
    unsigned long long generation;
    if (!snapshot || !snapshot->observer_active) return;
    generation = dluma_editor_exit_intent_begin(snapshot, reason, now_ms);
    if (generation)
        snapshot->state = DLUMA_EDITOR_EXIT_INTENT_COMMITTED;
}

void dluma_editor_exit_intent_commit_if_tentative(
    DlumaEditorExitIntentSnapshot* snapshot)
{
    if (snapshot &&
        snapshot->state == DLUMA_EDITOR_EXIT_INTENT_TENTATIVE)
        snapshot->state = DLUMA_EDITOR_EXIT_INTENT_COMMITTED;
}

int dluma_editor_exit_intent_should_suppress(
    const DlumaEditorExitIntentSnapshot* snapshot)
{
    return snapshot && snapshot->observer_active &&
        (snapshot->state == DLUMA_EDITOR_EXIT_INTENT_TENTATIVE ||
         snapshot->state == DLUMA_EDITOR_EXIT_INTENT_COMMITTED);
}

const char* dluma_editor_exit_intent_state_text(unsigned int state)
{
    switch (state) {
    case DLUMA_EDITOR_EXIT_INTENT_TENTATIVE: return "tentative";
    case DLUMA_EDITOR_EXIT_INTENT_COMMITTED: return "committed";
    default: return "none";
    }
}

const char* dluma_editor_exit_intent_reason_text(unsigned int reason)
{
    switch (reason) {
    case DLUMA_EDITOR_EXIT_REASON_WINDOW_CLOSE: return "window_close";
    case DLUMA_EDITOR_EXIT_REASON_WINDOW_DESTROYED: return "window_destroyed";
    case DLUMA_EDITOR_EXIT_REASON_SYSTEM_MENU_CLOSE:
        return "system_menu_close";
    case DLUMA_EDITOR_EXIT_REASON_ALT_F4: return "alt_f4";
    case DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END:
        return "system_session_end";
    default: return "none";
    }
}
