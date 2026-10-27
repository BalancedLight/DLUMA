#ifndef DLUMA_FIXES_FIX_REGISTRY_H
#define DLUMA_FIXES_FIX_REGISTRY_H

#include "../dlw_win32.h"

#include <cstdint>
#include <vector>

namespace dluma::fixes {

struct RuntimeContext {
    HMODULE gamedll_module;
    uintptr_t gamedll_base;
    uint32_t gamedll_timestamp;
    uint32_t gamedll_size_of_image;

    HMODULE engine_module;
    uintptr_t engine_base;
    uint32_t engine_timestamp;
    uint32_t engine_size_of_image;

    HMODULE filesystem_module;
    uintptr_t filesystem_base;
    uint32_t filesystem_timestamp;
    uint32_t filesystem_size_of_image;

    RuntimeContext();
    explicit RuntimeContext(HMODULE module);
};

enum class FixState {
    Disabled,
    UnsupportedBuild,
    OriginalBytesMismatch,
    DiagnosticOnly,
    Applied,
    RuntimePartial,
    RuntimeFailed,
    AlreadyApplied,
    ApplyFailed,
    Restored,
};

struct FixStatus {
    const char* id;
    const char* display_name;
    FixState state;
    uintptr_t target_address;
    uintptr_t target_rva;
    bool owned_by_loader;
};

bool InitializeAllFixes(const RuntimeContext& context);
void ShutdownAllFixes();

const std::vector<FixStatus>& GetFixStatuses();

}  // namespace dluma::fixes

#endif
