#include "dlw_project_launch_routes.h"

typedef struct DlumaMsvcCompleteObjectLocator {
    DWORD signature;
    DWORD offset;
    DWORD constructor_displacement_offset;
    DWORD type_descriptor_rva;
    DWORD class_descriptor_rva;
    DWORD self_rva;
} DlumaMsvcCompleteObjectLocator;

static const DlumaProjectLaunchOwnerDescriptor
    g_dluma_project_launch_owner_descriptors[] = {
        {
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_CUSTOM_GAME,
            "MenuCustomGame",
            "OnSelectedMap",
            0x007D9F90UL,
            16U,
            0x007DA1CAUL,
            0x007D82B0UL,
            0x007DA320UL,
            0x007DA4A0UL,
            0x0169C348UL,
            0x01A95BB8UL,
            0x01CCE8C0UL,
            ".?AVMenuCustomGame@@",
            0x03E0UL,
            0x03F0UL,
            0x0400UL,
            {
                0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C,
                0x89, 0x64, 0x24, 0x20, 0x55, 0x48,
                0x8D, 0x6C, 0x24, 0xA9, 0x48, 0x81,
                0xEC, 0x90, 0x00, 0x00, 0x00, 0x48,
            },
            {
                0xE9, 0x6B, 0x20, 0x00, 0x00, 0xCC,
                0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
            },
            {
                0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
                0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                0x74, 0x24, 0x18, 0x48, 0x89, 0x7C,
                0x24, 0x20, 0x41, 0x54, 0x48, 0x81,
            },
            {
                0x40, 0x53, 0x48, 0x83, 0xEC, 0x30,
                0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D,
                0xB0, 0xDC, 0x52, 0x01,
            },
        },
        {
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_START_LEVEL_DI,
            "MenuStartLevelDI",
            "OnSelectedMap",
            0x0091A220UL,
            12U,
            0x0091A2E8UL,
            0x00919A50UL,
            0x0091A360UL,
            0x00919A60UL,
            0x016F4098UL,
            0x01AA1770UL,
            0x01CD15E8UL,
            ".?AVMenuStartLevelDI@@",
            0x03C0UL,
            0x03D0UL,
            0x03E0UL,
            {
                0x40, 0x57, 0x48, 0x83, 0xEC, 0x40,
                0x48, 0x8B, 0xC2, 0x48, 0x8B, 0xF9,
                0x4C, 0x8D, 0x0D, 0x75, 0x13, 0x3A,
                0x01, 0x4C, 0x8D, 0x05, 0xCE, 0xD4,
            },
            {
                0xE9, 0x0B, 0x09, 0x00, 0x00, 0xCC,
                0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
            },
            {
                0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
                0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                0x74, 0x24, 0x18, 0x48, 0x89, 0x7C,
                0x24, 0x20, 0x41, 0x54, 0x48, 0x81,
            },
            {
                0x40, 0x53, 0x48, 0x83, 0xEC, 0x30,
                0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D,
                0xF0, 0xE6, 0x3E, 0x01,
            },
        },
        {
            DLUMA_PROJECT_LAUNCH_OWNER_MENU_STEAM_WORKSHOP,
            "MenuSteamWorkshop",
            "SelectedGamePreMount",
            0x0091C6C0UL,
            15U,
            0x0091C713UL,
            0x0091B520UL,
            0x0091C980UL,
            0x0091B540UL,
            0x016F5388UL,
            0x01AA1950UL,
            0x01CD1650UL,
            ".?AVMenuSteamWorkshop@@",
            0x03C0UL,
            0x03D0UL,
            0x03E0UL,
            {
                0x48, 0x89, 0x5C, 0x24, 0x18, 0x55,
                0x41, 0x54, 0x41, 0x55, 0x48, 0x8D,
                0x6C, 0x24, 0xB9, 0x48, 0x81, 0xEC,
                0x90, 0x00, 0x00, 0x00, 0x4C, 0x8B,
            },
            {
                0xE9, 0x5B, 0x14, 0x00, 0x00, 0xCC,
                0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
            },
            {
                0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
                0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                0x74, 0x24, 0x18, 0x57, 0x41, 0x54,
                0x41, 0x55, 0x48, 0x81, 0xEC, 0xA0,
            },
            {
                0x40, 0x53, 0x48, 0x83, 0xEC, 0x30,
                0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D,
                0x10, 0xCC, 0x3E, 0x01,
            },
        },
};

static int dluma_project_rva_fits(
    DWORD rva, SIZE_T size, SIZE_T image_size)
{
    return (SIZE_T)rva <= image_size &&
        size <= image_size - (SIZE_T)rva;
}

static int dluma_project_rtti_name_matches(
    const char* observed, const char* expected,
    SIZE_T maximum)
{
    SIZE_T index = 0;
    if (!observed || !expected || !maximum) return 0;
    while (index < maximum &&
           observed[index] && expected[index]) {
        if (observed[index] != expected[index])
            return 0;
        ++index;
    }
    return index < maximum &&
        observed[index] == 0 && expected[index] == 0;
}

