#define _HAS_EXCEPTIONS 0

#include "CustomAudioBankLedger.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio {
namespace {

class AudioGuard {
public:
    explicit AudioGuard(CustomAudioBankLedger* ledger) noexcept
        : ledger_(ledger), locked_(false)
    {
        if (ledger_ &&
            dluma_atomic_compare_exchange(&ledger_->lock, 1, 0) == 0)
            locked_ = true;
        else if (ledger_)
            dluma_atomic_increment(
                reinterpret_cast<volatile LONG*>(&ledger_->dropped_records));
    }
    ~AudioGuard()
    {
        if (locked_) dluma_atomic_exchange(&ledger_->lock, 0);
    }
    bool locked() const noexcept { return locked_; }

private:
    CustomAudioBankLedger* ledger_;
    bool locked_;
};

void CopyText(char* output, std::size_t capacity,
              const char* input) noexcept
{
    if (!output || !capacity) return;
    std::size_t index = 0;
    if (input) {
        while (index + 1 < capacity && input[index]) {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = 0;
}

char Lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool TextEqualsInsensitive(const char* left, const char* right) noexcept
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (Lower(*left++) != Lower(*right++)) return false;
    }
    return *left == *right;
}

bool BankIdentityEquals(std::uint64_t left_id, const char* left_name,
                        std::uint64_t right_id,
                        const char* right_name) noexcept
{
    if (left_id && right_id) return left_id == right_id;
    return left_name && right_name && left_name[0] && right_name[0] &&
           TextEqualsInsensitive(left_name, right_name);
}

BaselineAudioBank* FindBaseline(CustomAudioBankLedger* ledger,
                                std::uint64_t id,
                                const char* name) noexcept
{
    for (std::uint32_t index = 0; index < ledger->baseline_count; ++index)
        if (BankIdentityEquals(ledger->baseline[index].logical_bank_id,
                               ledger->baseline[index].bank_name,
                               id, name))
            return &ledger->baseline[index];
    return nullptr;
}

void Classify(CustomAudioBankRecord* record,
              const BaselineAudioBank* baseline,
              bool same_custom_instance) noexcept
{
    if (same_custom_instance) {
        record->classification =
            CustomAudioBankClass::ProjectReloadOfSameCustomInstance;
        return;
    }
    if (record->resolved_from_custom_source) {
        if (baseline) {
            record->classification =
                CustomAudioBankClass::CustomOverrideOfBaseline;
            record->custom_override_of_baseline = true;
            record->name_existed_in_baseline = true;
            record->baseline_reference_count =
                baseline->observed_reference_count;
        } else {
            record->classification = CustomAudioBankClass::ProjectUnique;
            record->project_unique = true;
        }
        return;
    }
    if (baseline) {
        record->classification =
            CustomAudioBankClass::SharedBaselineReference;
        record->shared_baseline_reference = true;
        record->name_existed_in_baseline = true;
        record->baseline_reference_count =
            baseline->observed_reference_count;
        return;
    }
    record->classification = CustomAudioBankClass::Unknown;
}

}  // namespace

void Initialize(CustomAudioBankLedger* ledger) noexcept
{
    if (!ledger) return;
    std::memset(ledger, 0, sizeof(*ledger));
    ledger->next_baseline_ordinal = 1;
}

bool BeginBaselineCapture(CustomAudioBankLedger* ledger) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || ledger->baseline_captured) return false;
    ledger->baseline_count = 0;
    ledger->baseline_capture_open = true;
    ledger->enumeration_complete = false;
    return true;
}

bool RecordBaselineBank(CustomAudioBankLedger* ledger,
                        std::uint64_t logical_bank_id,
                        const char* bank_name,
                        const char* source_path,
                        std::uint32_t reference_count,
                        bool loaded) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || !ledger->baseline_capture_open ||
        (!logical_bank_id && (!bank_name || !bank_name[0])))
        return false;
    BaselineAudioBank* existing =
        FindBaseline(ledger, logical_bank_id, bank_name);
    if (existing) {
        existing->observed_reference_count = reference_count;
        existing->loaded = loaded;
        CopyText(existing->source_path, sizeof(existing->source_path),
                 source_path);
        return true;
    }
    if (ledger->baseline_count >= kMaximumBaselineAudioBanks) {
        ++ledger->dropped_records;
        return false;
    }
    BaselineAudioBank* bank = &ledger->baseline[ledger->baseline_count++];
    std::memset(bank, 0, sizeof(*bank));
    bank->logical_bank_id = logical_bank_id;
    bank->observed_reference_count = reference_count;
    bank->load_ordinal = ledger->next_baseline_ordinal++;
    bank->loaded = loaded;
    CopyText(bank->bank_name, sizeof(bank->bank_name), bank_name);
    CopyText(bank->source_path, sizeof(bank->source_path), source_path);
    return true;
}

