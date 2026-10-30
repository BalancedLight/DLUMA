#include "dlw_game_frame_authority.h"

static ULONGLONG dluma_game_frame_increment_u64(ULONGLONG value)
{
    return value == ~(ULONGLONG)0 ? value : value + 1U;
}

static void dluma_game_frame_copy_text(
    char* output, unsigned int capacity, const char* input)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    output[0] = 0;
    if (!input) return;
    while (index + 1U < capacity && input[index]) {
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
}

static int dluma_game_frame_text_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        ++index;
    }
    return left[index] == right[index];
}

int dluma_game_frame_authority_identity_latched(
    const DlumaGameFrameAuthorityState* state)
{
    return state &&
           (state->game_frame_frontend_identity_latched ||
            state->game_frame_playtest_handoff_latched ||
            state->game_frame_standalone_start_latched);
}

static int dluma_game_frame_startup_probe_valid(
    const DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameFrontendProbe* probe)
{
    if (!probe ||
        probe->result != DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS ||
        !probe->level_pointer || !probe->level_vtable ||
        !probe->level_name[0])
        return 0;
    if (state && state->game_frame_playtest_level_configured)
        return state->expected_playtest_level_name[0] &&
               dluma_game_frame_text_equal(
                   probe->level_name,
                   state->expected_playtest_level_name);
    if (state && state->game_frame_standalone_start_configured &&
        !probe->frontend_identity_valid)
        return probe->gameplay_player &&
               dluma_game_frame_text_equal(probe->level_name,
                   state->expected_standalone_level_name);
    if (!probe->frontend_identity_valid)
        return 0;
    return 1;
}

static int dluma_game_frame_probe_matches(
    const DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameFrontendProbe* left,
    const DlumaGameFrameFrontendProbe* right)
{
    return dluma_game_frame_startup_probe_valid(state, left) &&
           dluma_game_frame_startup_probe_valid(state, right) &&
           left->level_pointer == right->level_pointer &&
           left->level_vtable == right->level_vtable &&
           left->gameplay_player == right->gameplay_player &&
           dluma_game_frame_text_equal(
               left->level_name, right->level_name);
}

static int dluma_game_frame_probe_matches_latched(
    const DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameFrontendProbe* probe)
{
    return state &&
           dluma_game_frame_startup_probe_valid(state, probe) &&
           state->latched_frontend_pointer &&
           state->latched_frontend_vtable &&
           probe->level_pointer ==
               state->latched_frontend_pointer &&
           probe->level_vtable ==
               state->latched_frontend_vtable &&
           dluma_game_frame_text_equal(
               probe->level_name,
               state->latched_frontend_name);
}

static void dluma_game_frame_store_latched_frontend(
    DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameFrontendProbe* probe,
    int rebind)
{
    if (!state || !dluma_game_frame_startup_probe_valid(state, probe))
        return;
    state->latched_frontend_pointer = probe->level_pointer;
    state->latched_frontend_vtable = probe->level_vtable;
    state->latched_frontend_observed_ms = probe->monotonic_ms;
    dluma_game_frame_copy_text(
        state->latched_frontend_name,
        (unsigned int)sizeof(state->latched_frontend_name),
        probe->level_name);
    if (rebind)
        state->frontend_identity_rebind_count =
            dluma_game_frame_increment_u64(
                state->frontend_identity_rebind_count);
}

static void dluma_game_frame_clear_pending_frontend(
    DlumaGameFrameAuthorityState* state)
{
    if (!state) return;
    state->pending_post_sequence = 0;
    memset(
        &state->pending_post_frontend, 0,
        sizeof(state->pending_post_frontend));
}

static int dluma_game_frame_probe_pointer_is_stale(
    const DlumaGameFrameFrontendProbe* probe,
    ULONGLONG now_ms)
{
    return probe && (probe->level_pointer || probe->level_vtable) &&
           (!probe->monotonic_ms ||
            now_ms < probe->monotonic_ms ||
            now_ms - probe->monotonic_ms >
                DLUMA_GAME_FRAME_AUTHORITY_MAX_FRONTEND_AGE_MS);
}

static void dluma_game_frame_expire_probe_pointer(
    DlumaGameFrameFrontendProbe* probe,
    ULONGLONG now_ms)
{
    if (!dluma_game_frame_probe_pointer_is_stale(probe, now_ms))
        return;
    probe->level_pointer = 0;
    probe->level_vtable = 0;
    probe->pending_identity_still_readable = 0U;
}

static int dluma_game_frame_route_matches(
    const DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameAuthorityObservation* observation)
{
    return state && observation &&
           state->exact_route_identity_seeded &&
           state->exact_route_game_subobject ==
               observation->game_subobject &&
           state->exact_route_game_primary ==
               observation->game_primary &&
           state->exact_route_caller_module_base ==
               observation->caller_module_base &&
           state->exact_route_caller_rva ==
               observation->caller_rva &&
           state->exact_route_serialization_token ==
               observation->serialization_token;
}

