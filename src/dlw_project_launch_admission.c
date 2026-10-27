#include "dlw_project_launch_admission.h"
#include "dlw_project_root_identity.h"

static ULONGLONG dluma_project_increment_u64(ULONGLONG value)
{
    return value == ~(ULONGLONG)0 ? value : value + 1U;
}

static void dluma_project_copy_text(
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

static char dluma_project_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value - 'A' + 'a') : value;
}

static int dluma_project_text_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        if (dluma_project_ascii_lower(left[index]) !=
            dluma_project_ascii_lower(right[index]))
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_project_identity_canonical_copy(
    DlumaProjectLaunchIdentity* output,
    const DlumaProjectLaunchIdentity* input)
{
    char display_canonical[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    if (!output || !input) return 0;
    *output = *input;
    if (!input->normalized_project_root[0])
        return !input->display_project_root[0];
    if (!dluma_project_root_canonicalize(
            input->normalized_project_root,
            output->normalized_project_root,
            (unsigned int)sizeof(
                output->normalized_project_root)))
        return 0;
    if (input->display_project_root[0]) {
        if (!dluma_project_root_canonicalize(
                input->display_project_root,
                display_canonical,
                (unsigned int)sizeof(display_canonical)) ||
            !dluma_project_root_identity_equal(
                display_canonical,
                output->normalized_project_root))
            return 0;
    } else {
        dluma_project_copy_text(
            output->display_project_root,
            (unsigned int)sizeof(
                output->display_project_root),
            input->normalized_project_root);
    }
    return 1;
}

static int dluma_project_identity_present(
    const DlumaProjectLaunchIdentity* identity)
{
    return identity &&
           (identity->selection_key[0] ||
            identity->normalized_project_root[0] ||
            identity->workshop_id[0] ||
            (identity->transition_identity_complete &&
             identity->campaign_name[0] &&
             identity->map_name[0]));
}

static int dluma_project_evidence_exact_for_premount(
    const DlumaProjectPreMountEvidence* evidence)
{
    const DlumaProjectLaunchIdentity* identity;
    if (!evidence) return 0;
    identity = &evidence->identity;
    if (!identity ||
        !identity->normalized_project_root[0] ||
        (!identity->selection_key[0] &&
         !identity->workshop_id[0]))
        return 0;
    if (evidence->owner ==
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP)
        return evidence->custom_content_name[0] &&
            evidence->custom_content_identity_corroborated &&
            evidence->map_source == 0;
    return identity->transition_identity_complete &&
        identity->campaign_name[0] &&
        identity->map_name[0];
}

/*
 * A request may carry only the Workshop ID while the selected-map boundary
 * adds the canonical project root and campaign tuple.  Every field already
 * present in the request must match; added exact evidence is allowed.
 */
static int dluma_project_identity_matches_request(
    const DlumaProjectLaunchIdentity* request,
    const DlumaProjectLaunchIdentity* selected)
{
    int stable_match = 0;
    if (!dluma_project_identity_present(request) ||
        !dluma_project_identity_present(selected))
        return 0;
    if (request->normalized_project_root[0]) {
        if (!selected->normalized_project_root[0] ||
            !dluma_project_root_identity_equal(
                request->normalized_project_root,
                selected->normalized_project_root))
            return 0;
        stable_match = 1;
    }
    if (request->workshop_id[0]) {
        if (!selected->workshop_id[0] ||
            !dluma_project_text_equal(
                request->workshop_id,
                selected->workshop_id))
            return 0;
        stable_match = 1;
    }
    if (request->selection_key[0]) {
        if (!selected->selection_key[0] ||
            !dluma_project_text_equal(
                request->selection_key,
                selected->selection_key))
            return 0;
        stable_match = 1;
    }
    if (request->transition_identity_complete) {
        if (!selected->transition_identity_complete ||
            request->profile_index != selected->profile_index ||
            !dluma_project_text_equal(
                request->campaign_name,
                selected->campaign_name) ||
            !dluma_project_text_equal(
                request->map_name,
                selected->map_name))
            return 0;
        stable_match = 1;
    }
    return stable_match;
}

static int dluma_project_identity_matches_attestation(
    const DlumaProjectLaunchIdentity* expected,
    const DlumaProjectLaunchIdentity* observed)
{
    if (!expected || !observed ||
        !expected->normalized_project_root[0] ||
        !observed->campaign_name[0] ||
        !observed->map_name[0])
        return 0;
    if (expected->campaign_name[0] &&
        !dluma_project_text_equal(
            expected->campaign_name, observed->campaign_name))
        return 0;
    if (expected->map_name[0] &&
        !dluma_project_text_equal(
            expected->map_name, observed->map_name))
        return 0;
    if (expected->transition_identity_complete &&
        expected->profile_index != observed->profile_index)
        return 0;
    return 1;
}

static int dluma_project_identity_equal(
    const DlumaProjectLaunchIdentity* left,
    const DlumaProjectLaunchIdentity* right)
{
    int project_roots_equal;
    if (!left || !right) return 0;
    project_roots_equal =
        !left->normalized_project_root[0] &&
        !right->normalized_project_root[0];
    if (!project_roots_equal &&
        left->normalized_project_root[0] &&
        right->normalized_project_root[0])
        project_roots_equal = dluma_project_root_identity_equal(
            left->normalized_project_root,
            right->normalized_project_root);
    return left && right &&
        left->profile_index == right->profile_index &&
        left->transition_identity_complete ==
            right->transition_identity_complete &&
        dluma_project_text_equal(
            left->selection_key, right->selection_key) &&
        dluma_project_text_equal(
            left->campaign_name, right->campaign_name) &&
        dluma_project_text_equal(left->map_name, right->map_name) &&
        project_roots_equal &&
        dluma_project_text_equal(
            left->workshop_id, right->workshop_id);
}

static int dluma_project_delivery_within_debounce(
    ULONGLONG previous_ms, ULONGLONG now_ms)
{
    return now_ms >= previous_ms &&
        now_ms - previous_ms <=
            DLUMA_PROJECT_DENIED_ACTIVATION_DEBOUNCE_MS;
}

static DlumaProjectLaunchAdmissionResult
dluma_project_b0_premount_result(
    const DlumaProjectPreMountEvidence* evidence)
{
    if (!evidence || !evidence->content_isolation_enabled)
        return DLUMA_PROJECT_LAUNCH_ADMISSION_NOT_ATTEMPTED;
    if (!evidence->startup_b0_complete_authorized) {
        if (evidence->startup_b0_permanent_source_failure)
            return
                DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PERMANENT_SOURCE_FAILURE;
        if (dluma_project_text_equal(
                evidence->b0_state, "PermanentFailure") ||
            dluma_project_text_equal(
                evidence->b0_state, "TrackingUnavailable") ||
            dluma_project_text_equal(
                evidence->b0_state,
                "MissedBeforeFirstCustomEpoch"))
            return
                DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED;
        return DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING;
    }
    if (!evidence->restart_registry_can_admit)
        return DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED;
    return DLUMA_PROJECT_LAUNCH_ADMISSION_NOT_ATTEMPTED;
}

static void dluma_project_record_first_failure(
    DlumaProjectLaunchAdmissionState* state,
    const char* reason)
{
    if (state && !state->first_failure_reason[0])
        dluma_project_copy_text(
            state->first_failure_reason,
            (unsigned int)sizeof(state->first_failure_reason),
            reason ? reason : "unknown");
}

static void dluma_project_clear_active_authority(
    DlumaProjectLaunchAdmissionState* state)
{
    if (!state) return;
    state->token_active = 0U;
    state->selected_map_body_active = 0U;
    state->update_dlc_content_active = 0U;
}

static void dluma_project_fail_token(
    DlumaProjectLaunchAdmissionState* state,
    const char* reason)
{
    if (!state) return;
    dluma_project_clear_active_authority(state);
    state->attestation_pending = 0U;
    state->token_state = DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED;
    dluma_project_record_first_failure(state, reason);
}

static int dluma_project_expire_active_if_needed(
    DlumaProjectLaunchAdmissionState* state,
    ULONGLONG now_ms)
{
    if (!state || !state->token_active)
        return 0;
    if (now_ms >= state->token_created_ms &&
        now_ms - state->token_created_ms <=
            DLUMA_PROJECT_PREMOUNT_TOKEN_MAX_AGE_MS)
        return 0;
    dluma_project_clear_active_authority(state);
    state->attestation_pending = 0U;
    state->token_state = DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
    dluma_project_record_first_failure(
        state, "premount_token_timeout");
    return 1;
}

static int dluma_project_new_premount_evidence_admissible(
    const DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectPreMountEvidence* evidence,
    const char** failure)
{
    if (failure) *failure = "cancelled_cleanup_new_request_invalid";
    if (!state || !evidence || !state->request_valid)
        return 0;
    if (evidence->owner !=
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP ||
        !evidence->entry_rva || !evidence->thread_id ||
        !evidence->controller || !evidence->controller_vtable ||
        !evidence->payload || !evidence->custom_map_source ||
        !dluma_project_evidence_exact_for_premount(evidence))
        return 0;
    if (evidence->now_ms < state->request_created_ms ||
        evidence->now_ms - state->request_created_ms >
            DLUMA_PROJECT_LAUNCH_REQUEST_MAX_AGE_MS) {
        if (failure) *failure = "cancelled_cleanup_new_request_stale";
        return 0;
    }
    if (!dluma_project_identity_matches_request(
            &state->request_identity, &evidence->identity)) {
        if (failure)
            *failure = "cancelled_cleanup_new_request_identity_mismatch";
        return 0;
    }
    if (evidence->authority_conflict) {
        if (failure)
            *failure = "cancelled_cleanup_frame_authority_conflict";
        return 0;
    }
    if (!evidence->startup_b0_complete_authorized ||
        !evidence->b0_generation || !evidence->frame_route_token) {
        if (failure) *failure = "cancelled_cleanup_b0_or_route_unavailable";
        return 0;
    }
    if (!evidence->restart_registry_can_admit ||
        state->admission_blocked) {
        if (failure) *failure = "cancelled_cleanup_restart_required";
        return 0;
    }
    return 1;
}

static void dluma_project_record_bypass(
    DlumaProjectLaunchAdmissionState* state,
    const char* normalized_project_root,
    const DlumaProjectSourceBypassEvidence* evidence,
    const char* reason)
{
    if (!state) return;
    state->custom_project_mount_bypass_count =
        dluma_project_increment_u64(
            state->custom_project_mount_bypass_count);
    if (!state->custom_project_mount_bypassed_admission_gate) {
        state->custom_project_mount_bypassed_admission_gate = 1U;
        dluma_project_copy_text(
            state->bypass_project_root,
            (unsigned int)sizeof(state->bypass_project_root),
            normalized_project_root ? normalized_project_root : "");
        if (evidence) {
            state->bypass_evidence = *evidence;
            state->bypass_evidence_valid = 1U;
        }
    }
    dluma_project_record_first_failure(state, reason);
}

static DWORD dluma_project_expected_update_return_rva(
    DlumaProjectLaunchOwner owner)
{
    switch (owner) {
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME:
        return 0x007DA1CAUL;
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI:
        return 0x0091A2E8UL;
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP:
        return 0x0091C713UL;
    default:
        return 0U;
    }
}

static DlumaProjectPreMountHit* dluma_project_owner_hit(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner)
{
    if (!state ||
        owner <= DLUMA_PROJECT_LAUNCH_OWNER_NONE ||
        owner >= DLUMA_PROJECT_LAUNCH_OWNER_COUNT)
        return 0;
    return &state->premount_owner_hits[
        (unsigned int)owner];
}

void dluma_project_launch_admission_initialize(
    DlumaProjectLaunchAdmissionState* state)
{
    if (state) memset(state, 0, sizeof(*state));
}

int dluma_project_launch_admission_note_activation(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchRoute route,
    const DlumaProjectLaunchIdentity* identity,
    ULONGLONG now_ms,
    DlumaProjectLaunchAdmissionResult preflight_result)
{
    DlumaProjectLaunchIdentity canonical_identity;
    if (!identity ||
        !dluma_project_identity_canonical_copy(
            &canonical_identity, identity))
        return 0;
    identity = &canonical_identity;
    if (!state || state->admission_blocked ||
        route == DLUMA_PROJECT_LAUNCH_ROUTE_NONE ||
        !dluma_project_identity_present(identity))
        return 0;
    if (preflight_result !=
            DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED &&
        state->last_activation_delivery_valid &&
        state->last_activation_delivery_route == route &&
        state->last_activation_delivery_result ==
            preflight_result &&
        dluma_project_identity_equal(
            &state->last_activation_delivery_identity,
            identity) &&
        dluma_project_delivery_within_debounce(
            state->last_activation_delivery_ms, now_ms)) {
        state->last_activation_delivery_debounced = 1U;
        state->denied_activation_duplicate_count =
            dluma_project_increment_u64(
                state->denied_activation_duplicate_count);
        return 0;
    }
    state->last_activation_delivery_valid = 1U;
    state->last_activation_delivery_debounced = 0U;
    state->last_activation_delivery_ms = now_ms;
    state->last_activation_delivery_route = route;
    state->last_activation_delivery_result = preflight_result;
    state->last_activation_delivery_identity = *identity;
    state->activation_attempt_count =
        dluma_project_increment_u64(
            state->activation_attempt_count);
    state->last_activation_route = route;
    state->last_selected_identity = *identity;
    state->last_admission_result = preflight_result;
    state->last_request_consumed =
        preflight_result == DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED
            ? 0U : 1U;
    state->last_transition_queued = 0U;
    if (preflight_result !=
            DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED) {
        state->request_valid = 0U;
        memset(
            &state->request_identity, 0,
            sizeof(state->request_identity));
        return 0;
    }
    state->generation =
        dluma_project_increment_u64(state->generation);
    if (!state->generation) state->generation = 1U;
    state->request_valid = 1U;
    state->request_generation = state->generation;
    state->request_created_ms = now_ms;
    state->request_route = route;
    state->request_identity = *identity;
    return 1;
}

DlumaProjectCancelledCleanupPreparation
dluma_project_launch_admission_prepare_cancelled_cleanup(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectPreMountEvidence* evidence,
    DlumaProjectCancelledCleanupPlan* plan)
{
    DlumaProjectPreMountEvidence canonical_evidence;
    const char* failure = "cancelled_cleanup_state_mismatch";
    if (plan) memset(plan, 0, sizeof(*plan));
    if (!state || !evidence || !plan)
        return DLUMA_PROJECT_CANCELLED_CLEANUP_REJECTED;
    canonical_evidence = *evidence;
    if (!dluma_project_identity_canonical_copy(
            &canonical_evidence.identity,
            &evidence->identity))
        return DLUMA_PROJECT_CANCELLED_CLEANUP_REJECTED;
    evidence = &canonical_evidence;
    if (!state->attestation_pending)
        return DLUMA_PROJECT_CANCELLED_CLEANUP_NOT_REQUIRED;

    state->custom_project_cancelled_cleanup_attempt_count =
        dluma_project_increment_u64(
            state->custom_project_cancelled_cleanup_attempt_count);
    plan->prior_token_generation = state->token_generation;
    plan->prior_token_created_ms = state->token_created_ms;
    plan->prior_epoch = state->token_epoch;
    plan->fresh_request_generation = state->request_generation;
    plan->fresh_request_created_ms = state->request_created_ms;
    plan->owner_thread_id = evidence->thread_id;
    plan->controller = evidence->controller;
    dluma_project_copy_text(
        plan->prior_normalized_project_root,
        (unsigned int)sizeof(plan->prior_normalized_project_root),
        state->token_identity.normalized_project_root);
    dluma_project_copy_text(
        plan->fresh_normalized_project_root,
        (unsigned int)sizeof(plan->fresh_normalized_project_root),
        evidence->identity.normalized_project_root);

    if (!dluma_project_new_premount_evidence_admissible(
            state, evidence, &failure))
        goto rejected;
    if (state->token_active) {
        failure = "cancelled_cleanup_prior_token_still_active";
        goto rejected;
    }
    if (state->selected_map_body_active) {
        failure = "cancelled_cleanup_selected_map_body_active";
        goto rejected;
    }
    if (state->update_dlc_content_active) {
        failure = "cancelled_cleanup_update_dlc_content_active";
        goto rejected;
    }
    if (state->transition_started || state->project_admitted) {
        failure = "cancelled_cleanup_transition_already_started";
        goto rejected;
    }
    if (state->token_state !=
            DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE ||
        state->token_owner !=
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP ||
        !state->token_project_root_observed ||
        !state->token_data_directory_observed ||
        !state->token_assets_pc_observed ||
        !state->token_epoch ||
        !state->token_identity.normalized_project_root[0]) {
        failure = "cancelled_cleanup_prior_source_set_incomplete";
        goto rejected;
    }
    if (state->request_generation <= state->token_generation ||
        state->request_created_ms < state->token_created_ms) {
        failure = "cancelled_cleanup_request_not_fresh";
        goto rejected;
    }
    return DLUMA_PROJECT_CANCELLED_CLEANUP_READY;

rejected:
    dluma_project_copy_text(
        plan->first_failure,
        (unsigned int)sizeof(plan->first_failure), failure);
    return DLUMA_PROJECT_CANCELLED_CLEANUP_REJECTED;
}

int dluma_project_launch_admission_note_cancelled_cleanup_result(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectCancelledCleanupPlan* plan,
    const DlumaProjectCancelledCleanupResult* result)
{
    int state_matches;
    if (!state || !plan || !result) return 0;
    state->custom_project_cancelled_cleanup_prior_token_generation =
        plan->prior_token_generation;
    state->custom_project_cancelled_cleanup_prior_epoch =
        plan->prior_epoch;
    state->custom_project_cancelled_cleanup_active_source_count_before =
        result->active_source_count_before;
    state->custom_project_cancelled_cleanup_removed_source_count =
        result->removed_source_count;
    state->custom_project_cancelled_cleanup_active_source_count_after =
        result->active_source_count_after;
    state_matches =
        state->attestation_pending &&
        !state->token_active &&
        !state->selected_map_body_active &&
        !state->update_dlc_content_active &&
        !state->transition_started &&
        state->token_generation == plan->prior_token_generation &&
        state->token_created_ms == plan->prior_token_created_ms &&
        state->token_epoch == plan->prior_epoch &&
        state->request_valid &&
        state->request_generation == plan->fresh_request_generation &&
        state->request_created_ms == plan->fresh_request_created_ms &&
        dluma_project_root_identity_equal(
            state->token_identity.normalized_project_root,
            plan->prior_normalized_project_root) &&
        dluma_project_root_identity_equal(
            state->request_identity.normalized_project_root,
            plan->fresh_normalized_project_root);
    if (state_matches && result->succeeded &&
        result->prior_epoch_consumed &&
        result->live_registry_verified_absent &&
        result->loader_sources_unchanged &&
        result->replacement_source_absent &&
        result->active_source_count_after == 0U) {
        dluma_project_clear_active_authority(state);
        state->attestation_pending = 0U;
        state->token_state = DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
        state->token_epoch = 0U;
        state->token_project_root_observed = 0U;
        state->token_data_directory_observed = 0U;
        state->token_assets_pc_observed = 0U;
        state->set_custom_content_generation = 0U;
        state->update_dlc_content_generation = 0U;
        memset(
            &state->token_identity, 0,
            sizeof(state->token_identity));
        state->custom_project_attestation_superseded_count =
            dluma_project_increment_u64(
                state->custom_project_attestation_superseded_count);
        state->custom_project_cancelled_cleanup_success_count =
            dluma_project_increment_u64(
                state->custom_project_cancelled_cleanup_success_count);
        state->custom_project_cancelled_cleanup_first_failure[0] = 0;
        return 1;
    }

    state->custom_project_cancelled_cleanup_failure_count =
        dluma_project_increment_u64(
            state->custom_project_cancelled_cleanup_failure_count);
    state->admission_blocked = 1U;
    state->request_valid = 0U;
    memset(&state->request_identity, 0,
           sizeof(state->request_identity));
    dluma_project_copy_text(
        state->custom_project_cancelled_cleanup_first_failure,
        (unsigned int)sizeof(
            state->custom_project_cancelled_cleanup_first_failure),
        !state_matches
            ? "cancelled_cleanup_state_changed"
            : result->first_failure[0]
                ? result->first_failure
                : plan->first_failure[0]
                    ? plan->first_failure
                    : "cancelled_cleanup_verification_failed");
    dluma_project_record_first_failure(
        state,
        state->custom_project_cancelled_cleanup_first_failure);
    return 0;
}

DlumaProjectLaunchAdmissionResult
dluma_project_launch_admission_begin_premount(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectPreMountEvidence* evidence)
{
    DlumaProjectPreMountEvidence canonical_evidence;
    DlumaProjectLaunchAdmissionResult result;
    DlumaProjectLaunchAdmissionResult b0_result;
    DlumaProjectLaunchRoute route;
    DlumaProjectPreMountHit* owner_hit = 0;
    if (!state || !evidence)
        return DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH;
    canonical_evidence = *evidence;
    if (!dluma_project_identity_canonical_copy(
            &canonical_evidence.identity,
            &evidence->identity))
        return DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH;
    evidence = &canonical_evidence;
    (void)dluma_project_expire_active_if_needed(
        state, evidence->now_ms);
    b0_result = dluma_project_b0_premount_result(evidence);
    if (b0_result !=
            DLUMA_PROJECT_LAUNCH_ADMISSION_NOT_ATTEMPTED &&
        state->last_premount_denial_valid &&
        state->last_premount_denial_controller ==
            evidence->controller &&
        state->last_premount_denial_payload ==
            evidence->payload &&
        state->last_premount_denial_result == b0_result &&
        dluma_project_identity_equal(
            &state->last_premount_denial_identity,
            &evidence->identity) &&
        dluma_project_delivery_within_debounce(
            state->last_premount_denial_ms,
            evidence->now_ms)) {
        state->last_premount_delivery_debounced = 1U;
        state->denied_premount_duplicate_count =
            dluma_project_increment_u64(
                state->denied_premount_duplicate_count);
        return b0_result;
    }
    state->last_premount_delivery_debounced = 0U;
    state->custom_project_premount_attempt_count =
        dluma_project_increment_u64(
            state->custom_project_premount_attempt_count);
    state->last_premount_route = evidence->route;
    state->last_premount_owner = evidence->owner;
    state->last_premount_entry_rva = evidence->entry_rva;
    state->last_selected_identity = evidence->identity;
    dluma_project_copy_text(
        state->last_b0_state,
        (unsigned int)sizeof(state->last_b0_state),
        evidence->b0_state[0]
            ? evidence->b0_state : "unavailable");
    state->last_request_consumed = 1U;
    state->last_original_selected_map_invoked = 0U;
    state->last_original_selected_map_returned = 0U;
    state->last_transition_queued = 0U;
    state->last_premount_hit.evidence = *evidence;
    state->last_premount_hit.set_custom_content_name_reached = 0U;
    state->last_premount_hit.update_dlc_content_reached = 0U;
    state->last_premount_hit.original_invoked = 0U;
    state->last_premount_hit.original_returned = 0U;
    owner_hit = dluma_project_owner_hit(
        state, evidence->owner);
    if (owner_hit) {
        state->premount_owner_hit_count[
            (unsigned int)evidence->owner] =
            dluma_project_increment_u64(
                state->premount_owner_hit_count[
                    (unsigned int)evidence->owner]);
        *owner_hit = state->last_premount_hit;
    }
    route = evidence->route;

    if (b0_result !=
            DLUMA_PROJECT_LAUNCH_ADMISSION_NOT_ATTEMPTED) {
        result = b0_result;
        goto denied;
    }
    if (evidence->owner ==
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP &&
        !state->request_valid) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST;
        goto denied;
    }
    if (evidence->owner <= DLUMA_PROJECT_LAUNCH_OWNER_NONE ||
        evidence->owner >= DLUMA_PROJECT_LAUNCH_OWNER_COUNT ||
        !evidence->entry_rva || !evidence->thread_id ||
        !evidence->controller || !evidence->controller_vtable ||
        !evidence->payload || !evidence->custom_map_source ||
        !dluma_project_evidence_exact_for_premount(evidence)) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH;
        goto denied;
    }
    if (state->admission_blocked) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_BLOCKED;
        goto denied;
    }
    if (state->token_active) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN;
        goto denied;
    }
    if (state->request_valid) {
        route = state->request_route;
        state->last_premount_route = route;
        if (evidence->now_ms < state->request_created_ms ||
            evidence->now_ms - state->request_created_ms >
                DLUMA_PROJECT_LAUNCH_REQUEST_MAX_AGE_MS) {
            result = DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST;
            goto denied;
        }
        if (!dluma_project_identity_matches_request(
                &state->request_identity,
                &evidence->identity)) {
            result = DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH;
            goto denied;
        }
    }
    if (evidence->authority_conflict) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_BLOCKED;
        goto denied;
    }
    if (!evidence->startup_b0_complete_authorized ||
        !evidence->b0_generation ||
        !evidence->frame_route_token) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_DEFERRED;
        goto denied;
    }
    if (!evidence->restart_registry_can_admit) {
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_BLOCKED;
        goto denied;
    }
    if (state->attestation_pending) {
        /*
         * SourceSetComplete proves only that the three expected adds were
         * observed. The UI owner must complete the two-phase, live-registry
         * verified cancellation cleanup before this pending token can move.
         */
        result = DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN;
        goto denied;
    }

    state->generation =
        dluma_project_increment_u64(state->generation);
    if (!state->generation) state->generation = 1U;
    state->token_state =
        DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT;
    state->token_active = 1U;
    state->attestation_pending = 0U;
    state->token_generation = state->generation;
    state->token_created_ms = evidence->now_ms;
    state->token_route = route;
    state->token_owner = evidence->owner;
    state->token_entry_rva = evidence->entry_rva;
    state->token_thread_id = evidence->thread_id;
    state->token_controller_identity = evidence->controller;
    state->token_controller_vtable =
        evidence->controller_vtable;
    state->token_ui_data_identity = evidence->ui_data;
    state->token_payload_identity = evidence->payload;
    state->token_map_source = evidence->map_source;
    state->token_b0_generation = evidence->b0_generation;
    state->token_frame_route_token =
        evidence->frame_route_token;
    state->token_epoch = 0U;
    state->token_project_root_observed = 0U;
    state->token_data_directory_observed = 0U;
    state->token_assets_pc_observed = 0U;
    state->selected_map_body_active = 0U;
    state->update_dlc_content_active = 0U;
    state->transition_started = 0U;
    state->project_admitted = 0U;
    state->set_custom_content_generation = 0U;
    state->update_dlc_content_generation = 0U;
    state->token_identity = evidence->identity;
    state->request_valid = 0U;
    memset(
        &state->request_identity, 0,
        sizeof(state->request_identity));
    state->last_admission_result =
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED;
    state->last_premount_hit.admission_result =
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED;
    if (owner_hit)
        owner_hit->admission_result =
            DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED;
    state->custom_project_premount_allowed_count =
        dluma_project_increment_u64(
            state->custom_project_premount_allowed_count);
    return DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED;

denied:
    state->request_valid = 0U;
    memset(
        &state->request_identity, 0,
        sizeof(state->request_identity));
    state->last_admission_result = result;
    state->last_premount_hit.admission_result = result;
    if (owner_hit)
        owner_hit->admission_result = result;
    state->custom_project_premount_denied_count =
        dluma_project_increment_u64(
            state->custom_project_premount_denied_count);
    state->last_premount_denial_valid = 1U;
    state->last_premount_denial_ms = evidence->now_ms;
    state->last_premount_denial_controller =
        evidence->controller;
    state->last_premount_denial_payload = evidence->payload;
    state->last_premount_denial_result = result;
    state->last_premount_denial_identity =
        evidence->identity;
    dluma_project_record_first_failure(
        state,
        dluma_project_launch_admission_result_name(result));
    return result;
}

void dluma_project_launch_admission_note_selected_map_original(
    DlumaProjectLaunchAdmissionState* state,
    int entering)
{
    DlumaProjectPreMountHit* owner_hit;
    if (!state) return;
    owner_hit = dluma_project_owner_hit(
        state, state->last_premount_owner);
    if (entering) {
        state->last_original_selected_map_invoked = 1U;
        state->last_original_selected_map_returned = 0U;
        state->last_premount_hit.original_invoked = 1U;
        state->last_premount_hit.original_returned = 0U;
        if (owner_hit) {
            owner_hit->original_invoked = 1U;
            owner_hit->original_returned = 0U;
        }
        state->original_selected_map_invocation_count =
            dluma_project_increment_u64(
                state->original_selected_map_invocation_count);
        state->selected_map_body_active =
            state->token_active &&
            state->token_state ==
                DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT
            ? 1U : 0U;
        return;
    }
    state->last_original_selected_map_returned =
        state->last_original_selected_map_invoked ? 1U : 0U;
    state->last_premount_hit.original_returned =
        state->last_original_selected_map_returned;
    if (owner_hit)
        owner_hit->original_returned =
            state->last_original_selected_map_returned;
    if (state->last_original_selected_map_returned)
        state->original_selected_map_return_count =
            dluma_project_increment_u64(
                state->original_selected_map_return_count);
    state->selected_map_body_active = 0U;
    state->update_dlc_content_active = 0U;
    if (state->token_state ==
            DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE &&
        state->token_project_root_observed &&
        state->token_data_directory_observed &&
        state->token_assets_pc_observed &&
        state->token_epoch) {
        state->token_active = 0U;
        state->attestation_pending = 1U;
    } else if (state->token_active) {
        dluma_project_fail_token(
            state,
            "source_set_incomplete_on_selected_map_return");
    }
}

void dluma_project_launch_admission_note_transition_queued(
    DlumaProjectLaunchAdmissionState* state,
    int queued)
{
    if (state)
        state->last_transition_queued = queued ? 1U : 0U;
}

int dluma_project_launch_admission_note_update_dlc_content(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner,
    DWORD caller_return_rva,
    DWORD current_thread_id,
    int entering)
{
    if (!state ||
        caller_return_rva !=
            dluma_project_expected_update_return_rva(owner))
        return 0;
    if (!entering) {
        if (!state->update_dlc_content_active ||
            state->token_owner != owner ||
            state->token_thread_id != current_thread_id)
            return 0;
        state->update_dlc_content_active = 0U;
        return 1;
    }
    if (!state->token_active ||
        state->token_state !=
            DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT ||
        !state->selected_map_body_active ||
        state->token_owner != owner ||
        state->token_thread_id != current_thread_id ||
        state->update_dlc_content_active ||
        state->update_dlc_content_generation)
        return 0;
    state->custom_project_update_dlc_content_count =
        dluma_project_increment_u64(
            state->custom_project_update_dlc_content_count);
    state->set_custom_content_generation =
        state->token_generation;
    state->update_dlc_content_generation =
        state->token_generation;
    state->update_dlc_content_active = 1U;
    state->last_premount_hit.
        set_custom_content_name_reached = 1U;
    state->last_premount_hit.update_dlc_content_reached = 1U;
    {
        DlumaProjectPreMountHit* owner_hit =
            dluma_project_owner_hit(state, owner);
        if (owner_hit) {
            owner_hit->set_custom_content_name_reached = 1U;
            owner_hit->update_dlc_content_reached = 1U;
        }
    }
    return 1;
}

int dluma_project_launch_admission_note_custom_source(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectMountSourceKind source_kind,
    const char* normalized_project_root,
    unsigned int epoch,
    DWORD current_thread_id,
    ULONGLONG now_ms)
{
    return dluma_project_launch_admission_note_custom_source_ex(
        state, source_kind, normalized_project_root, epoch,
        current_thread_id, now_ms, 0);
}

int dluma_project_launch_admission_note_custom_source_ex(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectMountSourceKind source_kind,
    const char* normalized_project_root,
    unsigned int epoch,
    DWORD current_thread_id,
    ULONGLONG now_ms,
    const DlumaProjectSourceBypassEvidence* evidence)
{
    char canonical_project_root[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    const char* failure = "mount_without_active_premount_token";
    if (!state || !normalized_project_root ||
        !normalized_project_root[0] ||
        !dluma_project_root_canonicalize(
            normalized_project_root,
            canonical_project_root,
            (unsigned int)sizeof(canonical_project_root)))
        return 0;
    normalized_project_root = canonical_project_root;
    state->source_mount_observation_count =
        dluma_project_increment_u64(
            state->source_mount_observation_count);
    if (dluma_project_expire_active_if_needed(state, now_ms))
        failure = "source_after_premount_token_timeout";
    if (!state->token_active)
        goto bypass;
    if (!state->selected_map_body_active ||
        !state->update_dlc_content_active ||
        state->token_thread_id != current_thread_id) {
        failure = "source_outside_selected_map_update_thread";
        goto fail_and_bypass;
    }
    if (state->set_custom_content_generation !=
            state->token_generation ||
        state->update_dlc_content_generation !=
            state->token_generation) {
        failure = "source_without_matching_update_generation";
        goto fail_and_bypass;
    }
    if (!epoch) {
        failure = "source_epoch_missing";
        goto fail_and_bypass;
    }
    if (!dluma_project_root_identity_equal(
            state->token_identity.normalized_project_root,
            normalized_project_root)) {
        failure = "source_project_identity_mismatch";
        goto fail_and_bypass;
    }

    if (source_kind ==
            DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT) {
        if (state->token_state !=
                DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT ||
            state->token_project_root_observed ||
            state->token_epoch) {
            failure = "duplicate_or_out_of_order_project_root";
            goto fail_and_bypass;
        }
        if (state->custom_project_cancelled_cleanup_prior_epoch &&
            epoch ==
                state->custom_project_cancelled_cleanup_prior_epoch) {
            failure = "fresh_presentation_reused_cancelled_epoch";
            goto fail_and_bypass;
        }
        state->token_state =
            DLUMA_PROJECT_PREMOUNT_TOKEN_PROJECT_ROOT_OBSERVED;
        state->token_project_root_observed = 1U;
        state->token_epoch = epoch;
        state->token_state =
            DLUMA_PROJECT_PREMOUNT_TOKEN_EPOCH_BOUND;
        state->custom_project_fresh_epoch = epoch;
        state->custom_project_source_epoch_bound_count =
            dluma_project_increment_u64(
                state->custom_project_source_epoch_bound_count);
        return 1;
    }

    if (state->token_state !=
            DLUMA_PROJECT_PREMOUNT_TOKEN_EPOCH_BOUND ||
        !state->token_project_root_observed ||
        state->token_epoch != epoch) {
        failure = "source_before_project_root_or_epoch_mismatch";
        goto fail_and_bypass;
    }
    if (source_kind ==
            DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY) {
        if (state->token_data_directory_observed ||
            state->token_assets_pc_observed) {
            failure = "duplicate_or_out_of_order_data_directory";
            goto fail_and_bypass;
        }
        state->token_data_directory_observed = 1U;
        return 1;
    }
    if (source_kind == DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC) {
        if (!state->token_data_directory_observed ||
            state->token_assets_pc_observed) {
            failure = "assets_pc_before_data_or_duplicate";
            goto fail_and_bypass;
        }
        state->token_assets_pc_observed = 1U;
        state->token_state =
            DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE;
        return 1;
    }
    failure = "unrelated_custom_project_source";

fail_and_bypass:
    dluma_project_fail_token(state, failure);
bypass:
    dluma_project_record_bypass(
        state, normalized_project_root, evidence, failure);
    return 0;
}

int dluma_project_launch_admission_attest_change_after_fade(
    DlumaProjectLaunchAdmissionState* state,
    const DlumaProjectChangeAfterFadeAttestation* attestation)
{
    DlumaProjectChangeAfterFadeAttestation canonical_attestation;
    ULONGLONG prior_transition_generation;
    DWORD transition_thread_id;
    DlumaProjectSourceSetSnapshotResult snapshot_result;
    int root_valid;
    int source_set_duplicate;
    int exact_token_source_evidence;
    int snapshot_proves_exact_source_ownership;
    int transient_snapshot_unavailable;
    int exact_source_ownership;
    const char* failure =
        "change_after_fade_without_authorized_premount";
    if (!state || !attestation) return 0;
    canonical_attestation = *attestation;
    root_valid = dluma_project_root_canonicalize(
        attestation->normalized_project_root,
        canonical_attestation.normalized_project_root,
        (unsigned int)sizeof(
            canonical_attestation.normalized_project_root));
    if (!root_valid)
        canonical_attestation.normalized_project_root[0] = 0;
    snapshot_result =
        canonical_attestation.source_set_snapshot_result;
    if ((unsigned int)snapshot_result >
            DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_REQUIRED_SOURCE_KIND_MISSING)
        snapshot_result =
            DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_INVALID_ARGUMENT;
    canonical_attestation.source_set_snapshot_result =
        snapshot_result;
    attestation = &canonical_attestation;
    prior_transition_generation =
        state->custom_project_last_consumed_transition_generation;
    transition_thread_id = attestation->transition_thread_id
        ? attestation->transition_thread_id : attestation->thread_id;
    source_set_duplicate =
        snapshot_result ==
            DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_DUPLICATE_SOURCE_KIND ||
        attestation->project_root_source_count > 1U ||
        attestation->data_directory_source_count > 1U ||
        attestation->assets_pc_source_count > 1U;
    exact_token_source_evidence =
        state->attestation_pending &&
        !state->token_active &&
        state->token_owner ==
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP &&
        state->token_state ==
            DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE &&
        state->token_project_root_observed &&
        state->token_data_directory_observed &&
        state->token_assets_pc_observed &&
        state->token_epoch &&
        state->token_epoch == attestation->epoch &&
        root_valid &&
        attestation->normalized_project_root[0] &&
        dluma_project_root_identity_equal(
            state->token_identity.normalized_project_root,
            attestation->normalized_project_root);
    snapshot_proves_exact_source_ownership =
        snapshot_result ==
            DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_SUCCEEDED &&
        attestation->source_ownership_complete &&
        attestation->source_set_complete &&
        attestation->project_root_source_count == 1U &&
        attestation->data_directory_source_count == 1U &&
        attestation->assets_pc_source_count == 1U;
    transient_snapshot_unavailable =
        snapshot_result ==
            DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_LEDGER_BUSY;
    exact_source_ownership =
        exact_token_source_evidence &&
        (snapshot_proves_exact_source_ownership ||
         transient_snapshot_unavailable);
    state->custom_project_change_after_fade_attestation_count =
        dluma_project_increment_u64(
            state->custom_project_change_after_fade_attestation_count);
    state->custom_project_transition_attestation_owner =
        state->token_owner;
    dluma_project_copy_text(
        state->custom_project_transition_attestation_destination,
        (unsigned int)sizeof(
            state->custom_project_transition_attestation_destination),
        attestation->destination_map);
    state->custom_project_transition_attestation_generation =
        attestation->transition_generation;
    state->custom_project_transition_context_generation =
        attestation->transition_context_generation;
    state->custom_project_premount_frame_route_token =
        state->token_frame_route_token;
    state->custom_project_current_frame_route_token =
        attestation->current_frame_route_token;
    state->custom_project_transition_attestation_session =
        attestation->session;
    state->custom_project_premount_thread_id =
        state->token_thread_id;
    state->custom_project_transition_thread_id =
        transition_thread_id;
    state->custom_project_change_after_fade_entry_rva =
        attestation->exact_hook_entry_rva;
    state->custom_project_transition_context_depth =
        attestation->transition_context_depth;
    state->custom_project_transition_context_serialized =
        attestation->transition_context_serialized;
    state->custom_project_transition_thread_migrated =
        state->token_thread_id && transition_thread_id &&
        state->token_thread_id != transition_thread_id;
    state->custom_project_source_set_snapshot_result =
        snapshot_result;
    state->custom_project_change_after_fade_disposition =
        DLUMA_PROJECT_CHANGE_AFTER_FADE_NOT_ATTESTED;
    state->custom_project_transition_attestation_failure_reason[0] = 0;

    if (state->admission_blocked)
        failure = "change_after_fade_admission_blocked";
    else if (!state->attestation_pending || state->token_active)
        failure = "change_after_fade_without_pending_token";
    else if (state->transition_started || state->project_admitted)
        failure = "change_after_fade_transition_already_attested";
    else if (state->token_owner !=
                 DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP)
        failure = "change_after_fade_wrong_presenter_owner";
    else if (!attestation->exact_change_after_fade_hook ||
             attestation->exact_hook_entry_rva !=
                 DLUMA_PROJECT_CHANGE_AFTER_FADE_ENTRY_RVA)
        failure = "change_after_fade_hook_unvalidated";
    else if (!attestation->transition_context_generation ||
             attestation->transition_context_generation !=
                 attestation->transition_generation ||
             !attestation->transition_context_current)
        failure = "change_after_fade_context_generation_invalid";
    else if (attestation->transition_context_recursive ||
             attestation->transition_context_depth != 1U)
        failure = "change_after_fade_context_recursive";
    else if (attestation->transition_context_concurrent ||
             !attestation->transition_context_serialized ||
             !attestation->serialized_route_valid)
        failure = "change_after_fade_context_concurrent";
    else if (!state->token_frame_route_token ||
             !attestation->premount_frame_route_token ||
             attestation->premount_frame_route_token !=
                 state->token_frame_route_token ||
             !attestation->current_frame_route_token ||
             attestation->current_frame_route_token !=
                 state->token_frame_route_token)
        failure = "change_after_fade_frame_route_changed";
    else if (attestation->authority_conflict)
        failure = "change_after_fade_frame_authority_conflict";
    else if (!state->token_epoch ||
             state->token_epoch != attestation->epoch)
        failure = "change_after_fade_epoch_mismatch";
    else if (!root_valid ||
             snapshot_result ==
                 DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_INVALID ||
             snapshot_result ==
                 DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_MISMATCH ||
             !attestation->normalized_project_root[0] ||
             !dluma_project_root_identity_equal(
                 state->token_identity.normalized_project_root,
                 attestation->normalized_project_root))
        failure = "change_after_fade_project_root_mismatch";
    else if (source_set_duplicate)
        failure = "change_after_fade_source_set_duplicate_kind";
    else if (snapshot_result ==
                 DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_LEDGER_BUSY)
        failure =
            "change_after_fade_source_set_snapshot_ledger_busy";
    else if (snapshot_result !=
                 DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_SUCCEEDED)
        failure = "change_after_fade_source_set_incomplete";
    else if (state->token_state !=
                 DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE ||
             !state->token_project_root_observed ||
             !state->token_data_directory_observed ||
             !state->token_assets_pc_observed ||
             !attestation->source_ownership_complete ||
             !attestation->source_set_complete ||
             attestation->project_root_source_count != 1U ||
             attestation->data_directory_source_count != 1U ||
             attestation->assets_pc_source_count != 1U)
        failure = "change_after_fade_source_set_incomplete";
    else if (!attestation->session)
        failure = "change_after_fade_session_missing";
    else if (!attestation->non_frontend_destination ||
             !attestation->destination_map[0])
        failure =
            "change_after_fade_destination_frontend_or_empty";
    else if (!attestation->transition_generation ||
             attestation->transition_generation <=
                 prior_transition_generation)
        failure = "change_after_fade_generation_not_fresh";
    else if (!transition_thread_id)
        failure = "change_after_fade_context_generation_invalid";
    else {
        state->transition_started = 1U;
        state->project_admitted = 1U;
        state->attestation_pending = 0U;
        state->token_state = DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
        state->custom_project_change_after_fade_attestation_success_count =
            dluma_project_increment_u64(
                state->
                    custom_project_change_after_fade_attestation_success_count);
        state->custom_project_last_consumed_transition_generation =
            attestation->transition_generation;
        state->custom_project_change_after_fade_disposition =
            DLUMA_PROJECT_CHANGE_AFTER_FADE_VERIFIED;
        state->active_epoch_quarantined = 0U;
        state->transition_attestation_failure = 0U;
        state->new_project_admission_blocked = 0U;
        state->cleanup_still_authorized = 0U;
        state->quarantined_epoch = 0U;
        return 1;
    }
    state->custom_project_change_after_fade_attestation_failure_count =
        dluma_project_increment_u64(
            state->
                custom_project_change_after_fade_attestation_failure_count);
    state->transition_attestation_failure = 1U;
    state->new_project_admission_blocked = 1U;
    state->admission_blocked = 1U;
    state->active_epoch_quarantined =
        exact_source_ownership ? 1U : 0U;
    state->cleanup_still_authorized =
        exact_source_ownership ? 1U : 0U;
    state->quarantined_epoch =
        exact_source_ownership ? state->token_epoch : 0U;
    state->custom_project_change_after_fade_disposition =
        exact_source_ownership
            ? DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP
            : DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION;
    dluma_project_copy_text(
        state->custom_project_transition_attestation_failure_reason,
        (unsigned int)sizeof(
            state->
                custom_project_transition_attestation_failure_reason),
        failure);
    dluma_project_record_first_failure(state, failure);
    return 0;
}

int dluma_project_launch_admission_note_quarantined_cleanup_result(
    DlumaProjectLaunchAdmissionState* state,
    unsigned int epoch,
    int cleanup_verified)
{
    if (!state || !epoch || !state->active_epoch_quarantined ||
        state->quarantined_epoch != epoch)
        return 0;
    if (!cleanup_verified) {
        state->cleanup_still_authorized = 0U;
        state->new_project_admission_blocked = 1U;
        state->admission_blocked = 1U;
        return 0;
    }
    state->active_epoch_quarantined = 0U;
    state->transition_attestation_failure = 0U;
    state->new_project_admission_blocked = 0U;
    state->cleanup_still_authorized = 0U;
    state->quarantined_epoch = 0U;
    state->admission_blocked = 0U;
    state->attestation_pending = 0U;
    state->token_active = 0U;
    state->token_epoch = 0U;
    state->token_project_root_observed = 0U;
    state->token_data_directory_observed = 0U;
    state->token_assets_pc_observed = 0U;
    state->token_state = DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
    return 1;
}

int dluma_project_launch_admission_attest_on_start_level(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner,
    ULONG_PTR controller,
    const DlumaProjectLaunchIdentity* identity,
    ULONGLONG now_ms)
{
    DlumaProjectLaunchIdentity canonical_identity;
    DlumaProjectOnStartLevelAttestation* attestation;
    if (!state) return 0;
    if (identity) {
        if (!dluma_project_identity_canonical_copy(
                &canonical_identity, identity)) {
            dluma_project_record_first_failure(
                state, "on_start_level_identity_mismatch");
            return 0;
        }
        identity = &canonical_identity;
    }
    state->custom_project_on_start_level_attestation_count =
        dluma_project_increment_u64(
            state->
                custom_project_on_start_level_attestation_count);
    attestation = &state->last_on_start_level_attestation;
    memset(attestation, 0, sizeof(*attestation));
    attestation->token_generation = state->token_generation;
    attestation->epoch = state->token_epoch;
    attestation->selected_map_owner = state->token_owner;
    attestation->on_start_level_owner = owner;
    attestation->controller = controller;
    if (identity) attestation->identity = *identity;
    attestation->source_set_complete =
        state->token_state ==
            DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE &&
        state->token_project_root_observed &&
        state->token_data_directory_observed &&
        state->token_assets_pc_observed
            ? 1U : 0U;
    if (state->transition_started && state->project_admitted &&
        !state->attestation_pending &&
        state->token_state == DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED) {
        /*
         * ChangeAfterFade is the primary DirectPresenter proof. OnStartLevel
         * remains a downstream corroboration and must not gate its original.
         */
        if (owner == state->token_owner &&
            controller == state->token_controller_identity) {
            attestation->succeeded = 1U;
            return 1;
        }
        dluma_project_record_first_failure(
            state, "on_start_level_optional_corroboration_mismatch");
        return 0;
    }
    if (state->attestation_pending &&
        (now_ms < state->token_created_ms ||
         now_ms - state->token_created_ms >
            DLUMA_PROJECT_PREMOUNT_TOKEN_MAX_AGE_MS)) {
        dluma_project_clear_active_authority(state);
        state->attestation_pending = 0U;
        state->token_state =
            DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
        dluma_project_record_first_failure(
            state, "on_start_level_after_premount_timeout");
        return 0;
    }
    if (!state->attestation_pending ||
        state->token_active ||
        state->token_state !=
            DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE ||
        !state->token_epoch ||
        state->token_owner != owner ||
        state->token_controller_identity != controller ||
        !state->last_original_selected_map_returned ||
        !identity ||
        !identity->transition_identity_complete ||
        !identity->campaign_name[0] ||
        !identity->map_name[0]) {
        dluma_project_fail_token(
            state, "on_start_level_without_authorized_premount");
        return 0;
    }
    /*
     * MenuSteamWorkshop does not publish campaign/map/profile until after
     * SetCustomContentName and UpdateDLCContent.  The pre-mount token is
     * already bound to its exact request, content selector, project root,
     * source set, owner, controller, and epoch, so bind the first downstream
     * tuple here before comparing and recording it.
     */
    if (!state->token_identity.transition_identity_complete) {
        dluma_project_copy_text(
            state->token_identity.campaign_name,
            (unsigned int)sizeof(
                state->token_identity.campaign_name),
            identity->campaign_name);
        dluma_project_copy_text(
            state->token_identity.map_name,
            (unsigned int)sizeof(
                state->token_identity.map_name),
            identity->map_name);
        state->token_identity.profile_index =
            identity->profile_index;
        state->token_identity.transition_identity_complete = 1U;
    }
    if (!dluma_project_identity_matches_attestation(
            &state->token_identity, identity)) {
        dluma_project_fail_token(
            state, "on_start_level_identity_mismatch");
        return 0;
    }
    attestation->succeeded = 1U;
    state->attestation_pending = 0U;
    state->token_state = DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
    return 1;
}

void dluma_project_launch_admission_note_on_start_level_original(
    DlumaProjectLaunchAdmissionState* state)
{
    if (!state ||
        !state->last_on_start_level_attestation.succeeded)
        return;
    state->last_on_start_level_attestation.
        original_invocation_count =
        dluma_project_increment_u64(
            state->last_on_start_level_attestation.
                original_invocation_count);
}

void dluma_project_launch_admission_cancel(
    DlumaProjectLaunchAdmissionState* state,
    const char* reason)
{
    if (!state) return;
    if (state->token_active || state->attestation_pending) {
        dluma_project_clear_active_authority(state);
        state->attestation_pending = 0U;
        state->token_state =
            DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED;
        dluma_project_record_first_failure(
            state, reason ? reason : "project_cancelled");
    }
    state->request_valid = 0U;
    memset(
        &state->request_identity, 0,
        sizeof(state->request_identity));
}

void dluma_project_launch_admission_block(
    DlumaProjectLaunchAdmissionState* state,
    const char* reason)
{
    if (!state) return;
    state->admission_blocked = 1U;
    state->request_valid = 0U;
    memset(&state->request_identity, 0,
           sizeof(state->request_identity));
    dluma_project_record_first_failure(
        state, reason ? reason : "project_admission_blocked");
}

const char* dluma_project_launch_owner_name(
    DlumaProjectLaunchOwner owner)
{
    switch (owner) {
    case DLUMA_PROJECT_LAUNCH_OWNER_NONE:
        return "none";
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME:
        return "MenuCustomGame";
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI:
        return "MenuStartLevelDI";
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP:
        return "MenuSteamWorkshop";
    default:
        return "unknown";
    }
}

const char* dluma_project_launch_route_name(
    DlumaProjectLaunchRoute route)
{
    switch (route) {
    case DLUMA_PROJECT_LAUNCH_ROUTE_NONE:
        return "none";
    case DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START:
        return "mouse_start";
    case DLUMA_PROJECT_LAUNCH_ROUTE_GAMEPAD_KEYBOARD_START:
        return "gamepad_keyboard_start";
    case DLUMA_PROJECT_LAUNCH_ROUTE_LIST_DOUBLE_CLICK:
        return "list_double_click";
    case DLUMA_PROJECT_LAUNCH_ROUTE_ACCEPT_ACTION_CHAIN:
        return "accept_action_chain";
    case DLUMA_PROJECT_LAUNCH_ROUTE_QUEUED_REQUEST:
        return "queued_request";
    case DLUMA_PROJECT_LAUNCH_ROUTE_DELAYED_TRANSITION:
        return "delayed_transition";
    case DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER:
        return "direct_presenter";
    default:
        return "unknown";
    }
}

const char* dluma_project_launch_admission_result_name(
    DlumaProjectLaunchAdmissionResult result)
{
    switch (result) {
    case DLUMA_PROJECT_LAUNCH_ADMISSION_NOT_ATTEMPTED:
        return "not_attempted";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_DEFERRED:
        return "deferred";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_BLOCKED:
        return "blocked";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED:
        return "allowed";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING:
        return "b0_preparing";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PERMANENT_SOURCE_FAILURE:
        return "b0_permanent_source_failure";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED:
        return "b0_restart_required";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST:
        return "stale_request";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH:
        return "identity_mismatch";
    case DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN:
        return "active_token";
    default:
        return "unknown";
    }
}

const char* dluma_project_premount_token_state_name(
    DlumaProjectPreMountTokenState state)
{
    switch (state) {
    case DLUMA_PROJECT_PREMOUNT_TOKEN_NONE:
        return "None";
    case DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT:
        return "ArmedPreMount";
    case DLUMA_PROJECT_PREMOUNT_TOKEN_PROJECT_ROOT_OBSERVED:
        return "ProjectRootObserved";
    case DLUMA_PROJECT_PREMOUNT_TOKEN_EPOCH_BOUND:
        return "EpochBound";
    case DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE:
        return "SourceSetComplete";
    case DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED:
        return "Expired";
    case DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED:
        return "Failed";
    default:
        return "Unknown";
    }
}

const char* dluma_project_mount_source_kind_name(
    DlumaProjectMountSourceKind kind)
{
    switch (kind) {
    case DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT:
        return "ProjectRoot";
    case DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY:
        return "DataDirectory";
    case DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC:
        return "AssetsPc";
    default:
        return "Unknown";
    }
}

const char* dluma_project_change_after_fade_disposition_name(
    DlumaProjectChangeAfterFadeDisposition disposition)
{
    switch (disposition) {
    case DLUMA_PROJECT_CHANGE_AFTER_FADE_NOT_ATTESTED:
        return "not_attested";
    case DLUMA_PROJECT_CHANGE_AFTER_FADE_VERIFIED:
        return "verified";
    case DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP:
        return "quarantined_for_cleanup";
    case DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION:
        return "fatal_source_contradiction";
    default:
        return "unknown";
    }
}

const char* dluma_project_source_set_snapshot_result_name(
    DlumaProjectSourceSetSnapshotResult result)
{
    switch (result) {
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_SUCCEEDED:
        return "Succeeded";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_INVALID_ARGUMENT:
        return "InvalidArgument";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_LEDGER_BUSY:
        return "LedgerBusy";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_EPOCH_MISSING:
        return "EpochMissing";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_INVALID:
        return "ProjectRootInvalid";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_MISMATCH:
        return "ProjectRootMismatch";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_OWNERSHIP_INCOMPLETE:
        return "OwnershipIncomplete";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_EXACT_HANDLE_INCOMPLETE:
        return "ExactHandleIncomplete";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_DUPLICATE_SOURCE_KIND:
        return "DuplicateSourceKind";
    case DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_REQUIRED_SOURCE_KIND_MISSING:
        return "RequiredSourceKindMissing";
    default:
        return "InvalidArgument";
    }
}
