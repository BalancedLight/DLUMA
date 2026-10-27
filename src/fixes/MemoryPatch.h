#ifndef DLUMA_FIXES_MEMORY_PATCH_H
#define DLUMA_FIXES_MEMORY_PATCH_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dluma::fixes {

enum class PatchResult {
    InvalidPatch,
    ReadFailed,
    OriginalBytesMismatch,
    AlreadyApplied,
    Applied,
    ApplyFailed,
    NotOwned,
    ModifiedAfterApply,
    Restored,
    RestoreFailed,
};

struct MemoryPatch {
    uintptr_t address;
    std::vector<uint8_t> expected_original;
    std::vector<uint8_t> replacement;
    bool applied_by_us;
};

/* The injectable accessor keeps unit tests on mock buffers. Production uses
 * the default in-memory Win32 accessor and never opens the DLL on disk. */
struct MemoryAccessor {
    void* user_data;
    bool (*read)(void* user_data, uintptr_t address, uint8_t* output,
                 size_t size);
    bool (*write_executable)(void* user_data, uintptr_t address,
                             const uint8_t* bytes, size_t size);
};

PatchResult ApplyValidatedPatch(MemoryPatch& patch);
PatchResult RestoreValidatedPatch(MemoryPatch& patch);

void SetMemoryAccessorForTesting(const MemoryAccessor* accessor);
void ResetMemoryAccessorForTesting();

}  // namespace dluma::fixes

#endif