static void dluma_game_frame_set_conflict(
    DlumaGameFrameAuthorityState* state,
    DlumaGameFrameAuthorityConflictReason reason)
{
    if (!state || reason == DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE)
        return;
    state->game_frame_authority_conflict = 1;
    if (state->game_frame_authority_conflict_reason ==
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE)
        state->game_frame_authority_conflict_reason = (LONG)reason;
}

static void dluma_game_frame_arm_selection_window(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG now_ms)
{
    ULONGLONG maximum = ~(ULONGLONG)0;
    if (!state || state->game_frame_authority_window_armed)
        return;
    state->game_frame_authority_window_armed = 1;
    state->game_frame_authority_started_ms = now_ms;
    state->game_frame_authority_deadline_ms =
        state->game_frame_authority_timeout_ms > maximum - now_ms
            ? maximum
            : now_ms + state->game_frame_authority_timeout_ms;
}

static DlumaGameFrameThreadAggregate*
dluma_game_frame_find_thread(
    DlumaGameFrameAuthorityState* state, LONG thread_id)
{
    unsigned int index;
    if (!state || !thread_id) return 0;
    for (index = 0; index < state->thread_count; ++index) {
        if (state->threads[index].thread_id == thread_id)
            return &state->threads[index];
    }
    return 0;
}

static DlumaGameFrameThreadAggregate*
dluma_game_frame_find_or_add_thread(
    DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameAuthorityObservation* observation)
{
    DlumaGameFrameThreadAggregate* aggregate;
    if (!state || !observation || !observation->thread_id)
        return 0;
    aggregate = dluma_game_frame_find_thread(
        state, observation->thread_id);
    if (aggregate) return aggregate;
    if (state->thread_count >=
            DLUMA_GAME_FRAME_AUTHORITY_THREAD_CAPACITY) {
        state->thread_diagnostic_overflow = 1U;
        return 0;
    }
    aggregate = &state->threads[state->thread_count++];
    memset(aggregate, 0, sizeof(*aggregate));
    aggregate->thread_id = observation->thread_id;
    aggregate->first_sequence = observation->sequence;
    return aggregate;
}

static DlumaGameFrameCallbackTrace*
dluma_game_frame_append_trace(
    DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameAuthorityObservation* observation,
    DlumaGameFrameAuthorityResult result)
{
    DlumaGameFrameCallbackTrace* trace;
    unsigned int index;
    if (!state || !observation) return 0;
    index = state->trace_write_index;
    trace = &state->trace[index];
    memset(trace, 0, sizeof(*trace));
    trace->sequence = observation->sequence;
    trace->thread_id = observation->thread_id;
    trace->previous_callback_thread_id =
        state->previous_callback_thread_id;
    trace->game_subobject = observation->game_subobject;
    trace->game_primary = observation->game_primary;
    trace->gesture_manager = observation->gesture_manager;
    trace->delta_bits = observation->delta_bits;
    trace->caller_return_address =
        observation->caller_return_address;
    trace->caller_module_base =
        observation->caller_module_base;
    trace->caller_rva = observation->caller_rva;
    trace->serialization_token =
        observation->serialization_token;
    dluma_game_frame_copy_text(
        trace->caller_module_name,
        (unsigned int)sizeof(trace->caller_module_name),
        observation->caller_module_name);
    trace->callback_entry_qpc =
        observation->callback_entry_qpc;
    trace->concurrent_depth = observation->concurrent_depth;
    trace->recursive_depth = observation->recursive_depth;
    trace->scheduler_migration_count =
        state->scheduler_migration_count;
    trace->global_game_object =
        observation->global_game_object;
    trace->game_interface = observation->game_interface;
    trace->route_proof = observation->route_proof;
    trace->pre_original_probe =
        observation->pre_original_probe;
    trace->authority_result = result;
    state->trace_write_index =
        (index + 1U) % DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY;
    if (state->trace_count <
            DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY)
        ++state->trace_count;
    else
        state->trace_overwrite_count =
            dluma_game_frame_increment_u64(
                state->trace_overwrite_count);
    return trace;
}

static DlumaGameFrameCallbackTrace*
dluma_game_frame_find_trace(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG sequence,
    LONG thread_id)
{
    unsigned int count;
    unsigned int offset;
    if (!state || !sequence || !thread_id) return 0;
    count = state->trace_count;
    for (offset = 0; offset < count; ++offset) {
        unsigned int index =
            (state->trace_write_index +
             DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY -
             1U - offset) %
            DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY;
        DlumaGameFrameCallbackTrace* trace =
            &state->trace[index];
        if (trace->sequence == sequence &&
            trace->thread_id == thread_id)
            return trace;
    }
    return 0;
}

