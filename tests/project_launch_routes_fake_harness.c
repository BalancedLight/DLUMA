#include "dlw_project_launch_routes.h"

#include <stdio.h>
#include <stdlib.h>
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

typedef struct FakeCompleteObjectLocator {
    DWORD signature;
    DWORD offset;
    DWORD constructor_displacement_offset;
    DWORD type_descriptor_rva;
    DWORD class_descriptor_rva;
    DWORD self_rva;
} FakeCompleteObjectLocator;

static void install_fake_rtti(
    BYTE* image,
    DWORD vtable_rva,
    DWORD col_rva,
    DWORD type_descriptor_rva,
    const char* type_name)
{
    ULONG_PTR col_pointer = (ULONG_PTR)(image + col_rva);
    FakeCompleteObjectLocator* col =
        (FakeCompleteObjectLocator*)(image + col_rva);
    memcpy(
        image + vtable_rva - sizeof(col_pointer),
        &col_pointer, sizeof(col_pointer));
    memset(col, 0, sizeof(*col));
    col->signature = 1U;
    col->type_descriptor_rva = type_descriptor_rva;
    col->self_rva = col_rva;
    strcpy_s(
        (char*)(image + type_descriptor_rva + 16U),
        64U, type_name);
}

static int exercise_owner(
    DlumaProjectLaunchOwner owner,
    DWORD expected_entry,
    BYTE expected_stolen_size,
    DWORD expected_update_return)
{
    const SIZE_T image_size = 0x01D00000U;
    const DlumaProjectLaunchOwnerDescriptor* descriptor =
        dluma_project_launch_owner_descriptor(owner);
    BYTE changed_premount[DLUMA_PROJECT_PREMOUNT_PREFIX_SIZE];
    BYTE* image;
    CHECK(descriptor != 0);
    CHECK(descriptor->owner == owner);
    CHECK(descriptor->premount_entry_rva == expected_entry);
    CHECK(descriptor->premount_stolen_size ==
        expected_stolen_size);
    CHECK(descriptor->update_dlc_content_return_rva ==
        expected_update_return);
    CHECK(descriptor->premount_entry_rva !=
        descriptor->on_start_thunk_rva);
    CHECK(descriptor->on_start_thunk_rva !=
        descriptor->on_start_body_rva);
    CHECK(dluma_project_launch_validate_owner_regions(
        descriptor,
        descriptor->expected_premount_prefix,
        sizeof(descriptor->expected_premount_prefix),
        descriptor->expected_on_start_thunk,
        sizeof(descriptor->expected_on_start_thunk),
        descriptor->expected_on_start_body_prefix,
        sizeof(descriptor->expected_on_start_body_prefix),
        descriptor->expected_after_fade_prefix,
        sizeof(descriptor->expected_after_fade_prefix)));
    memcpy(
        changed_premount, descriptor->expected_premount_prefix,
        sizeof(changed_premount));
    changed_premount[descriptor->premount_stolen_size - 1U] ^= 1U;
    CHECK(!dluma_project_launch_validate_owner_regions(
        descriptor,
        changed_premount, sizeof(changed_premount),
        descriptor->expected_on_start_thunk,
        sizeof(descriptor->expected_on_start_thunk),
        descriptor->expected_on_start_body_prefix,
        sizeof(descriptor->expected_on_start_body_prefix),
        descriptor->expected_after_fade_prefix,
        sizeof(descriptor->expected_after_fade_prefix)));

    image = (BYTE*)calloc(1U, image_size);
    CHECK(image != 0);
    install_fake_rtti(
        image,
        descriptor->primary_vtable_rva,
        descriptor->complete_object_locator_rva,
        descriptor->type_descriptor_rva,
        descriptor->rtti_type_name);
    CHECK(dluma_project_launch_validate_owner_rtti(
        descriptor, image, image_size));
    image[descriptor->type_descriptor_rva + 16U] ^= 1U;
    CHECK(!dluma_project_launch_validate_owner_rtti(
        descriptor, image, image_size));
    free(image);
    return 1;
}

static int test_ui_data_void_rtti(void)
{
    const SIZE_T image_size = 0x01D00000U;
    BYTE* image = (BYTE*)calloc(1U, image_size);
    CHECK(image != 0);
    install_fake_rtti(
        image,
        DLUMA_PROJECT_UI_DATA_VOID_VTABLE_RVA,
        DLUMA_PROJECT_UI_DATA_VOID_COL_RVA,
        DLUMA_PROJECT_UI_DATA_VOID_TYPE_DESCRIPTOR_RVA,
        ".?AVUIDataVoid@@");
    CHECK(dluma_project_launch_validate_ui_data_void_rtti(
        image, image_size));
    image[DLUMA_PROJECT_UI_DATA_VOID_COL_RVA] = 0U;
    CHECK(!dluma_project_launch_validate_ui_data_void_rtti(
        image, image_size));
    free(image);
    return 1;
}

static int test_map_source_allowlist(void)
{
    int source;
    CHECK(dluma_project_launch_is_custom_map_source(19));
    for (source = 24; source <= 27; ++source)
        CHECK(dluma_project_launch_is_custom_map_source(source));
    CHECK(dluma_project_launch_is_custom_map_source(54));
    CHECK(!dluma_project_launch_is_custom_map_source(18));
    CHECK(!dluma_project_launch_is_custom_map_source(28));
    CHECK(!dluma_project_launch_is_custom_map_source(0));
    return 1;
}

int main(void)
{
    if (!exercise_owner(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
            0x007D9F90UL, 16U, 0x007DA1CAUL) ||
        !exercise_owner(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI,
            0x0091A220UL, 12U, 0x0091A2E8UL) ||
        !exercise_owner(
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
            0x0091C6C0UL, 15U, 0x0091C713UL) ||
        !test_ui_data_void_rtti() ||
        !test_map_source_allowlist())
        return 1;
    puts("all three pre-mount route profiles passed");
    return 0;
}
