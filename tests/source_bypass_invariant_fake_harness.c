#include "dlw_project_launch_admission.h"

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
    "x:\\devtools\\workshop\\devtoolsdefaultproject";

static DlumaProjectLaunchIdentity identity(void)
{
    DlumaProjectLaunchIdentity value;
    memset(&value, 0, sizeof(value));
    strcpy_s(
        value.selection_key, sizeof(value.selection_key),
        "DevToolsDefaultProject/example");
    strcpy_s(
        value.campaign_name, sizeof(value.campaign_name),
        "DevToolsDefaultProject");
    strcpy_s(
        value.map_name, sizeof(value.map_name), "example");
    strcpy_s(
        value.normalized_project_root,
        sizeof(value.normalized_project_root), kRoot);
    value.profile_index = 2;
    value.transition_identity_complete = 1U;
    return value;
}

static DlumaProjectPreMountEvidence premount(void)
{
    DlumaProjectPreMountEvidence value;
    memset(&value, 0, sizeof(value));
    value.route = DLUMA_PROJECT_LAUNCH_ROUTE_DELAYED_TRANSITION;
    value.owner =
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME;
    value.entry_rva = 0x007D9F90U;
    value.thread_id = 77U;
    value.controller = 0xABCDEFU;
    value.controller_vtable = 0x180000000U + 0x0169C348U;
    value.ui_data = 0x2222U;
    value.payload = 0x3333U;
    value.caller_return_address = 0x180123456U;
    strcpy_s(
        value.caller_module, sizeof(value.caller_module),
        "gamedll_x64_rwdi.dll");
    value.caller_module_base = 0x180000000U;
    value.caller_module_rva = 0x123456U;
    value.map_source = 24;
    value.custom_map_source = 1U;
    value.identity = identity();
    strcpy_s(
        value.b0_state, sizeof(value.b0_state),
        "CompleteAuthorized");
    value.b0_generation = 11U;
    value.frame_route_token = 12U;
    value.now_ms = 1000U;
    value.startup_b0_complete_authorized = 1U;
    value.restart_registry_can_admit = 1U;
    return value;
}

static DlumaProjectSourceBypassEvidence evidence(
    ULONG_PTR source, const char* requested)
{
    DlumaProjectSourceBypassEvidence value;
    memset(&value, 0, sizeof(value));
    value.source_object = source;
    value.source_flags = 0x1234U;
    strcpy_s(
        value.requested_source_path,
        sizeof(value.requested_source_path), requested);
    strcpy_s(
        value.source_path, sizeof(value.source_path),
        "x:\\devtools\\workshop\\devtoolsdefaultproject\\data");
    strcpy_s(
        value.source_kind, sizeof(value.source_kind),
        "DataDirectory");
    value.caller_return_address = 0x180123456ULL;
    strcpy_s(
        value.caller_module, sizeof(value.caller_module),
        "gamedll_x64_rwdi.dll");
    value.caller_module_base = 0x180000000ULL;
    value.caller_module_rva = 0x123456U;
    value.thread_id = 77U;
    strcpy_s(
        value.current_campaign,
        sizeof(value.current_campaign),
        "DevToolsDefaultProject");
    strcpy_s(
        value.current_map,
        sizeof(value.current_map), "example");
    strcpy_s(
        value.project_identity,
        sizeof(value.project_identity), kRoot);
    value.active_launch_controller = 0xABCDEFU;
    value.last_activation_route =
        DLUMA_PROJECT_LAUNCH_ROUTE_MOUSE_START;
    value.last_premount_route =
        DLUMA_PROJECT_LAUNCH_ROUTE_DELAYED_TRANSITION;
    value.last_premount_owner =
        DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME;
    strcpy_s(
        value.last_b0_state, sizeof(value.last_b0_state),
        "CompleteAuthorized");
    value.allowed_token_generation = 9U;
    value.token_state =
        DLUMA_PROJECT_PREMOUNT_TOKEN_ARMED_PRE_MOUNT;
    value.bound_epoch = 91U;
    value.set_custom_content_generation = 9U;
    value.update_dlc_content_generation = 9U;
    return value;
}

