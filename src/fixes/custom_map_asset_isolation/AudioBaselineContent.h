#ifndef DLUMA_AUDIO_BASELINE_CONTENT_H
#define DLUMA_AUDIO_BASELINE_CONTENT_H
#include "AudioBaselineWindowsSnapshot.h"
#include "AudioStartupObservationLedger.h"
#include "NativeFileLifetimeLedger.h"
#include "SpeechContentLease.h"

namespace dluma::fixes::custom_map_asset_isolation::audio_baseline_content {
constexpr std::size_t kBanks = audio_startup::kAudioStartupBankCapacity;
enum class Phase : LONG { Observing, Hashing, ContentCaptured, Failed };
enum class Failure : LONG { None, Capacity, IncompleteOpen, DuplicateOpen, InvalidFreeze,
    ObservationLoss, FileIdentityChanged, HashFailed, BankJoinMissing, ScriptInputMissing,
    AmbiguousOrder, ProviderChanged };
struct NaturalBank {
    speech_content_lease::NaturalOpen open;
    file_lifetime::FileRecord file;
    file_lifetime::SourceKey contextual_source;
    std::uintptr_t manager;
    std::uint64_t load_enter_sequence;
    speech_content_lease::NativeBankFile kind;
};
struct Bank {
    volatile LONG published;
    NaturalBank natural;
    speech_content_lease::Pin pin;
    speech_content_lease::Identity identity;
    unsigned char sha256[32];
    std::uint64_t bytes;
    bool hashed;
};
struct Input {
    std::uint64_t enter_sequence, exit_sequence, parent_sequence;
    std::uintptr_t owner;
    file_lifetime::SourceKey source;
    std::uint64_t source_registry_generation, native_file_generation;
    std::uint32_t bank_pin_index;
    audio_startup::AudioStartupBankOwner bank_owner;
    unsigned char sha256[32];
    std::uint64_t bytes;
    char logical_path[audio_startup::kAudioStartupLogicalPathCapacity];
    bool script, localized, cache_hit;
};
struct State {
    volatile LONG phase, failure, count;
    std::uint32_t frozen_count, hashing_index, input_count;
    std::uint64_t frozen_revision, baseline_generation;
    Bank banks[kBanks];
    Input inputs[kBanks + audio_startup::kAudioStartupScriptCapacity];
    speech_content_lease::ReadJob job;
    bool inventory_joined;
};
// Startup producers only. Copies independently observed native file and source
// generations; duplicates the actual OS handle without reading or seeking it.
bool ObserveNatural(State*,const NaturalBank&,speech_content_lease::ReadMemoryFn,void*) noexcept;
bool Freeze(State*,std::uint64_t baseline_generation,std::uint64_t observation_revision) noexcept;
// One bounded 64 KiB read per cooperative call. Natural pins outlive hashing.
Phase Step(State*,std::uint64_t maximum_file_bytes) noexcept;
bool Join(State*,const audio_startup::AudioStartupObservationSnapshot&,
          const cold_reboot::audio_snapshot::AudioBaselineWindowsSnapshot&) noexcept;
// Script parser digests are historical observations, not raw script leases.
bool AllBankContentHeld(const State&) noexcept;
// Startup pins are released only at final owner shutdown, never per project.
bool Close(State*) noexcept;
}
#endif