static int dluma_project_validate_rtti_profile(
    const BYTE* image_base,
    SIZE_T image_size,
    DWORD vtable_rva,
    DWORD col_rva,
    DWORD type_descriptor_rva,
    const char* expected_name)
{
    ULONG_PTR observed_col;
    const DlumaMsvcCompleteObjectLocator* col;
    const char* type_name;
    if (!image_base || !expected_name ||
        vtable_rva < sizeof(ULONG_PTR) ||
        !dluma_project_rva_fits(
            vtable_rva - (DWORD)sizeof(ULONG_PTR),
            sizeof(ULONG_PTR), image_size) ||
        !dluma_project_rva_fits(
            col_rva, sizeof(*col), image_size) ||
        !dluma_project_rva_fits(
            type_descriptor_rva, 17U, image_size))
        return 0;
    memcpy(
        &observed_col,
        image_base + vtable_rva - sizeof(ULONG_PTR),
        sizeof(observed_col));
    if (observed_col !=
            (ULONG_PTR)(image_base + col_rva))
        return 0;
    col = (const DlumaMsvcCompleteObjectLocator*)(
        image_base + col_rva);
    if (col->signature != 1U || col->offset != 0U ||
        col->constructor_displacement_offset != 0U ||
        col->type_descriptor_rva != type_descriptor_rva ||
        col->self_rva != col_rva)
        return 0;
    type_name = (const char*)(
        image_base + type_descriptor_rva + 16U);
    return dluma_project_rtti_name_matches(
        type_name, expected_name,
        image_size - (type_descriptor_rva + 16U));
}

const DlumaProjectLaunchOwnerDescriptor*
dluma_project_launch_owner_descriptor(
    DlumaProjectLaunchOwner owner)
{
    unsigned int index;
    for (index = 0;
         index < (unsigned int)(
             sizeof(g_dluma_project_launch_owner_descriptors) /
             sizeof(g_dluma_project_launch_owner_descriptors[0]));
         ++index)
        if (g_dluma_project_launch_owner_descriptors[index].owner ==
            owner)
            return &g_dluma_project_launch_owner_descriptors[index];
    return 0;
}

int dluma_project_launch_validate_owner_regions(
    const DlumaProjectLaunchOwnerDescriptor* descriptor,
    const BYTE* premount,
    SIZE_T premount_size,
    const BYTE* on_start_thunk,
    SIZE_T on_start_thunk_size,
    const BYTE* on_start_body,
    SIZE_T on_start_body_size,
    const BYTE* after_fade,
    SIZE_T after_fade_size)
{
    if (!descriptor || !premount || !on_start_thunk ||
        !on_start_body || !after_fade ||
        descriptor->premount_stolen_size <
            DLUMA_PROJECT_PREMOUNT_PATCH_SIZE ||
        descriptor->premount_stolen_size >
            DLUMA_PROJECT_PREMOUNT_MAX_STOLEN_SIZE ||
        premount_size < DLUMA_PROJECT_PREMOUNT_PREFIX_SIZE ||
        on_start_thunk_size <
            DLUMA_PROJECT_ON_START_THUNK_SIZE ||
        on_start_body_size <
            DLUMA_PROJECT_ON_START_BODY_PREFIX_SIZE ||
        after_fade_size <
            DLUMA_PROJECT_LAUNCH_AFTER_FADE_PREFIX_SIZE)
        return 0;
    return memcmp(
               premount,
               descriptor->expected_premount_prefix,
               DLUMA_PROJECT_PREMOUNT_PREFIX_SIZE) == 0 &&
           memcmp(
               on_start_thunk,
               descriptor->expected_on_start_thunk,
               DLUMA_PROJECT_ON_START_THUNK_SIZE) == 0 &&
           memcmp(
               on_start_body,
               descriptor->expected_on_start_body_prefix,
               DLUMA_PROJECT_ON_START_BODY_PREFIX_SIZE) == 0 &&
           memcmp(
               after_fade,
               descriptor->expected_after_fade_prefix,
               DLUMA_PROJECT_LAUNCH_AFTER_FADE_PREFIX_SIZE) == 0;
}

int dluma_project_launch_validate_owner_rtti(
    const DlumaProjectLaunchOwnerDescriptor* descriptor,
    const BYTE* image_base,
    SIZE_T image_size)
{
    return descriptor &&
        dluma_project_validate_rtti_profile(
            image_base, image_size,
            descriptor->primary_vtable_rva,
            descriptor->complete_object_locator_rva,
            descriptor->type_descriptor_rva,
            descriptor->rtti_type_name);
}

int dluma_project_launch_validate_ui_data_void_rtti(
    const BYTE* image_base,
    SIZE_T image_size)
{
    return dluma_project_validate_rtti_profile(
        image_base, image_size,
        DLUMA_PROJECT_UI_DATA_VOID_VTABLE_RVA,
        DLUMA_PROJECT_UI_DATA_VOID_COL_RVA,
        DLUMA_PROJECT_UI_DATA_VOID_TYPE_DESCRIPTOR_RVA,
        ".?AVUIDataVoid@@");
}

int dluma_project_launch_is_custom_map_source(int map_source)
{
    return map_source == 19 ||
        (map_source >= 24 && map_source <= 27) ||
        map_source == 54;
}
