#ifndef DLUMA_QUEST_EDITOR_H
#define DLUMA_QUEST_EDITOR_H

#include "dlw_common.h"
#include "core/dlw_quest_phase.h"

typedef struct DlumaQuestEditorCompileReceipt {
    unsigned int callback_count;
    unsigned int matched_callbacks;
    unsigned int nested_callbacks;
    unsigned int compiler_returned;
    unsigned int output_verified;
    unsigned int verification_failed;
    unsigned int custom_sidecar_published;
    char output_sha256[DLUMA_SHA256_HEX_CAPACITY];
    FILETIME output_last_write;
} DlumaQuestEditorCompileReceipt;

#ifdef __cplusplus
extern "C" {
#endif

int dluma_quest_editor_start(void);
int dluma_quest_editor_stop(void);
int dluma_quest_editor_callbacks_drained(void);
/* Internal synchronous owner-thread observation. Snapshot the saved source;
 * only this generation's matching, source-locked transaction can fill it. */
ULONGLONG dluma_quest_editor_compile_observe_begin(
    const WCHAR* project_root, const WCHAR* source_path,
    const char* source_sha256);
int dluma_quest_editor_compile_observe_end(
    ULONGLONG generation, DlumaQuestEditorCompileReceipt* receipt);
unsigned int dluma_quest_editor_alias_snapshot(
    DlumaQuestPhaseAlias* output,
    unsigned int capacity);
int dluma_quest_editor_handler_declared(
    const char* provider_mod,
    const char* handler_id,
    unsigned int runtime_kind);
int dluma_quest_editor_provider_diagnostic(
    const char* provider_mod,
    unsigned int runtime_kind,
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY],
    char artifact_sha256[DLUMA_SHA256_HEX_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif
