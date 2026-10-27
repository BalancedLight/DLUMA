#ifndef DLUMA_QUEST_PHASE_RUNTIME_H
#define DLUMA_QUEST_PHASE_RUNTIME_H

#include "dlw_dual_host_runtime.h"
#include "core/dlw_quest_phase.h"

typedef struct DlumaQuestPhaseBindingSnapshot {
    unsigned int runtime_kind;
    unsigned int completion;
    unsigned int source_line;
    double wait_seconds;
    char quest_id[128];
    char phase_id[128];
    char provider_mod[128];
    char handler_id[128];
    char contract_id[257];
    char binding_hash[DLUMA_SHA256_HEX_CAPACITY];
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
    unsigned int arguments_size;
} DlumaQuestPhaseBindingSnapshot;

typedef struct DlumaQuestPhaseBindingSet {
    unsigned int map_generation;
    unsigned int binding_count;
    unsigned int sidecar_count;
    DlumaQuestPhaseBindingSnapshot* bindings;
} DlumaQuestPhaseBindingSet;

#ifdef __cplusplus
extern "C" {
#endif

/* Build an immutable, deterministic inventory of loader-owned quest phase
 * sidecars below one already-authoritative project root.  Every file and its
 * adjacent compiled XML are opened without write/delete sharing and verified
 * by their handle-derived paths before a record is published. */
int dluma_quest_phase_inventory_project(
    const WCHAR* project_root,
    DlumaHandoffSidecarRecord* output,
    unsigned int capacity,
    unsigned int* count_out);
int dluma_quest_phase_sidecar_is_loader_owned(
    const WCHAR* sidecar_path,
    const WCHAR* project_root);

/* Load every binding from one authoritative active-project root.  The
 * returned array owns immutable copies and must be released with the paired
 * function. */
int dluma_quest_phase_load_project_bindings(
    const WCHAR* project_root,
    unsigned int map_generation,
    DlumaQuestPhaseBindingSet* output);
void dluma_quest_phase_release_bindings(
    DlumaQuestPhaseBindingSet* bindings);

#ifdef __cplusplus
}
#endif

#endif