static int arm_update(
    DlumaProjectLaunchAdmissionState* state)
{
    DlumaProjectPreMountEvidence selected = premount();
    CHECK(dluma_project_launch_admission_begin_premount(
        state, &selected) ==
        DLUMA_PROJECT_LAUNCH_ADMISSION_ALLOWED);
    dluma_project_launch_admission_note_selected_map_original(
        state, 1);
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        state, selected.owner, 0x007DA1CAU,
        selected.thread_id, 1));
    return 1;
}

static int test_first_bypass_is_complete_and_sticky(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectSourceBypassEvidence first =
        evidence(0x1111U, "\\\\?\\X:\\first");
    DlumaProjectSourceBypassEvidence second =
        evidence(0x2222U, "\\?\\X:\\second");
    dluma_project_launch_admission_initialize(&state);
    CHECK(!dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        kRoot, 91U, 77U, 1000U, &first));
    CHECK(state.custom_project_mount_bypassed_admission_gate);
    CHECK(state.custom_project_mount_bypass_count == 1U);
    CHECK(state.bypass_evidence_valid);
    CHECK(state.bypass_evidence.source_object == 0x1111U);
    CHECK(state.bypass_evidence.source_flags == 0x1234U);
    CHECK(strcmp(
        state.bypass_evidence.requested_source_path,
        "\\\\?\\X:\\first") == 0);
    CHECK(!dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        "X:\\another", 92U, 88U, 1001U, &second));
    CHECK(state.custom_project_mount_bypass_count == 2U);
    CHECK(state.bypass_evidence.source_object == 0x1111U);
    CHECK(strcmp(state.bypass_project_root, kRoot) == 0);
    return 1;
}

static int test_exact_ordered_source_set_is_admitted(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectSourceBypassEvidence active =
        evidence(0x1111U, "\\?\\X:\\source");
    dluma_project_launch_admission_initialize(&state);
    CHECK(arm_update(&state));
    CHECK(dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT,
        kRoot, 91U, 77U, 1001U, &active));
    CHECK(dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
        kRoot, 91U, 77U, 1002U, &active));
    CHECK(dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC,
        kRoot, 91U, 77U, 1003U, &active));
    CHECK(state.token_state ==
        DLUMA_PROJECT_PREMOUNT_TOKEN_SOURCE_SET_COMPLETE);
    CHECK(state.custom_project_source_epoch_bound_count == 1U);
    CHECK(!state.custom_project_mount_bypassed_admission_gate);
    CHECK(dluma_project_launch_admission_note_update_dlc_content(
        &state, DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
        0x007DA1CAU, 77U, 0));
    dluma_project_launch_admission_note_selected_map_original(
        &state, 0);
    CHECK(state.attestation_pending);
    CHECK(!state.token_active);
    return 1;
}

static int test_out_of_order_source_fails_closed(void)
{
    DlumaProjectLaunchAdmissionState state;
    DlumaProjectSourceBypassEvidence active =
        evidence(0x1111U, "\\?\\X:\\source");
    dluma_project_launch_admission_initialize(&state);
    CHECK(arm_update(&state));
    CHECK(!dluma_project_launch_admission_note_custom_source_ex(
        &state, DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY,
        kRoot, 91U, 77U, 1001U, &active));
    CHECK(state.token_state ==
        DLUMA_PROJECT_PREMOUNT_TOKEN_FAILED);
    CHECK(state.custom_project_mount_bypassed_admission_gate);
    CHECK(strcmp(
        state.first_failure_reason,
        "source_before_project_root_or_epoch_mismatch") == 0);
    return 1;
}

int main(void)
{
    if (!test_first_bypass_is_complete_and_sticky() ||
        !test_exact_ordered_source_set_is_admitted() ||
        !test_out_of_order_source_fails_closed())
        return 1;
    puts("source bypass invariant fake harness passed");
    return 0;
}
