#ifndef DLUMA_NATIVE_RETAINED_RESOURCE_OWNER_H
#define DLUMA_NATIVE_RETAINED_RESOURCE_OWNER_H
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::retained {
constexpr std::size_t kMaximumInputs = 8;
constexpr std::size_t kMaximumResources = 16384;
constexpr std::size_t kNameCapacity = 512;
enum class Kind : std::uint8_t {
    Unknown, Mesh, Animation, FaceExpression, Cloth, Physics, MaterialPack,
    Material, Texture, Effect, Frontend,
};
enum class Realm : std::uint8_t { Unknown, Global, Project };
enum class Life : std::uint8_t { Empty, Live, Destroying, Ended };
enum class Result : std::uint8_t {
    Succeeded, InvalidArgument, Capacity, CoverageIncomplete, StaleGeneration,
    AddressReused, UnknownObject, InvalidOrigin, BaselineChanged,
    FenceUnavailable, InventoryChanged, NativeReferencesLive,
    DeferredResourceLive, CallbackRetained, UnsupportedRelease,
    NativeReleaseFailed, VerificationFailed, PendingDestruction,
};
struct Identity {
    std::uintptr_t object;
    std::uint64_t generation;
    Kind kind;
};
// Copied at the real open/parse boundary. A later filename resolution cannot
// manufacture this identity. A resource can retain several independent inputs.
struct Input {
    std::uintptr_t file, source;
    std::uint64_t file_generation, source_generation, content_bytes;
    unsigned char content_sha256[32];
    std::uint32_t epoch;
    Realm realm;
};
struct NativeState {
    std::uintptr_t manager, object, vtable, definition, backend, deferred;
    std::uintptr_t callback, callback_context;
    std::uint32_t references, flags;
    Kind kind;
    char name[kNameCapacity];
};
struct Record {
    Identity identity;
    Input inputs[kMaximumInputs];
    std::uint32_t input_count;
    std::uint64_t verified_retirement_transaction;
    Life life;
    bool baseline, global, project;
    NativeState baseline_state;
};
// Callers serialize observation through their actual native owner gate. This
// ledger never supplies a substitute mutex for the native renderer/worker gate.
struct Ledger {
    Record* records;
    std::size_t capacity, count;
    std::uint64_t next_generation, revision, baseline_generation;
    std::uint64_t verified_transaction, verified_revision;
    bool baseline_captured, coverage_lost;
};
struct Fence {
    std::uint64_t transaction, owner_generation, mutation_revision;
    std::uint32_t epoch, thread_id;
    bool authoritative_post_destroy, producers_closed, readers_drained;
    bool native_lock_owned, worker_completions_drained;
};
struct Inventory {
    NativeState* objects;
    std::size_t capacity, count;
    std::uintptr_t manager;
    Kind kind;
    Fence fence;
    bool complete;
};
using CaptureFn = Result (*)(void*, Inventory*) noexcept;
using FenceFn = bool (*)(void*, Fence*) noexcept;
using ReleaseFn = Result (*)(void*, const NativeState&) noexcept;
struct Operations { void* user; CaptureFn capture; FenceFn fence; ReleaseFn release; };
struct Plan {
    Identity* resources;
    std::size_t capacity, count;
    std::uint64_t ledger_revision, baseline_generation;
    Fence fence;
    std::uintptr_t manager;
    Kind kind;
    bool prepared, mutation_started, completed;
};

void Initialize(Ledger*, Record*, std::size_t) noexcept;
Result Constructed(Ledger*, std::uintptr_t, Kind, Identity*) noexcept;
Result RetainInput(Ledger*, Identity, const Input&) noexcept;
Result BeginDestruction(Ledger*, Identity) noexcept;
Result EndDestruction(Ledger*, Identity) noexcept;
Result FindIdentity(const Ledger*, std::uintptr_t, Kind, Identity*) noexcept;
void LoseCoverage(Ledger*) noexcept;
// B0 is captured only at the caller's observed clean startup boundary. Native
// state is copied; future project input cannot convert a baseline into a delta.
Result CaptureBaseline(Ledger*, const Inventory&, std::uint64_t) noexcept;
Result Prepare(const Ledger&, const Inventory&, Plan*) noexcept;
// Read-only preflight before a separately qualified native pack plan releases
// its texture owners. The complete manager inventory is still reconciled;
// only exact zero-ref textures from that plan can have a retained pack here.
Result PreflightWithPackTextures(const Ledger&,const Inventory&,const NativeState*,std::size_t)noexcept;
// A release is a native owner operation, never a direct free or refcount write.
// Fresh inventories surround every call; a disappearing address alone cannot
// satisfy completion without the matching destructor-return observation.
Result Execute(Ledger*, const Operations&, Inventory*, Plan*) noexcept;
Result Verify(const Ledger&, const Inventory&, const Plan&) noexcept;
Result CanRetireSource(const Ledger&, std::uintptr_t source,
                       std::uint64_t source_generation, std::uint64_t transaction) noexcept;
// Reuse storage only after all matching native destructors completed and every
// owning source has independently passed its retirement transaction.
Result ReclaimEnded(Ledger*, std::uintptr_t source,
                    std::uint64_t source_generation, std::uint64_t transaction) noexcept;
const char* ResultName(Result) noexcept;
const char* KindName(Kind) noexcept;
} // namespace
#endif
