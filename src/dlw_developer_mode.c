#include "dlw_common.h"
#include "dlw_path_security.h"
#include "dlw_developer_mode_core.h"

#define DLUMA_DEVELOPER_MODE_GAMEDLL_SHA256 \
    "cbc7dd7923778936341cdfb755474a0c714079d910849df16ffc5ec88a103b95"
#define DLUMA_IMAGE_SCN_MEM_EXECUTE 0x20000000UL
#define DLUMA_IMAGE_SCN_MEM_WRITE 0x80000000UL

typedef struct DlumaDeveloperModeMemory {
    BYTE* base;
    DWORD image_size;
} DlumaDeveloperModeMemory;

static int dluma_developer_mode_pe_identity(
    HMODULE module, DWORD* timestamp_out, DWORD* image_size_out,
    const BYTE** nt_out)
{
    const BYTE* image = (const BYTE*)module;
    const BYTE* nt;
    DWORD pe_offset;
    if (timestamp_out) *timestamp_out = 0;
    if (image_size_out) *image_size_out = 0;
    if (nt_out) *nt_out = 0;
    if (!image || !dluma_is_readable(image, 0x40U) ||
        *(const WORD*)image != 0x5A4DU)
        return 0;
    pe_offset = *(const DWORD*)(image + 0x3CU);
    if (pe_offset < 0x40U || pe_offset > 0x100000U ||
        !dluma_is_readable(image + pe_offset, 0x80U))
        return 0;
    nt = image + pe_offset;
    if (*(const DWORD*)nt != 0x00004550UL ||
        *(const WORD*)(nt + 0x18U) != 0x020BU)
        return 0;
    if (timestamp_out) *timestamp_out = *(const DWORD*)(nt + 0x08U);
    if (image_size_out) *image_size_out = *(const DWORD*)(nt + 0x50U);
    if (nt_out) *nt_out = nt;
    return 1;
}

static int dluma_developer_mode_rva_in_section(
    DWORD rva, DWORD bytes, DWORD section_rva, DWORD section_size)
{
    return bytes && rva >= section_rva &&
           rva <= section_rva + section_size &&
           bytes <= section_rva + section_size - rva;
}

static int dluma_developer_mode_validate_data_section(
    const BYTE* nt, DWORD image_size)
{
    const BYTE* section;
    WORD section_count;
    WORD optional_size;
    unsigned int index;
    if (!nt || !dluma_is_readable(nt, 0x18U))
        return 0;
    section_count = *(const WORD*)(nt + 0x06U);
    optional_size = *(const WORD*)(nt + 0x14U);
    if (!section_count || section_count > 96U || optional_size < 0x70U)
        return 0;
    section = nt + 0x18U + optional_size;
    if (!dluma_is_readable(section, (SIZE_T)section_count * 40U))
        return 0;
    for (index = 0; index < section_count; ++index, section += 40U)
    {
        DWORD virtual_size = *(const DWORD*)(section + 0x08U);
        DWORD virtual_rva = *(const DWORD*)(section + 0x0CU);
        DWORD characteristics = *(const DWORD*)(section + 0x24U);
        if (virtual_rva > image_size ||
            virtual_size > image_size - virtual_rva)
            return 0;
        if (dluma_developer_mode_rva_in_section(
                DLUMA_DEVELOPER_MODE_CHEATS_FLAG_RVA, 1,
                virtual_rva, virtual_size) &&
            dluma_developer_mode_rva_in_section(
                DLUMA_DEVELOPER_MODE_NO_LOGOS_FLAG_RVA, 1,
                virtual_rva, virtual_size) &&
            dluma_developer_mode_rva_in_section(
                DLUMA_DEVELOPER_MODE_SKIP_WAIT_FLAG_RVA, 1,
                virtual_rva, virtual_size))
        {
            return (characteristics & DLUMA_IMAGE_SCN_MEM_WRITE) != 0 &&
                   (characteristics & DLUMA_IMAGE_SCN_MEM_EXECUTE) == 0;
        }
    }
    return 0;
}