bool EndBaselineCapture(CustomAudioBankLedger* ledger,
                        bool enumeration_complete) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || !ledger->baseline_capture_open) return false;
    ledger->baseline_capture_open = false;
    ledger->enumeration_complete = enumeration_complete;
    ledger->baseline_captured = enumeration_complete &&
                                ledger->dropped_records == 0;
    return ledger->baseline_captured;
}

bool RecordProjectBankLoad(CustomAudioBankLedger* ledger,
                           std::uint32_t epoch,
                           std::uint64_t logical_bank_id,
                           void* bank_handle,
                           void* bank_object,
                           const char* bank_name,
                           const char* logical_path,
                           const char* resolved_source_path,
                           bool resolved_from_custom_source) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || !epoch ||
        (!logical_bank_id && (!bank_name || !bank_name[0])))
        return false;
    const std::uintptr_t handle =
        reinterpret_cast<std::uintptr_t>(bank_handle);
    const std::uintptr_t object =
        reinterpret_cast<std::uintptr_t>(bank_object);
    for (std::uint32_t index = 0; index < ledger->custom_count; ++index) {
        CustomAudioBankRecord* record = &ledger->custom[index];
        if (record->epoch != epoch) continue;
        const bool same_handle = handle && record->bank_handle == handle;
        const bool same_object = object && record->bank_object == object;
        const bool handles_compatible =
            !handle || !record->bank_handle || same_handle;
        const bool objects_compatible =
            !object || !record->bank_object || same_object;
        const bool same_identity = BankIdentityEquals(
            record->logical_bank_id, record->bank_name,
            logical_bank_id, bank_name);
        const bool same_provider =
            record->resolved_from_custom_source ==
                resolved_from_custom_source &&
            TextEqualsInsensitive(record->logical_path,
                                  logical_path ? logical_path : "") &&
            TextEqualsInsensitive(record->resolved_source_path,
                                  resolved_source_path
                                      ? resolved_source_path : "");
        if ((!same_handle && !same_object) || !handles_compatible ||
            !objects_compatible || !same_identity || !same_provider ||
            record->unloaded_naturally || record->unloaded_by_reset ||
            record->reset_failed)
            continue;
        ++record->project_load_count;
        record->unloaded_naturally = false;
        /* Re-observing the same instance never upgrades unknown ownership.
         * A repeated acquisition is cardinality evidence, not proof that the
         * loader owns a release. */
        if (record->classification != CustomAudioBankClass::Unknown)
            record->classification =
                CustomAudioBankClass::ProjectReloadOfSameCustomInstance;
        return true;
    }
    if (ledger->custom_count >= kMaximumCustomAudioBanks) {
        ++ledger->dropped_records;
        return false;
    }
    CustomAudioBankRecord* record =
        &ledger->custom[ledger->custom_count++];
    std::memset(record, 0, sizeof(*record));
    record->ledger_record_index = ledger->custom_count - 1U;
    record->epoch = epoch;
    record->logical_bank_id = logical_bank_id;
    record->bank_handle = handle;
    record->bank_object = object;
    record->project_load_count = 1;
    record->exact_handle_available = handle != 0;
    record->resolved_from_custom_source =
        resolved_from_custom_source;
    CopyText(record->bank_name, sizeof(record->bank_name), bank_name);
    CopyText(record->logical_path, sizeof(record->logical_path),
             logical_path);
    CopyText(record->resolved_source_path,
             sizeof(record->resolved_source_path),
             resolved_source_path);
    const BaselineAudioBank* baseline =
        FindBaseline(ledger, logical_bank_id, bank_name);
    Classify(record, baseline, false);
    return true;
}

bool RecordNaturalBankUnload(CustomAudioBankLedger* ledger,
                             std::uint32_t epoch,
                             void* bank_handle,
                             void* bank_object) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked()) return false;
    const std::uintptr_t handle =
        reinterpret_cast<std::uintptr_t>(bank_handle);
    const std::uintptr_t object =
        reinterpret_cast<std::uintptr_t>(bank_object);
    /* Handle/object addresses can be reused after an earlier instance has
     * fully unloaded.  Match the newest still-live acquisition, never the
     * oldest completed record. */
    for (std::uint32_t index = ledger->custom_count; index > 0; --index) {
        CustomAudioBankRecord* record = &ledger->custom[index - 1U];
        if (record->epoch != epoch) continue;
        const bool same_handle = handle && record->bank_handle == handle;
        const bool same_object = object && record->bank_object == object;
        const bool handles_compatible =
            !handle || !record->bank_handle || same_handle;
        const bool objects_compatible =
            !object || !record->bank_object || same_object;
        if ((same_handle || same_object) && handles_compatible &&
            objects_compatible) {
            if (record->natural_unload_count +
                    record->reset_release_count >=
                record->project_load_count)
                continue;
            ++record->natural_unload_count;
            record->unloaded_naturally =
                record->natural_unload_count ==
                record->project_load_count;
            record->unloaded_by_reset =
                record->reset_release_count != 0 &&
                record->natural_unload_count +
                        record->reset_release_count ==
                    record->project_load_count;
            return true;
        }
    }
    return false;
}

