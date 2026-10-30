#ifndef DLUMA_GAME_FRAME_AUTHORITY_H
#define DLUMA_GAME_FRAME_AUTHORITY_H

#include "dlw_win32.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_GAME_FRAME_AUTHORITY_THREAD_CAPACITY 16U
#define DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY 64U
#define DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY 128U
#define DLUMA_GAME_FRAME_AUTHORITY_CALLER_NAME_CAPACITY 64U
#define DLUMA_GAME_FRAME_AUTHORITY_MIN_STABLE_CALLBACKS 8U
#define DLUMA_GAME_FRAME_AUTHORITY_MAX_REPEATING_GAP_MS 1000ULL
#define DLUMA_GAME_FRAME_AUTHORITY_MAX_FRONTEND_AGE_MS 250ULL
#define DLUMA_GAME_FRAME_AUTHORITY_STARTUP_TIMEOUT_MS 10000ULL

typedef enum DlumaGameFrameAuthorityProof {
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_NONE = 0,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_SUPPORTED_BINARY = 1U << 0,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_HOOK_SLOT = 1U << 1,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_FRAME_SUBOBJECT = 1U << 2,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_PRIMARY_OBJECT = 1U << 3,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_CURRENT_GLOBAL_IDENTITY = 1U << 4,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_READABLE_GAME_INTERFACE = 1U << 5,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_BOUNDED_REPEATING_ROUTE = 1U << 6,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE = 1U << 7,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_CALLER_ROUTE = 1U << 8,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_ACTIVE_LEVEL_QUERY = 1U << 9,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_FRONTEND_IDENTITY = 1U << 10,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_SUBSEQUENT_ROUTE_CONFIRMATION = 1U << 11,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_VALIDATED_PLAYTEST_HANDOFF = 1U << 12,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_STANDALONE_START_INTENT = 1U << 13,
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_STANDALONE_SESSION_PLAYER = 1U << 14
} DlumaGameFrameAuthorityProof;

#define DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF                         \
    (DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_SUPPORTED_BINARY |          \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_HOOK_SLOT |                 \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_FRAME_SUBOBJECT |           \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_PRIMARY_OBJECT |            \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_READABLE_GAME_INTERFACE |         \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_CALLER_ROUTE)

#define DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF                       \
    (DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF |                          \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_BOUNDED_REPEATING_ROUTE |         \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE |                \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_ACTIVE_LEVEL_QUERY |              \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_FRONTEND_IDENTITY |               \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_SUBSEQUENT_ROUTE_CONFIRMATION)

#define DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF              \
    (DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF |                          \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE |                \
     DLUMA_GAME_FRAME_AUTHORITY_PROOF_VALIDATED_PLAYTEST_HANDOFF)

typedef enum DlumaGameFrameAuthorityConflictReason {
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE = 0,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_RECURSIVE_CALLBACK,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_DIAGNOSTIC_CAPACITY_EXHAUSTED,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_GAME_SUBOBJECT_CHANGED,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_PRIMARY_OBJECT_CHANGED,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CALLER_ROUTE_CHANGED,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_FRONTEND_IDENTITY_CHANGED,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_WINDOW_TIMER_OWNER_CHANGED,
    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_SELECTION_TIMEOUT
} DlumaGameFrameAuthorityConflictReason;

typedef enum DlumaGameFrameAuthorityResult {
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_UNSEEN = 0,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_DISCOVERY,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_PENDING,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_LATCHED,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_INVALID_ROUTE,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CONCURRENT,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_RECURSIVE,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_OBJECT_CHANGE,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CALLER_CHANGE,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_TIMEOUT,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_PENDING,
    DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_LATCHED
} DlumaGameFrameAuthorityResult;

typedef enum DlumaGameFrameAuthorityStatus {
    DLUMA_GAME_FRAME_AUTHORITY_STATUS_PENDING = 0,
    DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE,
    DLUMA_GAME_FRAME_AUTHORITY_STATUS_CONFLICT,
    DLUMA_GAME_FRAME_AUTHORITY_STATUS_TIMEOUT,
    DLUMA_GAME_FRAME_AUTHORITY_STATUS_OWNERSHIP_LOST
} DlumaGameFrameAuthorityStatus;

typedef enum DlumaGameFrameLevelQueryDiagnostic {
    DLUMA_GAME_FRAME_LEVEL_QUERY_NOT_ATTEMPTED = 0,
    DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS,
    DLUMA_GAME_FRAME_LEVEL_QUERY_NULL,
    DLUMA_GAME_FRAME_LEVEL_QUERY_INVALID
} DlumaGameFrameLevelQueryDiagnostic;

typedef struct DlumaGameFrameFrontendProbe {
    DlumaGameFrameLevelQueryDiagnostic result;
    ULONG_PTR level_pointer;
    ULONG_PTR level_vtable;
    ULONG_PTR gameplay_player;
    ULONGLONG qpc;
    ULONGLONG monotonic_ms;
    unsigned int frontend_identity_valid;
    unsigned int pending_identity_still_readable;
    char level_name[DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];
} DlumaGameFrameFrontendProbe;

/*
 * This is a per-Windows-thread distribution aggregate only. Thread IDs are
 * deliberately absent from the authority key: Player schedules one serialized
 * GameDI frame route over a worker pool.
 */
typedef struct DlumaGameFrameThreadAggregate {
    LONG thread_id;
    ULONGLONG callback_count;
    ULONGLONG first_sequence;
    ULONGLONG last_sequence;
    ULONGLONG dispatch_count;
    ULONGLONG migration_arrival_count;
    ULONGLONG pre_level_success_count;
    ULONGLONG pre_level_null_count;
    ULONGLONG pre_level_invalid_count;
    ULONGLONG post_level_success_count;
    ULONGLONG post_level_null_count;
    ULONGLONG post_level_invalid_count;
    ULONGLONG recursive_callback_count;
    ULONGLONG concurrent_callback_count;
    DlumaGameFrameAuthorityResult authority_result;
} DlumaGameFrameThreadAggregate;

typedef struct DlumaGameFrameCallbackTrace {
    ULONGLONG sequence;
    LONG thread_id;
    LONG previous_callback_thread_id;
    ULONG_PTR game_subobject;
    ULONG_PTR game_primary;
    ULONG_PTR gesture_manager;
    unsigned int delta_bits;
    ULONG_PTR caller_return_address;
    ULONG_PTR caller_module_base;
    ULONG_PTR caller_rva;
    ULONGLONG serialization_token;
    char caller_module_name[
        DLUMA_GAME_FRAME_AUTHORITY_CALLER_NAME_CAPACITY];
    ULONGLONG callback_entry_qpc;
    ULONGLONG original_entry_qpc;
    ULONGLONG original_return_qpc;
    ULONGLONG callback_return_qpc;
    ULONGLONG callback_duration_qpc;
    unsigned int concurrent_depth;
    unsigned int recursive_depth;
    ULONGLONG scheduler_migration_count;
    ULONG_PTR global_game_object;
    ULONG_PTR game_interface;
    unsigned int route_proof;
    DlumaGameFrameFrontendProbe pre_original_probe;
    DlumaGameFrameFrontendProbe post_original_probe;
    unsigned int original_callback_invoked;
    unsigned int original_callback_returned;
    unsigned int pump_ran;
    DlumaGameFrameAuthorityResult authority_result;
} DlumaGameFrameCallbackTrace;

typedef struct DlumaGameFrameAuthorityObservation {
    ULONGLONG sequence;
    ULONGLONG monotonic_ms;
    ULONGLONG callback_entry_qpc;
    LONG thread_id;
    ULONG_PTR game_subobject;
    ULONG_PTR game_primary;
    ULONG_PTR gesture_manager;
    unsigned int delta_bits;
    unsigned int recursive_depth;
    unsigned int concurrent_depth;
    ULONG_PTR global_game_object;
    ULONG_PTR game_interface;
    ULONG_PTR caller_return_address;
    ULONG_PTR caller_module_base;
    ULONG_PTR caller_rva;
    ULONGLONG serialization_token;
    char caller_module_name[
        DLUMA_GAME_FRAME_AUTHORITY_CALLER_NAME_CAPACITY];
    unsigned int route_proof;
    DlumaGameFrameFrontendProbe pre_original_probe;
} DlumaGameFrameAuthorityObservation;

typedef struct DlumaGameFrameAuthorityDecision {
    DlumaGameFrameAuthorityResult result;
    DlumaGameFrameAuthorityConflictReason conflict_reason;
    unsigned int run_loader_dispatch;
    unsigned int authority_selected;
    unsigned int dispatch_thread_migrated;
    unsigned int ownership_lost;
    ULONGLONG serialization_token;
} DlumaGameFrameAuthorityDecision;