static void dluma_game_frame_count_probe(
    DlumaGameFrameThreadAggregate* aggregate,
    const DlumaGameFrameFrontendProbe* probe,
    int post_original)
{
    ULONGLONG* success;
    ULONGLONG* null_count;
    ULONGLONG* invalid;
    if (!aggregate || !probe) return;
    success = post_original
        ? &aggregate->post_level_success_count
        : &aggregate->pre_level_success_count;
    null_count = post_original
        ? &aggregate->post_level_null_count
        : &aggregate->pre_level_null_count;
    invalid = post_original
        ? &aggregate->post_level_invalid_count
        : &aggregate->pre_level_invalid_count;
    if (probe->result == DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS)
        *success = dluma_game_frame_increment_u64(*success);
    else if (probe->result == DLUMA_GAME_FRAME_LEVEL_QUERY_NULL)
        *null_count = dluma_game_frame_increment_u64(*null_count);
    else if (probe->result == DLUMA_GAME_FRAME_LEVEL_QUERY_INVALID)
        *invalid = dluma_game_frame_increment_u64(*invalid);
}

void dluma_game_frame_authority_initialize(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG started_ms,
    ULONGLONG timeout_ms)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->game_frame_authority_initialized_ms = started_ms;
    state->game_frame_authority_timeout_ms = timeout_ms;
}

int dluma_game_frame_authority_configure_validated_playtest_handoff(
    DlumaGameFrameAuthorityState* state,
    const char* expected_level_name)
{
    unsigned int length = 0;
    if (!state || !expected_level_name || !expected_level_name[0] ||
        state->game_frame_authority_conflict ||
        state->game_frame_authority_timed_out ||
        state->game_frame_loader_dispatch_started ||
        dluma_game_frame_authority_identity_latched(state))
        return 0;
    while (expected_level_name[length]) {
        if (length + 1U >=
                DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY)
            return 0;
        ++length;
    }
    if (state->game_frame_playtest_level_configured)
        return dluma_game_frame_text_equal(
            state->expected_playtest_level_name,
            expected_level_name) &&
            state->game_frame_playtest_handoff_validated;
    dluma_game_frame_copy_text(
        state->expected_playtest_level_name,
        (unsigned int)sizeof(
            state->expected_playtest_level_name),
        expected_level_name);
    state->game_frame_playtest_level_configured = 1;
    state->game_frame_playtest_handoff_validated = 1;
    return 1;
}

int dluma_game_frame_authority_configure_standalone_start(
    DlumaGameFrameAuthorityState* state, const char* expected_level_name)
{
    unsigned int length = 0;
    if (!state || !expected_level_name || !expected_level_name[0] ||
        state->game_frame_authority_conflict || state->game_frame_authority_timed_out ||
        state->game_frame_playtest_level_configured ||
        state->game_frame_loader_dispatch_started ||
        dluma_game_frame_authority_identity_latched(state))
        return 0;
    while (expected_level_name[length]) {
        if (length + 1U >= DLUMA_GAME_FRAME_AUTHORITY_LEVEL_NAME_CAPACITY) return 0;
        ++length;
    }
    dluma_game_frame_copy_text(state->expected_standalone_level_name,
        (unsigned int)sizeof(state->expected_standalone_level_name), expected_level_name);
    state->game_frame_standalone_start_configured = 1;
    return 1;
}

void dluma_game_frame_authority_expire_frontend_evidence(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG now_ms)
{
    unsigned int offset;
    if (!state) return;
    if (state->pending_post_sequence &&
        dluma_game_frame_probe_pointer_is_stale(
            &state->pending_post_frontend, now_ms)) {
        dluma_game_frame_expire_probe_pointer(
            &state->pending_post_frontend, now_ms);
        state->pending_post_sequence = 0;
    }
    if ((state->latched_frontend_pointer ||
         state->latched_frontend_vtable) &&
        (!state->latched_frontend_observed_ms ||
         now_ms < state->latched_frontend_observed_ms ||
         now_ms - state->latched_frontend_observed_ms >
             DLUMA_GAME_FRAME_AUTHORITY_MAX_FRONTEND_AGE_MS)) {
        state->latched_frontend_pointer = 0;
        state->latched_frontend_vtable = 0;
    }
    dluma_game_frame_expire_probe_pointer(
        &state->last_window_timer_probe, now_ms);
    for (offset = 0; offset < state->trace_count; ++offset) {
        unsigned int index =
            (state->trace_write_index +
             DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY -
             1U - offset) %
            DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY;
        dluma_game_frame_expire_probe_pointer(
            &state->trace[index].pre_original_probe, now_ms);
        dluma_game_frame_expire_probe_pointer(
            &state->trace[index].post_original_probe, now_ms);
    }
}

DlumaGameFrameAuthorityDecision dluma_game_frame_authority_observe_pre(
    DlumaGameFrameAuthorityState* state,
    const DlumaGameFrameAuthorityObservation* observation)
{
    DlumaGameFrameAuthorityDecision decision;
    DlumaGameFrameThreadAggregate* aggregate;
    DlumaGameFrameAuthorityConflictReason mismatch_reason =
        DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE;
    unsigned int route_valid;
    ULONGLONG route_gap_ms = 0;
    unsigned int pending_fresh = 0;
    unsigned int pending_confirmed = 0;

    memset(&decision, 0, sizeof(decision));
    decision.result =
        DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_INVALID_ROUTE;
    if (!state || !observation || !observation->sequence ||
        !observation->thread_id || !observation->game_subobject ||
        !observation->game_primary ||
        !observation->serialization_token)
        return decision;
    dluma_game_frame_authority_expire_frontend_evidence(
        state, observation->monotonic_ms);

    aggregate = dluma_game_frame_find_or_add_thread(
        state, observation);
    if (!aggregate) {
        dluma_game_frame_set_conflict(
            state,
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_DIAGNOSTIC_CAPACITY_EXHAUSTED);
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT;
        decision.conflict_reason =
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_DIAGNOSTIC_CAPACITY_EXHAUSTED;
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    }

    aggregate->callback_count =
        dluma_game_frame_increment_u64(aggregate->callback_count);
    aggregate->last_sequence = observation->sequence;
    dluma_game_frame_count_probe(
        aggregate, &observation->pre_original_probe, 0);

    if (observation->concurrent_depth > 1U) {
        aggregate->concurrent_callback_count =
            dluma_game_frame_increment_u64(
                aggregate->concurrent_callback_count);
        aggregate->authority_result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CONCURRENT;
        dluma_game_frame_set_conflict(
            state,
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK);
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CONCURRENT;
        decision.conflict_reason =
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK;
        decision.ownership_lost =
            dluma_game_frame_authority_identity_latched(state);
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    }
    if (observation->recursive_depth > 1U) {
        aggregate->recursive_callback_count =
            dluma_game_frame_increment_u64(
                aggregate->recursive_callback_count);
        aggregate->authority_result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_RECURSIVE;
        dluma_game_frame_set_conflict(
            state,
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_RECURSIVE_CALLBACK);
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_RECURSIVE;
        decision.conflict_reason =
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_RECURSIVE_CALLBACK;
        decision.ownership_lost =
            dluma_game_frame_authority_identity_latched(state);
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    }

    route_valid =
        (observation->route_proof &
         DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF) ==
            DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF &&
        (observation->route_proof &
         DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE) != 0;
    if (!route_valid) {
        aggregate->authority_result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_INVALID_ROUTE;
        if (state->exact_route_identity_seeded) {
            dluma_game_frame_set_conflict(
                state,
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST);
            decision.conflict_reason =
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST;
            decision.ownership_lost =
                dluma_game_frame_authority_identity_latched(state);
        }
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    }

    if (!state->exact_route_identity_seeded) {
        state->exact_route_game_subobject =
            observation->game_subobject;
        state->exact_route_game_primary =
            observation->game_primary;
        state->exact_route_caller_module_base =
            observation->caller_module_base;
        state->exact_route_caller_rva =
            observation->caller_rva;
        state->exact_route_serialization_token =
            observation->serialization_token;
        state->exact_route_identity_seeded = 1;
        state->repeating_route_count = 1U;
        state->last_route_sequence = observation->sequence;
        state->last_route_ms = observation->monotonic_ms;
        if (!state->game_frame_standalone_start_configured ||
            dluma_game_frame_startup_probe_valid(state, &observation->pre_original_probe))
            dluma_game_frame_arm_selection_window(state, observation->monotonic_ms);
    } else if (!dluma_game_frame_route_matches(
                   state, observation)) {
        if (state->exact_route_game_subobject !=
                observation->game_subobject)
            mismatch_reason =
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_GAME_SUBOBJECT_CHANGED;
        else if (state->exact_route_game_primary !=
                     observation->game_primary)
            mismatch_reason =
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_PRIMARY_OBJECT_CHANGED;
        else if (state->exact_route_serialization_token !=
                     observation->serialization_token)
            mismatch_reason =
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST;
        else
            mismatch_reason =
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CALLER_ROUTE_CHANGED;
        dluma_game_frame_set_conflict(state, mismatch_reason);
        aggregate->authority_result =
            mismatch_reason ==
                    DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CALLER_ROUTE_CHANGED
                ? DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CALLER_CHANGE
                : DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_OBJECT_CHANGE;
        decision.result = aggregate->authority_result;
        decision.conflict_reason = mismatch_reason;
        decision.ownership_lost =
            dluma_game_frame_authority_identity_latched(state);
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    } else {
        if (observation->sequence <= state->last_route_sequence ||
            observation->monotonic_ms < state->last_route_ms) {
            dluma_game_frame_set_conflict(
                state,
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST);
            aggregate->authority_result =
                DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_INVALID_ROUTE;
            decision.result = aggregate->authority_result;
            decision.conflict_reason =
                DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST;
            decision.ownership_lost =
                dluma_game_frame_authority_identity_latched(state);
            (void)dluma_game_frame_append_trace(
                state, observation, decision.result);
            return decision;
        }
        route_gap_ms =
            observation->monotonic_ms - state->last_route_ms;
        if (route_gap_ms > state->maximum_repeating_gap_ms)
            state->maximum_repeating_gap_ms = route_gap_ms;
        if (!dluma_game_frame_authority_identity_latched(state) &&
            route_gap_ms >
                DLUMA_GAME_FRAME_AUTHORITY_MAX_REPEATING_GAP_MS)
            state->repeating_route_count = 1U;
        else
            state->repeating_route_count =
                dluma_game_frame_increment_u64(
                    state->repeating_route_count);
        state->last_route_sequence = observation->sequence;
        state->last_route_ms = observation->monotonic_ms;
    }

    if (state->game_frame_authority_conflict) {
        aggregate->authority_result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT;
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT;
        decision.conflict_reason =
            (DlumaGameFrameAuthorityConflictReason)
                state->game_frame_authority_conflict_reason;
        decision.ownership_lost =
            dluma_game_frame_authority_identity_latched(state);
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    }
    if (state->game_frame_standalone_start_configured &&
        !state->game_frame_authority_window_armed &&
        dluma_game_frame_startup_probe_valid(state, &observation->pre_original_probe))
        dluma_game_frame_arm_selection_window(state, observation->monotonic_ms);
    if (state->game_frame_authority_timed_out) {
        aggregate->authority_result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_TIMEOUT;
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_TIMEOUT;
        decision.conflict_reason =
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_SELECTION_TIMEOUT;
        (void)dluma_game_frame_append_trace(
            state, observation, decision.result);
        return decision;
    }

    state->serialized_callback_count =
        dluma_game_frame_increment_u64(
            state->serialized_callback_count);
    state->previous_callback_thread_id =
        state->last_callback_thread_id;
    if (state->last_callback_thread_id &&
        state->last_callback_thread_id !=
            observation->thread_id) {
        state->scheduler_migration_count =
            dluma_game_frame_increment_u64(
                state->scheduler_migration_count);
        aggregate->migration_arrival_count =
            dluma_game_frame_increment_u64(
                aggregate->migration_arrival_count);
        decision.dispatch_thread_migrated = 1U;
    }
    state->last_callback_thread_id = observation->thread_id;
    state->current_dispatch_thread_id = observation->thread_id;

    if (state->repeating_route_count >=
            DLUMA_GAME_FRAME_AUTHORITY_MIN_STABLE_CALLBACKS)
        state->game_frame_authority_proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_BOUNDED_REPEATING_ROUTE |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE;
    state->game_frame_authority_proof |=
        (LONG)(observation->route_proof &
               DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF);
    if (state->game_frame_playtest_level_configured &&
        state->game_frame_playtest_handoff_validated)
        state->game_frame_authority_proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_VALIDATED_PLAYTEST_HANDOFF;

    if (state->pending_post_sequence &&
        observation->sequence > state->pending_post_sequence &&
        observation->monotonic_ms >=
            state->pending_post_frontend.monotonic_ms &&
        observation->monotonic_ms -
                state->pending_post_frontend.monotonic_ms <=
            DLUMA_GAME_FRAME_AUTHORITY_MAX_FRONTEND_AGE_MS)
        pending_fresh = 1U;
    if (pending_fresh) {
        pending_confirmed =
            dluma_game_frame_probe_matches(
                state,
                &state->pending_post_frontend,
                &observation->pre_original_probe);
        if (state->game_frame_standalone_start_configured &&
            !observation->pre_original_probe.frontend_identity_valid &&
            state->pending_post_sequence + 1U != observation->sequence)
            pending_confirmed = 0;
        if (!dluma_game_frame_authority_identity_latched(state) &&
            !state->game_frame_standalone_start_configured)
            pending_confirmed =
                pending_confirmed ||
                (observation->pre_original_probe.
                     pending_identity_still_readable != 0 &&
                 state->pending_post_frontend.level_pointer &&
                 state->pending_post_frontend.level_vtable);
    }

    /*
     * The validated frontend ILevel is startup evidence for selecting this
     * exact GameDI route; it is not the owner of that route.  Chrome can
     * legitimately destroy and recreate menu_lvl_new while preserving the
     * same serialized GameDI subobject/caller/token.  Rebind only after a
     * positive post-original probe is confirmed by the next pre-original
     * probe, while every route-identity and serialization check above remains
     * fail-closed.
     */
    if (dluma_game_frame_authority_identity_latched(state) &&
        !state->game_frame_standalone_start_latched &&
        pending_confirmed) {
        dluma_game_frame_store_latched_frontend(
            state, &state->pending_post_frontend, 1);
        dluma_game_frame_clear_pending_frontend(state);
    } else if (dluma_game_frame_authority_identity_latched(state) &&
               !state->game_frame_standalone_start_latched &&
               dluma_game_frame_probe_matches_latched(
                   state, &observation->pre_original_probe)) {
        state->latched_frontend_observed_ms =
            observation->pre_original_probe.monotonic_ms;
    }

    if (!dluma_game_frame_authority_identity_latched(state) &&
        state->game_frame_standalone_start_configured &&
        !observation->pre_original_probe.frontend_identity_valid) {
        if (pending_confirmed)
            state->standalone_confirmed_callbacks = dluma_game_frame_increment_u64(
                state->standalone_confirmed_callbacks);
        else
            state->standalone_confirmed_callbacks = 0;
    }
    if (!dluma_game_frame_authority_identity_latched(state) &&
        state->game_frame_standalone_start_configured &&
        state->standalone_confirmed_callbacks >= DLUMA_GAME_FRAME_AUTHORITY_MIN_STABLE_CALLBACKS) {
        state->game_frame_standalone_start_latched = 1;
        state->game_frame_authority_frozen = 1;
        state->game_frame_authority_sequence = observation->sequence;
        state->game_frame_authority_proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_ACTIVE_LEVEL_QUERY |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_SUBSEQUENT_ROUTE_CONFIRMATION |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_STANDALONE_START_INTENT |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_STANDALONE_SESSION_PLAYER;
        dluma_game_frame_clear_pending_frontend(state);
        decision.result = DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH;
        decision.authority_selected = 1U;
        decision.run_loader_dispatch = 1U;
    } else if (!dluma_game_frame_authority_identity_latched(state) &&
        state->game_frame_playtest_level_configured &&
        state->game_frame_playtest_handoff_validated &&
        (state->game_frame_authority_proof &
             DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF) ==
            DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF) {
        state->game_frame_playtest_handoff_latched = 1;
        state->game_frame_authority_frozen = 1;
        state->game_frame_authority_sequence =
            observation->sequence;
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_LATCHED;
        decision.authority_selected = 1U;
        decision.run_loader_dispatch = 1U;
    } else if (!dluma_game_frame_authority_identity_latched(state) &&
        !state->game_frame_playtest_level_configured &&
        (!state->game_frame_standalone_start_configured ||
         observation->pre_original_probe.frontend_identity_valid) &&
        state->repeating_route_count >=
            DLUMA_GAME_FRAME_AUTHORITY_MIN_STABLE_CALLBACKS &&
        pending_confirmed) {
        dluma_game_frame_store_latched_frontend(
            state, &state->pending_post_frontend, 0);
        state->game_frame_frontend_identity_latched = 1;
        state->game_frame_authority_frozen = 1;
        state->game_frame_authority_sequence =
            observation->sequence;
        state->game_frame_authority_proof |=
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_ACTIVE_LEVEL_QUERY |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_FRONTEND_IDENTITY |
            DLUMA_GAME_FRAME_AUTHORITY_PROOF_SUBSEQUENT_ROUTE_CONFIRMATION;
        dluma_game_frame_clear_pending_frontend(state);
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_LATCHED;
        decision.authority_selected = 1U;
        decision.run_loader_dispatch = 1U;
    } else if (dluma_game_frame_authority_identity_latched(state)) {
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH;
        decision.run_loader_dispatch = 1U;
    } else if (state->pending_post_sequence) {
        decision.result =
            state->game_frame_playtest_level_configured
                ? DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_PENDING
                : DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_PENDING;
    } else {
        decision.result =
            DLUMA_GAME_FRAME_AUTHORITY_RESULT_DISCOVERY;
    }
    decision.serialization_token =
        state->exact_route_serialization_token;
    aggregate->authority_result = decision.result;
    if (decision.run_loader_dispatch)
        aggregate->dispatch_count =
            dluma_game_frame_increment_u64(
                aggregate->dispatch_count);
    {
        DlumaGameFrameCallbackTrace* trace =
            dluma_game_frame_append_trace(
                state, observation, decision.result);
        if (trace) {
            trace->previous_callback_thread_id =
                state->previous_callback_thread_id;
            trace->scheduler_migration_count =
                state->scheduler_migration_count;
            trace->pump_ran =
                decision.run_loader_dispatch ? 1U : 0U;
        }
    }
    return decision;
}