bool RecordResetRelease(CustomAudioBankLedger* ledger,
                        std::uint32_t record_index) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || record_index >= ledger->custom_count)
        return false;
    CustomAudioBankRecord* record = &ledger->custom[record_index];
    if (record->reset_failed ||
        record->natural_unload_count + record->reset_release_count >=
            record->project_load_count)
        return false;
    ++record->reset_release_count;
    record->unloaded_by_reset =
        record->natural_unload_count + record->reset_release_count ==
        record->project_load_count;
    return true;
}

bool MarkResetResult(CustomAudioBankLedger* ledger,
                     std::uint32_t record_index,
                     bool succeeded) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || record_index >= ledger->custom_count)
        return false;
    CustomAudioBankRecord* record = &ledger->custom[record_index];
    const bool complete =
        record->natural_unload_count + record->reset_release_count ==
        record->project_load_count;
    record->unloaded_by_reset = succeeded && complete;
    record->reset_failed = !succeeded || !complete;
    return succeeded && complete;
}

std::size_t SnapshotEpochBanks(CustomAudioBankLedger* ledger,
                               std::uint32_t epoch,
                               CustomAudioBankRecord* output,
                               std::size_t capacity) noexcept
{
    if (!output || !capacity) return 0;
    AudioGuard guard(ledger);
    if (!guard.locked()) return 0;
    std::size_t count = 0;
    for (std::uint32_t index = 0;
         index < ledger->custom_count && count < capacity; ++index)
        if (ledger->custom[index].epoch == epoch)
            output[count++] = ledger->custom[index];
    return count;
}

std::size_t SnapshotBaselineBanks(CustomAudioBankLedger* ledger,
                                  BaselineAudioBank* output,
                                  std::size_t capacity) noexcept
{
    if (!output || !capacity) return 0;
    AudioGuard guard(ledger);
    if (!guard.locked()) return 0;
    const std::size_t count = ledger->baseline_count < capacity
        ? ledger->baseline_count : capacity;
    std::memcpy(output, ledger->baseline,
                sizeof(BaselineAudioBank) * count);
    return count;
}

bool CanDestructivelyResetEpoch(CustomAudioBankLedger* ledger,
                                std::uint32_t epoch) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || !ledger->baseline_captured ||
        !ledger->enumeration_complete || ledger->dropped_records)
        return false;
    for (std::uint32_t index = 0; index < ledger->custom_count; ++index) {
        const CustomAudioBankRecord& record = ledger->custom[index];
        if (record.epoch != epoch) continue;
        if (record.reset_failed) return false;
        if (record.unloaded_naturally || record.unloaded_by_reset) continue;
        /* A logical id or a same-name baseline match is evidence for
         * classification only.  It is never sufficient authority to unload
         * a live bank: every destructive path must retain the exact handle
         * returned for the project acquisition. */
        if (record.classification == CustomAudioBankClass::Unknown ||
            !record.exact_handle_available)
            return false;
    }
    return true;
}

bool VerifyBaseline(CustomAudioBankLedger* ledger) noexcept
{
    AudioGuard guard(ledger);
    if (!guard.locked() || !ledger->baseline_captured ||
        !ledger->enumeration_complete || ledger->dropped_records)
        return false;
    for (std::uint32_t index = 0; index < ledger->custom_count; ++index) {
        const CustomAudioBankRecord& record = ledger->custom[index];
        if (record.reset_failed ||
            (!record.unloaded_naturally && !record.unloaded_by_reset &&
             record.classification !=
                 CustomAudioBankClass::SharedBaselineReference))
            return false;
    }
    return true;
}

const char* BankClassName(CustomAudioBankClass classification) noexcept
{
    switch (classification) {
    case CustomAudioBankClass::ProjectUnique: return "ProjectUnique";
    case CustomAudioBankClass::CustomOverrideOfBaseline:
        return "CustomOverrideOfBaseline";
    case CustomAudioBankClass::SharedBaselineReference:
        return "SharedBaselineReference";
    case CustomAudioBankClass::ProjectReloadOfSameCustomInstance:
        return "ProjectReloadOfSameCustomInstance";
    case CustomAudioBankClass::Unknown:
    default: return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio
