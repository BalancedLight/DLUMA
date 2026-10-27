#include "dlw_project_launch_admission.h"
#include "dlw_project_root_identity.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(                                                        \
                stderr, "CHECK failed at line %d: %s\n",                  \
                __LINE__, #expression);                                     \
            return 0;                                                       \
        }                                                                   \
    } while (0)

static const char* const kRoot =
    "x:\\steam\\workshop\\content\\239140\\3728869100";

static DWORD entry_rva(DlumaProjectLaunchOwner owner)
{
    switch (owner) {
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME:
        return 0x007D9F90UL;
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI:
        return 0x0091A220UL;
    default:
        return 0x0091C6C0UL;
    }
}

static DWORD update_return_rva(DlumaProjectLaunchOwner owner)
{
    switch (owner) {
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME:
        return 0x007DA1CAUL;
    case DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI:
        return 0x0091A2E8UL;
    default:
        return 0x0091C713UL;
    }
}

static DlumaProjectLaunchIdentity project_identity(
    const char* workshop_id,
    const char* selection_key,
    const char* project_root)
{
    DlumaProjectLaunchIdentity identity;
    memset(&identity, 0, sizeof(identity));
    if (workshop_id)
        strcpy_s(
            identity.workshop_id,
            sizeof(identity.workshop_id), workshop_id);
    if (selection_key)
        strcpy_s(
            identity.selection_key,
            sizeof(identity.selection_key), selection_key);
    if (project_root)
        strcpy_s(
            identity.normalized_project_root,
            sizeof(identity.normalized_project_root),
            project_root);
    strcpy_s(
        identity.campaign_name,
        sizeof(identity.campaign_name), "campaign_a");
    strcpy_s(
        identity.map_name,
        sizeof(identity.map_name), "map_a");
    identity.profile_index = 2;
    identity.transition_identity_complete = 1U;
    return identity;
}

static DlumaProjectPreMountEvidence premount_evidence(
    DlumaProjectLaunchOwner owner,
    DlumaProjectLaunchRoute route,
    ULONGLONG now_ms)
{
    DlumaProjectPreMountEvidence evidence;
    memset(&evidence, 0, sizeof(evidence));
    evidence.route = route;
    evidence.owner = owner;
    evidence.entry_rva = entry_rva(owner);
    evidence.thread_id = 77U;
    evidence.controller = 0x100000U + owner;
    evidence.controller_vtable = 0x200000U + owner;
    evidence.ui_data =
        owner == DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP
            ? 0U : 0x300000U + owner;
    evidence.payload = 0x400000U + owner;
    evidence.caller_return_address = 0x500000U + owner;
    strcpy_s(
        evidence.caller_module,
        sizeof(evidence.caller_module), "gamedll_x64_rwdi.dll");
    evidence.caller_module_base = 0x180000000ULL;
    evidence.caller_module_rva = 0x1234U;
    evidence.map_source =
        owner == DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP
            ? 0 : 54;
    evidence.custom_map_source = 1U;
    evidence.identity =
        project_identity("3728869100", "3728869100", kRoot);
    if (owner ==
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP) {
        strcpy_s(
            evidence.custom_content_name,
            sizeof(evidence.custom_content_name), kRoot);
        evidence.custom_content_identity_corroborated = 1U;
        evidence.identity.campaign_name[0] = 0;
        evidence.identity.map_name[0] = 0;
        evidence.identity.profile_index = 0;
        evidence.identity.transition_identity_complete = 0U;
    }
    strcpy_s(
        evidence.b0_state,
        sizeof(evidence.b0_state), "CompleteAuthorized");
    evidence.b0_generation = 9U;
    evidence.frame_route_token = 11U;
    evidence.now_ms = now_ms;
    evidence.startup_b0_complete_authorized = 1U;
    evidence.restart_registry_can_admit = 1U;
    evidence.content_isolation_enabled = 1U;
    return evidence;
}

static int begin_update(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner,
    ULONGLONG now_ms)
{
    DlumaProjectPreMountEvidence evidence =
        premount_evidence(
            owner,
            DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER,
            now_ms);
    if (owner ==
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP)
        CHECK(dluma_project_launch_admission_note_activation(
            state, evidence.route, &evidence.identity,
            now_ms - 1U,
            DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_begin_premount(
              state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    dluma_project_launch_admission_note_selected_map_original(
        state, 1);
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        state, owner, update_return_rva(owner), 77U, 1));
    return 1;
}

static int complete_sources(
    DlumaProjectLaunchAdmissionState* state,
    DlumaProjectLaunchOwner owner,
    unsigned int epoch,
    ULONGLONG now_ms)
{
    CHECK(dluma_project_launch_admission_note_custom_source(
        state, DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        kRoot, epoch, 77U, now_ms));
    CHECK(state->token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_EPOCH_BOUND);
    CHECK(dluma_project_launch_admission_note_custom_source(
        state, DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
        kRoot, epoch, 77U, now_ms));
    CHECK(dluma_project_launch_admission_note_custom_source(
        state, DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC,
        kRoot, epoch, 77U, now_ms));
    CHECK(state->token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE);
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        state, owner, update_return_rva(owner), 77U, 0));
    dluma_project_launch_admission_note_selected_map_original(
        state, 0);
    return 1;
}

static DlumaProjectChangeAfterFadeAttestation change_attestation(
    const DlumaProjectLaunchAdmissionState* state,
    unsigned int epoch,
    ULONGLONG transition_generation,
    DWORD transition_thread_id)
{
    DlumaProjectChangeAfterFadeAttestation attestation;
    memset(&attestation, 0, sizeof(attestation));
    attestation.session = 0x123400U;
    attestation.transition_generation = transition_generation;
    attestation.transition_context_generation =
        transition_generation;
    attestation.premount_frame_route_token =
        state->token_frame_route_token;
    attestation.current_frame_route_token =
        state->token_frame_route_token;
    attestation.epoch = epoch;
    attestation.exact_hook_entry_rva =
        DLUMA_PROJECT_CHANGE_AFTER_FADE_ENTRY_RVA;
    attestation.premount_thread_id = state->token_thread_id;
    attestation.transition_thread_id = transition_thread_id;
    attestation.thread_id = transition_thread_id;
    attestation.exact_change_after_fade_hook = 1U;
    attestation.transition_context_current = 1U;
    attestation.transition_context_depth = 1U;
    attestation.transition_context_serialized = 1U;
    attestation.serialized_route_valid = 1U;
    attestation.non_frontend_destination = 1U;
    attestation.source_set_complete = 1U;
    attestation.source_ownership_complete = 1U;
    attestation.project_root_source_count = 1U;
    attestation.data_directory_source_count = 1U;
    attestation.assets_pc_source_count = 1U;
    attestation.source_set_snapshot_result =
        DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_SUCCEEDED;
    strcpy_s(
        attestation.normalized_project_root,
        sizeof(attestation.normalized_project_root), kRoot);
    strcpy_s(
        attestation.destination_map,
        sizeof(attestation.destination_map), "map_a");
    return attestation;
}

static int prepare_steam_transition(
    DlumaProjectLaunchAdmissionState* state,
    unsigned int epoch,
    ULONGLONG now_ms)
{
    dluma_project_launch_admission_initialize(state);
    CHECK(begin_update(
        state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        now_ms));
    CHECK(complete_sources(
        state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        epoch, now_ms + 1U));
    return 1;
}

static int test_button_deferred_without_queue(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity identity =
        project_identity("3728869100", "3728869100", kRoot);
    dluma_project_launch_admission_initialize(&state);
    CHECK(!dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        &identity, 1000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING));
    CHECK(state.activation_attempt_count == 1U);
    CHECK(state.request_valid == 0U);
    CHECK(state.last_request_consumed == 1U);
    CHECK(state.token_active == 0U);
    return 1;
}

static int test_runtime10_complete_authorized_permits_one_play_activation(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, 1010U);
    dluma_project_launch_admission_initialize(&state);

    CHECK(dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        &evidence.identity, 1000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(state.activation_attempt_count == 1U);
    CHECK(state.request_valid == 1U);
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    CHECK(state.activation_attempt_count == 1U);
    CHECK(state.custom_project_premount_attempt_count == 1U);
    CHECK(state.custom_project_premount_allowed_count == 1U);
    CHECK(state.token_active == 1U);
    CHECK(!state.last_premount_hit.set_custom_content_name_reached);
    CHECK(!state.last_premount_hit.update_dlc_content_reached);
    CHECK(!state.last_premount_hit.original_invoked);
    return 1;
}

static int test_runtime10_permanent_failure_blocks_before_mutation(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, 2000U);
    dluma_project_launch_admission_initialize(&state);
    evidence.startup_b0_complete_authorized = 0U;
    strcpy_s(
        evidence.b0_state, sizeof(evidence.b0_state),
        "PermanentFailure");

    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED);
    CHECK(state.custom_project_premount_attempt_count == 1U);
    CHECK(state.custom_project_premount_allowed_count == 0U);
    CHECK(state.custom_project_premount_denied_count == 1U);
    CHECK(!state.token_active);
    CHECK(!state.last_premount_hit.set_custom_content_name_reached);
    CHECK(!state.last_premount_hit.update_dlc_content_reached);
    CHECK(!state.last_premount_hit.original_invoked);
    CHECK(state.custom_project_update_dlc_content_count == 0U);
    return 1;
}

static int test_every_launch_route_reaches_premount_gate(void)
{
    const DlumaProjectLaunchRoute routes[] = {
        DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        DLUMA_PROJECT_LAUNCH_ROUTE_GAMEPAD_KEYBOARD_START,
        DLUMA_PROJECT_LAUNCH_ROUTE_LIST_DOUBLE_CLICK,
        DLUMA_PROJECT_LAUNCH_ROUTE_ACCEPT_ACTION_CHAIN,
        DLUMA_PROJECT_LAUNCH_ROUTE_QUEUED_REQUEST,
        DLUMA_PROJECT_LAUNCH_ROUTE_DELAYED_TRANSITION,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER,
    };
    unsigned int index;
    for (index = 0;
         index < sizeof(routes) / sizeof(routes[0]); ++index) {
        DlumaProjectLaunchAdmissionState state;
        DlumaProjectPreMountEvidence evidence =
            premount_evidence(
                DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
                routes[index], 1100U);
        dluma_project_launch_admission_initialize(&state);
        CHECK(dluma_project_launch_admission_note_activation(
            &state, routes[index], &evidence.identity, 1000U,
            DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
        CHECK(dluma_project_launch_admission_begin_premount(
                  &state, &evidence) ==
              DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
        CHECK(state.custom_project_premount_attempt_count == 1U);
        CHECK(state.custom_project_premount_allowed_count == 1U);
        CHECK(state.premount_owner_hit_count[
                  DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP] ==
              1U);
        CHECK(state.premount_owner_hits[
                  DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP].
                  evidence.owner ==
              DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP);
        CHECK(state.token_active == 1U);
        CHECK(state.token_state ==
              DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT);
        CHECK(state.token_route == routes[index]);
    }
    return 1;
}

static int test_stale_and_mismatched_requests_fail_closed(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity request =
        project_identity("3728869100", "3728869100", 0);
    DlumaProjectPreMountEvidence evidence =
        premount_evidence(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
            DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
            1000U + DLUMA_PROJECT_LAUNCH_REQUEST_MAX_AGE_MS + 1U);
    dluma_project_launch_admission_initialize(&state);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        &request, 1000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST);
    CHECK(state.token_active == 0U);

    dluma_project_launch_admission_initialize(&state);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        &request, 8000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, 8100U);
    strcpy_s(
        evidence.identity.workshop_id,
        sizeof(evidence.identity.workshop_id),
        "3771245504");
    strcpy_s(
        evidence.identity.selection_key,
        sizeof(evidence.identity.selection_key),
        "3771245504");
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH);
    CHECK(state.token_active == 0U);
    return 1;
}

static int test_all_three_owners_deny_before_update(void)
{
    DlumaProjectLaunchOwner owner;
    for (owner =
             DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME;
         owner < DLUMA_PROJECT_LAUNCH_OWNER_COUNT;
         owner = (DlumaProjectLaunchOwner)(
             (unsigned int)owner + 1U)) {
        DlumaProjectLaunchAdmissionState state;
        DlumaProjectPreMountEvidence evidence =
            premount_evidence(
                owner,
                DLUMA_PROJECT_LAUNCH_ROUTE_DELAYED_TRANSITION,
                1000U);
        evidence.startup_b0_complete_authorized = 0U;
        strcpy_s(
            evidence.b0_state,
            sizeof(evidence.b0_state),
            "WaitingForStability");
        dluma_project_launch_admission_initialize(&state);
        if (owner ==
                DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP)
            CHECK(dluma_project_launch_admission_note_activation(
                &state, evidence.route, &evidence.identity,
                999U,
                DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
        CHECK(dluma_project_launch_admission_begin_premount(
                  &state, &evidence) ==
              DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING);
        CHECK(state.custom_project_premount_attempt_count == 1U);
        CHECK(state.custom_project_premount_denied_count == 1U);
        CHECK(state.custom_project_premount_allowed_count == 0U);
        CHECK(state.token_active == 0U);
        CHECK(!state.last_original_selected_map_invoked);
        CHECK(!state.last_transition_queued);
        CHECK(!dluma_project_launch_admission_note_update_dlc_content(
            &state, owner, update_return_rva(owner),
            evidence.thread_id, 1));
        CHECK(state.custom_project_update_dlc_content_count == 0U);
        CHECK(state.source_mount_observation_count == 0U);
    }
    return 1;
}

static int test_gate_rechecks_b0_registry_authority_and_active_token(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence =
        premount_evidence(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
            DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER,
            1000U);
    dluma_project_launch_admission_initialize(&state);
    evidence.startup_b0_complete_authorized = 0U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING);
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 1100U);
    evidence.restart_registry_can_admit = 0U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED);
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 1200U);
    evidence.authority_conflict = 1U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_BLOCKED);
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 1300U);
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    evidence.now_ms = 1400U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN);
    CHECK(state.custom_project_premount_attempt_count == 5U);
    CHECK(state.custom_project_premount_denied_count == 4U);
    return 1;
}

static int test_steam_requires_request_and_content_corroboration(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence =
        premount_evidence(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
            DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, 1100U);
    dluma_project_launch_admission_initialize(&state);
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_STALE_REQUEST);
    CHECK(state.token_active == 0U);

    dluma_project_launch_admission_initialize(&state);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, evidence.route, &evidence.identity, 1000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    evidence.custom_content_identity_corroborated = 0U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_IDENTITY_MISMATCH);
    CHECK(state.token_active == 0U);

    dluma_project_launch_admission_initialize(&state);
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, 1300U);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, evidence.route, &evidence.identity, 1200U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    CHECK(state.token_identity.transition_identity_complete == 0U);
    return 1;
}

static int test_steam_direct_presenter_creates_fresh_exact_request(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence =
        premount_evidence(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
            DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 2100U);
    dluma_project_launch_admission_initialize(&state);
    CHECK(!state.request_valid);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER,
        &evidence.identity, 2100U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(state.request_valid);
    CHECK(state.request_route ==
          DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER);
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    CHECK(!state.request_valid);
    CHECK(state.token_route ==
          DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER);
    CHECK(state.token_identity.normalized_project_root[0]);
    return 1;
}

static int
test_cancelled_confirmation_requires_verified_cleanup(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence;
    DlumaProjectCancelledCleanupPlan plan;
    DlumaProjectCancelledCleanupResult result;
    ULONGLONG first_token_generation;
    unsigned int first_epoch;

    /*
     * SourceSetComplete is not inactivity and a fresh click cannot
     * automatically supersede the pending presentation.
     */
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        2000U));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        12U, 2001U));
    CHECK(!state.token_active);
    CHECK(state.attestation_pending);
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE);
    first_token_generation = state.token_generation;
    first_epoch = state.token_epoch;

    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 2100U);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, evidence.route, &evidence.identity, 2099U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ACTIVE_TOKEN);
    CHECK(!state.token_active);
    CHECK(state.attestation_pending);
    CHECK(state.token_generation == first_token_generation);
    CHECK(state.token_epoch == first_epoch);
    CHECK(state.custom_project_attestation_superseded_count == 0U);

    /*
     * A separately admitted fresh activation may proceed only after an exact
     * cleanup result proves the old epoch absent and consumed.
     */
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        3000U));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        13U, 3001U));
    first_token_generation = state.token_generation;
    first_epoch = state.token_epoch;
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 3100U);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, evidence.route, &evidence.identity, 3099U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_prepare_cancelled_cleanup(
              &state, &evidence, &plan) ==
          DLUMA_PROJECT_CANCELLED_CLEANUP_READY);
    CHECK(plan.prior_token_generation == first_token_generation);
    CHECK(plan.prior_epoch == first_epoch);
    memset(&result, 0, sizeof(result));
    result.active_source_count_before = 3U;
    result.removed_source_count = 3U;
    result.live_registry_verified_absent = 1U;
    result.loader_sources_unchanged = 1U;
    result.replacement_source_absent = 1U;
    result.prior_epoch_consumed = 1U;
    result.succeeded = 1U;
    CHECK(dluma_project_launch_admission_note_cancelled_cleanup_result(
        &state, &plan, &result));
    CHECK(!state.attestation_pending);
    CHECK(state.token_epoch == 0U);
    CHECK(state.custom_project_cancelled_cleanup_success_count == 1U);
    CHECK(state.custom_project_attestation_superseded_count == 1U);
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    CHECK(state.token_generation > first_token_generation);
    dluma_project_launch_admission_note_selected_map_original(
        &state, 1);
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        update_return_rva(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP),
        77U, 1));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        14U, 3101U));
    CHECK(state.custom_project_fresh_epoch == 14U);
    CHECK(state.custom_project_fresh_epoch != first_epoch);

    /*
     * Any cleanup/registry proof failure retains the old authority, consumes
     * neither token nor epoch, and blocks all later admission.
     */
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        4000U));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        15U, 4001U));
    first_token_generation = state.token_generation;
    first_epoch = state.token_epoch;
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 4100U);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, evidence.route, &evidence.identity, 4099U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_prepare_cancelled_cleanup(
              &state, &evidence, &plan) ==
          DLUMA_PROJECT_CANCELLED_CLEANUP_READY);
    memset(&result, 0, sizeof(result));
    result.active_source_count_before = 3U;
    result.removed_source_count = 2U;
    result.active_source_count_after = 1U;
    strcpy_s(
        result.first_failure, sizeof(result.first_failure),
        "cancelled_cleanup_live_source_survived");
    CHECK(!dluma_project_launch_admission_note_cancelled_cleanup_result(
        &state, &plan, &result));
    CHECK(state.admission_blocked);
    CHECK(!state.token_active);
    CHECK(state.attestation_pending);
    CHECK(state.token_generation == first_token_generation);
    CHECK(state.token_epoch == first_epoch);
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE);
    CHECK(!state.request_valid);
    CHECK(state.custom_project_cancelled_cleanup_failure_count == 1U);
    CHECK(state.custom_project_attestation_superseded_count == 0U);
    return 1;
}

static int test_change_after_fade_is_primary_transition_attestation(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectChangeAfterFadeAttestation attestation;
    DlumaProjectLaunchIdentity optional_identity =
        project_identity(0, 0, 0);

    CHECK(prepare_steam_transition(&state, 16U, 5000U));
    attestation = change_attestation(
        &state, 16U, 1U, 42112U);
    CHECK(state.token_thread_id == 77U);
    CHECK(attestation.transition_thread_id !=
          state.token_thread_id);
    CHECK(dluma_project_launch_admission_attest_change_after_fade(
        &state, &attestation));
    CHECK(state.transition_started && state.project_admitted);
    CHECK(!state.attestation_pending);
    CHECK(state.token_state == DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED);
    CHECK(state.custom_project_change_after_fade_attestation_count == 1U);
    CHECK(
        state.custom_project_change_after_fade_attestation_success_count ==
        1U);
    CHECK(strcmp(
        state.custom_project_transition_attestation_destination,
        "map_a") == 0);
    CHECK(state.custom_project_transition_thread_migrated);
    CHECK(state.custom_project_premount_thread_id == 77U);
    CHECK(state.custom_project_transition_thread_id == 42112U);
    CHECK(state.custom_project_change_after_fade_disposition ==
          DLUMA_PROJECT_CHANGE_AFTER_FADE_VERIFIED);
    CHECK(state.custom_project_last_consumed_transition_generation ==
          1U);
    CHECK(dluma_project_launch_admission_attest_on_start_level(
        &state, DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        state.token_controller_identity, &optional_identity, 5100U));
    return 1;
}

static int test_project_root_canonical_contract_and_live_shape(void)
{
    static const char* const raw_presenter =
        "X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\DevTools\\"
        "workshop\\DevToolsDefaultProject";
    static const char* const canonical_ledger =
        "x:\\fixturelibrary\\steamapps\\common\\dying light\\devtools\\"
        "workshop\\devtoolsdefaultproject";
    char ordinary[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char forward[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char extended[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char pseudo[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char slash_extended[DLUMA_PROJECT_LAUNCH_ROOT_CAPACITY];
    char rejected[32];
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence;
    DlumaProjectChangeAfterFadeAttestation attestation;

    CHECK(dluma_project_root_canonicalize(
        raw_presenter, ordinary, sizeof(ordinary)));
    CHECK(strcmp(ordinary, canonical_ledger) == 0);
    CHECK(dluma_project_root_canonicalize(
        "X:/FixtureLibrary/steamapps/common/Dying Light/DevTools/"
        "workshop/DevToolsDefaultProject/",
        forward, sizeof(forward)));
    CHECK(dluma_project_root_canonicalize(
        "\\\\?\\X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
        "DevTools\\workshop\\DevToolsDefaultProject",
        extended, sizeof(extended)));
    CHECK(dluma_project_root_canonicalize(
        "\\?\\X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
        "DevTools\\workshop\\DevToolsDefaultProject",
        pseudo, sizeof(pseudo)));
    CHECK(dluma_project_root_canonicalize(
        "//?/X:/FixtureLibrary/steamapps/common/Dying Light/DevTools/"
        "workshop/DevToolsDefaultProject",
        slash_extended, sizeof(slash_extended)));
    CHECK(strcmp(ordinary, forward) == 0);
    CHECK(strcmp(ordinary, extended) == 0);
    CHECK(strcmp(ordinary, pseudo) == 0);
    CHECK(strcmp(ordinary, slash_extended) == 0);
    CHECK(dluma_project_root_identity_equal(
        raw_presenter, canonical_ledger));
    CHECK(!dluma_project_root_identity_equal(
        raw_presenter, "X:\\different\\project"));
    CHECK(!dluma_project_root_canonicalize(
        "X:\\FixtureLibrary\\..\\escape",
        rejected, sizeof(rejected)));
    CHECK(!dluma_project_root_canonicalize(
        raw_presenter, rejected, 8U));
    CHECK(rejected[0] == 0);

    dluma_project_launch_admission_initialize(&state);
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 8000U);
    strcpy_s(
        evidence.identity.normalized_project_root,
        sizeof(evidence.identity.normalized_project_root),
        raw_presenter);
    strcpy_s(
        evidence.identity.display_project_root,
        sizeof(evidence.identity.display_project_root),
        raw_presenter);
    strcpy_s(
        evidence.custom_content_name,
        sizeof(evidence.custom_content_name), raw_presenter);
    CHECK(dluma_project_launch_admission_note_activation(
        &state, evidence.route, &evidence.identity, 7999U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED));
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    CHECK(strcmp(
        state.token_identity.normalized_project_root,
        canonical_ledger) == 0);
    CHECK(strcmp(
        state.token_identity.display_project_root,
        raw_presenter) == 0);
    dluma_project_launch_admission_note_selected_map_original(
        &state, 1);
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        &state, DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        update_return_rva(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP),
        77U, 1));
    CHECK(dluma_project_launch_admission_note_custom_source(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        canonical_ledger, 41U, 77U, 8001U));
    CHECK(dluma_project_launch_admission_note_custom_source(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
        "X:/FixtureLibrary/steamapps/common/Dying Light/DevTools/"
        "workshop/DevToolsDefaultProject",
        41U, 77U, 8001U));
    CHECK(dluma_project_launch_admission_note_custom_source(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC,
        "\\\\?\\X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
        "DevTools\\workshop\\DevToolsDefaultProject",
        41U, 77U, 8001U));
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        &state, DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        update_return_rva(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP),
        77U, 0));
    dluma_project_launch_admission_note_selected_map_original(
        &state, 0);
    attestation = change_attestation(
        &state, 41U, 17U, 42112U);
    strcpy_s(
        attestation.normalized_project_root,
        sizeof(attestation.normalized_project_root),
        "\\?\\X:\\FixtureLibrary\\steamapps\\common\\Dying Light\\"
        "DevTools\\workshop\\DevToolsDefaultProject");
    CHECK(dluma_project_launch_admission_attest_change_after_fade(
        &state, &attestation));
    CHECK(state.custom_project_transition_thread_migrated);
    CHECK(state.custom_project_source_set_snapshot_result ==
          DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_SUCCEEDED);
    CHECK(strcmp(
        state.token_identity.normalized_project_root,
        canonical_ledger) == 0);
    return 1;
}

static int test_rootless_identity_debounce_remains_compatible(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity identity =
        project_identity("3728869100", "3728869100", 0);
    dluma_project_launch_admission_initialize(&state);
    CHECK(!dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, &identity, 9000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING));
    CHECK(!state.last_activation_delivery_debounced);
    CHECK(!dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START, &identity, 9001U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING));
    CHECK(state.last_activation_delivery_debounced);
    CHECK(state.denied_activation_duplicate_count == 1U);
    return 1;
}

static int test_change_after_fade_exact_failure_reasons(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectChangeAfterFadeAttestation attestation;

#define PREPARE_CHANGE_CASE(epoch_value, generation_value)                  \
    do {                                                                    \
        CHECK(prepare_steam_transition(                                     \
            &state, (epoch_value), 6000U + (epoch_value)));                \
        attestation = change_attestation(                                   \
            &state, (epoch_value), (generation_value), 42112U);            \
    } while (0)
#define CHECK_CHANGE_FAILURE(expected_reason, expected_disposition)         \
    do {                                                                    \
        CHECK(!dluma_project_launch_admission_attest_change_after_fade(       \
            &state, &attestation));                                         \
        CHECK(strcmp(                                                       \
            state.custom_project_transition_attestation_failure_reason,     \
            (expected_reason)) == 0);                                       \
        CHECK(state.custom_project_change_after_fade_disposition ==         \
              (expected_disposition));                                      \
    } while (0)

    PREPARE_CHANGE_CASE(20U, 1U);
    attestation.exact_change_after_fade_hook = 0U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_hook_unvalidated",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);
    CHECK(state.cleanup_still_authorized);
    CHECK(!state.project_admitted);
    CHECK(!dluma_project_launch_admission_note_quarantined_cleanup_result(
        &state, 20U, 0));
    CHECK(!state.cleanup_still_authorized);

    PREPARE_CHANGE_CASE(21U, 2U);
    attestation.transition_context_generation = 1U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_context_generation_invalid",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    PREPARE_CHANGE_CASE(22U, 3U);
    attestation.transition_context_depth = 2U;
    attestation.transition_context_recursive = 1U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_context_recursive",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    PREPARE_CHANGE_CASE(23U, 4U);
    attestation.transition_context_serialized = 0U;
    attestation.transition_context_concurrent = 1U;
    attestation.serialized_route_valid = 0U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_context_concurrent",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    PREPARE_CHANGE_CASE(24U, 5U);
    attestation.current_frame_route_token++;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_frame_route_changed",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    PREPARE_CHANGE_CASE(25U, 6U);
    attestation.authority_conflict = 1U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_frame_authority_conflict",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    PREPARE_CHANGE_CASE(26U, 7U);
    attestation.epoch = 27U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_epoch_mismatch",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION);
    CHECK(!state.cleanup_still_authorized);
    CHECK(!state.active_epoch_quarantined);

    PREPARE_CHANGE_CASE(27U, 8U);
    strcpy_s(
        attestation.normalized_project_root,
        sizeof(attestation.normalized_project_root),
        "X:\\wrong\\project");
    CHECK_CHANGE_FAILURE(
        "change_after_fade_project_root_mismatch",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION);

    PREPARE_CHANGE_CASE(28U, 9U);
    attestation.project_root_source_count = 2U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_source_set_duplicate_kind",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION);

    PREPARE_CHANGE_CASE(29U, 10U);
    attestation.assets_pc_source_count = 0U;
    attestation.source_set_complete = 0U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_source_set_incomplete",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION);

    PREPARE_CHANGE_CASE(35U, 15U);
    attestation.source_set_snapshot_result =
        DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_LEDGER_BUSY;
    attestation.source_set_complete = 0U;
    attestation.source_ownership_complete = 0U;
    attestation.project_root_source_count = 0U;
    attestation.data_directory_source_count = 0U;
    attestation.assets_pc_source_count = 0U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_source_set_snapshot_ledger_busy",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);
    CHECK(state.cleanup_still_authorized);
    CHECK(state.quarantined_epoch == 35U);

    PREPARE_CHANGE_CASE(36U, 16U);
    attestation.source_set_snapshot_result =
        DLUMA_PROJECT_SOURCE_SET_SNAPSHOT_PROJECT_ROOT_MISMATCH;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_project_root_mismatch",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_FATAL_SOURCE_CONTRADICTION);

    PREPARE_CHANGE_CASE(30U, 11U);
    attestation.session = 0U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_session_missing",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    PREPARE_CHANGE_CASE(31U, 12U);
    attestation.non_frontend_destination = 0U;
    attestation.destination_map[0] = 0;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_destination_frontend_or_empty",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);

    CHECK(prepare_steam_transition(&state, 32U, 7000U));
    attestation = change_attestation(&state, 32U, 13U, 42112U);
    CHECK(dluma_project_launch_admission_attest_change_after_fade(
        &state, &attestation));
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        7100U));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        33U, 7101U));
    attestation = change_attestation(&state, 33U, 13U, 42112U);
    CHECK_CHANGE_FAILURE(
        "change_after_fade_generation_not_fresh",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);
    CHECK(state.custom_project_last_consumed_transition_generation ==
          13U);

    PREPARE_CHANGE_CASE(34U, 14U);
    attestation.non_frontend_destination = 0U;
    CHECK_CHANGE_FAILURE(
        "change_after_fade_destination_frontend_or_empty",
        DLUMA_PROJECT_CHANGE_AFTER_FADE_QUARANTINED_FOR_CLEANUP);
    CHECK(dluma_project_launch_admission_note_quarantined_cleanup_result(
        &state, 34U, 1));
    CHECK(!state.active_epoch_quarantined);
    CHECK(!state.new_project_admission_blocked);
    CHECK(!state.admission_blocked);
    CHECK(!state.attestation_pending);