void dluma_game_frame_authority_note_post_original(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG sequence,
    LONG thread_id,
    ULONG_PTR game_subobject,
    ULONGLONG original_entry_qpc,
    ULONGLONG original_return_qpc,
    const DlumaGameFrameFrontendProbe* post_probe)
{
    DlumaGameFrameCallbackTrace* trace;
    DlumaGameFrameThreadAggregate* aggregate;
    if (!state || !sequence || !thread_id || !game_subobject)
        return;
    trace = dluma_game_frame_find_trace(
        state, sequence, thread_id);
    if (trace) {
        trace->original_entry_qpc = original_entry_qpc;
        trace->original_return_qpc = original_return_qpc;
        trace->original_callback_invoked =
            original_entry_qpc ? 1U : 0U;
        trace->original_callback_returned =
            original_return_qpc ? 1U : 0U;
        if (post_probe)
            trace->post_original_probe = *post_probe;
    }
    aggregate = dluma_game_frame_find_thread(state, thread_id);
    if (aggregate && post_probe)
        dluma_game_frame_count_probe(aggregate, post_probe, 1);
    if (!post_probe || state->game_frame_authority_conflict ||
        state->exact_route_game_subobject != game_subobject ||
        !original_entry_qpc || !original_return_qpc ||
        !dluma_game_frame_startup_probe_valid(state, post_probe))
        return;
    if (state->game_frame_standalone_start_latched) return;
    if (state->game_frame_standalone_start_configured &&
        !post_probe->frontend_identity_valid &&
        (!trace || !dluma_game_frame_probe_matches(
            state, &trace->pre_original_probe, post_probe))) {
        dluma_game_frame_clear_pending_frontend(state);
        return;
    }
    if (dluma_game_frame_authority_identity_latched(state)) {
        if (dluma_game_frame_probe_matches_latched(
                state, post_probe)) {
            state->latched_frontend_observed_ms =
                post_probe->monotonic_ms;
            dluma_game_frame_clear_pending_frontend(state);
        } else {
            /*
             * Do not replace a proven pointer on one observation.  The next
             * callback must confirm this exact pointer/vtable/name tuple.
             */
            state->pending_post_frontend = *post_probe;
            state->pending_post_sequence = sequence;
        }
        return;
    }
    state->pending_post_frontend = *post_probe;
    state->pending_post_sequence = sequence;
    if (trace)
        trace->authority_result =
            state->game_frame_playtest_level_configured
                ? DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_PENDING
                : DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_PENDING;
}

