#include "dlw_common.h"
#include "dlw_custom_main_menu.h"
#include "dlw_custom_main_menu_runtime.h"
#include "dlw_frontend_level.h"
#include "dlw_discord_presence.h"
#include "dlw_player_media_adapter.h"
#include "game/dlw_media_service.h"
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

static DlumaEngineState* g_dluma_engine_hook_state;
/*
 * Exact virtual ABI for the fingerprint-gated GameDI::OnFrame slot:
 *
 *   GameDI::OnFrame(GameDI*, GestureManager*, float)
 *
 * The original begins by forwarding all three arguments to
 * GameTcr::OnFrame.  The loader pump calls enough native/Lua code to clobber
 * the volatile RDX and XMM2 argument registers, so declaring this as a
 * one-argument function and calling it after the pump corrupts the forwarded
 * GestureManager/frame-delta values.  Keep the opaque return value as well so
 * the vtable call remains ABI-transparent to its caller.
 */
typedef ULONG_PTR (__fastcall *DlumaGameFrameFn)(
    void* game_subobject, void* gesture_manager, float delta_seconds);
typedef union DlumaGameFrameAddress {
    DlumaGameFrameFn function;
    void* address;
} DlumaGameFrameAddress;
static DlumaGameFrameFn g_dluma_game_frame_original;
static volatile LONG g_dluma_game_frame_first_tick_logged;
static volatile LONG g_dluma_game_frame_last_route_proof_logged = -1;
static volatile LONG g_dluma_game_frame_route_log_count;
static volatile LONG g_dluma_game_frame_result_seen_mask;
static volatile LONG g_dluma_game_frame_result_log_count;
static ULONG_PTR __fastcall dluma_game_frame_hook(
    void* game_subobject, void* gesture_manager, float delta_seconds);
static int dluma_get_pe_identity(
    HMODULE module, DWORD* timestamp, DWORD* image_size);

#define DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_DEVTOOLS_GAME_FRAME_VTABLE_RVA 0x0157E6D0UL
#define DLUMA_DEVTOOLS_GAME_FRAME_RVA 0x003C0EF0UL
#define DLUMA_DEVTOOLS_GAMEDI_PRIMARY_VTABLE_RVA 0x0157D5F8UL
#define DLUMA_DEVTOOLS_GAMEDI_FRAME_VTABLE_RVA 0x0157E2E8UL
#define DLUMA_DEVTOOLS_GAMEDI_FRAME_SUBOBJECT_OFFSET 0x0010U
#define DLUMA_DEVTOOLS_GAME_FRAME_SLOT_OFFSET                         \
    (DLUMA_DEVTOOLS_GAME_FRAME_VTABLE_RVA -                          \
     DLUMA_DEVTOOLS_GAMEDI_FRAME_VTABLE_RVA)
#define DLUMA_ENABLE_LEVEL_DESTRUCTOR_HOOK 0
#define DLUMA_GAME_FRAME_AUTHORITY_LOCK_ATTEMPTS 4096U
#define DLUMA_GAME_FRAME_AUTHORITY_TRANSITION_LOG_LIMIT 8U

typedef struct DlumaGameFrameCallbackGate {
    unsigned int recursive_depth;
    unsigned int another_callback_concurrently_active;
    unsigned int owns_active_thread;
} DlumaGameFrameCallbackGate;

static int dluma_engine_game_frame_authority_lock(
    DlumaEngineState* state)
{
    unsigned int attempt;
    if (!state) return 0;
    for (attempt = 0;
         attempt < DLUMA_GAME_FRAME_AUTHORITY_LOCK_ATTEMPTS;
         ++attempt) {
        if (dluma_atomic_compare_exchange(
                &state->game_frame_authority_lock, 1, 0) == 0)
            return 1;
    }
    return 0;
}

static void dluma_engine_game_frame_authority_unlock(
    DlumaEngineState* state)
{
    if (state)
        dluma_atomic_exchange(
            &state->game_frame_authority_lock, 0);
}

static void dluma_engine_fold_pending_authority_conflict_locked(
    DlumaEngineState* state)
{
    LONG pending;
    DlumaGameFrameAuthorityState* authority;
    if (!state) return;
    pending = dluma_atomic_exchange(
        &state->game_frame_authority_conflict_pending, 0);
    if (!pending) return;
    authority = &state->game_frame_authority;
    authority->game_frame_authority_conflict = 1;
    if (authority->game_frame_authority_conflict_reason ==
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE)
        authority->game_frame_authority_conflict_reason = pending;
}

static DlumaGameFrameCallbackGate
dluma_engine_enter_game_frame_callback(
    DlumaEngineState* state, LONG current_thread)
{
    DlumaGameFrameCallbackGate gate;
    LONG active_thread;
    memset(&gate, 0, sizeof(gate));
    if (!state || !current_thread) return gate;
    active_thread = dluma_atomic_compare_exchange(
        &state->game_frame_callback_active_thread_id,
        current_thread, 0);
    if (!active_thread) {
        gate.owns_active_thread = 1U;
        gate.recursive_depth = (unsigned int)dluma_atomic_increment(
            &state->game_frame_callback_active_depth);
    } else if (active_thread == current_thread) {
        gate.owns_active_thread = 1U;
        gate.recursive_depth = (unsigned int)dluma_atomic_increment(
            &state->game_frame_callback_active_depth);
    } else {
        gate.another_callback_concurrently_active = 1U;
        gate.recursive_depth = 1U;
    }
    return gate;
}

static void dluma_engine_leave_game_frame_callback(
    DlumaEngineState* state, LONG current_thread,
    const DlumaGameFrameCallbackGate* gate)
{
    LONG depth;
    if (!state || !gate || !gate->owns_active_thread)
        return;
    if (dluma_atomic_compare_exchange(
            &state->game_frame_callback_active_thread_id,
            0, 0) != current_thread)
        return;
    depth = dluma_atomic_decrement(
        &state->game_frame_callback_active_depth);
    if (depth == 0)
        (void)dluma_atomic_compare_exchange(
            &state->game_frame_callback_active_thread_id,
            0, current_thread);
}

static int dluma_engine_read_pointer(
    const void* address, void** value_out)
{
    if (!address || !value_out ||
        !dluma_is_readable(address, sizeof(*value_out)))
        return 0;
    memcpy(value_out, address, sizeof(*value_out));
    return 1;
}

static ULONGLONG dluma_engine_query_performance_counter(void)
{
    LARGE_INTEGER value;
    value.QuadPart = 0;
    return QueryPerformanceCounter(&value) && value.QuadPart > 0
        ? (ULONGLONG)value.QuadPart
        : 0;
}

static int dluma_engine_resolve_frame_caller(
    DlumaEngineState* state,
    void* caller_return,
    ULONG_PTR* module_base_out,
    ULONG_PTR* rva_out,
    char* module_name_out,
    unsigned int module_name_capacity)
{
    MEMORY_BASIC_INFORMATION memory;
    HMODULE module;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    ULONG_PTR caller;
    ULONG_PTR base;
    const char* name = "validated_pe_image";
    if (module_base_out) *module_base_out = 0;
    if (rva_out) *rva_out = 0;
    if (module_name_out && module_name_capacity)
        module_name_out[0] = 0;
    if (!state || !caller_return || !module_base_out || !rva_out ||
        !module_name_out || !module_name_capacity ||
        VirtualQuery(
            caller_return, &memory, sizeof(memory)) !=
                sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        !memory.AllocationBase)
        return 0;
    caller = (ULONG_PTR)caller_return;
    base = (ULONG_PTR)memory.AllocationBase;
    if (caller < base)
        return 0;
    module = (HMODULE)memory.AllocationBase;
    if (!dluma_get_pe_identity(
            module, &timestamp, &image_size) ||
        !image_size || caller - base >= image_size)
        return 0;
    if (module == state->game_frame_module)
        name = "gamedll_x64_rwdi.dll";
    else if (module == state->engine_module)
        name = "engine_x64_rwdi.dll";
    dluma_str_copy(
        module_name_out, module_name_capacity, name);
    *module_base_out = base;
    *rva_out = caller - base;
    return 1;
}