#undef CHECK_CHANGE_FAILURE
#undef PREPARE_CHANGE_CASE
    return 1;
}

static int test_steam_attestation_binds_late_transition_identity(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity attested =
        project_identity(0, 0, 0);
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        4000U));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        12U, 4001U));
    CHECK(state.token_identity.transition_identity_complete == 0U);
    CHECK(dluma_project_launch_admission_attest_on_start_level(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        state.token_controller_identity, &attested, 4100U));
    CHECK(state.token_identity.transition_identity_complete == 1U);
    CHECK(strcmp(
        state.token_identity.campaign_name,
        attested.campaign_name) == 0);
    CHECK(strcmp(
        state.token_identity.map_name,
        attested.map_name) == 0);
    CHECK(state.token_identity.profile_index ==
          attested.profile_index);
    return 1;
}

static int test_token_source_epoch_and_attestation_lifecycle(void)
{
    DlumaProjectLaunchOwner owner =
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI;
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity identity =
        project_identity("3728869100", "3728869100", kRoot);
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(&state, owner, 1000U));
    CHECK(complete_sources(&state, owner, 7U, 1001U));
    CHECK(state.token_active == 0U);
    CHECK(state.attestation_pending == 1U);
    CHECK(state.token_epoch == 7U);
    CHECK(state.custom_project_source_epoch_bound_count == 1U);
    CHECK(state.custom_project_update_dlc_content_count == 1U);
    CHECK(dluma_project_launch_admission_attest_on_start_level(
        &state, owner, state.token_controller_identity,
        &identity, 2000U));
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED);
    CHECK(state.last_on_start_level_attestation.succeeded == 1U);
    CHECK(state.last_on_start_level_attestation.
              original_invocation_count == 0U);
    dluma_project_launch_admission_note_on_start_level_original(
        &state);
    CHECK(state.last_on_start_level_attestation.
              original_invocation_count == 1U);
    CHECK(state.custom_project_on_start_level_attestation_count == 1U);
    return 1;
}

static int test_wrong_source_sequence_and_unrelated_source_fail(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectSourceBypassEvidence evidence;
    memset(&evidence, 0, sizeof(evidence));
    evidence.source_object = 0xABCDEFU;
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        1000U));
    CHECK(!dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
        kRoot, 4U, 77U, 1001U, &evidence));
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED);
    CHECK(state.custom_project_mount_bypass_count == 1U);
    CHECK(state.bypass_evidence_valid == 1U);

    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        2000U));
    CHECK(!dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_UNKNOWN,
        kRoot, 5U, 77U, 2001U, &evidence));
    CHECK(state.custom_project_mount_bypass_count == 1U);
    return 1;
}

static int test_on_start_without_token_and_timeout_fail(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity identity =
        project_identity("3728869100", "3728869100", kRoot);
    dluma_project_launch_admission_initialize(&state);
    CHECK(!dluma_project_launch_admission_attest_on_start_level(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        0x1234U, &identity, 1000U));
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED);

    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        2000U));
    CHECK(!dluma_project_launch_admission_note_custom_source(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        kRoot, 8U, 77U,
        2000U + DLUMA_PROJECT_PREMOUNT_TOKEN_MAX_AGE_MS + 1U));
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED);
    CHECK(state.custom_project_mount_bypass_count == 1U);

    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        3000U));
    CHECK(complete_sources(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        9U, 3001U));
    CHECK(!dluma_project_launch_admission_attest_on_start_level(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        state.token_controller_identity, &identity,
        3000U + DLUMA_PROJECT_PREMOUNT_TOKEN_MAX_AGE_MS + 1U));
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_EXPIRED);
    CHECK(state.attestation_pending == 0U);
    return 1;
}

static int test_incomplete_transition_return_cannot_attest(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectLaunchIdentity identity =
        project_identity("3728869100", "3728869100", kRoot);
    dluma_project_launch_admission_initialize(&state);
    CHECK(begin_update(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        3000U));
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        update_return_rva(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME),
        77U, 0));
    dluma_project_launch_admission_note_selected_map_original(
        &state, 0);
    CHECK(state.token_state ==
          DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED);
    CHECK(!dluma_project_launch_admission_attest_on_start_level(
        &state,
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        state.token_controller_identity,
        &identity, 3100U));
    return 1;
}