static int dluma_developer_mode_memory_read(
    void* user, uint32_t rva, void* out, size_t size)
{
    DlumaDeveloperModeMemory* memory = (DlumaDeveloperModeMemory*)user;
    if (!memory || !memory->base || !out || !size ||
        rva > memory->image_size || size > memory->image_size - rva ||
        !dluma_is_readable(memory->base + rva, size))
        return 0;
    memcpy(out, memory->base + rva, size);
    return 1;
}

static int dluma_developer_mode_flag_rva(uint32_t rva)
{
    return rva == DLUMA_DEVELOPER_MODE_CHEATS_FLAG_RVA ||
           rva == DLUMA_DEVELOPER_MODE_NO_LOGOS_FLAG_RVA ||
           rva == DLUMA_DEVELOPER_MODE_SKIP_WAIT_FLAG_RVA;
}

static int dluma_developer_mode_memory_write(
    void* user, uint32_t rva, const void* bytes, size_t size)
{
    DlumaDeveloperModeMemory* memory = (DlumaDeveloperModeMemory*)user;
    MEMORY_BASIC_INFORMATION info;
    DWORD protection;
    BYTE value;
    if (!memory || !memory->base || !bytes || size != 1 ||
        !dluma_developer_mode_flag_rva(rva) ||
        rva >= memory->image_size)
        return 0;
    value = *(const BYTE*)bytes;
    if (value > 1 ||
        VirtualQuery(memory->base + rva, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    protection = info.Protect & 0xFFUL;
    if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY)
        return 0;
    *(volatile BYTE*)(memory->base + rva) = value;
    return 1;
}

int dluma_developer_mode_apply_startup_profile(HMODULE gamedll)
{
    DWORD timestamp = 0;
    DWORD image_size = 0;
    const BYTE* nt = 0;
    WCHAR module_path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaDeveloperModeMemory memory;
    DlumaDeveloperModeCoreInput input;
    DlumaDeveloperModeCoreResult result;
    int applied;
    if (!g_dluma.config.developer_mode)
    {
        dluma_log_pair("INFO", "developer_mode_startup_profile", "disabled");
        return 1;
    }
    if (!dluma_developer_mode_pe_identity(
            gamedll, &timestamp, &image_size, &nt))
    {
        dluma_log_pair(
            "WARN", "developer_mode_startup_profile", "pe_identity_unreadable");
        return 0;
    }
    sha256[0] = 0;
    if (!dluma_get_module_path_checked(
            gamedll, module_path, DLUMA_ARRAY_COUNT(module_path)) ||
        !dluma_sha256_file_hex(
            module_path, 512ULL * 1024ULL * 1024ULL, sha256) ||
        dluma_stricmp(sha256, DLUMA_DEVELOPER_MODE_GAMEDLL_SHA256) != 0)
    {
        dluma_log_pair(
            "WARN", "developer_mode_startup_profile",
            "unsupported_gamedll_fingerprint");
        return 0;
    }
    if (!dluma_developer_mode_validate_data_section(nt, image_size))
    {
        dluma_log_pair(
            "WARN", "developer_mode_startup_profile",
            "writable_data_section_validation_failed");
        return 0;
    }

    memory.base = (BYTE*)gamedll;
    memory.image_size = image_size;
    input.enabled = 1;
    input.timestamp = timestamp;
    input.image_size = image_size;
    input.read = dluma_developer_mode_memory_read;
    input.write = dluma_developer_mode_memory_write;
    input.user = &memory;
    applied = dluma_developer_mode_core_apply(&input, &result);
    if (!applied)
    {
        dluma_log_pair(
            "WARN", "developer_mode_startup_profile",
            result.failure_stage ? result.failure_stage : "validation_failed");
        return 0;
    }
    dluma_log_pair("INFO", "developer_mode_startup_profile", "applied");
    dluma_log_u64(
        "INFO", "developer_mode_startup_flags_changed",
        (ULONGLONG)result.changed_count);
    dluma_log_line(
        "INFO",
        "Developer keys/cheats, startup-logo suppression, and loaded-level wait bypass were enabled before the original game-script initializer.");
    return 1;
}
