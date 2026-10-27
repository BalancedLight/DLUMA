#ifndef DLUMA_FIXES_FILESYSTEM_SOURCE_WINDOWS_VIEW_H
#define DLUMA_FIXES_FILESYSTEM_SOURCE_WINDOWS_VIEW_H

#include "FilesystemSourceInventory.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::vfs {

/* Dying Light 1.49 Windows filesystem_x64_rwdi.dll. These RVAs are enabled
 * only after the host validates the exact PE identity plus the construction,
 * add, remove, lock, global-reference, vtable, and path-accessor evidence. */
constexpr std::uintptr_t kFilesystemSourceHeadRva = 0x00053398U;
constexpr std::uintptr_t kFilesystemSourceTailRva = 0x000533A0U;
constexpr std::uintptr_t kFilesystemSourceCountRva = 0x000533A8U;
constexpr std::uintptr_t kFilesystemFallbackLinkRva = 0x00053AC8U;
constexpr std::uintptr_t kFilesystemSourcesSrwLockRva = 0x00053AD0U;
constexpr std::uintptr_t kFilesystemAddSourcePathRva = 0x00012DD0U;
constexpr std::uintptr_t kFilesystemAddSourceObjectRva = 0x000137B0U;
constexpr std::uintptr_t kFilesystemRemoveSourcePathRva = 0x00013BA0U;
constexpr std::uintptr_t kFilesystemRemoveSourceObjectRva = 0x00014220U;

using ReadSourceCountFn = bool (*)(
    void* user, std::uintptr_t address,
    std::uint32_t* output) noexcept;
using ReadSourceRegistryGenerationFn = std::uint64_t (*)(
    void* user) noexcept;
using SourceRegistryObservationIdleFn = bool (*)(
    void* user) noexcept;

struct FilesystemSourceWindowsBinding {
    std::uintptr_t filesystem_base;
    ReadSourcePointerFn read_pointer;
    ReadSourceCountFn read_count;
    CopySourcePathFn copy_path;
    ReadSourceRegistryGenerationFn read_generation;
    SourceRegistryObservationIdleFn observation_idle;
    void* user;
    bool supported_pe_identity_validated;
    bool registry_layout_validated;
    bool source_path_reader_validated;
};

struct FilesystemSourceWindowsViewDiagnostics {
    bool filesystem_registry_view_available;
    std::uintptr_t filesystem_registry_head;
    std::uintptr_t filesystem_registry_tail;
    std::uint32_t filesystem_registry_count;
    bool filesystem_registry_fallback_known;
    bool filesystem_registry_fallback_installed;
    std::uintptr_t filesystem_registry_fallback_link;
    std::uint64_t filesystem_registry_generation_before;
    std::uint64_t filesystem_registry_generation_after;
    bool filesystem_registry_lock_acquired;
    FilesystemSourceInventoryFailure filesystem_registry_inventory_result;
};

bool CaptureLiveFilesystemSourceRegistryView(
    const FilesystemSourceWindowsBinding* binding,
    FilesystemSourceInventory* output,
    FilesystemSourceWindowsViewDiagnostics* diagnostics) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::vfs

#endif