typedef struct DlumaGameFrameAuthorityState {
    ULONG_PTR exact_route_game_subobject;
    ULONG_PTR exact_route_game_primary;
    ULONG_PTR exact_route_caller_module_base;
    ULONG_PTR exact_route_caller_rva;
    ULONG_PTR exact_route_hook_slot;
    ULONGLONG exact_route_serialization_token;
    volatile LONG exact_route_identity_seeded;

    ULONGLONG last_route_sequence;
    ULONGLONG last_route_ms;
    ULONGLONG maximum_repeating_gap_ms;
    volatile LONG current_dispatch_thread_id;
    LONG previous_callback_thread_id;
    LONG last_callback_thread_id;
    ULONGLONG scheduler_migration_count;
    ULONGLONG serialized_callback_count;
    ULONGLONG repeating_route_count;
    ULONGLONG game_frame_authority_sequence;
    volatile LONG game_frame_authority_proof;
    volatile LONG game_frame_authority_frozen;
    volatile LONG game_frame_authority_conflict;
    volatile LONG game_frame_authority_conflict_reason;

    volatile LONG game_frame_loader_dispatch_started;
    volatile LONG game_frame_frontend_identity_latched;
    volatile LONG game_frame_playtest_level_configured;
    volatile LONG game_frame_playtest_handoff_validated;
    volatile LONG game_frame_playtest_handoff_latched;
    volatile LONG game_frame_standalone_start_configured;
    volatile LONG game_frame_standalone_start_latched;
    ULONGLONG standalone_confirmed_callbacks;
    char expected_standalone_level_name[
        DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];
    volatile LONG game_frame_project_admitted;
    volatile LONG game_frame_registry_mutation_started;
    char expected_playtest_level_name[
        DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];

    DlumaGameFrameFrontendProbe pending_post_frontend;
    ULONGLONG pending_post_sequence;
    ULONG_PTR latched_frontend_pointer;
    ULONG_PTR latched_frontend_vtable;
    ULONGLONG latched_frontend_observed_ms;
    ULONGLONG frontend_identity_rebind_count;
    char latched_frontend_name[
        DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];

    volatile LONG game_frame_message_timer_thread_id;
    volatile LONG game_frame_window_timer_owner_conflict;
    ULONGLONG window_timer_probe_count;
    ULONGLONG window_timer_query_success_count;
    ULONGLONG window_timer_query_null_count;
    ULONGLONG window_timer_query_invalid_count;
    DlumaGameFrameFrontendProbe last_window_timer_probe;

    volatile LONG game_frame_authority_timed_out;
    volatile LONG game_frame_authority_window_armed;
    ULONGLONG game_frame_authority_initialized_ms;
    ULONGLONG game_frame_authority_timeout_ms;
    ULONGLONG game_frame_authority_started_ms;
    ULONGLONG game_frame_authority_deadline_ms;

    unsigned int thread_count;
    unsigned int thread_diagnostic_overflow;
    DlumaGameFrameThreadAggregate
        threads[DLUMA_GAME_FRAME_AUTHORITY_THREAD_CAPACITY];

    unsigned int trace_count;
    unsigned int trace_write_index;
    ULONGLONG trace_overwrite_count;
    DlumaGameFrameCallbackTrace
        trace[DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY];
} DlumaGameFrameAuthorityState;

void dluma_game_frame_authority_initialize(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG started_ms,
    ULONGLONG timeout_ms);

/* Publish the alternate startup proof used only by a child whose Editor
 * handoff, command line, project/map directory identity, and package hashes
 * have already been revalidated.  The first exact serialized GameDI route
 * may then own the game-thread pump without waiting minutes for the sparse
 * direct-map OnFrame cadence to repeat.  Standalone Player never calls this.
 * It must be configured before authority is selected or lost. */
int dluma_game_frame_authority_configure_validated_playtest_handoff(
    DlumaGameFrameAuthorityState* state,
    const char* expected_level_name);

/* Configure only after validating an explicit physical AutoStart request. */
int dluma_game_frame_authority_configure_standalone_start(
    DlumaGameFrameAuthorityState* state, const char* expected_level_name);

int dluma_game_frame_authority_identity_latched(
    const DlumaGameFrameAuthorityState* state);

DlumaGameFrameAuthorityDecision dluma_game_frame_authority_observe_pre(
    DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameAuthorityObservation* observation);

void dluma_game_frame_authority_note_post_original(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG sequence,
    LONG thread_id,
    ULONG_PTR game_subobject,
    ULONGLONG original_entry_qpc,
    ULONGLONG original_return_qpc,
    const DlumaGameFrameFrontendProbe* post_probe);

void dluma_game_frame_authority_note_callback_return(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG sequence,
    LONG thread_id,
    ULONGLONG callback_return_qpc);

void dluma_game_frame_authority_note_message_timer_probe(
    DlumaGameFrameAuthorityState* state,
    LONG thread_id,
    const DlumaGameFrameFrontendProbe* probe);

void dluma_game_frame_authority_expire_frontend_evidence(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG now_ms);

DlumaGameFrameAuthorityStatus dluma_game_frame_authority_status(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG now_ms);

void dluma_game_frame_authority_mark_loader_dispatch_started(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG serialization_token,
    LONG current_dispatch_thread_id);

void dluma_game_frame_authority_mark_project_admitted(
    DlumaGameFrameAuthorityState* state);

void dluma_game_frame_authority_mark_registry_mutation_started(
    DlumaGameFrameAuthorityState* state);

int dluma_game_frame_authority_can_finalize(
    const DlumaGameFrameAuthorityState* state,
    LONG current_thread_id,
    unsigned int inside_serialized_route,
    LONG validated_window_owner_thread_id);

const char* dluma_game_frame_authority_conflict_reason_name(
    DlumaGameFrameAuthorityConflictReason reason);
const char* dluma_game_frame_authority_result_name(
    DlumaGameFrameAuthorityResult result);
const char* dluma_game_frame_authority_status_name(
    DlumaGameFrameAuthorityStatus status);
const char* dluma_game_frame_level_query_diagnostic_name(
    DlumaGameFrameLevelQueryDiagnostic result);

#ifdef __cplusplus
}
#endif

#endif
