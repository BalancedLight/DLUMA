#ifndef DLUMA_EDITOR_EXIT_INTENT_CORE_H
#define DLUMA_EDITOR_EXIT_INTENT_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DlumaEditorExitIntentState {
    DLUMA_EDITOR_EXIT_INTENT_NONE = 0,
    DLUMA_EDITOR_EXIT_INTENT_TENTATIVE = 1,
    DLUMA_EDITOR_EXIT_INTENT_COMMITTED = 2
} DlumaEditorExitIntentState;

typedef enum DlumaEditorExitIntentReason {
    DLUMA_EDITOR_EXIT_REASON_NONE = 0,
    DLUMA_EDITOR_EXIT_REASON_WINDOW_CLOSE = 1,
    DLUMA_EDITOR_EXIT_REASON_SYSTEM_MENU_CLOSE = 2,
    DLUMA_EDITOR_EXIT_REASON_ALT_F4 = 3,
    DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END = 4,
    DLUMA_EDITOR_EXIT_REASON_WINDOW_DESTROYED = 5
} DlumaEditorExitIntentReason;

typedef struct DlumaEditorExitIntentSnapshot {
    unsigned int observer_active;
    unsigned int state;
    unsigned int reason;
    unsigned int reserved;
    unsigned long long generation;
    unsigned long long observed_ms;
} DlumaEditorExitIntentSnapshot;

void dluma_editor_exit_intent_initialize(
    DlumaEditorExitIntentSnapshot *snapshot,
    unsigned int observer_active);
unsigned long long dluma_editor_exit_intent_begin(
    DlumaEditorExitIntentSnapshot *snapshot,
    DlumaEditorExitIntentReason reason,
    unsigned long long now_ms);
void dluma_editor_exit_intent_resolve(
    DlumaEditorExitIntentSnapshot *snapshot,
    unsigned long long generation,
    int accepted);
void dluma_editor_exit_intent_commit(
    DlumaEditorExitIntentSnapshot *snapshot,
    DlumaEditorExitIntentReason reason,
    unsigned long long now_ms);
void dluma_editor_exit_intent_commit_if_tentative(
    DlumaEditorExitIntentSnapshot *snapshot);
int dluma_editor_exit_intent_should_suppress(
    const DlumaEditorExitIntentSnapshot *snapshot);
const char *dluma_editor_exit_intent_state_text(unsigned int state);
const char *dluma_editor_exit_intent_reason_text(unsigned int reason);

#ifdef __cplusplus
}
#endif

#endif