static void dluma_engine_collect_game_frame_observation(
    DlumaEngineState* state,
    void* game_subobject,
    void* gesture_manager,
    float delta_seconds,
    LONG current_thread,
    ULONGLONG sequence,
    void* caller_return,
    const DlumaGameFrameCallbackGate* gate,
    DlumaGameFrameAuthorityObservation* observation)
{
    void* subobject_vtable = 0;
    void* primary_vtable = 0;
    void* global_game_object = 0;
    void* game_interface = 0;
    BYTE* game_primary = 0;
    unsigned int proof = 0;

    if (!observation) return;
    memset(observation, 0, sizeof(*observation));
    observation->sequence = sequence;
    observation->monotonic_ms = GetTickCount64();
    observation->callback_entry_qpc =
        dluma_engine_query_performance_counter();
    observation->thread_id = current_thread;
    observation->game_subobject =
        (ULONG_PTR)game_subobject;
    observation->gesture_manager =
        (ULONG_PTR)gesture_manager;
    memcpy(
        &observation->delta_bits, &delta_seconds,
        sizeof(observation->delta_bits));
    if (gate) {
        observation->recursive_depth =
            gate->recursive_depth;
        observation->concurrent_depth =
            gate->another_callback_concurrently_active
                ? 2U : 1U;
    }
    if (!state || !game_subobject ||
        (ULONG_PTR)game_subobject <
            DLUMA_DEVTOOLS_GAMEDI_FRAME_SUBOBJECT_OFFSET)
        return;

    proof |=
        DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_SUPPORTED_BINARY;
    game_primary = (BYTE*)game_subobject -
        DLUMA_DEVTOOLS_GAMEDI_FRAME_SUBOBJECT_OFFSET;
    observation->game_primary = (ULONG_PTR)game_primary;
    if (state->game_frame_vtable_slot &&
        state->game_frame_expected_subobject_vtable &&
        (BYTE*)state->game_frame_vtable_slot ==
            (BYTE*)state->game_frame_expected_subobject_vtable +
                DLUMA_DEVTOOLS_GAME_FRAME_SLOT_OFFSET &&
        dluma_is_readable(
            state->game_frame_vtable_slot, sizeof(void*)) &&
        *state->game_frame_vtable_slot ==
            (void*)dluma_game_frame_hook)
        proof |= DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_HOOK_SLOT;
    if (dluma_engine_read_pointer(
            game_subobject, &subobject_vtable) &&
        subobject_vtable ==
            state->game_frame_expected_subobject_vtable)
        proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_FRAME_SUBOBJECT;
    if (dluma_engine_read_pointer(
            game_primary, &primary_vtable) &&
        primary_vtable ==
            state->game_frame_expected_primary_vtable)
        proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_PRIMARY_OBJECT;
    if (state->game_global_slot &&
        dluma_engine_read_pointer(
            state->game_global_slot, &global_game_object)) {
        observation->global_game_object =
            (ULONG_PTR)global_game_object;
        /*
         * This accessor belongs to engine_x64_rwdi's CGame/IGame owner,
         * whereas the hooked receiver is gamedll_x64_rwdi's GameDI frame
         * subobject. Equality is useful diagnostic evidence on a future
         * profile, but it is not a required identity proof for this Player.
         */
        if (global_game_object == game_primary)
            proof |=
                DLUMA_GAME_FRAME_AUTHORITY_PROOF_CURRENT_GLOBAL_IDENTITY;
    }
    if (global_game_object &&
        state->image_size >= sizeof(void*) &&
        state->game_interface_offset <=
            state->image_size - sizeof(void*) &&
        dluma_engine_read_pointer(
            (BYTE*)global_game_object +
                state->game_interface_offset,
            &game_interface) &&
        game_interface &&
        dluma_is_readable(game_interface, sizeof(void*))) {
        observation->game_interface =
            (ULONG_PTR)game_interface;
        proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_READABLE_GAME_INTERFACE;
    }
    if (observation->concurrent_depth <= 1U)
        proof |= DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE;
    observation->caller_return_address =
        (ULONG_PTR)caller_return;
    observation->serialization_token =
        (ULONGLONG)(ULONG_PTR)
            &state->game_frame_callback_active_thread_id;
    if (dluma_engine_resolve_frame_caller(
            state, caller_return,
            &observation->caller_module_base,
            &observation->caller_rva,
            observation->caller_module_name,
            (unsigned int)sizeof(
                observation->caller_module_name)))
        proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_CALLER_ROUTE;
    observation->route_proof = proof;
}

static void dluma_engine_log_game_frame_route_transition(
    const DlumaGameFrameAuthorityObservation* observation)
{
    LONG previous;
    LONG count;
    unsigned int static_required;
    unsigned int missing;
    if (!observation) return;
    previous = dluma_atomic_exchange(
        &g_dluma_game_frame_last_route_proof_logged,
        (LONG)observation->route_proof);
    if (previous == (LONG)observation->route_proof)
        return;
    count = dluma_atomic_increment(
        &g_dluma_game_frame_route_log_count);
    if (count <= 0 ||
        (unsigned int)count >
            DLUMA_GAME_FRAME_AUTHORITY_TRANSITION_LOG_LIMIT)
        return;
    static_required = DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF;
    missing = static_required &
        ~observation->route_proof;
    dluma_log_u64(
        "INFO", "game_frame_authority_route_thread_id",
        (ULONGLONG)(unsigned int)observation->thread_id);
    dluma_log_hex(
        "INFO", "game_frame_authority_route_proof",
        (DWORD)observation->route_proof);
    dluma_log_hex(
        missing ? "WARN" : "INFO",
        "game_frame_authority_missing_static_proof",
        (DWORD)missing);
}

static void dluma_engine_log_game_frame_result_transition(
    const DlumaGameFrameAuthorityDecision* decision,
    const DlumaGameFrameAuthorityObservation* observation)
{
    LONG observed_mask;
    LONG result_bit;
    LONG count;
    unsigned int result_index;
    if (!decision || !observation) return;
    result_index = (unsigned int)decision->result;
    if (!result_index || result_index >= 31U)
        return;
    result_bit = (LONG)(1U << result_index);
    for (;;) {
        observed_mask = dluma_atomic_compare_exchange(
            &g_dluma_game_frame_result_seen_mask, 0, 0);
        if (observed_mask & result_bit)
            return;
        if (dluma_atomic_compare_exchange(
                &g_dluma_game_frame_result_seen_mask,
                observed_mask | result_bit,
                observed_mask) == observed_mask)
            break;
    }
    count = dluma_atomic_increment(
        &g_dluma_game_frame_result_log_count);
    if (count <= 0 ||
        (unsigned int)count >
            DLUMA_GAME_FRAME_AUTHORITY_TRANSITION_LOG_LIMIT)
        return;
    dluma_log_pair(
        decision->run_loader_dispatch ? "INFO" : "WARN",
        "game_frame_authority_result",
        dluma_game_frame_authority_result_name(decision->result));
    dluma_log_u64(
        "INFO", "game_frame_authority_result_thread_id",
        (ULONGLONG)(unsigned int)observation->thread_id);
    dluma_log_u64(
        "INFO", "game_frame_authority_result_game_subobject",
        (ULONGLONG)observation->game_subobject);
    if (decision->conflict_reason !=
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE)
        dluma_log_pair(
            "WARN", "game_frame_authority_conflict_reason",
            dluma_game_frame_authority_conflict_reason_name(
                decision->conflict_reason));
}

void dluma_engine_note_startup_b0_onframe_skip(
    DlumaEngineState* state, DlumaStartupB0OnFrameSkipReason reason)
{
    if (!state || reason == DLUMA_STARTUP_B0_ONFRAME_SKIP_NONE)
        return;
    dluma_atomic_increment(&state->startup_b0_onframe_pump_skipped_count);
    dluma_atomic_exchange(
        &state->startup_b0_onframe_last_skip_reason, (LONG)reason);
}

const char* dluma_startup_b0_onframe_skip_reason_name(
    DlumaStartupB0OnFrameSkipReason reason)
{
    switch (reason) {
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_NONE:
        return "none";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_LOADER_NOT_STARTED:
        return "loader_not_started";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_SHUTDOWN_STOPPING:
        return "shutdown_stopping";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_THREAD_OWNERSHIP_VALIDATION_FAILED:
        return "thread_ownership_validation_failed";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_PENDING:
        return "authority_selection_pending";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_CONFLICT:
        return "authority_selection_conflict";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_TIMEOUT:
        return "authority_selection_timeout";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_OWNERSHIP_LOST:
        return "authority_ownership_lost";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_RECURSIVE_CALLBACK:
        return "recursive_callback";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_INVALID_CALLBACK_ROUTE:
        return "invalid_callback_route";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_CONTENT_ISOLATION_DISABLED:
        return "content_isolation_disabled";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_CONTENT_TRACKING_UNAVAILABLE:
        return "content_tracking_unavailable";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_STATE_BUSY:
        return "startup_b0_state_busy";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_COMPLETE_AUTHORIZED:
        return "complete_authorized";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_MISSED_BEFORE_FIRST_CUSTOM_EPOCH:
        return "missed_before_first_custom_epoch";
    case DLUMA_STARTUP_B0_ONFRAME_SKIP_PERMANENT_FAILURE:
        return "permanent_failure";
    default:
        return "unknown";
    }
}

