#ifndef DLUMA_FIXES_CUSTOM_AUDIO_BANK_LEDGER_H
#define DLUMA_FIXES_CUSTOM_AUDIO_BANK_LEDGER_H

#include "CustomContentLedger.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio {

constexpr std::size_t kMaximumBaselineAudioBanks = 128;
constexpr std::size_t kMaximumCustomAudioBanks = 256;
constexpr std::size_t kAudioBankNameCapacity = 256;
constexpr std::size_t kAudioLogicalPathCapacity = 1024;
constexpr std::size_t kAudioSourcePathCapacity = 2048;

enum class CustomAudioBankClass : std::uint8_t {
    ProjectUnique,
    CustomOverrideOfBaseline,
    SharedBaselineReference,
    ProjectReloadOfSameCustomInstance,
    Unknown,
};

struct BaselineAudioBank {
    std::uint64_t logical_bank_id;
    char bank_name[kAudioBankNameCapacity];
    char source_path[kAudioSourcePathCapacity];
    std::uint32_t observed_reference_count;
    std::uint32_t load_ordinal;
    bool loaded;
};

struct CustomAudioBankRecord {
    std::uint32_t ledger_record_index;
    std::uint32_t epoch;
    std::uint64_t logical_bank_id;
    std::uintptr_t bank_handle;
    std::uintptr_t bank_object;
    char bank_name[kAudioBankNameCapacity];
    char logical_path[kAudioLogicalPathCapacity];
    char resolved_source_path[kAudioSourcePathCapacity];
    std::uint32_t baseline_reference_count;
    std::uint32_t project_load_count;
    std::uint32_t natural_unload_count;
    std::uint32_t reset_release_count;
    CustomAudioBankClass classification;
    bool exact_handle_available;
    bool resolved_from_custom_source;
    bool name_existed_in_baseline;
    bool custom_override_of_baseline;
    bool project_unique;
    bool shared_baseline_reference;
    bool unloaded_naturally;
    bool unloaded_by_reset;
    bool reset_failed;
};

struct CustomAudioBankLedger {
    volatile LONG lock;
    BaselineAudioBank baseline[kMaximumBaselineAudioBanks];
    CustomAudioBankRecord custom[kMaximumCustomAudioBanks];
    std::uint32_t baseline_count;
    std::uint32_t custom_count;
    std::uint32_t next_baseline_ordinal;
    std::uint32_t dropped_records;
    bool baseline_capture_open;
    bool baseline_captured;
    bool enumeration_complete;
};

void Initialize(CustomAudioBankLedger* ledger) noexcept;
bool BeginBaselineCapture(CustomAudioBankLedger* ledger) noexcept;
bool RecordBaselineBank(CustomAudioBankLedger* ledger,
                        std::uint64_t logical_bank_id,
                        const char* bank_name,
                        const char* source_path,
                        std::uint32_t reference_count,
                        bool loaded) noexcept;
bool EndBaselineCapture(CustomAudioBankLedger* ledger,
                        bool enumeration_complete) noexcept;

bool RecordProjectBankLoad(CustomAudioBankLedger* ledger,
                           std::uint32_t epoch,
                           std::uint64_t logical_bank_id,
                           void* bank_handle,
                           void* bank_object,
                           const char* bank_name,
                           const char* logical_path,
                           const char* resolved_source_path,
                           bool resolved_from_custom_source) noexcept;
bool RecordNaturalBankUnload(CustomAudioBankLedger* ledger,
                             std::uint32_t epoch,
                             void* bank_handle,
                             void* bank_object) noexcept;
bool RecordResetRelease(CustomAudioBankLedger* ledger,
                        std::uint32_t record_index) noexcept;
bool MarkResetResult(CustomAudioBankLedger* ledger,
                     std::uint32_t record_index,
                     bool succeeded) noexcept;

std::size_t SnapshotEpochBanks(CustomAudioBankLedger* ledger,
                               std::uint32_t epoch,
                               CustomAudioBankRecord* output,
                               std::size_t capacity) noexcept;
std::size_t SnapshotBaselineBanks(CustomAudioBankLedger* ledger,
                                  BaselineAudioBank* output,
                                  std::size_t capacity) noexcept;
bool CanDestructivelyResetEpoch(CustomAudioBankLedger* ledger,
                                std::uint32_t epoch) noexcept;
bool VerifyBaseline(CustomAudioBankLedger* ledger) noexcept;

const char* BankClassName(CustomAudioBankClass classification) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio

#endif
