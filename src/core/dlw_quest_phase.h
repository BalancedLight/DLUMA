#ifndef DLUMA_QUEST_PHASE_H
#define DLUMA_QUEST_PHASE_H

#include <stddef.h>
#include <stdint.h>

#define DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES 8192U
#define DLUMA_QUEST_PHASE_MAX_JSON_DEPTH 8U
#define DLUMA_QUEST_PHASE_MAX_JSON_NODES 256U
#define DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES 1024U
#define DLUMA_QUEST_PHASE_MAX_BINDINGS 256U
#define DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES (1024U * 1024U)
#define DLUMA_QUEST_PHASE_QUEUE_CAPACITY 256U
#define DLUMA_QUEST_PHASE_MAX_PROVIDERS 128U
#define DLUMA_QUEST_PHASE_MAX_OBSERVERS 256U

typedef enum DlumaQuestPhaseRuntimeKind {
    DLUMA_QUEST_PHASE_RUNTIME_LUA = 1,
    DLUMA_QUEST_PHASE_RUNTIME_NATIVE = 2
} DlumaQuestPhaseRuntimeKind;

typedef enum DlumaQuestPhaseCompletionMode {
    DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET = 1,
    DLUMA_QUEST_PHASE_COMPLETION_STOCK_TIMED_WAIT = 2
} DlumaQuestPhaseCompletionMode;

typedef struct DlumaQuestPhaseDiagnostic {
    unsigned int byte_offset;
    char code[64];
    char message[192];
} DlumaQuestPhaseDiagnostic;

typedef struct DlumaQuestPhaseAlias {
    char command[64];
    char provider_mod[128];
    char handler_id[128];
    unsigned int runtime_kind;
    int load_priority;
} DlumaQuestPhaseAlias;

typedef struct DlumaQuestPhaseDirective {
    unsigned int struct_size;
    unsigned int source_line;
    unsigned int runtime_kind;
    unsigned int completion;
    double wait_seconds;
    char phase_id[128];
    char provider_mod[128];
    char handler_id[128];
    char contract_id[257];
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
    unsigned int arguments_size;
} DlumaQuestPhaseDirective;

int dluma_quest_phase_json_canonicalize_object(
    const void* bytes,
    unsigned int size,
    char* output,
    unsigned int output_capacity,
    unsigned int* output_size,
    DlumaQuestPhaseDiagnostic* diagnostic);

int dluma_quest_phase_parse_directive(
    const void* line_bytes,
    unsigned int line_size,
    unsigned int source_line,
    const DlumaQuestPhaseAlias* aliases,
    unsigned int alias_count,
    DlumaQuestPhaseDirective* output,
    DlumaQuestPhaseDiagnostic* diagnostic);

/* Parse the deliberately narrow decimal representation used by authored
 * stock-wait carriers.  Exponents, signs, NaN, and infinities are rejected;
 * accepted values are finite and in the inclusive 0..86400 range. */
int dluma_quest_phase_parse_wait_seconds(
    const void* bytes,
    unsigned int size,
    double* output);

int dluma_quest_phase_aliases_validate(
    const DlumaQuestPhaseAlias* aliases,
    unsigned int alias_count,
    const char* const* stock_commands,
    unsigned int stock_command_count,
    unsigned char* accepted,
    DlumaQuestPhaseDiagnostic* diagnostic);

int dluma_quest_phase_build_edf(
    const DlumaQuestPhaseAlias* aliases,
    const unsigned char* accepted,
    unsigned int alias_count,
    char* output,
    unsigned int output_capacity,
    unsigned int* output_size);

/* Lower one already-validated named authoring directive into the positional
 * macro call accepted by pxslcc.  The argument payload is intentionally not
 * copied into the compiler carrier: the immutable original source remains
 * authoritative for sidecar generation, while the EDF needs only the phase
 * identity and stock wait duration. */
int dluma_quest_phase_build_compiler_line(
    const char* command,
    const DlumaQuestPhaseDirective* directive,
    const char* wait_token,
    char* output,
    unsigned int output_capacity,
    unsigned int* output_size);

typedef struct DlumaQuestPhaseEvent {
    unsigned int struct_size;
    unsigned int map_generation;
    uint64_t sequence;
    unsigned int runtime_kind;
    unsigned int completion;
    double wait_seconds;
    char quest_id[128];
    char phase_id[128];
    char contract_id[257];
    char provider_mod[128];
    char handler_id[128];
    char binding_hash[65];
    const char* arguments_json;
    unsigned int arguments_size;
    unsigned int local_peer;
} DlumaQuestPhaseEvent;

typedef void (*DlumaQuestPhaseDispatchCallback)(
    void* user,
    const DlumaQuestPhaseEvent* event_value);

typedef struct DlumaQuestPhaseRegistration {
    uint64_t token;
    uint64_t epoch;
    uint64_t generation;
    unsigned int active;
    int load_priority;
    char owner_mod[128];
    char key[257];
    DlumaQuestPhaseDispatchCallback callback;
    void* user;
} DlumaQuestPhaseRegistration;

typedef struct DlumaQuestPhaseQueuedEvent {
    DlumaQuestPhaseEvent event_value;
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
} DlumaQuestPhaseQueuedEvent;

typedef struct DlumaQuestPhaseDispatchTarget {
    uint64_t token;
    int load_priority;
    char owner_mod[128];
    DlumaQuestPhaseDispatchCallback callback;
    void* user;
} DlumaQuestPhaseDispatchTarget;

typedef struct DlumaQuestPhaseDispatcher {
    uint64_t generation;
    uint64_t registration_epoch;
    uint64_t next_token;
    uint64_t next_sequence;
    uint64_t overflow_count;
    unsigned int stopping;
    unsigned int dispatch_depth;
    DlumaQuestPhaseRegistration providers[DLUMA_QUEST_PHASE_MAX_PROVIDERS];
    DlumaQuestPhaseRegistration observers[DLUMA_QUEST_PHASE_MAX_OBSERVERS];
    DlumaQuestPhaseQueuedEvent queue[DLUMA_QUEST_PHASE_QUEUE_CAPACITY];
    unsigned int queue_head;
    unsigned int queue_count;
    DlumaQuestPhaseDispatchTarget
        dispatch_targets[DLUMA_QUEST_PHASE_MAX_OBSERVERS + 1U];
} DlumaQuestPhaseDispatcher;

void dluma_quest_phase_dispatcher_initialize(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t generation);

int dluma_quest_phase_register_provider(
    DlumaQuestPhaseDispatcher* dispatcher,
    const char* owner_mod,
    const char* handler_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* out_token);

int dluma_quest_phase_register_observer(
    DlumaQuestPhaseDispatcher* dispatcher,
    const char* owner_mod,
    const char* contract_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* out_token);

int dluma_quest_phase_unregister(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t token);

int dluma_quest_phase_enqueue(
    DlumaQuestPhaseDispatcher* dispatcher,
    const DlumaQuestPhaseEvent* event_value);

unsigned int dluma_quest_phase_pump(
    DlumaQuestPhaseDispatcher* dispatcher,
    unsigned int maximum_events);

int dluma_quest_phase_dispatcher_quiesce(
    DlumaQuestPhaseDispatcher* dispatcher);

/* Discard queued events at a map boundary while retaining registrations.
 * Host wrappers still revalidate their owner generation before invocation,
 * so stale project/map registrations fail closed even if teardown could not
 * synchronously remove their token. */
int dluma_quest_phase_dispatcher_advance_generation(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t generation);

#endif
