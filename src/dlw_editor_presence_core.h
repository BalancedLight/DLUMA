#ifndef DLUMA_EDITOR_PRESENCE_CORE_H
#define DLUMA_EDITOR_PRESENCE_CORE_H

#include "dlw_presence_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_EDITOR_PRESENCE_TITLE_CAPACITY 2048U

typedef enum DlumaEditorDocumentKind {
    DLUMA_EDITOR_DOCUMENT_NONE = 0,
    DLUMA_EDITOR_DOCUMENT_MAP = 1,
    DLUMA_EDITOR_DOCUMENT_QUEST = 2,
    DLUMA_EDITOR_DOCUMENT_SCRIPT = 3,
    DLUMA_EDITOR_DOCUMENT_GENERIC = 4
} DlumaEditorDocumentKind;

typedef struct DlumaEditorPresenceObservation {
    DlumaPresenceSnapshot snapshot;
    unsigned long long document_identity;
    unsigned int document_kind;
    unsigned int has_document;
} DlumaEditorPresenceObservation;


typedef enum DlumaEditorOptionsAction {
    DLUMA_EDITOR_OPTIONS_REFUSE = 0,
    DLUMA_EDITOR_OPTIONS_FORWARD_STOCK = 1,
    DLUMA_EDITOR_OPTIONS_SHOW_CUSTOM = 2,
    DLUMA_EDITOR_OPTIONS_PERSIST_TOGGLE = 3,
    DLUMA_EDITOR_OPTIONS_PERSIST_RESET = 4,
    DLUMA_EDITOR_OPTIONS_TEARDOWN = 5
} DlumaEditorOptionsAction;

typedef struct DlumaEditorOptionsModel {
    unsigned int admitted;
    unsigned int inserted;
    unsigned int enabled;
    int selected_index;
} DlumaEditorOptionsModel;

typedef struct DlumaEditorPlaytestOwnershipEvidence {
    unsigned int pid_match;
    unsigned int start_time_match;
    unsigned int executable_match;
    unsigned int loader_match;
    unsigned int handoff_match;
    unsigned int unexpired;
} DlumaEditorPlaytestOwnershipEvidence;

typedef struct DlumaEditorPlaytestOwnershipState {
    unsigned long long generation;
    unsigned int active;
} DlumaEditorPlaytestOwnershipState;

int dluma_editor_options_model_admit(
    DlumaEditorOptionsModel* model,
    int exact_build,
    int exact_dialog,
    int exact_notifications,
    unsigned int stock_tab_count,
    unsigned int stock_page_count,
    int already_inserted,
    int enabled);
DlumaEditorOptionsAction dluma_editor_options_model_select(
    DlumaEditorOptionsModel* model,
    unsigned int total_tab_count,
    int selected_index);
DlumaEditorOptionsAction dluma_editor_options_model_toggle(
    DlumaEditorOptionsModel* model,
    int enabled);
DlumaEditorOptionsAction dluma_editor_options_model_reset(
    DlumaEditorOptionsModel* model);
DlumaEditorOptionsAction dluma_editor_options_model_teardown(
    DlumaEditorOptionsModel* model);

int dluma_editor_playtest_ownership_begin(
    DlumaEditorPlaytestOwnershipState* state,
    const DlumaEditorPlaytestOwnershipEvidence* evidence,
    unsigned long long generation);
int dluma_editor_playtest_ownership_finish(
    DlumaEditorPlaytestOwnershipState* state,
    unsigned long long generation);
void dluma_editor_presence_build_browsing(
    DlumaEditorPresenceObservation* output);
int dluma_editor_presence_parse_title_utf8(
    const char* title,
    unsigned int title_capacity,
    DlumaEditorPresenceObservation* output);
int dluma_editor_presence_observation_equal(
    const DlumaEditorPresenceObservation* left,
    const DlumaEditorPresenceObservation* right);

#ifdef __cplusplus
}
#endif

#endif
