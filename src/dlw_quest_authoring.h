#ifndef DLUMA_QUEST_AUTHORING_H
#define DLUMA_QUEST_AUTHORING_H

#include "dlw_common.h"

typedef enum DlumaQuestAuthoringPrepareResult {
    DLUMA_QUEST_AUTHORING_UNAVAILABLE = -1,
    DLUMA_QUEST_AUTHORING_REJECT = 0,
    DLUMA_QUEST_AUTHORING_STOCK = 1,
    DLUMA_QUEST_AUTHORING_CUSTOM = 2
} DlumaQuestAuthoringPrepareResult;

typedef struct DlumaQuestAuthoringTransaction DlumaQuestAuthoringTransaction;

typedef struct DlumaQuestAuthoringOutput {
    unsigned int verified;
    unsigned int fresh;
    unsigned int custom_sidecar_published;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    FILETIME last_write;
} DlumaQuestAuthoringOutput;

#ifdef __cplusplus
extern "C" {
#endif

DlumaQuestAuthoringPrepareResult dluma_quest_authoring_prepare(
    const char* source_path,
    const char* output_path,
    DlumaQuestAuthoringTransaction** transaction_out);
DlumaQuestAuthoringPrepareResult dluma_quest_authoring_prepare_for_project(
    const char* source_path,
    const char* output_path,
    const WCHAR* project_root,
    DlumaQuestAuthoringTransaction** transaction_out);
int dluma_quest_authoring_finish(
    DlumaQuestAuthoringTransaction* transaction);
void dluma_quest_authoring_release(
    DlumaQuestAuthoringTransaction* transaction);
const char* dluma_quest_authoring_diagnostic(
    const DlumaQuestAuthoringTransaction* transaction);
const char* dluma_quest_authoring_compiler_source(
    const DlumaQuestAuthoringTransaction* transaction);
const char* dluma_quest_authoring_compiler_source_sha256(
    const DlumaQuestAuthoringTransaction* transaction);
int dluma_quest_authoring_source_matches(
    const DlumaQuestAuthoringTransaction* transaction,
    const WCHAR* project_root, const WCHAR* source_path,
    const BY_HANDLE_FILE_INFORMATION* source_identity,
    const char* source_sha256);
void dluma_quest_authoring_output(
    const DlumaQuestAuthoringTransaction* transaction,
    DlumaQuestAuthoringOutput* output);

#ifdef __cplusplus
}
#endif

#endif