DlumaEngineLevelQueryResult dluma_engine_readonly_frontend_probe(
    DlumaEngineState* state, DlumaGameFrameFrontendProbe* probe_out)
{
    void* game_object = 0;
    void* game_interface = 0;
    void* level_interface = 0;
    ULONG_PTR level_vtable = 0;
    const char* level_name = 0;
    char raw[DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];
    char normalized[DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];
    unsigned int index;
    int terminated = 0;
    DlumaEngineLevelQueryResult result =
        DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    if (!probe_out)
        return result;
    memset(probe_out, 0, sizeof(*probe_out));
    probe_out->result =
        DLUMA_GAME_FRAME_LEVEL_QUERY_INVALID;
    probe_out->qpc =
        dluma_engine_query_performance_counter();
    probe_out->monotonic_ms = GetTickCount64();
    if (!state || !state->ready ||
        !state->game_global_slot ||
        !state->get_active_level ||
        !state->get_level_name)
        return result;
    __try {
        if (!dluma_is_readable(
                state->game_global_slot, sizeof(void*)))
            return result;
        memcpy(
            &game_object, state->game_global_slot,
            sizeof(game_object));
        if (!game_object) {
            result = DLUMA_ENGINE_LEVEL_QUERY_NULL;
            probe_out->result =
                DLUMA_GAME_FRAME_LEVEL_QUERY_NULL;
            return result;
        }
        if (!dluma_is_readable(
                (BYTE*)game_object +
                    state->game_interface_offset,
                sizeof(void*)))
            return result;
        memcpy(
            &game_interface,
            (BYTE*)game_object +
                state->game_interface_offset,
            sizeof(game_interface));
        if (!game_interface) {
            result = DLUMA_ENGINE_LEVEL_QUERY_NULL;
            probe_out->result =
                DLUMA_GAME_FRAME_LEVEL_QUERY_NULL;
            return result;
        }
        if (!dluma_is_readable(
                game_interface, sizeof(void*)))
            return result;
        level_interface =
            state->get_active_level(game_interface);
        if (!level_interface) {
            result = DLUMA_ENGINE_LEVEL_QUERY_NULL;
            probe_out->result =
                DLUMA_GAME_FRAME_LEVEL_QUERY_NULL;
            return result;
        }
        if (!dluma_is_readable(
                level_interface, sizeof(level_vtable)))
            return result;
        memcpy(
            &level_vtable, level_interface,
            sizeof(level_vtable));
        if (!level_vtable ||
            !dluma_is_readable(
                (void*)level_vtable, sizeof(void*)))
            return result;
        level_name = state->get_level_name(level_interface);
        if (!level_name ||
            !dluma_is_readable(level_name, 1))
            return result;
        for (index = 0;
             index < (unsigned int)sizeof(raw);
             ++index) {
            if (!dluma_is_readable(
                    level_name + index, 1))
                return result;
            raw[index] = level_name[index];
            if (!raw[index]) {
                terminated = 1;
                break;
            }
        }
        if (!terminated || !raw[0])
            return result;
        dluma_normalize_map_name(
            raw, normalized,
            (unsigned int)sizeof(normalized));
        if (!normalized[0])
            return result;
        probe_out->level_pointer =
            (ULONG_PTR)level_interface;
        probe_out->level_vtable = level_vtable;
        dluma_str_copy(
            probe_out->level_name,
            (unsigned int)sizeof(probe_out->level_name),
            normalized);
        probe_out->frontend_identity_valid =
            dluma_frontend_level_name_is_frontend(normalized);
        probe_out->result =
            DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS;
        result = DLUMA_ENGINE_LEVEL_QUERY_SUCCESS;
    }
    __except (1) {
        memset(
            probe_out, 0, sizeof(*probe_out));
        probe_out->result =
            DLUMA_GAME_FRAME_LEVEL_QUERY_INVALID;
        probe_out->qpc =
            dluma_engine_query_performance_counter();
        probe_out->monotonic_ms = GetTickCount64();
        result = DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    }
    return result;
}

static void dluma_engine_validate_pending_frontend_freshness(
    DlumaEngineState* state,
    DlumaGameFrameFrontendProbe* current_probe)
{
    ULONG_PTR pointer = 0;
    ULONG_PTR expected_vtable = 0;
    ULONG_PTR observed_vtable = 0;
    if (!state || !current_probe ||
        !dluma_engine_game_frame_authority_lock(state))
        return;
    pointer =
        state->game_frame_authority.
            pending_post_frontend.level_pointer;
    expected_vtable =
        state->game_frame_authority.
            pending_post_frontend.level_vtable;
    dluma_engine_game_frame_authority_unlock(state);
    if (!pointer || !expected_vtable)
        return;
    __try {
        if (dluma_is_readable(
                (const void*)pointer,
                sizeof(observed_vtable))) {
            memcpy(
                &observed_vtable,
                (const void*)pointer,
                sizeof(observed_vtable));
            if (observed_vtable == expected_vtable)
                current_probe->
                    pending_identity_still_readable = 1U;
        }
    }
    __except (1) {
        current_probe->pending_identity_still_readable = 0U;
    }
}

int dluma_engine_copy_fresh_latched_frontend(
    DlumaEngineState* state,
    ULONGLONG now_ms,
    char* map_out,
    unsigned int map_capacity,
    void** level_out,
    ULONG_PTR* level_vtable_out)
{
    ULONG_PTR pointer = 0;
    ULONG_PTR expected_vtable = 0;
    ULONG_PTR observed_vtable = 0;
    ULONGLONG observed_ms = 0;
    char name[DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY];
    LONG current_thread;
    int copied = 0;
    if (map_out && map_capacity) map_out[0] = 0;
    if (level_out) *level_out = 0;
    if (level_vtable_out) *level_vtable_out = 0;
    if (!state || !map_out || !map_capacity ||
        !level_out || !level_vtable_out ||
        !dluma_engine_game_frame_dispatch_active_on_current_thread(
            state) ||
        !dluma_engine_game_frame_authority_lock(state))
        return 0;
    current_thread = (LONG)GetCurrentThreadId();
    if (state->game_frame_authority.
            game_frame_frontend_identity_latched &&
        !state->game_frame_authority.
             game_frame_authority_conflict &&
        state->game_frame_authority.
            current_dispatch_thread_id == current_thread) {
        pointer =
            state->game_frame_authority.latched_frontend_pointer;
        expected_vtable =
            state->game_frame_authority.latched_frontend_vtable;
        observed_ms =
            state->game_frame_authority.
                latched_frontend_observed_ms;
        dluma_str_copy(
            name, (unsigned int)sizeof(name),
            state->game_frame_authority.latched_frontend_name);
    } else {
        name[0] = 0;
    }
    if (pointer && expected_vtable &&
        (!observed_ms || now_ms < observed_ms ||
         now_ms - observed_ms >
             DLUMA_GAME_FRAME_AUTHORITY_MAX_FRONTEND_AGE_MS)) {
        state->game_frame_authority.latched_frontend_pointer = 0;
        state->game_frame_authority.latched_frontend_vtable = 0;
        pointer = 0;
        expected_vtable = 0;
    }
    dluma_engine_game_frame_authority_unlock(state);
    if (!pointer || !expected_vtable || !observed_ms ||
        now_ms < observed_ms ||
        now_ms - observed_ms >
            DLUMA_GAME_FRAME_AUTHORITY_MAX_FRONTEND_AGE_MS ||
        !name[0])
        return 0;
    __try {
        if (dluma_is_readable(
                (const void*)pointer,
                sizeof(observed_vtable))) {
            memcpy(
                &observed_vtable, (const void*)pointer,
                sizeof(observed_vtable));
            if (observed_vtable == expected_vtable &&
                strlen(name) + 1U <= map_capacity) {
                dluma_str_copy(map_out, map_capacity, name);
                *level_out = (void*)pointer;
                *level_vtable_out = expected_vtable;
                copied = 1;
            }
        }
    }
    __except (1) {
        copied = 0;
    }
    if (!copied) {
        map_out[0] = 0;
        *level_out = 0;
        *level_vtable_out = 0;
    }
    return copied;
}