void dluma_game_frame_authority_note_callback_return(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG sequence,
    LONG thread_id,
    ULONGLONG callback_return_qpc)
{
    DlumaGameFrameCallbackTrace* trace;
    if (!state || !sequence || !thread_id) return;
    trace = dluma_game_frame_find_trace(
        state, sequence, thread_id);
    if (trace) {
        trace->callback_return_qpc = callback_return_qpc;
        if (callback_return_qpc >= trace->callback_entry_qpc)
            trace->callback_duration_qpc =
                callback_return_qpc -
                trace->callback_entry_qpc;
    }
    if (state->current_dispatch_thread_id == thread_id)
        state->current_dispatch_thread_id = 0;
}

void dluma_game_frame_authority_note_message_timer_probe(
    DlumaGameFrameAuthorityState* state,
    LONG thread_id,
    const DlumaGameFrameFrontendProbe* probe)
{
    if (!state || !thread_id || !probe) return;
    dluma_game_frame_authority_expire_frontend_evidence(
        state, probe->monotonic_ms);
    if (!state->game_frame_message_timer_thread_id)
        state->game_frame_message_timer_thread_id = thread_id;
    else if (state->game_frame_message_timer_thread_id != thread_id) {
        state->game_frame_window_timer_owner_conflict = 1;
        return;
    }
    state->window_timer_probe_count =
        dluma_game_frame_increment_u64(
            state->window_timer_probe_count);
    if (probe->result == DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS)
        state->window_timer_query_success_count =
            dluma_game_frame_increment_u64(
                state->window_timer_query_success_count);
    else if (probe->result == DLUMA_GAME_FRAME_LEVEL_QUERY_NULL)
        state->window_timer_query_null_count =
            dluma_game_frame_increment_u64(
                state->window_timer_query_null_count);
    else if (probe->result == DLUMA_GAME_FRAME_LEVEL_QUERY_INVALID)
        state->window_timer_query_invalid_count =
            dluma_game_frame_increment_u64(
                state->window_timer_query_invalid_count);
    state->last_window_timer_probe = *probe;
}

