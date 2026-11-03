#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "dlw_editor_theme_profile.h"

extern int dluma_dual_host_runtime_is_editor(void);
extern int dluma_sha256_file_hex(const WCHAR *, unsigned long long, char *);

static struct {
    SRWLOCK lock;
    HMODULE module;
    int admitted;
    DWORD image_size;
} profiles[4];

static const struct {
    const WCHAR *module;
    const char *sha256;
} identities[4] = {
    {NULL, "CACD9C96BA5D8C35A23170DC8BE8029DB9AA5E6223F5E6CD5DE3E61D8E72DA28"},
    {L"QuestEditor.dll", "56554E8EC993B1580E51E2E4F37B934B0E1B705D8282BA0309C1EF309CF4A3EB"},
    {L"MEControl_x64_rwdi.dll", "5276211682AB7BA70F8F987AECFD4AE83484332FADD42C8D428524298985EC95"},
    {L"mfc100.dll", "EF2E0DF287AF95855B6B13173259DF847A2CB8A1872BA3D4573E82ABD4FB9699"}
};

int dluma_editor_theme_profile_admit(unsigned int profile)
{
    HMODULE module, pinned;
    WCHAR path[32768];
    char digest[65];
    DWORD length;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    int admitted = 0;
    if (profile >= 4U || !dluma_dual_host_runtime_is_editor()) return 0;
    module = GetModuleHandleW(identities[profile].module);
    if (!module) return 0;
    AcquireSRWLockExclusive(&profiles[profile].lock);
    if (profiles[profile].module == module) {
        admitted = profiles[profile].admitted;
        ReleaseSRWLockExclusive(&profiles[profile].lock);
        return admitted;
    }
    profiles[profile].module = module;
    profiles[profile].admitted = 0;
    profiles[profile].image_size = 0;
    length = GetModuleFileNameW(module, path, 32768U);
    if (!length || length >= 32768U ||
        !dluma_sha256_file_hex(path, 64ULL * 1024ULL * 1024ULL, digest) ||
        lstrcmpiA(digest, identities[profile].sha256)) goto done;
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x100000) goto done;
    nt = (IMAGE_NT_HEADERS64 *)((BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) goto done;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)module, &pinned) || pinned != module) goto done;
    profiles[profile].image_size = nt->OptionalHeader.SizeOfImage;
    profiles[profile].admitted = admitted = 1;
done:
    ReleaseSRWLockExclusive(&profiles[profile].lock);
    return admitted;
}

void *dluma_editor_theme_profile_address(unsigned int profile,
                                        uintptr_t rva, size_t bytes)
{
    BYTE *address = NULL;
    MEMORY_BASIC_INFORMATION memory;
    if (!dluma_editor_theme_profile_admit(profile)) return NULL;
    AcquireSRWLockShared(&profiles[profile].lock);
    if (rva < profiles[profile].image_size &&
        bytes <= profiles[profile].image_size - rva)
        address = (BYTE *)profiles[profile].module + rva;
    ReleaseSRWLockShared(&profiles[profile].lock);
    if (!address || !VirtualQuery(address, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        (uintptr_t)address + bytes > (uintptr_t)memory.BaseAddress + memory.RegionSize)
        return NULL;
    return address;
}