static ULONG_PTR __fastcall dluma_game_frame_hook(
    void* game_subobject, void* gesture_manager, float delta_seconds)
{
    DlumaEngineState* state = &g_dluma.engine;
    DlumaGameFrameAuthorityObservation observation;
    DlumaGameFrameAuthorityDecision decision;
    DlumaGameFrameCallbackGate callback_gate;
    DlumaGameFrameFrontendProbe post_probe;
    DlumaGameFrameFn original;
    ULONG_PTR result = 0;
    LONG current_thread;
    LONG call_count;
    ULONGLONG callback_sequence;
    ULONGLONG original_entry_qpc = 0;
    ULONGLONG original_return_qpc = 0;
    ULONGLONG callback_return_qpc = 0;
    void* caller_return = _ReturnAddress();
    int dispatch_allowed = 0;
    int structurally_valid_serialized = 0;

    dluma_atomic_increment(&state->game_frame_active_calls);
    call_count =
        dluma_atomic_increment(&state->game_frame_hook_call_count);
    callback_sequence = call_count > 0
        ? (ULONGLONG)(unsigned int)call_count
        : (ULONGLONG)0;
    current_thread = (LONG)GetCurrentThreadId();
    callback_gate = dluma_engine_enter_game_frame_callback(
        state, current_thread);
    dluma_engine_collect_game_frame_observation(
        state, game_subobject, gesture_manager, delta_seconds,
        current_thread, callback_sequence, caller_return,
        &callback_gate,
        &observation);
    structurally_valid_serialized =
        (observation.route_proof &
         DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF) ==
            DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF &&
        observation.concurrent_depth <= 1U &&
        observation.recursive_depth <= 1U;
    if (structurally_valid_serialized &&
        !state->game_frame_stopping) {
        (void)dluma_engine_readonly_frontend_probe(
            state, &observation.pre_original_probe);
        dluma_engine_validate_pending_frontend_freshness(
            state, &observation.pre_original_probe);

    }
    dluma_engine_log_game_frame_route_transition(
        &observation);
    memset(&decision, 0, sizeof(decision));
    decision.result =
        DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT;
    if (callback_sequence &&
        dluma_engine_game_frame_authority_lock(state)) {
        dluma_engine_fold_pending_authority_conflict_locked(state);
        (void)dluma_game_frame_authority_status(
            &state->game_frame_authority,
            observation.monotonic_ms);
        decision = dluma_game_frame_authority_observe_pre(
            &state->game_frame_authority, &observation);
        dluma_engine_game_frame_authority_unlock(state);
    } else {
        (void)dluma_atomic_compare_exchange(
            &state->game_frame_authority_conflict_pending,
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK,
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE);
    }
    dluma_engine_log_game_frame_result_transition(
        &decision, &observation);

    /* The exact serialized GameDI::OnFrame route is outside the nested
     * MainMenuModuleDI background-load stack, but still runs before the
     * original frame can begin tearing down UI/level objects. Prepare the
     * selected menu's native custom-content sources and transformed overlay
     * here. The runtime call is idempotent and treats a not-yet-published content
     * helper as retryable on a later frame. */
    /* Run the loader pump while the frame's engine/UI objects are known to be
     * alive.  Calling it after the original OnFrame is unsafe on the final
     * frontend frame because the original call may begin tearing down virtual
     * UI objects before returning.  That ordering was a plausible contributor
     * to the observed R6025 pure-virtual shutdown failure. */
    if (!decision.run_loader_dispatch) {
        DlumaStartupB0OnFrameSkipReason skip_reason =
            DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_PENDING;
        if (decision.ownership_lost &&
            decision.conflict_reason !=
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE) {
            dluma_custom_map_startup_b0_note_authority_failure(
                (unsigned int)decision.conflict_reason);
        }
        if (decision.result ==
                DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_RECURSIVE)
            skip_reason =
                DLUMA_STARTUP_B0_ONFRAME_SKIP_RECURSIVE_CALLBACK;
        else if (decision.result ==
                     DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_INVALID_ROUTE ||
                 decision.result ==
                     DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_OBJECT_CHANGE ||
                 decision.result ==
                     DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CALLER_CHANGE)
            skip_reason =
                DLUMA_STARTUP_B0_ONFRAME_SKIP_INVALID_CALLBACK_ROUTE;
        else if (decision.result ==
                     DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_TIMEOUT)
            skip_reason =
                DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_TIMEOUT;
        else if (decision.ownership_lost)
            skip_reason =
                DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_OWNERSHIP_LOST;
        else if (decision.result ==
                     DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CONCURRENT ||
                 decision.result ==
                     DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT)
            skip_reason =
                DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_CONFLICT;
        dluma_engine_note_startup_b0_onframe_skip(
            state, skip_reason);
    } else if (state->game_frame_stopping || g_dluma.stop_requested) {
        dluma_engine_note_startup_b0_onframe_skip(
            state, DLUMA_STARTUP_B0_ONFRAME_SKIP_SHUTDOWN_STOPPING);
    } else if (!g_dluma.started) {
        dluma_engine_note_startup_b0_onframe_skip(
            state, DLUMA_STARTUP_B0_ONFRAME_SKIP_LOADER_NOT_STARTED);
    } else if (dluma_engine_game_frame_authority_lock(state)) {
        dluma_engine_fold_pending_authority_conflict_locked(state);
        if (!state->game_frame_authority.
                 game_frame_authority_conflict &&
            state->game_frame_authority.
                current_dispatch_thread_id ==
                    current_thread &&
            state->game_frame_authority.
                exact_route_game_subobject ==
                    (ULONG_PTR)game_subobject) {
            dluma_game_frame_authority_mark_loader_dispatch_started(
                &state->game_frame_authority,
                decision.serialization_token,
                current_thread);
            dispatch_allowed = 1;
        }
        dluma_engine_game_frame_authority_unlock(state);
        if (dispatch_allowed) {
            if (dluma_atomic_compare_exchange(
                    &state->
                        game_frame_loader_dispatch_active_thread_id,
                    current_thread, 0) == 0) {
                dluma_media_service_tick(
                    game_subobject, delta_seconds);
                if (dluma_atomic_compare_exchange(
                        &g_dluma_game_frame_first_tick_logged,
                        1, 0) == 0)
                    dluma_log_line(
                        "INFO",
                        state->game_frame_authority.
                                game_frame_playtest_handoff_latched
                            ? "GameDI::OnFrame loader dispatch latched from a validated Editor playtest handoff on the first exact serialized route; worker migration is permitted."
                            : "GameDI::OnFrame loader dispatch latched from positive frontend evidence on one exact serialized route; worker migration is permitted.");
                dluma_localization_game_thread_tick();
                dluma_ui_game_thread_tick(
                    game_subobject, callback_sequence);
                dluma_discord_presence_game_thread_tick(
                    game_subobject, callback_sequence);
                (void)dluma_atomic_compare_exchange(
                    &state->
                        game_frame_loader_dispatch_active_thread_id,
                    0, current_thread);
            } else {
                dispatch_allowed = 0;
                (void)dluma_atomic_compare_exchange(
                    &state->
                        game_frame_authority_conflict_pending,
                    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK,
                    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE);
            }
        } else {
            dluma_engine_note_startup_b0_onframe_skip(
                state,
                DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_OWNERSHIP_LOST);
        }
    } else {
        dluma_engine_note_startup_b0_onframe_skip(
            state, DLUMA_STARTUP_B0_ONFRAME_SKIP_AUTHORITY_CONFLICT);
    }

    original = g_dluma_game_frame_original;
    memset(&post_probe, 0, sizeof(post_probe));
    post_probe.result =
        DLUMA_GAME_FRAME_LEVEL_QUERY_NOT_ATTEMPTED;
    if (original) {
        original_entry_qpc =
            dluma_engine_query_performance_counter();
        result = original(game_subobject, gesture_manager, delta_seconds);
        original_return_qpc =
            dluma_engine_query_performance_counter();
        if (structurally_valid_serialized &&
            !state->game_frame_stopping)
            (void)dluma_engine_readonly_frontend_probe(
                state, &post_probe);
    }
    /*
     * Techland's original frame work may change or release the shared cursor
     * clip. Reassert the loader-owned foreground-only client rectangle after
     * that work, without touching any engine/UI object that may have been
     * destroyed by the original callback.
     */
    dluma_mouse_lock_post_game_frame();
    if (callback_sequence &&
        dluma_engine_game_frame_authority_lock(state)) {
        dluma_game_frame_authority_note_post_original(
            &state->game_frame_authority,
            callback_sequence, current_thread,
            (ULONG_PTR)game_subobject,
            original_entry_qpc, original_return_qpc,
            &post_probe);
        dluma_engine_game_frame_authority_unlock(state);
    }
    callback_return_qpc =
        dluma_engine_query_performance_counter();
    if (callback_sequence &&
        dluma_engine_game_frame_authority_lock(state)) {
        dluma_game_frame_authority_note_callback_return(
            &state->game_frame_authority,
            callback_sequence, current_thread,
            callback_return_qpc);
        dluma_engine_game_frame_authority_unlock(state);
    }
    dluma_engine_leave_game_frame_callback(
        state, current_thread, &callback_gate);
    dluma_atomic_decrement(&state->game_frame_active_calls);
    return result;
}

static void __fastcall dluma_i_level_destructor_hook(void* level_interface)
{
    DlumaEngineState* state = g_dluma_engine_hook_state;
    if (state) {
        if (level_interface == (void*)state->tracked_level_interface)
            dluma_atomic_exchange(&state->tracked_level_destroyed, 1);
        /* The verified ILevel base destructor only installs this vtable. */
        *(void**)level_interface = state->level_destructor_vtable;
    }
}

static DWORD dluma_read_u32(const void* p)
{
    const BYTE* b = (const BYTE*)p;
    return (DWORD)b[0] | ((DWORD)b[1] << 8) | ((DWORD)b[2] << 16) | ((DWORD)b[3] << 24);
}

static int dluma_get_pe_identity(HMODULE module, DWORD* timestamp, DWORD* image_size)
{
    BYTE* base = (BYTE*)module;
    DWORD nt;
    if (!base || !dluma_is_readable(base, 0x100)) return 0;
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    nt = dluma_read_u32(base + 0x3C);
    if (nt > 0x100000 || !dluma_is_readable(base + nt, 0x58)) return 0;
    if (base[nt] != 'P' || base[nt + 1] != 'E' || base[nt + 2] != 0 || base[nt + 3] != 0) return 0;
    if (timestamp) *timestamp = dluma_read_u32(base + nt + 8);
    if (image_size) *image_size = dluma_read_u32(base + nt + 0x50);
    return 1;
}

