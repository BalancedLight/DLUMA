#define _HAS_EXCEPTIONS 0

#include "MemoryPatch.h"

extern "C" {
#include "../dlw_common.h"
}

#include <new>

/* This loader intentionally has no CRT startup or CRT DLL imports. These
 * operators let the two tiny std::vector instances use the process heap. */
void* __cdecl operator new(size_t size)
{
    return dluma_alloc(size ? size : 1);
}

void* __cdecl operator new[](size_t size)
{
    return dluma_alloc(size ? size : 1);
}

void __cdecl operator delete(void* memory) noexcept
{
    dluma_free(memory);
}

void __cdecl operator delete[](void* memory) noexcept
{
    dluma_free(memory);
}

void __cdecl operator delete(void* memory, size_t) noexcept
{
    dluma_free(memory);
}

void __cdecl operator delete[](void* memory, size_t) noexcept
{
    dluma_free(memory);
}

#if defined(_MSC_VER)
extern "C" void __fastfail(unsigned int code);
#pragma intrinsic(__fastfail)

namespace std {
void (__cdecl* _Raise_handler)(const stdext::exception&) = nullptr;

[[noreturn]] void __cdecl _Xlength_error(const char*)
{
    /* All loader vectors are bounded to one status or five bytes. Reaching
     * this STL contract violation indicates corrupt process state. */
    __fastfail(7);
}
}  // namespace std
#endif

namespace dluma::fixes {
namespace {

volatile LONG g_patch_lock;
const MemoryAccessor* g_test_accessor;

class PatchLock {
public:
    PatchLock()
    {
        while (dluma_atomic_compare_exchange(&g_patch_lock, 1, 0) != 0) Sleep(1);
    }

    ~PatchLock()
    {
        dluma_atomic_exchange(&g_patch_lock, 0);
    }

    PatchLock(const PatchLock&) = delete;
    PatchLock& operator=(const PatchLock&) = delete;
};

bool DefaultRead(void*, uintptr_t address, uint8_t* output, size_t size)
{
    if (!address || !output || !size || !dluma_is_readable(
            reinterpret_cast<const void*>(address), size)) {
        return false;
    }
    memcpy(output, reinterpret_cast<const void*>(address), size);
    return true;
}

bool DefaultWriteExecutable(void*, uintptr_t address, const uint8_t* bytes,
                            size_t size)
{
    DWORD old_protection = 0;
    DWORD ignored_protection = 0;
    bool flushed;
    bool restored;

    if (!address || !bytes || !size ||
        !VirtualProtect(reinterpret_cast<void*>(address), size,
                        PAGE_EXECUTE_READWRITE, &old_protection)) {
        return false;
    }

    memcpy(reinterpret_cast<void*>(address), bytes, size);
    flushed = FlushInstructionCache(GetCurrentProcess(),
                                    reinterpret_cast<const void*>(address),
                                    size) != FALSE;
    restored = VirtualProtect(reinterpret_cast<void*>(address), size,
                              old_protection, &ignored_protection) != FALSE;
    return flushed && restored;
}

const MemoryAccessor& CurrentAccessor()
{
    static const MemoryAccessor default_accessor = {
        nullptr, DefaultRead, DefaultWriteExecutable};
    return g_test_accessor ? *g_test_accessor : default_accessor;
}

bool ReadBytes(const MemoryAccessor& accessor, uintptr_t address,
               std::vector<uint8_t>& bytes)
{
    return !bytes.empty() && accessor.read &&
           accessor.read(accessor.user_data, address, bytes.data(), bytes.size());
}

}  // namespace

PatchResult ApplyValidatedPatch(MemoryPatch& patch)
{
    PatchLock lock;
    const MemoryAccessor& accessor = CurrentAccessor();
    std::vector<uint8_t> current;
    bool write_succeeded;

    if (!patch.address || patch.expected_original.empty() ||
        patch.expected_original.size() != patch.replacement.size() ||
        !accessor.read || !accessor.write_executable) {
        return PatchResult::InvalidPatch;
    }

    current.resize(patch.replacement.size());
    if (!ReadBytes(accessor, patch.address, current))
        return PatchResult::ReadFailed;

    if (current == patch.replacement) {
        /* Preserve ownership on a repeated call using the same patch object.
         * A fresh object that discovers replacement bytes records external
         * ownership and will never restore them. */
        if (!patch.applied_by_us) patch.applied_by_us = false;
        return PatchResult::AlreadyApplied;
    }

    if (current != patch.expected_original) {
        patch.applied_by_us = false;
        return PatchResult::OriginalBytesMismatch;
    }

    write_succeeded = accessor.write_executable(
        accessor.user_data, patch.address, patch.replacement.data(),
        patch.replacement.size());

    if (!ReadBytes(accessor, patch.address, current))
        return PatchResult::ApplyFailed;
    if (current != patch.replacement)
        return PatchResult::ApplyFailed;

    patch.applied_by_us = true;
    return write_succeeded ? PatchResult::Applied : PatchResult::ApplyFailed;
}

PatchResult RestoreValidatedPatch(MemoryPatch& patch)
{
    PatchLock lock;
    const MemoryAccessor& accessor = CurrentAccessor();
    std::vector<uint8_t> current;
    bool write_succeeded;

    if (!patch.applied_by_us) return PatchResult::NotOwned;
    if (!patch.address || patch.expected_original.empty() ||
        patch.expected_original.size() != patch.replacement.size() ||
        !accessor.read || !accessor.write_executable) {
        return PatchResult::InvalidPatch;
    }

    current.resize(patch.replacement.size());
    if (!ReadBytes(accessor, patch.address, current))
        return PatchResult::ReadFailed;
    if (current != patch.replacement) {
        patch.applied_by_us = false;
        return PatchResult::ModifiedAfterApply;
    }

    write_succeeded = accessor.write_executable(
        accessor.user_data, patch.address, patch.expected_original.data(),
        patch.expected_original.size());

    if (!ReadBytes(accessor, patch.address, current))
        return PatchResult::RestoreFailed;
    if (current != patch.expected_original)
        return PatchResult::RestoreFailed;

    patch.applied_by_us = false;
    return write_succeeded ? PatchResult::Restored : PatchResult::RestoreFailed;
}

void SetMemoryAccessorForTesting(const MemoryAccessor* accessor)
{
    PatchLock lock;
    g_test_accessor = accessor;
}

void ResetMemoryAccessorForTesting()
{
    PatchLock lock;
    g_test_accessor = nullptr;
}

}  // namespace dluma::fixes
