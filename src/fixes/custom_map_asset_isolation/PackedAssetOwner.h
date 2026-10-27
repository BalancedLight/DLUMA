#ifndef DLUMA_PACKED_ASSET_OWNER_H
#define DLUMA_PACKED_ASSET_OWNER_H
#include "NativeRetainedResourceOwner.h"

namespace dluma::fixes::custom_map_asset_isolation::packed {
constexpr std::size_t kMaximumPacks = 2048;
constexpr std::size_t kMaximumPackTextures = retained::kMaximumResources;
constexpr std::size_t kMaximumPhysicalGroups = 256;
enum class Result : std::uint8_t {
    Succeeded, InvalidArgument, Capacity, Incomplete, UnknownPack, UnknownTexture,
    StaleIdentity, BaselineChanged, NativeReferencesLive, UnsupportedResourceType,
    UnsupportedPatch, PendingInput, PendingNativeCall, FenceUnavailable,
    NativeFailure, VerificationFailed, QueueStillRetained, UnclassifiedStreamOwner,
};
struct Key { std::uintptr_t address; std::uint64_t generation; std::uint32_t slot; };
struct Origin {
    Key key;
    retained::Input inputs[retained::kMaximumInputs];
    std::uint32_t input_count, pending_inputs;
    std::uintptr_t baseline_metadata;
    std::uint64_t verified_transaction;
    bool baseline, project, global, load_completed, registered;
    bool destructor_entered, destructor_returned, unregistered, empty;
    std::uint32_t unclassified_stream_epoch;
};
struct Ledger {
    Origin* records; std::size_t capacity, count;
    std::uint64_t next_generation, revision, baseline_generation;
    std::uint64_t verified_revision, verified_transaction;
    bool baseline_captured, lost;
};
struct NativePack {
    std::uintptr_t object, metadata, file, overlapped, patch;
    std::uint32_t flags, references, load_flags;
    std::uint64_t resource_types[4];
    std::uint32_t physical_group_count, mesh_count;
    std::size_t texture_begin, texture_count;
    std::uint32_t queued_entries[4];
    bool renderer_streams_closed;
};
struct Fence {
    std::uint64_t transaction, owner_generation, mutation_revision;
    std::uint32_t epoch, thread_id;
    bool authoritative_post_destroy, producers_closed, consumers_drained;
    bool renderer_lock_owned, worker_lock_owned;
};
struct Snapshot {
    NativePack* packs; std::size_t pack_capacity, pack_count;
    retained::NativeState* textures; std::size_t texture_capacity, texture_count;
    std::uintptr_t runtime, runtime_vtable;
    Fence fence;
    bool complete;
};
struct PlanItem { Key pack; std::uint32_t release_count; };
struct Plan {
    PlanItem* items; std::size_t capacity, count;
    Fence fence; std::uint64_t ledger_revision, baseline_generation;
    std::uintptr_t runtime;
    bool prepared, mutation_started, completed;
};
using CaptureFn = Result (*)(void*, Snapshot*) noexcept;
using EvictFn = Result (*)(void*, const Key&) noexcept;
using ReleaseFn = Result (*)(void*, const Key&, bool zero_references) noexcept;
struct Operations { void* user; CaptureFn capture; EvictFn evict; ReleaseFn release; };

void Initialize(Ledger*, Origin*, std::size_t) noexcept;
Result Constructed(Ledger*, std::uintptr_t, Key*) noexcept;
Result FindActive(const Ledger*, std::uintptr_t, Key*) noexcept;
Result RetainInput(Ledger*, Key, const retained::Input&) noexcept;
Result InputPending(Ledger*, Key) noexcept;
Result InputCompleted(Ledger*, Key, const retained::Input&) noexcept;
Result Registered(Ledger*, Key) noexcept;
Result Loaded(Ledger*, Key) noexcept;
Result ObserveUnclassifiedStream(Ledger*,Key,std::uint32_t epoch)noexcept;
Result DestructorEntered(Ledger*, Key) noexcept;
Result DestructorReturned(Ledger*, Key) noexcept;
Result Unregistered(Ledger*, Key) noexcept;
void LoseCoverage(Ledger*) noexcept;
Result CaptureBaseline(Ledger*, const Snapshot&, std::uint64_t) noexcept;
// Restricts the concrete path to texture-only packs. Metadata owns every
// listed texture and contributes precisely one native pack ref per texture.
Result Prepare(const Ledger&, const retained::Ledger&, const Snapshot&, Plan*) noexcept;
Result Reset(Ledger*, retained::Ledger*, const Operations&, Snapshot*, Plan*) noexcept;
Result CanRetireSource(const Ledger&, std::uintptr_t, std::uint64_t,
                      std::uint64_t transaction) noexcept;
Result ReclaimRetired(Ledger*, std::uintptr_t, std::uint64_t,
                     std::uint64_t transaction) noexcept;
const char* ResultName(Result) noexcept;
} // namespace
#endif