static int dluma_scan_create_game_accessor(DlumaEngineState* state)
{
    BYTE* function = (BYTE*)state->create_game;
    BYTE* base = (BYTE*)state->engine_module;
    unsigned int i;
    if (!function || !base || !state->image_size || !dluma_is_readable(function, 0x180)) return 0;

    /*
     * Verified form in the current DevTools build near CreateGame's return:
     *   mov rax, qword ptr [rip + game_global]
     *   mov rax, qword ptr [rax + game_interface_offset]
     *
     * We derive both values from code instead of shipping a build-specific RVA.
     */
    for (i = 0; i + 14 <= 0x180; ++i) {
        if (function[i + 0] == 0x48 && function[i + 1] == 0x8B && function[i + 2] == 0x05 &&
            function[i + 7] == 0x48 && function[i + 8] == 0x8B && function[i + 9] == 0x80) {
            LONG rel = (LONG)dluma_read_u32(function + i + 3);
            DWORD interface_offset = dluma_read_u32(function + i + 10);
            BYTE* slot = function + i + 7 + rel;
            if (slot >= base && slot + sizeof(void*) <= base + state->image_size &&
                interface_offset >= 0x40 && interface_offset <= 0x800 &&
                dluma_is_readable(slot, sizeof(void*))) {
                state->game_global_slot = (void**)slot;
                state->game_interface_offset = interface_offset;
                return 1;
            }
        }
    }
    return 0;
}

int dluma_engine_prepare_game_locator(DlumaEngineState* state)
{
    if (!state) return 0;
    if (state->engine_module && state->create_game &&
        state->game_global_slot && state->image_size)
        return 1;
    state->engine_module = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!state->engine_module) return 0;
    state->create_game = GetProcAddress(state->engine_module, "CreateGame");
    if (!state->create_game ||
        !dluma_get_pe_identity(state->engine_module,
                             &state->image_timestamp,
                             &state->image_size))
        return 0;
    return dluma_scan_create_game_accessor(state);
}

static int dluma_install_level_destructor_hook(DlumaEngineState* state)
{
    static const BYTE tail[4] = { 0x48, 0x89, 0x01, 0xC3 };
    BYTE patch[12];
    BYTE* target;
    LONG rel;
    DWORD old_protect;
    DWORD restored_protect;
    void* hook_address;

    target = (BYTE*)GetProcAddress(state->engine_module, "??1ILevel@@MEAA@XZ");
    if (!target || !dluma_is_readable(target, sizeof(patch))) return 0;
    if (target[0] != 0x48 || target[1] != 0x8D || target[2] != 0x05 ||
        memcmp(target + 7, tail, sizeof(tail)) != 0 || target[11] != 0xCC)
        return 0;

    rel = (LONG)dluma_read_u32(target + 3);
    state->level_destructor = target;
    state->level_destructor_vtable = target + 7 + rel;
    memcpy(state->level_destructor_original, target, sizeof(state->level_destructor_original));

    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0x48;
    patch[1] = 0xB8;
    hook_address = (void*)dluma_i_level_destructor_hook;
    memcpy(patch + 2, &hook_address, sizeof(hook_address));
    patch[10] = 0xFF;
    patch[11] = 0xE0;

    if (!VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &old_protect)) return 0;
    /* Publish the emulation state before another engine thread can enter the
     * patched destructor. */
    g_dluma_engine_hook_state = state;
    memcpy(target, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    VirtualProtect(target, sizeof(patch), old_protect, &restored_protect);
    state->level_destructor_hooked = 1;
    return 1;
}

static int dluma_install_game_frame_hook(DlumaEngineState* state)
{
    static const BYTE frame_signature[16] = {
        0x48, 0x8B, 0xC4, 0x55, 0x57, 0x48, 0x8D, 0xA8,
        0x18, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD8
    };
    HMODULE gamedll;
    BYTE* base;
    BYTE* frame;
    void* primary_vtable;
    void* frame_vtable;
    void** slot;
    DWORD timestamp;
    DWORD image_size;
    DWORD old_protect;
    DWORD restored_protect;

    gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!gamedll ||
        !dluma_get_pe_identity(gamedll, &timestamp, &image_size) ||
        timestamp != DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP ||
        image_size != DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE)
        return 0;

    base = (BYTE*)gamedll;
    frame = base + DLUMA_DEVTOOLS_GAME_FRAME_RVA;
    primary_vtable =
        base + DLUMA_DEVTOOLS_GAMEDI_PRIMARY_VTABLE_RVA;
    frame_vtable =
        base + DLUMA_DEVTOOLS_GAMEDI_FRAME_VTABLE_RVA;
    slot = (void**)(base + DLUMA_DEVTOOLS_GAME_FRAME_VTABLE_RVA);
    if (!dluma_is_readable(frame, sizeof(frame_signature)) ||
        memcmp(frame, frame_signature, sizeof(frame_signature)) != 0 ||
        !dluma_is_readable(primary_vtable, sizeof(void*)) ||
        !dluma_is_readable(
            frame_vtable,
            DLUMA_DEVTOOLS_GAME_FRAME_SLOT_OFFSET + sizeof(void*)) ||
        (BYTE*)slot !=
            (BYTE*)frame_vtable +
                DLUMA_DEVTOOLS_GAME_FRAME_SLOT_OFFSET ||
        !dluma_is_readable(slot, sizeof(*slot)) || *slot != frame)
        return 0;

    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect))
        return 0;
    state->game_frame_module = gamedll;
    state->game_frame_expected_primary_vtable =
        primary_vtable;
    state->game_frame_expected_subobject_vtable =
        frame_vtable;
    state->game_frame_vtable_slot = slot;
    state->game_frame_original = *slot;
    state->game_frame_active_calls = 0;
    state->game_frame_stopping = 0;
    state->game_frame_authority_lock = 0;
    state->game_frame_authority_conflict_pending = 0;
    state->game_frame_callback_active_thread_id = 0;
    state->game_frame_callback_active_depth = 0;
    state->game_frame_loader_dispatch_active_thread_id = 0;
    g_dluma_game_frame_last_route_proof_logged = -1;
    g_dluma_game_frame_route_log_count = 0;
    g_dluma_game_frame_result_seen_mask = 0;
    g_dluma_game_frame_result_log_count = 0;
    dluma_game_frame_authority_initialize(
        &state->game_frame_authority,
        GetTickCount64(),
        DLUMA_GAME_FRAME_AUTHORITY_STARTUP_TIMEOUT_MS);
    state->game_frame_authority.exact_route_hook_slot =
        (ULONG_PTR)slot;
    state->game_frame_hook_call_count = 0;
    state->startup_b0_onframe_pump_call_count = 0;
    state->startup_b0_onframe_pump_skipped_count = 0;
    state->startup_b0_onframe_last_skip_reason =
        DLUMA_STARTUP_B0_ONFRAME_SKIP_NONE;
    state->startup_b0_active_level_query_call_count = 0;
    state->startup_b0_active_level_query_success_count = 0;
    state->startup_b0_active_level_query_null_count = 0;
    state->startup_b0_active_level_query_invalid_count = 0;
    g_dluma_game_frame_original = (DlumaGameFrameFn)*slot;
    *slot = (void*)dluma_game_frame_hook;
    FlushInstructionCache(
        GetCurrentProcess(), slot, sizeof(*slot));
    if (!VirtualProtect(
            slot, sizeof(*slot), old_protect,
            &restored_protect)) {
        *slot = state->game_frame_original;
        FlushInstructionCache(
            GetCurrentProcess(), slot, sizeof(*slot));
        (void)VirtualProtect(
            slot, sizeof(*slot), old_protect,
            &restored_protect);
        g_dluma_game_frame_original = 0;
        state->game_frame_vtable_slot = 0;
        state->game_frame_original = 0;
        return 0;
    }
    state->game_frame_hooked = 1;
    return 1;
}


