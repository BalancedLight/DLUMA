#ifndef DLUMA_FIXES_LOCALIZED_TEXT_OBSERVATION_GATE_H
#define DLUMA_FIXES_LOCALIZED_TEXT_OBSERVATION_GATE_H

#include "LocalizedTextBaselineOwner.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::localized_texts {

enum class TextHook : std::uint8_t {
    Substitute, Exists, CopyFiltered, SaveBinary, Insert, Clear, FromFile,
    LoadBinary, RemoveBinary, RemoveOne, ValueLookup, EntryLookup,
    FindOrInsert, ClearBucket, InitializeTable, CopyEntry, TableAddress, Count,
};
constexpr std::size_t kTextHookCount = static_cast<std::size_t>(TextHook::Count);
struct TextHookDescriptor {
    TextHook kind;
    const char* name;
    std::uintptr_t function_rva;
    // Nonzero for the short accessor: hook this sole vtable slot, not its body.
    std::uintptr_t pointer_slot_rva;
    std::uint8_t original_prefix[16];
};
enum class TextGateResult : std::uint8_t {
    Succeeded, InvalidArgument, ProfileMismatch, AlreadyInitialized,
    IncompleteCoverage, StartupUnproven, UnsafeBorrow, LostObservation, WrongThread,
    WrongLease, Busy, TimedOut, LateProducer,
};
struct TextCallToken {
    std::uintptr_t gate_identity;
    std::uint64_t serial;
    std::uint32_t depth;
    TextHook kind;
    bool active;
    bool outer;
    bool revision_observed;
};
struct TextGateLease { std::uint64_t generation; std::uint32_t thread_id; };
struct ObservedTextStartup {
    // Actual observations captured at the exported script-DLL entry. Never
    // substitute the expected return constant for the observed address.
    std::uintptr_t callback_return_address;
    std::uintptr_t callback_address;
    std::uint32_t thread_id;
};
struct TextGateState {
    std::uint64_t revision;
    std::uint32_t active_calls;
    bool admission_closed;
    bool held_by_this_thread;
    bool unknown_borrow;
    bool lost_observation;
    bool late_producer;
};
struct LocalizedTextObservationGate {
    std::uintptr_t engine_base = 0;
    ReadMemoryFn read = nullptr;
    void* read_user = nullptr;
    std::uintptr_t originals[kTextHookCount] = {};
    ObservedTextStartup startup = {};
    std::atomic<std::uint64_t> revision{1};
    std::atomic<std::uint64_t> lease_generation{0};
    std::atomic<std::uint32_t> active_calls{0};
    // Zero means open. Setting the owner closes admission in one atomic step.
    std::atomic<std::uint32_t> owner_thread{0};
    std::atomic<bool> held{false};
    std::atomic<bool> unknown_borrow{false};
    std::atomic<bool> lost_observation{false};
    std::atomic<bool> late_producer{false};
    // Observation-only between atomic text restoration and final commit.
    // Readers and writers run normally; any writer invalidates that commit.
    std::atomic<std::uint64_t> commit_watch_transaction{0};
    std::atomic<bool> startup_sealed{false};
    bool initialized = false;
};

const TextHookDescriptor* TextHookCatalog(std::size_t* count) noexcept;
std::uintptr_t TextHookWrapper(TextHook kind) noexcept;
// The caller supplies completed, root-owned relay trampolines. All original
// entry prefixes and the accessor slot are checked before any hooks install.
// No installation, native mutation, or game launch occurs in this module.
TextGateResult InitializeTextGate(LocalizedTextObservationGate* gate,
    std::uintptr_t engine_base, const char* exact_sha256, ReadMemoryFn read,
    void* read_user, const std::uintptr_t* originals, std::size_t count,
    const ObservedTextStartup* startup) noexcept;
// Bind once before installing the entry hooks. The bridge/gate must remain
// allocated while any route can call a wrapper, including partial installs.
TextGateResult BindTextHookBridge(LocalizedTextObservationGate* gate) noexcept;
// Call after all routes install, still inside the initial script callback.
// Qualifies the known startup ordering while all native game owners are absent.
TextGateResult SealTextHookStartup(LocalizedTextObservationGate* gate) noexcept;
TextGateResult VerifyTextHookCoverage(const LocalizedTextObservationGate* gate) noexcept;
void BeginTextCall(LocalizedTextObservationGate* gate, TextHook kind,
    TextCallToken* token) noexcept;
void EndTextCall(LocalizedTextObservationGate* gate, TextCallToken* token) noexcept;
TextGateState InspectTextGate(const LocalizedTextObservationGate* gate) noexcept;
// Bounded drain, failure-atomic: every failed close reopens admission. Calls
// arriving on other threads wait without fabricating a native return value.
TextGateResult CloseTextGate(LocalizedTextObservationGate* gate,
    std::uint32_t timeout_ms, TextGateLease* lease) noexcept;
TextGateResult OpenTextGate(LocalizedTextObservationGate* gate,
    const TextGateLease* lease) noexcept;
TextGateResult BeginTextCommitWatch(LocalizedTextObservationGate* gate,
    const TextGateLease* lease, std::uint64_t transaction_generation) noexcept;
TextGateResult CompleteTextCommitWatch(LocalizedTextObservationGate* gate,
    const TextGateLease* lease, std::uint64_t transaction_generation) noexcept;

using CaptureTextContextFn = bool (*)(void*, TextFence*) noexcept;
enum class TextReplayOperation : std::uint8_t { None, Clear, Insert };
struct TextReplayAuthority {
    std::uint64_t transaction_generation;
    TextReplayOperation operation;
};
using CaptureTextReplayAuthorityFn = bool (*)(void*, TextReplayAuthority*) noexcept;
struct ObservedTextAccess {
    LocalizedTextObservationGate* gate;
    CaptureTextContextFn context;
    void* context_user;
    // Root reads the actual registry transaction and active native-owner
    // callback and its internal operation. The text reset performs Clear and
    // Insert atomically; unrelated owner callbacks cannot borrow this authority.
    CaptureTextReplayAuthorityFn replay_authority;
    void* replay_user;
    TextGateLease replay_lease;
    std::uint64_t replay_transaction;
};
// The context supplies locale, content generation, epoch and validated C10
// authority. Admission/revision/drain fields always come from this gate.
bool MakeObservedTextAccess(ObservedTextAccess* bridge,
    NativeTextAccess* access) noexcept;
bool BindObservedTextReplay(ObservedTextAccess* bridge,
    const TextGateLease* lease, std::uint64_t transaction_generation) noexcept;
bool CaptureObservedTextFence(void* bridge, TextFence* output) noexcept;
bool VerifyObservedTextProfile(void* bridge) noexcept;
bool InvokeObservedTextClear(void* bridge, std::uintptr_t function) noexcept;
bool InvokeObservedTextInsert(void* bridge, std::uintptr_t function,
    const char* key, const std::uint16_t* value) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::localized_texts
#endif