static int test_b0_priority_named_failure_and_debounce(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectPreMountEvidence evidence =
        premount_evidence(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
            DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER,
            1000U);
    DlumaProjectLaunchIdentity identity = evidence.identity;

    dluma_project_launch_admission_initialize(&state);
    evidence.startup_b0_complete_authorized = 0U;
    strcpy_s(
        evidence.b0_state, sizeof(evidence.b0_state),
        "CapturingDomains");
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING);
    CHECK(state.last_admission_result ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING);
    CHECK(state.custom_project_premount_attempt_count == 1U);
    CHECK(state.custom_project_premount_denied_count == 1U);
    CHECK(!state.last_premount_hit.set_custom_content_name_reached);
    CHECK(!state.last_premount_hit.update_dlc_content_reached);
    CHECK(!state.last_premount_hit.original_invoked);

    evidence.now_ms = 1100U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING);
    CHECK(state.last_premount_delivery_debounced);
    CHECK(state.denied_premount_duplicate_count == 1U);
    CHECK(state.custom_project_premount_attempt_count == 1U);
    CHECK(state.custom_project_premount_denied_count == 1U);

    dluma_project_launch_admission_initialize(&state);
    evidence = premount_evidence(
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        DLUMA_PROJECT_LAUNCH_ROUTE_DIRECT_PRESENTER, 2000U);
    evidence.startup_b0_complete_authorized = 0U;
    evidence.startup_b0_permanent_source_failure = 1U;
    strcpy_s(
        evidence.b0_state, sizeof(evidence.b0_state),
        "PermanentFailure");
    strcpy_s(
        evidence.startup_b0_source_failure_reason,
        sizeof(evidence.startup_b0_source_failure_reason),
        "manifest_fingerprint_mismatch");
    strcpy_s(
        evidence.startup_b0_rejected_source,
        sizeof(evidence.startup_b0_rejected_source),
        "PlayerMedia");
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PERMANENT_SOURCE_FAILURE);
    CHECK(strcmp(
        state.last_premount_hit.evidence.
            startup_b0_source_failure_reason,
        "manifest_fingerprint_mismatch") == 0);
    CHECK(!state.token_active);
    CHECK(!dluma_project_launch_admission_note_update_dlc_content(
        &state, DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
        update_return_rva(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP),
        evidence.thread_id, 1));

    dluma_project_launch_admission_initialize(&state);
    evidence.startup_b0_permanent_source_failure = 0U;
    evidence.startup_b0_source_failure_reason[0] = 0;
    strcpy_s(
        evidence.b0_state, sizeof(evidence.b0_state),
        "TrackingUnavailable");
    evidence.now_ms = 2900U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED);
    CHECK(!state.token_active);
    CHECK(!state.last_premount_hit.original_invoked);

    dluma_project_launch_admission_initialize(&state);
    strcpy_s(evidence.b0_state, sizeof(evidence.b0_state),
        "MissedBeforeFirstCustomEpoch");
    evidence.now_ms = 3000U;
    CHECK(dluma_project_launch_admission_begin_premount(
              &state, &evidence) ==
          DLUMA_PROJECT_LAUNCH_ADMISSION_B0_RESTART_REQUIRED);

    dluma_project_launch_admission_initialize(&state);
    CHECK(!dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        &identity, 4000U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING));
    CHECK(!dluma_project_launch_admission_note_activation(
        &state, DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START,
        &identity, 4100U,
        DLUMA_PROJECT_LAUNCH_ADMISSION_B0_PREPARING));
    CHECK(state.last_activation_delivery_debounced);
    CHECK(state.denied_activation_duplicate_count == 1U);
    CHECK(state.activation_attempt_count == 1U);
    CHECK(!state.request_valid);
    return 1;
}

int main(void)
{
    if (!test_button_deferred_without_queue() ||
        !test_runtime10_complete_authorized_permits_one_play_activation() ||
        !test_runtime10_permanent_failure_blocks_before_mutation() ||
        !test_every_launch_route_reaches_premount_gate() ||
        !test_stale_and_mismatched_requests_fail_closed() ||
        !test_all_three_owners_deny_before_update() ||
        !test_steam_requires_request_and_content_corroboration() ||
        !test_steam_direct_presenter_creates_fresh_exact_request() ||
        !test_cancelled_confirmation_requires_verified_cleanup() ||
        !test_change_after_fade_is_primary_transition_attestation() ||
        !test_project_root_canonical_contract_and_live_shape() ||
        !test_rootless_identity_debounce_remains_compatible() ||
        !test_change_after_fade_exact_failure_reasons() ||
        !test_steam_attestation_binds_late_transition_identity() ||
        !test_gate_rechecks_b0_registry_authority_and_active_token() ||
        !test_token_source_epoch_and_attestation_lifecycle() ||
        !test_wrong_source_sequence_and_unrelated_source_fail() ||
        !test_on_start_without_token_and_timeout_fail() ||
        !test_incomplete_transition_return_cannot_attest() ||
        !test_b0_priority_named_failure_and_debounce())
        return 1;
    puts("project pre-mount admission fake harness passed");
    return 0;
}