int dluma_engine_bind(DlumaEngineState* state)
{
    FARPROC active;
    FARPROC level_name;
    FARPROC get_ui_screen_id;
    FARPROC register_ui_screen;
    FARPROC load_ui_screen;
    FARPROC show_ui_screen;
    FARPROC hide_ui_screen;
    FARPROC is_ui_screen_visible;
    FARPROC is_ui_screen_loaded;
    FARPROC is_ui_screen_registered;
    FARPROC find_ui_screen;
    FARPROC find_ui_element;
    FARPROC get_owner_screen;
    FARPROC get_screen_id;
    FARPROC get_screen_script_name;
    FARPROC get_game_window;
    if (!state) return 0;
    if (state->ready) return 1;
    memset(state, 0, sizeof(*state));

    if (!dluma_engine_prepare_game_locator(state)) return 0;
    active = GetProcAddress(state->engine_module, "?GetActiveLevel@IGame@@QEAAPEAVILevel@@XZ");
    level_name = GetProcAddress(state->engine_module, "?GetLevelName@ILevel@@QEAAPEBDXZ");
    if (!state->create_game || !active || !level_name) {
        dluma_log_line("ERROR", "Engine exports required by the prototype were not found.");
        return 0;
    }
    state->get_active_level = (void* (__fastcall *)(void*))active;
    state->get_level_name = (const char* (__fastcall *)(void*))level_name;
    get_game_window = GetProcAddress(state->engine_module,
        "?GethWnd@IGame@@QEBAPEAUHWND__@@XZ");
    state->get_game_window = (HWND (__fastcall *)(void*))get_game_window;
    get_ui_screen_id = GetProcAddress(state->engine_module,
        "?GetUIScreenIdByScriptName@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z");
    register_ui_screen = GetProcAddress(state->engine_module,
        "?RegisterUIScreen@ILevel@@QEAAHAEBV?$string_base@D@ttl@@HH_N0H@Z");
    load_ui_screen = GetProcAddress(state->engine_module,
        "?LoadUIScreen@ILevel@@QEAAPEAVIUIScreen@@H@Z");
    show_ui_screen = GetProcAddress(state->engine_module,
        "?ShowUIScreen@ILevel@@QEAAXHPEAVIUIData@@@Z");
    hide_ui_screen = GetProcAddress(state->engine_module,
        "?HideUIScreen@ILevel@@QEAAXH@Z");
    is_ui_screen_visible = GetProcAddress(state->engine_module,
        "?IsUIScreenVisible@ILevel@@QEAA_NH@Z");
    is_ui_screen_loaded = GetProcAddress(state->engine_module,
        "?IsUIScreenLoaded@ILevel@@QEAA_NH@Z");
    is_ui_screen_registered = GetProcAddress(state->engine_module,
        "?IsUIScreenRegistered@ILevel@@QEAA_NH@Z");
    find_ui_screen = GetProcAddress(state->engine_module,
        "?FindUiScreen@ILevel@@QEBAPEAVIUIElement@@H@Z");
    find_ui_element = GetProcAddress(state->engine_module,
        "?FindElement@IUIElement@@QEAAPEAV1@AEBV?$string_base@D@ttl@@_N@Z");
    get_owner_screen = GetProcAddress(state->engine_module,
        "?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ");
    get_screen_id = GetProcAddress(state->engine_module,
        "?GetScreenId@IUIScreen@@QEBAHXZ");
    get_screen_script_name = GetProcAddress(state->engine_module,
        "?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ");
    if (get_ui_screen_id && register_ui_screen && load_ui_screen &&
        show_ui_screen && hide_ui_screen && is_ui_screen_visible &&
        is_ui_screen_loaded && is_ui_screen_registered &&
        find_ui_screen && find_ui_element && get_owner_screen && get_screen_id &&
        get_screen_script_name) {
        state->get_ui_screen_id_by_script_name =
            (int (__fastcall *)(void*, const void*))get_ui_screen_id;
        state->register_ui_screen =
            (int (__fastcall *)(void*, const void*, unsigned int, int,
                                unsigned char, const void*, unsigned int))register_ui_screen;
        state->load_ui_screen = (void* (__fastcall *)(void*, int))load_ui_screen;
        state->show_ui_screen = (void (__fastcall *)(void*, int, void*))show_ui_screen;
        state->hide_ui_screen = (void (__fastcall *)(void*, int))hide_ui_screen;
        state->is_ui_screen_visible =
            (unsigned char (__fastcall *)(void*, int))is_ui_screen_visible;
        state->is_ui_screen_loaded =
            (unsigned char (__fastcall *)(void*, int))is_ui_screen_loaded;
        state->is_ui_screen_registered =
            (unsigned char (__fastcall *)(void*, int))is_ui_screen_registered;
        state->find_ui_screen =
            (void* (__fastcall *)(void*, int))find_ui_screen;
        state->find_ui_element =
            (void* (__fastcall *)(void*, const void*, unsigned char))find_ui_element;
        state->get_owner_screen =
            (void* (__fastcall *)(void*))get_owner_screen;
        state->get_screen_id =
            (int (__fastcall *)(void*))get_screen_id;
        state->get_screen_script_name =
            (const void* (__fastcall *)(void*))get_screen_script_name;
        state->ui_exports_ready = 1;
    }
    dluma_log_line("INFO", "Engine lifecycle exports resolved without hardcoded RVAs.");
    dluma_log_line("INFO", "Level queries use exported IGame::GetActiveLevel and ILevel::GetLevelName directly.");
    dluma_log_hex("INFO", "engine_timestamp", state->image_timestamp);
    dluma_log_hex("INFO", "engine_size_of_image", state->image_size);
    dluma_log_hex("INFO", "game_interface_offset", state->game_interface_offset);
    if (state->ui_exports_ready)
        dluma_log_line("INFO", "Exported ILevel UI screen registration methods resolved.");
    else
        dluma_log_line("WARN", "UI registration exports are unavailable; the MODS page will remain disabled.");
    if (DLUMA_ENABLE_LEVEL_DESTRUCTOR_HOOK &&
        dluma_install_level_destructor_hook(state)) {
        dluma_log_line("INFO", "Validated ILevel destructor hook installed for authoritative map-unload detection.");
    } else {
        state->level_destructor_hooked = 0;
        dluma_log_line("INFO", "ILevel destructor patch is disabled in this shutdown-safety build; confirmed frontend transitions own local-script unload.");
    }
    if (dluma_install_game_frame_hook(state))
        dluma_log_line("INFO", "Validated GameDI::OnFrame vtable pump installed for responsive main-thread updates.");
    else
        dluma_log_line("WARN", "GameDI::OnFrame vtable binding did not match; the Windows message-timer fallback remains active.");
    /* Publish readiness only after every callback pointer and optional hook
     * has reached its final startup state. */
    state->ready = 1;
    return 1;
}

void* dluma_engine_get_game_interface(DlumaEngineState* state)
{
    if (!state || !state->ready) return 0;
    return dluma_engine_get_game_interface_for_localization(state);
}

void* dluma_engine_get_game_interface_for_localization(DlumaEngineState* state)
{
    void* game_object;
    void* game_interface;
    if (!state || !state->game_global_slot ||
        !dluma_is_readable(state->game_global_slot, sizeof(void*)))
        return 0;
    game_object = *state->game_global_slot;
    if (!game_object ||
        !dluma_is_readable((BYTE*)game_object + state->game_interface_offset,
                         sizeof(void*)))
        return 0;
    game_interface = *(void**)((BYTE*)game_object + state->game_interface_offset);
    if (!game_interface || !dluma_is_readable(game_interface, sizeof(void*)))
        return 0;
    return game_interface;
}

HWND dluma_engine_get_window(DlumaEngineState* state)
{
    void* game_interface = dluma_engine_get_game_interface(state);
    if (!game_interface || !state->get_game_window) return 0;
    return state->get_game_window(game_interface);
}

static DlumaEngineLevelQueryResult dluma_engine_read_level_identity(
    DlumaEngineState* state, void* level_interface,
    char* map_out, unsigned int map_cap,
    void** level_out, ULONG_PTR* level_vtable_out)
{
    const char* level_name;
    char raw[DLUMA_MAX_MAP_NAME * 2];
    char normalized[DLUMA_MAX_MAP_NAME * 2];
    ULONG_PTR level_vtable = 0;
    unsigned int i;
    int terminated = 0;
    if (!map_out || map_cap == 0 || !level_out || !level_vtable_out)
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    map_out[0] = 0;
    *level_out = 0;
    *level_vtable_out = 0;
    if (!state || !state->ready || !state->get_level_name)
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    if (!level_interface)
        return DLUMA_ENGINE_LEVEL_QUERY_NULL;
    if (!dluma_is_readable(level_interface, sizeof(level_vtable)))
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    memcpy(&level_vtable, level_interface, sizeof(level_vtable));
    if (!level_vtable ||
        !dluma_is_readable((void*)level_vtable, sizeof(void*)))
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    level_name = state->get_level_name(level_interface);
    if (!level_name || !dluma_is_readable(level_name, 1))
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;

    for (i = 0; i < DLUMA_ARRAY_COUNT(raw); ++i) {
        if (!dluma_is_readable(level_name + i, 1))
            return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
        raw[i] = level_name[i];
        if (!raw[i]) {
            terminated = 1;
            break;
        }
    }
    if (!terminated || !raw[0])
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    dluma_normalize_map_name(
        raw, normalized, DLUMA_ARRAY_COUNT(normalized));
    if (!normalized[0] || strlen(normalized) + 1 > map_cap)
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    dluma_str_copy(map_out, map_cap, normalized);
    *level_out = level_interface;
    *level_vtable_out = level_vtable;
    return DLUMA_ENGINE_LEVEL_QUERY_SUCCESS;
}

