#ifndef DLUMA_FIXES_FILESYSTEM_SOURCE_INVENTORY_H
#define DLUMA_FIXES_FILESYSTEM_SOURCE_INVENTORY_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::vfs {

/* The supported Player filesystem keeps source ownership in an intrusive
 * doubly-linked list.  This helper knows only the independently validated
 * link contract; all process-memory access remains injectable. */
constexpr std::size_t kFilesystemSourceInventoryCapacity = 192;
constexpr std::size_t kFilesystemSourcePathCapacity = 512;
constexpr std::size_t kFilesystemSourceLinkOffset = 8;

enum class FilesystemSourceInventoryFailure : std::uint8_t {
    None,
    InvalidArguments,
    SynchronizationNotHeld,
    LayoutNotValidated,
    RegistryUnavailable,
    CapacityExceeded,
    LinkUnreadable,
    SourceUnreadable,
    PreviousLinkMismatch,
    DuplicateOrCyclicLink,
    TailMismatch,
    FallbackStateUnknown,
    FallbackMismatch,
    CountMismatch,
    PathUnavailable,
    ClassificationUnavailable,
    RegistryChangedDuringCapture,
};

using ReadSourcePointerFn = bool (*)(
    void* user, std::uintptr_t address,
    std::uintptr_t* output) noexcept;

using CopySourcePathFn = bool (*)(
    void* user, std::uintptr_t source,
    char* output, std::size_t capacity) noexcept;

struct FilesystemSourceInventoryReader {
    ReadSourcePointerFn read_pointer;
    CopySourcePathFn copy_path;
    void* user;
};

struct FilesystemSourceRegistryView {
    std::uintptr_t head_link;
    std::uintptr_t tail_link;
    std::uintptr_t fallback_link;
    std::uint64_t registry_generation;
    std::uint32_t reported_count;
    bool synchronization_held;
    bool layout_validated;
    bool registry_generation_observed;
    bool fallback_state_known;
    bool fallback_installed;
};

struct FilesystemSourceInventoryRecord {
    std::uintptr_t link;
    std::uintptr_t source;
    /* One-based position in this frozen intrusive-list traversal.  This is
     * not the content ledger's successful-add ordinal. */
    std::uint32_t list_ordinal;
    /* Exact byte count copied from the live source path accessor.  The path
     * buffer itself remains the raw accessor representation; classification
     * writes its canonical form into a separate report row. */
    std::uint32_t raw_path_length;
    bool native_fallback;
    char path[kFilesystemSourcePathCapacity];
};

struct FilesystemSourceInventory {
    std::uint32_t reported_count;
    std::uint32_t source_count;
    std::uintptr_t observed_head_link;
    std::uintptr_t observed_tail_link;
    std::uintptr_t observed_fallback_link;
    std::uint64_t registry_generation;
    FilesystemSourceInventoryFailure failure;
    bool complete;
    bool registry_generation_observed;
    bool fallback_state_known;
    bool fallback_installed;
    FilesystemSourceInventoryRecord
        sources[kFilesystemSourceInventoryCapacity];
};

bool ExactFilesystemSourceInventoryIdentityMatches(
    const FilesystemSourceInventory* baseline,
    const FilesystemSourceInventory* current) noexcept;

bool CaptureFilesystemSourceInventory(
    const FilesystemSourceRegistryView* registry,
    const FilesystemSourceInventoryReader* reader,
    FilesystemSourceInventory* output) noexcept;

const char* FilesystemSourceInventoryFailureName(
    FilesystemSourceInventoryFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::vfs

#endif
