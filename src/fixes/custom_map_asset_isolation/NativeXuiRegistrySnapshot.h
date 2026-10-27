#ifndef DLUMA_NATIVE_XUI_REGISTRY_SNAPSHOT_H
#define DLUMA_NATIVE_XUI_REGISTRY_SNAPSHOT_H
#include <cstddef>
#include <cstdint>
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::xui {
constexpr char kPlayerEngineSha256[] =
    "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c";
constexpr std::size_t kMaximumEntries = 32768;
constexpr std::size_t kMaximumPackIds = 262144;
constexpr std::size_t kMaximumStringBytes = 32U * 1024U * 1024U;
constexpr std::size_t kMaximumNameBytes = 16384;

enum class EntryKind : std::uint8_t { Pack, Skin, Screen };
enum class Result : std::uint8_t {
    Succeeded, InvalidArgument, ProfileMismatch, FenceUnavailable,
    WrongOwnerType, InvalidVector, InvalidEntry, InvalidString,
    CapacityExceeded, ReadFailed, Changed,
};
struct Fence {
    std::uintptr_t manager;
    std::uint64_t generation, revision, frame_token;
    std::uint32_t thread_id;
};
using ReadFn = bool(*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
// The native owner supplies this only while its actual registry/instance
// writer routes are fenced. This reader does not acquire or invent that gate.
using FenceFn = bool(*)(void*, Fence*) noexcept;
struct Access {
    std::uintptr_t engine_base;
    char sha256[65];
    ReadFn read;
    FenceFn fence;
    void* user;
};
struct Entry {
    EntryKind kind;
    std::int32_t id;
    std::uint32_t name_offset, name_bytes, template_offset, template_bytes;
    std::uint32_t pack_offset, pack_count;
    std::int16_t category;
    std::uint8_t screen_flags, skin_loaded;
    // The skin's three native cached layout words are kept separately from
    // its registration. They may change during native loading.
    std::uint32_t skin_layout[3];
};
struct Snapshot {
    Entry* entries;
    std::size_t entry_capacity, entry_count;
    std::uint32_t* pack_ids;
    std::size_t pack_capacity, pack_count;
    char* strings;
    std::size_t string_capacity, string_bytes;
    Fence fence;
    Result result;
    bool complete;
};
// Deep-copy only. No borrowed string/array pointers survive this call. The
// owner generation and writer fence must match before and after both passes.
Result Capture(const Access&, Snapshot*) noexcept;
// Equality of registry metadata does not prove equality of the compiled XUI
// bodies or their providers. The owning adapter must verify those separately.
bool Equal(const Snapshot&, const Snapshot&, bool include_loaded_state) noexcept;
const char* ResultName(Result) noexcept;

struct Consumer {
    std::uintptr_t object, interface_object, vtable;
    std::uint8_t flags;
    bool awaiting_destruction;
};
struct Consumers {
    Consumer* objects;
    std::size_t capacity, count, active_count, dead_count;
    std::size_t input_receiver_references;
    Fence fence;
    Result result;
    bool complete;
};
// These are actual native factory children, including objects already retired
// from its active list but not yet destroyed. Input references are counted
// separately. Empty lists do not prove compiled-table, renderer or callback
// closure; the owning cleanup adapter must establish those independently.
Result CaptureConsumers(const Access&, Consumers*) noexcept;

constexpr std::size_t kMaximumManagers = 128;
enum class ManagerLife : std::uint8_t { Empty, Constructing, Live, Destroying, Ended };
struct ManagerRecord {
    std::uintptr_t manager, level;
    std::uint64_t generation;
    std::uint32_t birth_epoch;
    ManagerLife life;
};
struct ManagerToken {
    std::uintptr_t ledger;
    std::uint64_t generation;
    std::uint32_t index;
    ManagerLife phase;
    bool active;
};
struct ManagerLedger {
    ManagerRecord records[kMaximumManagers] = {};
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    std::atomic<bool> lost{false};
    std::uint64_t next_generation = 0, revision = 0;
    bool startup_sealed = false;
};
// These operations only observe the native class-wide constructor/destructor.
// They never delete a manager or infer a provider from its level/path.
bool SealManagerStartup(ManagerLedger*) noexcept;
bool BeginManagerConstruction(ManagerLedger*, std::uintptr_t manager,
    std::uintptr_t level, std::uint32_t epoch, ManagerToken*) noexcept;
bool EndManagerConstruction(ManagerLedger*, ManagerToken*, const Access&,
    std::uintptr_t native_return) noexcept;
bool BeginManagerDestruction(ManagerLedger*, std::uintptr_t, ManagerToken*) noexcept;
bool EndManagerDestruction(ManagerLedger*, ManagerToken*) noexcept;
bool CopyManagerInventory(ManagerLedger*, ManagerRecord*, std::size_t capacity,
    std::size_t* count, std::uint64_t* revision) noexcept;
// Identity lookup for observed constructor/destructor child calls. Transitional
// records do not grant a stable snapshot fence or a completed lifetime receipt.
bool LookupManager(ManagerLedger*,std::uintptr_t manager,ManagerRecord*)noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::xui
#endif