DlumaEngineLevelQueryResult dluma_engine_query_level_identity(
    DlumaEngineState* state, char* map_out, unsigned int map_cap,
    void** level_out, ULONG_PTR* level_vtable_out)
{
    void* game_object;
    void* game_interface;
    void* level_interface;
    if (!map_out || !map_cap || !level_out || !level_vtable_out)
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    map_out[0] = 0;
    *level_out = 0;
    *level_vtable_out = 0;
    if (!state || !state->ready || !state->game_global_slot ||
        !state->get_active_level || !state->get_level_name ||
        !dluma_is_readable(state->game_global_slot, sizeof(void*)))
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    game_object = *state->game_global_slot;
    if (!game_object) return DLUMA_ENGINE_LEVEL_QUERY_NULL;
    if (!dluma_is_readable(
            (BYTE*)game_object + state->game_interface_offset, sizeof(void*)))
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    game_interface = *(void**)((BYTE*)game_object + state->game_interface_offset);
    if (!game_interface) return DLUMA_ENGINE_LEVEL_QUERY_NULL;
    if (!dluma_is_readable(game_interface, sizeof(void*)))
        return DLUMA_ENGINE_LEVEL_QUERY_INVALID;
    level_interface = state->get_active_level(game_interface);
    if (level_interface) state->last_level_interface = level_interface;
    return dluma_engine_read_level_identity(
        state, level_interface, map_out, map_cap, level_out, level_vtable_out);
}

int dluma_engine_query_session_level_identity(
    DlumaEngineState* state, char* map_out, unsigned int map_cap,
    void** level_out, ULONG_PTR* level_vtable_out)
{
    void* level = 0;
    void* current_level = 0;
    int valid = 0;
    if (!map_out || !map_cap || !level_out || !level_vtable_out) return 0;
    map_out[0] = 0;
    *level_out = 0;
    *level_vtable_out = 0;
    if (!dluma_engine_game_frame_dispatch_active_on_current_thread(state)) return 0;
    __try {
        if (dluma_player_media_adapter_get_session_level(&level) &&
            dluma_engine_read_level_identity(
                state, level, map_out, map_cap, level_out, level_vtable_out) ==
                DLUMA_ENGINE_LEVEL_QUERY_SUCCESS &&
            !dluma_frontend_level_name_is_frontend(map_out) &&
            dluma_player_media_adapter_get_session_level(&current_level) &&
            current_level == level &&
            dluma_is_readable(level, sizeof(void*)) &&
            *(ULONG_PTR*)level == *level_vtable_out)
            valid = 1;
    } __except (1) {
        valid = 0;
    }
    if (!valid) {
        map_out[0] = 0;
        *level_out = 0;
        *level_vtable_out = 0;
    }
    return valid;
}

int dluma_engine_query_level(
    DlumaEngineState* state, char* map_out, unsigned int map_cap)
{
    void* level = 0;
    ULONG_PTR level_vtable = 0;
    return dluma_engine_query_level_identity(
               state, map_out, map_cap, &level, &level_vtable) ==
           DLUMA_ENGINE_LEVEL_QUERY_SUCCESS;
}

LONG dluma_engine_game_frame_current_dispatch_thread_id(
    const DlumaEngineState* state)
{
    if (!state) return 0;
    return dluma_atomic_compare_exchange(
        (volatile LONG*)&state->game_frame_authority.
            current_dispatch_thread_id,
        0, 0);
}

void* dluma_engine_game_frame_route_subobject(
    const DlumaEngineState* state)
{
    if (!state ||
        !state->game_frame_authority.
             exact_route_identity_seeded)
        return 0;
    return (void*)state->game_frame_authority.
        exact_route_game_subobject;
}

ULONGLONG dluma_engine_game_frame_route_token(
    const DlumaEngineState* state)
{
    return state
        ? state->game_frame_authority.
              exact_route_serialization_token
        : 0;
}

int dluma_engine_game_frame_dispatch_active_on_current_thread(
    const DlumaEngineState* state)
{
    LONG current;
    if (!state) return 0;
    current = (LONG)GetCurrentThreadId();
    return current &&
           dluma_atomic_compare_exchange(
               (volatile LONG*)&state->
                   game_frame_loader_dispatch_active_thread_id,
           0, 0) == current;
}

int dluma_engine_game_frame_hook_owned(const DlumaEngineState* state)
{
    DlumaGameFrameAddress hook;
    hook.function = dluma_game_frame_hook;
    return state && state->game_frame_hooked &&
           state->game_frame_vtable_slot &&
           dluma_is_readable(
               state->game_frame_vtable_slot, sizeof(void*)) &&
           *state->game_frame_vtable_slot ==
               hook.address;
}

int dluma_engine_configure_playtest_frame_authority(
    DlumaEngineState* state, const char* expected_level_name)
{
    int configured = 0;
    if (!state || !state->ready || !expected_level_name ||
        !expected_level_name[0] ||
        !dluma_engine_game_frame_hook_owned(state) ||
        !dluma_engine_game_frame_authority_lock(state))
        return 0;
    dluma_engine_fold_pending_authority_conflict_locked(state);
    configured =
        dluma_game_frame_authority_configure_validated_playtest_handoff(
            &state->game_frame_authority,
            expected_level_name);
    dluma_engine_game_frame_authority_unlock(state);
    if (configured)
        dluma_log_pair(
            "INFO", "game_frame_playtest_expected_level",
            expected_level_name);
    return configured;
}

LONG dluma_engine_game_frame_window_owner_thread_id(
    const DlumaEngineState* state)
{
    if (!state ||
        state->game_frame_authority.
            game_frame_window_timer_owner_conflict)
        return 0;
    return dluma_atomic_compare_exchange(
        (volatile LONG*)&state->game_frame_authority.
            game_frame_message_timer_thread_id,
        0, 0);
}

DlumaGameFrameAuthorityStatus dluma_engine_game_frame_authority_status(
    DlumaEngineState* state, ULONGLONG now_ms)
{
    DlumaGameFrameAuthorityStatus status =
        DLUMA_GAME_FRAME_AUTHORITY_STATUS_CONFLICT;
    if (!state ||
        !dluma_engine_game_frame_authority_lock(state))
        return status;
    dluma_engine_fold_pending_authority_conflict_locked(state);
    status = dluma_game_frame_authority_status(
        &state->game_frame_authority, now_ms);
    dluma_engine_game_frame_authority_unlock(state);
    return status;
}

int dluma_engine_snapshot_game_frame_authority(
    DlumaEngineState* state,
    DlumaGameFrameAuthorityState* snapshot_out)
{
    if (!state || !snapshot_out ||
        !dluma_engine_game_frame_authority_lock(state))
        return 0;
    dluma_engine_fold_pending_authority_conflict_locked(state);
    dluma_game_frame_authority_expire_frontend_evidence(
        &state->game_frame_authority, GetTickCount64());
    memcpy(
        snapshot_out, &state->game_frame_authority,
        sizeof(*snapshot_out));
    dluma_engine_game_frame_authority_unlock(state);
    return 1;
}

void dluma_engine_note_game_frame_message_timer_probe(
    DlumaEngineState* state,
    LONG thread_id,
    const DlumaGameFrameFrontendProbe* probe)
{
    if (!state || !thread_id || !probe ||
        !dluma_engine_game_frame_authority_lock(state))
        return;
    dluma_engine_fold_pending_authority_conflict_locked(state);
    dluma_game_frame_authority_note_message_timer_probe(
        &state->game_frame_authority, thread_id, probe);
    dluma_engine_game_frame_authority_unlock(state);
}

void dluma_engine_note_custom_project_admitted(
    DlumaEngineState* state)
{
    if (!state ||
        !dluma_engine_game_frame_authority_lock(state))
        return;
    dluma_game_frame_authority_mark_project_admitted(
        &state->game_frame_authority);
    dluma_engine_game_frame_authority_unlock(state);
}

void dluma_engine_note_registry_mutation_started(
    DlumaEngineState* state)
{
    if (!state ||
        !dluma_engine_game_frame_authority_lock(state))
        return;
    dluma_game_frame_authority_mark_registry_mutation_started(
        &state->game_frame_authority);
    dluma_engine_game_frame_authority_unlock(state);
}

int dluma_engine_current_thread_can_finalize_content(
    DlumaEngineState* state, LONG validated_window_owner_thread_id)
{
    return dluma_engine_current_thread_can_finalize_content_ex(
        state, validated_window_owner_thread_id, 0, 0);
}

