#ifndef DLUMA_PROJECT_LAUNCH_ROUTES_H
#define DLUMA_PROJECT_LAUNCH_ROUTES_H

#include "dlw_project_launch_admission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_PROJECT_PREMOUNT_PATCH_SIZE 12U
#define DLUMA_PROJECT_PREMOUNT_PREFIX_SIZE 24U
#define DLUMA_PROJECT_PREMOUNT_MAX_STOLEN_SIZE 16U
#define DLUMA_PROJECT_ON_START_THUNK_SIZE 12U
#define DLUMA_PROJECT_ON_START_BODY_PREFIX_SIZE 24U
#define DLUMA_PROJECT_LAUNCH_AFTER_FADE_PREFIX_SIZE 16U
#define DLUMA_PROJECT_UI_DATA_VOID_VTABLE_RVA 0x0190B958UL
#define DLUMA_PROJECT_UI_DATA_VOID_COL_RVA 0x01B1D3E8UL
#define DLUMA_PROJECT_UI_DATA_VOID_TYPE_DESCRIPTOR_RVA 0x01CBB5A8UL

typedef struct DlumaProjectLaunchOwnerDescriptor {
    DlumaProjectLaunchOwner owner;
    const char* class_name;
    const char* premount_method_name;
    DWORD premount_entry_rva;
    BYTE premount_stolen_size;
    DWORD update_dlc_content_return_rva;

    DWORD on_start_thunk_rva;
    DWORD on_start_body_rva;
    DWORD after_fade_rva;

    DWORD primary_vtable_rva;
    DWORD complete_object_locator_rva;
    DWORD type_descriptor_rva;
    const char* rtti_type_name;

    DWORD selected_campaign_offset;
    DWORD selected_map_offset;
    DWORD selected_profile_offset;

    BYTE expected_premount_prefix[
        DLUMA_PROJECT_PREMOUNT_PREFIX_SIZE];
    BYTE expected_on_start_thunk[
        DLUMA_PROJECT_ON_START_THUNK_SIZE];
    BYTE expected_on_start_body_prefix[
        DLUMA_PROJECT_ON_START_BODY_PREFIX_SIZE];
    BYTE expected_after_fade_prefix[
        DLUMA_PROJECT_LAUNCH_AFTER_FADE_PREFIX_SIZE];
} DlumaProjectLaunchOwnerDescriptor;

const DlumaProjectLaunchOwnerDescriptor*
dluma_project_launch_owner_descriptor(
    DlumaProjectLaunchOwner owner);

int dluma_project_launch_validate_owner_regions(
    const DlumaProjectLaunchOwnerDescriptor* descriptor,
    const BYTE* premount,
    SIZE_T premount_size,
    const BYTE* on_start_thunk,
    SIZE_T on_start_thunk_size,
    const BYTE* on_start_body,
    SIZE_T on_start_body_size,
    const BYTE* after_fade,
    SIZE_T after_fade_size);

int dluma_project_launch_validate_owner_rtti(
    const DlumaProjectLaunchOwnerDescriptor* descriptor,
    const BYTE* image_base,
    SIZE_T image_size);

int dluma_project_launch_validate_ui_data_void_rtti(
    const BYTE* image_base,
    SIZE_T image_size);

int dluma_project_launch_is_custom_map_source(int map_source);

#ifdef __cplusplus
}
#endif

#endif