DlumaGameFrameAuthorityStatus dluma_game_frame_authority_status(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG now_ms)
{
    unsigned int index;
    if (!state)
        return DLUMA_GAME_FRAME_AUTHORITY_STATUS_CONFLICT;
    dluma_game_frame_authority_expire_frontend_evidence(
        state, now_ms);
    if (dluma_game_frame_authority_identity_latched(state))
        return state->game_frame_authority_conflict
            ? DLUMA_GAME_FRAME_AUTHORITY_STATUS_OWNERSHIP_LOST
            : DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE;
    if (state->game_frame_authority_timed_out)
        return DLUMA_GAME_FRAME_AUTHORITY_STATUS_TIMEOUT;
    if (state->game_frame_authority_conflict)
        return DLUMA_GAME_FRAME_AUTHORITY_STATUS_CONFLICT;
    if (!state->game_frame_authority_window_armed)
        return DLUMA_GAME_FRAME_AUTHORITY_STATUS_PENDING;
    if (state->game_frame_authority_deadline_ms &&
        now_ms >= state->game_frame_authority_deadline_ms) {
        state->game_frame_authority_timed_out = 1;
        dluma_game_frame_set_conflict(
            state,
            DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_SELECTION_TIMEOUT);
        for (index = 0; index < state->thread_count; ++index)
            state->threads[index].authority_result =
                DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_TIMEOUT;
        return DLUMA_GAME_FRAME_AUTHORITY_STATUS_TIMEOUT;
    }
    return DLUMA_GAME_FRAME_AUTHORITY_STATUS_PENDING;
}

