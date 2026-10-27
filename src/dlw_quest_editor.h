#ifndef DLUMA_QUEST_EDITOR_H
#define DLUMA_QUEST_EDITOR_H

#include "dlw_common.h"
#include "core/dlw_quest_phase.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_quest_editor_start(void);
int dluma_quest_editor_stop(void);
int dluma_quest_editor_callbacks_drained(void);
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