int dluma_engine_current_thread_can_finalize_content_ex(
    DlumaEngineState* state,
    LONG validated_window_owner_thread_id,
    const DlumaNaturalShutdownOwnerProof* natural_shutdown,
    DlumaContentFinalizationAuthorityEvidence* evidence_out)
{
    int allowed = 0;
    int serialized_allowed = 0;
    int natural_allowed = 0;
    LONG current_thread;
    unsigned int inside_route;
    DlumaContentFinalizationAuthorityEvidence evidence;
    memset(&evidence, 0, sizeof(evidence));
    if (!state ||
        !dluma_engine_game_frame_authority_lock(state))
        return 0;
    current_thread = (LONG)GetCurrentThreadId();
    inside_route =
        dluma_atomic_compare_exchange(
            &state->game_frame_callback_active_thread_id,
            0, 0) == current_thread;
    evidence.current_thread_id = current_thread;
    evidence.serialized_route_active_thread_id =
        dluma_atomic_compare_exchange(
            &state->game_frame_callback_active_thread_id,
            0, 0);
    evidence.serialized_route_dispatch_thread_id =
        state->game_frame_authority.current_dispatch_thread_id;
    evidence.serialized_route_token =
        state->game_frame_authority.
            exact_route_serialization_token;
    evidence.inside_serialized_route = inside_route;
    evidence.serialized_frontend_latched =
        state->game_frame_authority.
            game_frame_frontend_identity_latched != 0;
    evidence.serialized_playtest_handoff_latched =
        state->game_frame_authority.
            game_frame_playtest_handoff_latched != 0;
    evidence.serialized_authority_frozen =
        state->game_frame_authority.
            game_frame_authority_frozen != 0;
    evidence.serialized_authority_conflict =
        state->game_frame_authority.
            game_frame_authority_conflict != 0;
    evidence.validated_window_owner_thread_id =
        validated_window_owner_thread_id;
    evidence.observed_window_owner_thread_id =
        state->game_frame_authority.
            game_frame_message_timer_thread_id;
    evidence.window_owner_conflict =
        state->game_frame_authority.
            game_frame_window_timer_owner_conflict != 0;
    if (natural_shutdown)
        evidence.natural_shutdown = *natural_shutdown;
    allowed = dluma_game_frame_authority_can_finalize(
        &state->game_frame_authority,
        current_thread, inside_route,
        validated_window_owner_thread_id);
    serialized_allowed =
        inside_route &&
        !state->game_frame_authority.
             game_frame_authority_conflict &&
        dluma_game_frame_authority_identity_latched(
            &state->game_frame_authority) &&
        state->game_frame_authority.
            game_frame_authority_frozen &&
        state->game_frame_authority.
            exact_route_serialization_token &&
        state->game_frame_authority.
            current_dispatch_thread_id == current_thread;
    natural_allowed =
        natural_shutdown &&
        natural_shutdown->exact &&
        natural_shutdown->expected_exit_owner_calls == 1U &&
        natural_shutdown->observed_exit_owner_calls == 1U &&
        natural_shutdown->expected_thread_id == current_thread &&
        natural_shutdown->observed_thread_id == current_thread &&
        natural_shutdown->expected_token &&
        natural_shutdown->expected_token ==
            natural_shutdown->observed_token &&
        !natural_shutdown->owner_conflict &&
        natural_shutdown->hook_owned;
    if (serialized_allowed)
        evidence.selected_proof =
            DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE;
    else if (allowed)
        evidence.selected_proof =
            DLUMA_CONTENT_FINALIZATION_PROOF_WINDOW_OWNER;
    else if (natural_allowed) {
        evidence.selected_proof =
            DLUMA_CONTENT_FINALIZATION_PROOF_NATURAL_SHUTDOWN_OWNER;
        allowed = 1;
    }
    dluma_engine_game_frame_authority_unlock(state);
    if (evidence_out)
        *evidence_out = evidence;
    return allowed;
}

const char* dluma_content_finalization_authority_proof_name(
    DlumaContentFinalizationAuthorityProof proof)
{
    switch (proof) {
    case DLUMA_CONTENT_FINALIZATION_PROOF_NONE:
        return "none";
    case DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE:
        return "serialized_frame_route";
    case DLUMA_CONTENT_FINALIZATION_PROOF_WINDOW_OWNER:
        return "validated_window_owner";
    case DLUMA_CONTENT_FINALIZATION_PROOF_NATURAL_SHUTDOWN_OWNER:
        return "natural_shutdown_owner";
    default:
        return "unknown";
    }
}

void dluma_engine_track_last_level(DlumaEngineState* state)
{
    if (!state) return;
    state->tracked_level_interface = state->last_level_interface;
    dluma_atomic_exchange(&state->tracked_level_destroyed, 0);
}

void dluma_engine_clear_tracked_level(DlumaEngineState* state)
{
    if (!state) return;
    state->tracked_level_interface = 0;
    dluma_atomic_exchange(&state->tracked_level_destroyed, 0);
}

int dluma_engine_take_tracked_level_destroyed(DlumaEngineState* state)
{
    if (!state) return 0;
    return dluma_atomic_exchange(&state->tracked_level_destroyed, 0) != 0;
}

void dluma_engine_quiesce(DlumaEngineState* state)
{
    DWORD old_protect;
    DWORD restored_protect;

    if (!state) return;
    dluma_atomic_exchange(&state->game_frame_stopping, 1);

    /* This path can run from inside ExitGame or a window callback.  Restore
     * routing synchronously, but never wait for the callback that invoked us. */
    if (state->game_frame_hooked && state->game_frame_vtable_slot &&
        dluma_is_readable(state->game_frame_vtable_slot, sizeof(void*))) {
        if (*state->game_frame_vtable_slot != (void*)dluma_game_frame_hook) {
            /* A later owner may be chaining through our callback. Keep both
             * the pass-through gate and original pointer published. */
            dluma_log_line("WARN", "GameDI::OnFrame vtable slot changed ownership; loader callback state is retained in pass-through mode.");
        } else if (VirtualProtect(state->game_frame_vtable_slot, sizeof(void*),
                                   PAGE_READWRITE, &old_protect)) {
            *state->game_frame_vtable_slot = state->game_frame_original;
            FlushInstructionCache(GetCurrentProcess(),
                                  state->game_frame_vtable_slot, sizeof(void*));
            if (VirtualProtect(
                    state->game_frame_vtable_slot, sizeof(void*),
                    old_protect, &restored_protect)) {
                state->game_frame_hooked = 0;
                dluma_log_line(
                    "INFO",
                    "GameDI::OnFrame vtable hook restored.");
            } else {
                dluma_log_hex(
                    "WARN",
                    "GameDI::OnFrame bytes restored but page-protection restoration failed; callback state is retained.",
                    GetLastError());
            }
        } else {
            dluma_log_hex("WARN", "VirtualProtect failed while restoring GameDI::OnFrame hook",
                        GetLastError());
        }
    }
}

int dluma_engine_unbind(DlumaEngineState* state)
{
    DWORD old_protect;
    DWORD restored_protect;
    ULONGLONG drain_started;

    int destructor_restored = 1;

    if (!state) return 1;
    dluma_engine_quiesce(state);
    dluma_engine_clear_tracked_level(state);

    /* A frame that entered before the slot restore is allowed to finish while
     * the original callback pointer and UI state remain valid. */
    drain_started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &state->game_frame_active_calls, 0, 0) != 0 &&
           GetTickCount64() - drain_started < 2000) {
        Sleep(1);
    }
    if (dluma_atomic_compare_exchange(&state->game_frame_active_calls, 0, 0) != 0)
        dluma_log_line("WARN", "Timed out draining GameDI::OnFrame callbacks; original pointer is retained for process shutdown safety.");
    else if (!state->game_frame_hooked)
        g_dluma_game_frame_original = 0;
    else
        dluma_log_line("WARN", "GameDI::OnFrame hook was not restored; original callback pointer is retained.");

    if (!state->game_frame_hooked &&
        dluma_atomic_compare_exchange(&state->game_frame_active_calls, 0, 0) == 0) {
        state->game_frame_vtable_slot = 0;
        state->game_frame_original = 0;
    }
    dluma_atomic_exchange(&g_dluma_game_frame_first_tick_logged, 0);
    if (!state->game_frame_hooked &&
        dluma_atomic_compare_exchange(
            &state->game_frame_active_calls, 0, 0) == 0) {
        memset(
            &state->game_frame_authority, 0,
            sizeof(state->game_frame_authority));
        state->game_frame_loader_dispatch_active_thread_id = 0;
        state->game_frame_module = 0;
        state->game_frame_expected_primary_vtable = 0;
        state->game_frame_expected_subobject_vtable = 0;
    }

    if (state->level_destructor_hooked) {
        destructor_restored = 0;
        if (state->level_destructor &&
            VirtualProtect(state->level_destructor,
                           sizeof(state->level_destructor_original),
                           PAGE_EXECUTE_READWRITE, &old_protect)) {
            memcpy(state->level_destructor, state->level_destructor_original,
                   sizeof(state->level_destructor_original));
            FlushInstructionCache(GetCurrentProcess(), state->level_destructor,
                                  sizeof(state->level_destructor_original));
            if (VirtualProtect(state->level_destructor,
                               sizeof(state->level_destructor_original),
                               old_protect, &restored_protect))
                destructor_restored = 1;
        }
    }
    if (state->game_frame_hooked ||
        dluma_atomic_compare_exchange(&state->game_frame_active_calls, 0, 0) != 0 ||
        !destructor_restored)
        return 0;
    if (g_dluma_engine_hook_state == state) g_dluma_engine_hook_state = 0;
    state->level_destructor_hooked = 0;
    return 1;
}