void dluma_game_frame_authority_mark_loader_dispatch_started(
    DlumaGameFrameAuthorityState* state,
    ULONGLONG serialization_token,
    LONG current_dispatch_thread_id)
{
    if (!state || state->game_frame_authority_conflict ||
        !dluma_game_frame_authority_identity_latched(state) ||
        serialization_token !=
            state->exact_route_serialization_token ||
        current_dispatch_thread_id !=
            state->current_dispatch_thread_id)
        return;
    state->game_frame_loader_dispatch_started = 1;
}

void dluma_game_frame_authority_mark_project_admitted(
    DlumaGameFrameAuthorityState* state)
{
    if (state && dluma_game_frame_authority_identity_latched(state) &&
        !state->game_frame_authority_conflict)
        state->game_frame_project_admitted = 1;
}

void dluma_game_frame_authority_mark_registry_mutation_started(
    DlumaGameFrameAuthorityState* state)
{
    if (state && state->game_frame_project_admitted &&
        !state->game_frame_authority_conflict)
        state->game_frame_registry_mutation_started = 1;
}

int dluma_game_frame_authority_can_finalize(
    const DlumaGameFrameAuthorityState* state,
    LONG current_thread_id,
    unsigned int inside_serialized_route,
    LONG validated_window_owner_thread_id)
{
    if (!state || !current_thread_id ||
        state->game_frame_authority_conflict)
        return 0;
    if (inside_serialized_route &&
        dluma_game_frame_authority_identity_latched(state) &&
        state->game_frame_authority_frozen &&
        state->exact_route_serialization_token &&
        state->current_dispatch_thread_id == current_thread_id)
        return 1;
    return validated_window_owner_thread_id &&
           !state->game_frame_window_timer_owner_conflict &&
           state->game_frame_message_timer_thread_id ==
               validated_window_owner_thread_id &&
           current_thread_id == validated_window_owner_thread_id;
}

const char* dluma_game_frame_authority_conflict_reason_name(
    DlumaGameFrameAuthorityConflictReason reason)
{
    switch (reason) {
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_NONE:
        return "none";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK:
        return "concurrent_frame_callback";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_RECURSIVE_CALLBACK:
        return "recursive_frame_callback";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_DIAGNOSTIC_CAPACITY_EXHAUSTED:
        return "thread_diagnostic_capacity_exhausted";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_GAME_SUBOBJECT_CHANGED:
        return "game_frame_subobject_changed";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_PRIMARY_OBJECT_CHANGED:
        return "game_di_primary_object_changed";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CALLER_ROUTE_CHANGED:
        return "caller_route_changed";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST:
        return "hook_route_identity_lost";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_FRONTEND_IDENTITY_CHANGED:
        return "frontend_identity_changed";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_WINDOW_TIMER_OWNER_CHANGED:
        return "window_timer_owner_changed";
    case DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_SELECTION_TIMEOUT:
        return "authority_selection_timeout";
    default:
        return "unknown";
    }
}

const char* dluma_game_frame_authority_result_name(
    DlumaGameFrameAuthorityResult result)
{
    switch (result) {
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_UNSEEN:
        return "unseen";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_DISCOVERY:
        return "read_only_discovery";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_PENDING:
        return "post_original_frontend_pending";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_LATCHED:
        return "frontend_route_latched";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH:
        return "serialized_route_dispatch";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_INVALID_ROUTE:
        return "rejected_invalid_route";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CONCURRENT:
        return "rejected_concurrent_callback";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_RECURSIVE:
        return "rejected_recursive_callback";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_OBJECT_CHANGE:
        return "rejected_object_change";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CALLER_CHANGE:
        return "rejected_caller_change";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_CONFLICT:
        return "authority_conflict";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_AUTHORITY_TIMEOUT:
        return "authority_timeout";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_PENDING:
        return "playtest_handoff_pending";
    case DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_LATCHED:
        return "playtest_handoff_latched";
    default:
        return "unknown";
    }
}

const char* dluma_game_frame_authority_status_name(
    DlumaGameFrameAuthorityStatus status)
{
    switch (status) {
    case DLUMA_GAME_FRAME_AUTHORITY_STATUS_PENDING:
        return "pending";
    case DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE:
        return "authoritative";
    case DLUMA_GAME_FRAME_AUTHORITY_STATUS_CONFLICT:
        return "conflict";
    case DLUMA_GAME_FRAME_AUTHORITY_STATUS_TIMEOUT:
        return "timeout";
    case DLUMA_GAME_FRAME_AUTHORITY_STATUS_OWNERSHIP_LOST:
        return "ownership_lost";
    default:
        return "unknown";
    }
}

const char* dluma_game_frame_level_query_diagnostic_name(
    DlumaGameFrameLevelQueryDiagnostic result)
{
    switch (result) {
    case DLUMA_GAME_FRAME_LEVEL_QUERY_NOT_ATTEMPTED:
        return "not_attempted";
    case DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS:
        return "success";
    case DLUMA_GAME_FRAME_LEVEL_QUERY_NULL:
        return "null";
    case DLUMA_GAME_FRAME_LEVEL_QUERY_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}
