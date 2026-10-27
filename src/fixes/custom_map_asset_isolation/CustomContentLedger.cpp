#define _HAS_EXCEPTIONS 0

#include "CustomContentLedger.h"
#include "../../dlw_project_root_identity.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::content {
namespace {

class LedgerGuard {
public:
    explicit LedgerGuard(
        CustomContentLedger* ledger,
        LedgerLossReason reason =
            LedgerLossReason::GenericOwnershipContention,
        bool ownership_mutation = true) noexcept
        : ledger_(ledger), locked_(false)
    {
        if (ledger_ &&
            dluma_atomic_compare_exchange(&ledger_->lock, 1, 0) == 0) {
            locked_ = true;
        } else if (ledger_ && ownership_mutation)
            RecordOwnershipMutationLoss(ledger_, reason);
        else if (ledger_)
            RecordDiagnosticObservationLoss(ledger_, reason);
    }

    ~LedgerGuard()
    {
        if (locked_) dluma_atomic_exchange(&ledger_->lock, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    CustomContentLedger* ledger_;
    bool locked_;
};

class LedgerReadGuard {
public:
    explicit LedgerReadGuard(CustomContentLedger* ledger) noexcept
        : ledger_(ledger), locked_(false)
    {
        if (ledger_ &&
            dluma_atomic_compare_exchange(&ledger_->lock, 1, 0) == 0)
            locked_ = true;
    }

    ~LedgerReadGuard()
    {
        if (locked_) dluma_atomic_exchange(&ledger_->lock, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    CustomContentLedger* ledger_;
    bool locked_;
};

char Lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
}

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

void CopyWide(WCHAR* output, std::size_t capacity,
              const WCHAR* input) noexcept
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

bool TextEquals(const char* left, const char* right) noexcept
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

bool StartsWith(const char* text, const char* prefix) noexcept
{
    if (!text || !prefix) return false;
    while (*prefix) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

bool IsReservedMapDevice(const char* value) noexcept
{
    char base[16];
    std::size_t length = 0;
    while (value && value[length] && value[length] != '.' &&
           length + 1U < sizeof(base)) {
        base[length] = Lower(value[length]);
        ++length;
    }
    base[length] = 0;
    if (TextEquals(base, "con") ||
        TextEquals(base, "prn") ||
        TextEquals(base, "aux") ||
        TextEquals(base, "nul") ||
        TextEquals(base, "clock$"))
        return true;
    return length == 4U &&
        ((base[0] == 'c' && base[1] == 'o' &&
          base[2] == 'm') ||
         (base[0] == 'l' && base[1] == 'p' &&
          base[2] == 't')) &&
        base[3] >= '1' && base[3] <= '9';
}

bool NormalizeMapSegment(const char* input, char* output,
                         std::size_t capacity) noexcept
{
    if (!input || !output || capacity < 2) return false;
    output[0] = 0;
    std::size_t length = 0;
    while (input[length]) {
        const unsigned char value =
            static_cast<unsigned char>(input[length]);
        if (length + 1 >= capacity || value < 0x20 || value >= 0x7f ||
            input[length] == '\\' || input[length] == '/' ||
            input[length] == ':' || input[length] == '*' ||
            input[length] == '?' || input[length] == '"' ||
            input[length] == '<' || input[length] == '>' ||
            input[length] == '|')
            return false;
        output[length] = Lower(input[length]);
        ++length;
    }
    if (!length || output[length - 1] == '.' ||
        output[length - 1] == ' ')
        return false;
    output[length] = 0;
    return !TextEquals(output, ".") &&
        !TextEquals(output, "..") &&
        !IsReservedMapDevice(output);
}

bool ExtractExactMapAnchor(const char* logical_path,
                           char* map_name,
                           std::size_t map_capacity) noexcept
{
    static const char prefix[] = "data\\maps\\";
    static const char suffix[] = ".scr";
    char normalized[kLogicalPathCapacity];
    char segment[kMapNameCapacity];
    char normalized_segment[kMapNameCapacity];
    if (!logical_path || !map_name || !map_capacity ||
        !NormalizeLogicalPath(logical_path, normalized,
                              sizeof(normalized)) ||
        !StartsWith(normalized, prefix))
        return false;
    const char* const segment_start =
        normalized + sizeof(prefix) - 1U;
    std::size_t segment_length = 0;
    while (segment_start[segment_length] &&
           segment_start[segment_length] != '\\')
        ++segment_length;
    if (!segment_length ||
        segment_start[segment_length] != '\\' ||
        segment_length + 1U >= sizeof(segment))
        return false;
    std::memcpy(segment, segment_start, segment_length);
    segment[segment_length] = 0;
    if (!NormalizeMapSegment(segment, normalized_segment,
                             sizeof(normalized_segment)))
        return false;
    const char* const file_name =
        segment_start + segment_length + 1U;
    std::size_t index = 0;
    while (normalized_segment[index] &&
           file_name[index] == normalized_segment[index])
        ++index;
    if (normalized_segment[index] ||
        !TextEquals(file_name + index, suffix))
        return false;
    CopyText(map_name, map_capacity, normalized_segment);
    return TextEquals(map_name, normalized_segment);
}

bool EndsWith(const char* text, const char* suffix) noexcept
{
    if (!text || !suffix) return false;
    std::size_t text_length = 0;
    std::size_t suffix_length = 0;
    while (text[text_length]) ++text_length;
    while (suffix[suffix_length]) ++suffix_length;
    if (suffix_length > text_length) return false;
    return TextEquals(text + text_length - suffix_length, suffix);
}

bool IsDataWorkshopPak(const char* relative) noexcept
{
    constexpr const char prefix[] = "dataworkshop_";
    if (!StartsWith(relative, prefix)) return false;
    const char* cursor = relative + sizeof(prefix) - 1;
    bool have_digit = false;
    while (*cursor >= '0' && *cursor <= '9') {
        have_digit = true;
        ++cursor;
    }
    return have_digit && TextEquals(cursor, ".pak");
}

bool IsLocaleStem(const char* relative) noexcept
{
    if (!StartsWith(relative, "data") || TextEquals(relative, "data"))
        return false;
    const char* cursor = relative + 4;
    while (*cursor && *cursor != '.') {
        const char value = *cursor++;
        if (!((value >= 'a' && value <= 'z') || value == '_' ||
              (value >= '0' && value <= '9')))
            return false;
    }
    return cursor != relative + 4;
}

bool IsSpeechStem(const char* relative) noexcept
{
    if (!StartsWith(relative, "speech")) return false;
    const char* cursor = relative + 6;
    if (!*cursor || *cursor == '.') return false;
    while (*cursor && *cursor != '.') {
        const char value = *cursor++;
        if (!((value >= 'a' && value <= 'z') || value == '_' ||
              (value >= '0' && value <= '9')))
            return false;
    }
    return true;
}

CustomContentEpoch* FindEpochUnlocked(CustomContentLedger* ledger,
                                      std::uint32_t id) noexcept
{
    if (!ledger || !id) return nullptr;
    for (std::uint32_t index = 0; index < ledger->epoch_count; ++index)
        if (ledger->epochs[index].id == id) return &ledger->epochs[index];
    return nullptr;
}

const CustomContentEpoch* FindEpochUnlocked(
    const CustomContentLedger* ledger, std::uint32_t id) noexcept
{
    return FindEpochUnlocked(const_cast<CustomContentLedger*>(ledger), id);
}

void PushDiagnosticUnlocked(CustomContentLedger* ledger,
                            LedgerDiagnosticType type,
                            std::uint32_t epoch,
                            std::uint32_t result,
                            const char* text0,
                            const char* text1,
                            std::uint64_t sequence,
                            std::uintptr_t object = 0,
                            std::uint64_t value0 = 0,
                            std::uint64_t value1 = 0) noexcept
{
    const std::uint32_t next =
        (ledger->diagnostic_head + 1U) %
        static_cast<std::uint32_t>(kMaximumDiagnostics);
    if (next == ledger->diagnostic_tail) {
        ledger->diagnostic_overflow_latched = true;
        RecordDiagnosticObservationLoss(
            ledger, LedgerLossReason::DiagnosticQueueOverflow);
        return;
    }
    LedgerDiagnosticEvent* event =
        &ledger->diagnostics[ledger->diagnostic_head];
    std::memset(event, 0, sizeof(*event));
    event->sequence = sequence;
    event->type = type;
    event->epoch = epoch;
    event->result = result;
    event->object = object;
    event->value0 = value0;
    event->value1 = value1;
    CopyText(event->text0, sizeof(event->text0), text0);
    CopyText(event->text1, sizeof(event->text1), text1);
    ledger->diagnostic_head = next;
}

bool NormalizePath(const char* input, char* output, std::size_t capacity,
                   bool strip_leading) noexcept
{
    if (!output || !capacity) return false;
    output[0] = 0;
    if (!input) return false;
    std::size_t input_index = 0;
    std::size_t output_index = 0;
    if (strip_leading)
        while (input[input_index] == '/' || input[input_index] == '\\')
            ++input_index;
    bool last_was_separator = false;
    while (input[input_index]) {
        char value = input[input_index++];
        if (value == '/') value = '\\';
        value = Lower(value);
        if (value == '\\') {
            if (last_was_separator) continue;
            last_was_separator = true;
        } else {
            last_was_separator = false;
        }
        if (output_index + 1 >= capacity) {
            output[0] = 0;
            return false;
        }
        output[output_index++] = value;
    }
    while (output_index > 0 && output[output_index - 1] == '\\') {
        if (output_index == 3 && output[1] == ':') break;
        --output_index;
    }
    output[output_index] = 0;
    std::size_t segment_begin = 0;
    for (std::size_t index = 0; index <= output_index; ++index) {
        if (index != output_index && output[index] != '\\') continue;
        const std::size_t segment_size = index - segment_begin;
        if ((segment_size == 1 && output[segment_begin] == '.') ||
            (segment_size == 2 && output[segment_begin] == '.' &&
             output[segment_begin + 1] == '.')) {
            output[0] = 0;
            return false;
        }
        segment_begin = index + 1;
    }
    return output_index != 0;
}

bool FileKeyEquals(const CustomFileLoadRecord& record,
                   std::uint32_t epoch,
                   const char* logical_path,
                   std::uint32_t caller_rva,
                   const char* caller_module) noexcept
{
    return record.epoch == epoch && record.caller_rva == caller_rva &&
           TextEquals(record.logical_path, logical_path) &&
           TextEquals(record.caller_module, caller_module);
}

bool SourceTokenMatches(const CustomContentLedger* ledger,
                        std::uint32_t index,
                        const SourceResetSnapshot* expected) noexcept
{
    if (!ledger || index >= ledger->source_count) return false;
    const auto& record = ledger->sources[index];
    if (!record.add_succeeded || !record.epoch) return false;
    if (!expected) return !ledger->source_slot_reused[index];
    return expected->record_index == index && expected->epoch == record.epoch &&
        expected->successful_add_ordinal == record.successful_add_ordinal &&
        expected->source == record.source;
}

bool FileTokenMatches(const CustomContentLedger* ledger,
                      std::uint32_t index,
                      const CustomFileMutationToken* expected) noexcept
{
    if (!ledger || index >= ledger->file_count) return false;
    const auto& record = ledger->files[index];
    if (!record.open_count) return false;
    if (!expected) return !ledger->file_slot_reused[index];
    return expected->record_index == index && expected->epoch == record.epoch &&
        expected->source_add_ordinal == record.source_add_ordinal &&
        expected->source == record.source &&
        expected->first_open_sequence == record.first_open_sequence;
}

std::uint64_t HashRetirementBytes(std::uint64_t hash, const void* memory,
                                  std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(memory);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t EpochRecordsFingerprint(const CustomContentLedger* ledger,
                                      const CustomContentEpoch* epoch) noexcept
{
    auto hash = HashRetirementBytes(14695981039346656037ULL, epoch, sizeof(*epoch));
    for (std::uint32_t i = 0; i < ledger->source_count; ++i)
        if (ledger->sources[i].epoch == epoch->id)
            hash = HashRetirementBytes(hash, &ledger->sources[i], sizeof(ledger->sources[i]));
    for (std::uint32_t i = 0; i < ledger->file_count; ++i)
        if (ledger->files[i].epoch == epoch->id)
            hash = HashRetirementBytes(hash, &ledger->files[i], sizeof(ledger->files[i]));
    for (std::uint32_t i = 0; i < ledger->pack_count; ++i)
        if (ledger->packs[i].epoch == epoch->id)
            hash = HashRetirementBytes(hash, &ledger->packs[i], sizeof(ledger->packs[i]));
    return hash;
}

}  // namespace

void Initialize(CustomContentLedger* ledger) noexcept
{
    if (!ledger) return;
    std::memset(ledger, 0, sizeof(*ledger));
    ledger->next_epoch_id = 1;
    ledger->next_source_ordinal = 1;
}

const char* LedgerLossReasonName(LedgerLossReason reason) noexcept
{
    switch (reason) {
    case LedgerLossReason::SourceAddContention:
        return "source_add_contention";
    case LedgerLossReason::SourceAddCapacity:
        return "source_add_capacity";
    case LedgerLossReason::SourceIdentityIncomplete:
        return "source_identity_incomplete";
    case LedgerLossReason::SourceRemovalContention:
        return "source_removal_contention";
    case LedgerLossReason::EpochContention:
        return "epoch_contention";
    case LedgerLossReason::EpochCapacity:
        return "epoch_capacity";
    case LedgerLossReason::EpochConflict:
        return "epoch_conflict";
    case LedgerLossReason::ReleaseContention:
        return "release_contention";
    case LedgerLossReason::ReleaseCapacity:
        return "release_capacity";
    case LedgerLossReason::BindingContention:
        return "binding_contention";
    case LedgerLossReason::ConsumptionContention:
        return "consumption_contention";
    case LedgerLossReason::DiagnosticQueueOverflow:
        return "diagnostic_queue_overflow";
    case LedgerLossReason::OptionalObservationContention:
        return "optional_observation_contention";
    case LedgerLossReason::BroadFileObservationContention:
        return "broad_file_observation_contention";
    case LedgerLossReason::BroadFileCapacity:
        return "broad_file_capacity";
    case LedgerLossReason::IrrelevantWithoutEpoch:
        return "irrelevant_without_epoch";
    case LedgerLossReason::GenericOwnershipContention:
        return "ownership_contention";
    default: return "none";
    }
}

void RecordOwnershipMutationLoss(CustomContentLedger* ledger,
                                 LedgerLossReason reason) noexcept
{
    if (!ledger) return;
    dluma_atomic_increment(&ledger->ownership_mutation_loss);
    dluma_atomic_increment(&ledger->dropped_mutations);
    dluma_atomic_exchange(
        &ledger->last_ownership_loss_reason,
        static_cast<LONG>(reason));
}

void RecordDiagnosticObservationLoss(CustomContentLedger* ledger,
                                     LedgerLossReason reason) noexcept
{
    if (!ledger) return;
    dluma_atomic_increment(&ledger->diagnostic_observation_loss);
    dluma_atomic_exchange(
        &ledger->last_diagnostic_loss_reason,
        static_cast<LONG>(reason));
}

void RecordBroadFileObservationLoss(CustomContentLedger* ledger,
                                    LedgerLossReason reason,
                                    std::uint64_t sequence) noexcept
{
    if (!ledger) return;
    dluma_atomic_increment(&ledger->broad_file_observation_loss);
    dluma_atomic_exchange(
        &ledger->last_broad_file_loss_reason,
        static_cast<LONG>(reason));
    if (sequence) {
        _InterlockedCompareExchange64(
            &ledger->broad_file_first_loss_sequence,
            static_cast<LONGLONG>(sequence), 0);
        _InterlockedExchange64(
            &ledger->broad_file_last_loss_sequence,
            static_cast<LONGLONG>(sequence));
    }
}

void RecordIrrelevantObservationWithoutEpoch(
    CustomContentLedger* ledger, LedgerLossReason reason) noexcept
{
    if (!ledger) return;
    dluma_atomic_increment(&ledger->irrelevant_without_epoch);
    dluma_atomic_exchange(
        &ledger->last_irrelevant_loss_reason,
        static_cast<LONG>(reason));
}

LedgerLossState CaptureLossState(
    const CustomContentLedger* ledger) noexcept
{
    LedgerLossState result = {};
    if (!ledger) return result;
    result.ownership_mutation_loss = static_cast<std::uint32_t>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->ownership_mutation_loss),
            0, 0));
    result.diagnostic_observation_loss = static_cast<std::uint32_t>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->diagnostic_observation_loss),
            0, 0));
    result.broad_file_observation_loss =
        static_cast<std::uint32_t>(dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(
                &ledger->broad_file_observation_loss),
            0, 0));
    result.irrelevant_without_epoch = static_cast<std::uint32_t>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->irrelevant_without_epoch),
            0, 0));
    result.last_ownership_reason = static_cast<LedgerLossReason>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->last_ownership_loss_reason),
            0, 0));
    result.last_diagnostic_reason = static_cast<LedgerLossReason>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->last_diagnostic_loss_reason),
            0, 0));
    result.last_broad_file_reason = static_cast<LedgerLossReason>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(
                &ledger->last_broad_file_loss_reason),
            0, 0));
    result.last_irrelevant_reason = static_cast<LedgerLossReason>(
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->last_irrelevant_loss_reason),
            0, 0));
    result.broad_file_first_loss_sequence =
        static_cast<std::uint64_t>(_InterlockedCompareExchange64(
            const_cast<volatile LONGLONG*>(
                &ledger->broad_file_first_loss_sequence),
            0, 0));
    result.broad_file_last_loss_sequence =
        static_cast<std::uint64_t>(_InterlockedCompareExchange64(
            const_cast<volatile LONGLONG*>(
                &ledger->broad_file_last_loss_sequence),
            0, 0));
    return result;
}

bool MarkBaselineCaptured(CustomContentLedger* ledger, bool captured,
                          std::uint64_t sequence) noexcept
{
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    ledger->baseline_captured = captured;
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::StartupBaselineCaptured,
        0, captured ? 1U : 0U, "startup-B0-capture",
        captured ? "logical-baseline-captured"
                 : "logical-baseline-incomplete",
        sequence);
    return true;
}

bool NormalizeLogicalPath(const char* input, char* output,
                          std::size_t capacity) noexcept
{
    return NormalizePath(input, output, capacity, true);
}

bool NormalizePhysicalPath(const char* input, char* output,
                           std::size_t capacity) noexcept
{
    return NormalizePath(input, output, capacity, false);
}

bool NormalizeProjectRoot(const char* input, char* output,
                          std::size_t capacity) noexcept
{
    if (capacity > static_cast<std::size_t>(UINT32_MAX))
        return false;
    return dluma_project_root_canonicalize(
               input, output,
               static_cast<unsigned int>(capacity)) != 0;
}

bool PathsEqual(const char* left, const char* right) noexcept
{
    char normalized_left[kSourcePathCapacity];
    char normalized_right[kSourcePathCapacity];
    const bool canonical_drive_paths =
        NormalizeProjectRoot(left, normalized_left,
                             sizeof(normalized_left)) &&
        NormalizeProjectRoot(right, normalized_right,
                             sizeof(normalized_right));
    if (canonical_drive_paths)
        return TextEquals(normalized_left, normalized_right);
    return NormalizePhysicalPath(left, normalized_left,
                                 sizeof(normalized_left)) &&
           NormalizePhysicalPath(right, normalized_right,
                                 sizeof(normalized_right)) &&
           TextEquals(normalized_left, normalized_right);
}

bool ProjectRootsEqual(const char* left, const char* right) noexcept
{
    return dluma_project_root_identity_equal(left, right) != 0;
}

bool PathWithinRoot(const char* path, const char* root) noexcept
{
    char normalized_path[kSourcePathCapacity];
    char normalized_root[kSourcePathCapacity];
    if (!NormalizeProjectRoot(path, normalized_path,
                              sizeof(normalized_path)) ||
        !NormalizeProjectRoot(root, normalized_root,
                              sizeof(normalized_root))) {
        if (!NormalizePhysicalPath(path, normalized_path,
                                   sizeof(normalized_path)) ||
            !NormalizePhysicalPath(root, normalized_root,
                                   sizeof(normalized_root)))
            return false;
    }
    std::size_t root_length = 0;
    while (normalized_root[root_length]) ++root_length;
    if (!StartsWith(normalized_path, normalized_root)) return false;
    return normalized_path[root_length] == 0 ||
           normalized_path[root_length] == '\\';
}

CustomSourceKind ClassifySourceKind(
    const char* normalized_path,
    const char* normalized_project_root) noexcept
{
    char canonical_path[kSourcePathCapacity];
    char canonical_root[kSourcePathCapacity];
    if (!NormalizeProjectRoot(
            normalized_path, canonical_path,
            sizeof(canonical_path)) ||
        !NormalizeProjectRoot(
            normalized_project_root, canonical_root,
            sizeof(canonical_root)) ||
        !PathWithinRoot(canonical_path, canonical_root))
        return CustomSourceKind::UnknownProjectSource;
    if (TextEquals(canonical_path, canonical_root))
        return CustomSourceKind::ProjectRoot;
    std::size_t root_length = 0;
    while (canonical_root[root_length]) ++root_length;
    const char* relative = canonical_path + root_length;
    if (*relative == '\\') ++relative;
    if (TextEquals(relative, "data"))
        return CustomSourceKind::DataDirectory;
    if (IsDataWorkshopPak(relative))
        return CustomSourceKind::DataWorkshopPak;
    if (TextEquals(relative, "assets_pc"))
        return CustomSourceKind::AssetsPc;
    if (IsSpeechStem(relative))
        return EndsWith(relative, ".pak")
                   ? CustomSourceKind::SpeechPak
                   : CustomSourceKind::SpeechDirectory;
    if (IsLocaleStem(relative))
        return EndsWith(relative, ".pak")
                   ? CustomSourceKind::LocalePak
                   : CustomSourceKind::LocaleDirectory;
    return CustomSourceKind::UnknownProjectSource;
}

const char* SourceKindName(CustomSourceKind kind) noexcept
{
    switch (kind) {
    case CustomSourceKind::ProjectRoot: return "ProjectRoot";
    case CustomSourceKind::DataDirectory: return "DataDirectory";
    case CustomSourceKind::DataWorkshopPak: return "DataWorkshopPak";
    case CustomSourceKind::LocaleDirectory: return "LocaleDirectory";
    case CustomSourceKind::LocalePak: return "LocalePak";
    case CustomSourceKind::SpeechDirectory: return "SpeechDirectory";
    case CustomSourceKind::SpeechPak: return "SpeechPak";
    case CustomSourceKind::AssetsPc: return "AssetsPc";
    case CustomSourceKind::Other: return "Other";
    case CustomSourceKind::UnknownProjectSource:
    default: return "UnknownProjectSource";
    }
}

const char* LifetimeName(ContentLifetimeClass lifetime) noexcept
{
    switch (lifetime) {
    case ContentLifetimeClass::SourceOnly: return "SourceOnly";
    case ContentLifetimeClass::LevelOwnedResource:
        return "LevelOwnedResource";
    case ContentLifetimeClass::GlobalDataManager:
        return "GlobalDataManager";
    case ContentLifetimeClass::AudioBank: return "AudioBank";
    case ContentLifetimeClass::ProcessGlobalAllowed:
        return "ProcessGlobalAllowed";
    case ContentLifetimeClass::Unknown:
    default: return "Unknown";
    }
}

const char* ProviderDispositionName(
    BaselineProviderDisposition disposition) noexcept
{
    switch (disposition) {
    case BaselineProviderDisposition::BaselineOverride:
        return "baseline-override";
    case BaselineProviderDisposition::CustomOnly: return "custom-only";
    case BaselineProviderDisposition::UnknownFallback:
        return "unknown-fallback";
    case BaselineProviderDisposition::Unresolved:
    default: return "unresolved";
    }
}

const char* ContentOpenStagingPhaseName(
    ContentOpenStagingPhase phase) noexcept
{
    switch (phase) {
    case ContentOpenStagingPhase::CustomContentStaging:
        return "custom_content_staging";
    case ContentOpenStagingPhase::TransitionActive:
        return "transition_active";
    case ContentOpenStagingPhase::MapActive: return "map_active";
    case ContentOpenStagingPhase::Frontend: return "frontend";
    default: return "unknown";
    }
}

const char* WatchedParserOutcomeName(
    WatchedParserOutcome outcome) noexcept
{
    switch (outcome) {
    case WatchedParserOutcome::ParsedNew: return "parsed_new";
    case WatchedParserOutcome::ParsedReplaced: return "parsed_replaced";
    case WatchedParserOutcome::ParsedMerged: return "parsed_merged";
    case WatchedParserOutcome::CacheHitSameEpoch:
        return "cache_hit_same_epoch";
    case WatchedParserOutcome::CacheHitPriorEpoch:
        return "cache_hit_prior_epoch";
    case WatchedParserOutcome::RegistrationConflict:
        return "registration_conflict";
    case WatchedParserOutcome::ParseFailed: return "parse_failed";
    case WatchedParserOutcome::CorrelationUnknown:
        return "correlation_unknown";
    default: return "unknown";
    }
}

std::uint64_t NextSequence(CustomContentLedger* ledger) noexcept
{
    if (!ledger) return 0;
    LONGLONG observed =
        _InterlockedCompareExchange64(&ledger->sequence, 0, 0);
    for (;;) {
        const LONGLONG next = observed + 1;
        const LONGLONG previous = _InterlockedCompareExchange64(
            &ledger->sequence, next, observed);
        if (previous == observed)
            return static_cast<std::uint64_t>(next);
        observed = previous;
    }
}

std::uint32_t BeginEpoch(CustomContentLedger* ledger,
                         const char* selected_content_name,
                         const WCHAR* project_root,
                         const char* normalized_project_root,
                         std::uint64_t sequence) noexcept
{
    char canonical_project_root[kSourcePathCapacity];
    if (!normalized_project_root ||
        !NormalizeProjectRoot(
            normalized_project_root,
            canonical_project_root,
            sizeof(canonical_project_root)))
        return 0;
    LedgerGuard guard(ledger, LedgerLossReason::EpochContention);
    if (!guard.locked())
        return 0;
    CustomContentEpoch* current =
        FindEpochUnlocked(ledger, ledger->current_epoch);
    if (current && current->active &&
        ProjectRootsEqual(current->normalized_project_root,
                          canonical_project_root))
        return current->id;
    if (current && current->active) {
        current->active = false;
        current->accepting_records = false;
        current->frontend_reset_pending = true;
        PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::EpochEnd,
                               current->id, 0,
                               current->normalized_project_root,
                               "superseded-by-project", sequence);
    }
    std::uint32_t epoch_slot = 0;
    while (epoch_slot < ledger->epoch_count && ledger->epochs[epoch_slot].id) ++epoch_slot;
    if (epoch_slot >= kMaximumEpochs || !ledger->next_epoch_id) {
        RecordOwnershipMutationLoss(
            ledger, LedgerLossReason::EpochCapacity);
        return 0;
    }
    if (epoch_slot == ledger->epoch_count) ++ledger->epoch_count;
    CustomContentEpoch* epoch = &ledger->epochs[epoch_slot];
    std::memset(epoch, 0, sizeof(*epoch));
    epoch->id = ledger->next_epoch_id++;
    epoch->begin_sequence = sequence;
    CopyText(epoch->selected_content_name,
             sizeof(epoch->selected_content_name),
             selected_content_name);
    epoch->selected_content_name_available =
        selected_content_name && selected_content_name[0] &&
        selected_content_name[0] != '<';
    CopyWide(epoch->project_root, kProjectRootCapacity, project_root);
    CopyText(epoch->normalized_project_root,
             sizeof(epoch->normalized_project_root),
             canonical_project_root);
    epoch->active = true;
    epoch->accepting_records = true;
    epoch->ownership_complete = true;
    ledger->current_epoch = epoch->id;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::EpochBegin,
                           epoch->id, 1, epoch->selected_content_name,
                           epoch->normalized_project_root, sequence);
    return epoch->id;
}

bool CloseEpochForFrontend(CustomContentLedger* ledger,
                           std::uint64_t sequence) noexcept
{
    return CloseEpochForFrontend(ledger,
                                 CurrentEpochId(ledger), sequence);
}

bool CloseEpochForFrontend(CustomContentLedger* ledger,
                           std::uint32_t epoch_id,
                           std::uint64_t sequence) noexcept
{
    LedgerGuard guard(ledger, LedgerLossReason::ConsumptionContention);
    if (!guard.locked()) return false;
    CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, epoch_id);
    if (!epoch) return false;
    if (!epoch->active && epoch->frontend_reset_pending) return true;
    if (!epoch->active) return false;
    epoch->active = false;
    epoch->accepting_records = false;
    epoch->frontend_reset_pending = true;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::EpochEnd,
                           epoch->id, 1, epoch->selected_content_name,
                           "frontend-reset", sequence);
    return true;
}

std::uint32_t CurrentEpochId(CustomContentLedger* ledger) noexcept
{
    LedgerReadGuard guard(ledger);
    return guard.locked() ? ledger->current_epoch : 0;
}

bool TryGetCurrentEpochId(CustomContentLedger* ledger,
                          std::uint32_t* epoch_out) noexcept
{
    if (!epoch_out) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    *epoch_out = ledger->current_epoch;
    return true;
}

bool SnapshotEpoch(CustomContentLedger* ledger, std::uint32_t epoch,
                   CustomContentEpoch* output) noexcept
{
    if (!output) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) {
        RecordDiagnosticObservationLoss(
            ledger, LedgerLossReason::OptionalObservationContention);
        return false;
    }
    const CustomContentEpoch* found = FindEpochUnlocked(ledger, epoch);
    if (!found) return false;
    *output = *found;
    return true;
}

bool MarkEpochComponentResults(CustomContentLedger* ledger,
                               std::uint32_t epoch,
                               bool audio_quiesced,
                               bool sources_removed,
                               bool global_data_restored,
                               bool audio_restored,
                               std::uint64_t sequence) noexcept
{
    LedgerGuard guard(ledger, LedgerLossReason::ConsumptionContention);
    if (!guard.locked()) return false;
    CustomContentEpoch* found = FindEpochUnlocked(ledger, epoch);
    if (!found) return false;
    found->audio_quiesced = audio_quiesced;
    found->sources_removed = sources_removed;
    found->global_data_restored = global_data_restored;
    found->audio_restored = audio_restored;
    const bool complete = audio_quiesced && sources_removed &&
                          global_data_restored && audio_restored;
    PushDiagnosticUnlocked(
        ledger,
        complete ? LedgerDiagnosticType::FrontendResetComplete
                 : LedgerDiagnosticType::FrontendResetPartial,
        epoch, complete ? 1U : 0U,
        complete ? "all-components-restored"
                 : "one-or-more-components-partial",
        "component-status-recorded", sequence);
    return true;
}

bool MarkEpochOwnershipLost(CustomContentLedger* ledger,
                            std::uint32_t epoch,
                            LedgerLossReason reason,
                            bool project_root_conflict,
                            std::uint64_t sequence) noexcept
{
    LedgerGuard guard(ledger, reason);
    if (!guard.locked()) return false;
    CustomContentEpoch* found = FindEpochUnlocked(ledger, epoch);
    if (!found) return false;
    found->ownership_complete = false;
    found->ownership_mutation_lost = true;
    found->project_root_conflict = project_root_conflict;
    RecordOwnershipMutationLoss(ledger, reason);
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::OwnershipMutationLost,
        epoch, 0, LedgerLossReasonName(reason),
        found->normalized_project_root, sequence);
    return true;
}

bool MarkEpochConsumed(CustomContentLedger* ledger,
                       std::uint32_t epoch,
                       std::uint64_t sequence) noexcept
{
    LedgerGuard guard(ledger, LedgerLossReason::ConsumptionContention);
    if (!guard.locked()) return false;
    CustomContentEpoch* found = FindEpochUnlocked(ledger, epoch);
    if (!found || found->consumed) {
        RecordOwnershipMutationLoss(
            ledger, LedgerLossReason::ConsumptionContention);
        return false;
    }
    found->consumed = true;
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::EpochEnd, epoch, 1,
        found->normalized_project_root, "consumed-for-reset", sequence);
    return true;
}

bool UpdateEpochOptionalContentName(
    CustomContentLedger* ledger, std::uint32_t epoch,
    const char* selected_content_name,
    std::uint64_t sequence) noexcept
{
    if (!selected_content_name || !selected_content_name[0] ||
        selected_content_name[0] == '<')
        return false;
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    CustomContentEpoch* found = FindEpochUnlocked(ledger, epoch);
    if (!found || found->consumed || found->sources_removed)
        return false;
    if (!found->selected_content_name_available) {
        CopyText(found->selected_content_name,
                 sizeof(found->selected_content_name),
                 selected_content_name);
        found->selected_content_name_available = true;
        PushDiagnosticUnlocked(
            ledger, LedgerDiagnosticType::LazyEpochReused, epoch, 1,
            found->selected_content_name,
            "optional-content-label-attached", sequence);
    }
    return true;
}

bool RecordSuccessfulSourceAdd(CustomContentLedger* ledger, void* source,
                               const char* original_path,
                               const char* normalized_path,
                               std::uint32_t flags,
                               CustomSourceKind kind,
                               bool loader_owned_custom_content,
                               std::uint64_t sequence,
                               bool path_created_ownership) noexcept
{
    const std::uint32_t epoch = CurrentEpochId(ledger);
    if (!epoch) return false;
    return RecordSuccessfulSourceAddForEpochDetailed(
               ledger, epoch, source, original_path, normalized_path,
               flags, kind, loader_owned_custom_content, sequence,
               path_created_ownership) ==
        SourceAddRecordResult::Recorded;
}

bool RecordSuccessfulSourceAddForEpoch(
    CustomContentLedger* ledger, std::uint32_t epoch_id, void* source,
    const char* original_path, const char* normalized_path,
    std::uint32_t flags, CustomSourceKind kind,
    bool loader_owned_custom_content, std::uint64_t sequence,
    bool path_created_ownership) noexcept
{
    return RecordSuccessfulSourceAddForEpochDetailed(
               ledger, epoch_id, source, original_path, normalized_path,
               flags, kind, loader_owned_custom_content, sequence,
               path_created_ownership) ==
        SourceAddRecordResult::Recorded;
}

SourceAddRecordResult RecordSuccessfulSourceAddForEpochDetailed(
    CustomContentLedger* ledger, std::uint32_t epoch_id, void* source,
    const char* original_path, const char* normalized_path,
    std::uint32_t flags, CustomSourceKind kind,
    bool loader_owned_custom_content, std::uint64_t sequence,
    bool path_created_ownership) noexcept
{
    char canonical_path[kSourcePathCapacity];
    if (!normalized_path ||
        !NormalizeProjectRoot(
            normalized_path, canonical_path,
            sizeof(canonical_path)))
        return SourceAddRecordResult::Failed;
    LedgerGuard guard(ledger, LedgerLossReason::SourceAddContention);
    if (!guard.locked())
        return SourceAddRecordResult::Failed;
    CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, epoch_id);
    if (!epoch || !epoch->accepting_records ||
        !PathWithinRoot(canonical_path, epoch->normalized_project_root))
        return SourceAddRecordResult::Failed;
    std::uint32_t source_slot = 0;
    while (source_slot < ledger->source_count && ledger->sources[source_slot].add_succeeded) ++source_slot;
    if (source_slot >= kMaximumSources || !ledger->next_source_ordinal) {
        RecordOwnershipMutationLoss(
            ledger, LedgerLossReason::SourceAddCapacity);
        epoch->ownership_complete = false;
        epoch->ownership_mutation_lost = true;
        return SourceAddRecordResult::Failed;
    }
    const bool singleton_kind =
        kind == CustomSourceKind::ProjectRoot ||
        kind == CustomSourceKind::DataDirectory ||
        kind == CustomSourceKind::AssetsPc;
    bool duplicate_active_kind = false;
    if (singleton_kind) {
        for (std::uint32_t index = 0;
             index < ledger->source_count; ++index) {
            const CustomSourceRecord& prior = ledger->sources[index];
            if (!prior.add_succeeded || !prior.active ||
                !prior.loader_owned_custom_content ||
                prior.kind != kind)
                continue;
            const CustomContentEpoch* prior_epoch =
                FindEpochUnlocked(ledger, prior.epoch);
            if (!prior_epoch ||
                ProjectRootsEqual(
                    prior_epoch->normalized_project_root,
                    epoch->normalized_project_root)) {
                duplicate_active_kind = true;
                break;
            }
        }
    }
    if (source_slot == ledger->source_count) ++ledger->source_count;
    else ledger->source_slot_reused[source_slot] = true;
    CustomSourceRecord* record = &ledger->sources[source_slot];
    std::memset(record, 0, sizeof(*record));
    record->source = source;
    CopyText(record->original_path, sizeof(record->original_path),
             original_path);
    CopyText(record->normalized_path, sizeof(record->normalized_path),
             canonical_path);
    record->flags = flags;
    record->epoch = epoch->id;
    record->successful_add_ordinal = ledger->next_source_ordinal++;
    record->kind = kind;
    record->add_succeeded = true;
    record->active = true;
    record->exact_handle_available = source != nullptr;
    record->loader_owned_custom_content = loader_owned_custom_content;
    record->path_created_ownership = path_created_ownership;
    ++epoch->successful_source_count;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::SourceTracked,
                           epoch->id, static_cast<std::uint32_t>(kind),
                           record->normalized_path,
                           SourceKindName(kind), sequence,
                           reinterpret_cast<std::uintptr_t>(source),
                           flags, record->successful_add_ordinal);
    if (duplicate_active_kind) {
        RecordOwnershipMutationLoss(
            ledger, LedgerLossReason::SourceIdentityIncomplete);
        epoch->ownership_complete = false;
        epoch->ownership_mutation_lost = true;
        PushDiagnosticUnlocked(
            ledger, LedgerDiagnosticType::OwnershipMutationLost,
            epoch->id, static_cast<std::uint32_t>(kind),
            record->normalized_path, "duplicate-active-source-kind",
            sequence, reinterpret_cast<std::uintptr_t>(source),
            record->successful_add_ordinal, 0);
        return SourceAddRecordResult::DuplicateActiveKindRecorded;
    }
    return SourceAddRecordResult::Recorded;
}

bool RecordNaturalSourceRemoval(CustomContentLedger* ledger, void* source,
                                const char* path, bool succeeded,
                                std::uint64_t sequence) noexcept
{
    if (!succeeded) return false;
    char normalized[kSourcePathCapacity];
    normalized[0] = 0;
    if (path) NormalizePhysicalPath(path, normalized, sizeof(normalized));
    LedgerGuard guard(ledger, LedgerLossReason::SourceRemovalContention);
    if (!guard.locked()) return false;
    /* After any address can have been retired, an unqualified pointer/path
     * observation cannot distinguish its former and replacement incarnations. */
    if (ledger->retirement_history.epochs) return false;
    CustomSourceRecord* match = nullptr;
    std::uint32_t match_count = 0;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
        CustomSourceRecord* record = &ledger->sources[index];
        if (!record->active) continue;
        const bool same = source
            ? record->source == source
            : normalized[0] &&
                  TextEquals(record->normalized_path, normalized);
        if (!same) continue;
        match = record;
        ++match_count;
    }
    /* An object-form natural removal may never fall back to a same-path
     * record: that can deactivate a replacement after pointer/path ownership
     * drift.  Both exact-object and path-only observations must select one
     * active ledger record. */
    if (!match || match_count != 1) {
        if (match_count > 1)
            RecordOwnershipMutationLoss(
                ledger, LedgerLossReason::SourceRemovalContention);
        return false;
    }
    match->active = false;
    match->removed_naturally = true;
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::SourceRemovedNaturally,
        match->epoch, 1, match->normalized_path,
        source ? "exact-handle" : "unique-path", sequence);
    return true;
}

std::size_t SnapshotActiveSourcesForReset(
    CustomContentLedger* ledger, SourceResetSnapshot* output,
    std::size_t capacity, std::uint64_t sequence) noexcept
{
    return SnapshotActiveSourcesThroughEpoch(
        ledger, UINT32_MAX, output, capacity, sequence);
}

std::size_t SnapshotActiveSourcesThroughEpoch(
    CustomContentLedger* ledger, std::uint32_t maximum_epoch,
    SourceResetSnapshot* output, std::size_t capacity,
    std::uint64_t sequence) noexcept
{
    return SnapshotActiveSourcesThroughEpochComplete(
               ledger, maximum_epoch, output, capacity, sequence)
        .count;
}

SourceSnapshotResult SnapshotActiveSourcesThroughEpochComplete(
    CustomContentLedger* ledger, std::uint32_t maximum_epoch,
    SourceResetSnapshot* output, std::size_t capacity,
    std::uint64_t sequence) noexcept
{
    SourceSnapshotResult result = {};
    if (!ledger || !output || !capacity) return result;
    LedgerGuard guard(ledger, LedgerLossReason::ConsumptionContention);
    if (!guard.locked()) {
        result.dropped_mutations = DroppedMutationCount(ledger);
        return result;
    }
    std::size_t count = 0;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
        const CustomSourceRecord& record = ledger->sources[index];
        if (record.epoch > maximum_epoch || !record.active ||
            !record.add_succeeded ||
            !record.loader_owned_custom_content)
            continue;
        ++result.expected_active_count;
        if (count >= capacity) {
            RecordOwnershipMutationLoss(
                ledger, LedgerLossReason::SourceAddCapacity);
            continue;
        }
        SourceResetSnapshot* snapshot = &output[count++];
        std::memset(snapshot, 0, sizeof(*snapshot));
        snapshot->record_index = index;
        snapshot->epoch = record.epoch;
        snapshot->successful_add_ordinal =
            record.successful_add_ordinal;
        snapshot->source = record.source;
        snapshot->flags = record.flags;
        snapshot->kind = record.kind;
        snapshot->exact_handle_available =
            record.exact_handle_available;
        snapshot->path_created_ownership =
            record.path_created_ownership;
        CopyText(snapshot->original_path, sizeof(snapshot->original_path),
                 record.original_path);
        CopyText(snapshot->normalized_path,
                 sizeof(snapshot->normalized_path),
                 record.normalized_path);
    }
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = left + 1; right < count; ++right) {
            if (output[right].successful_add_ordinal >
                output[left].successful_add_ordinal) {
                const SourceResetSnapshot temporary = output[left];
                output[left] = output[right];
                output[right] = temporary;
            }
        }
    }
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::FrontendResetBegin,
                           ledger->current_epoch,
                           static_cast<std::uint32_t>(count),
                           "source-reset-snapshot",
                           "reverse-successful-add-order", sequence);
    result.count = count;
    result.dropped_mutations = static_cast<std::uint32_t>(
        ledger->ownership_mutation_loss);
    result.complete = result.dropped_mutations == 0 &&
                      result.count == result.expected_active_count;
    return result;
}

SourceSnapshotResult SnapshotActiveSourcesForEpochComplete(
    CustomContentLedger* ledger, std::uint32_t epoch_id,
    SourceResetSnapshot* output, std::size_t capacity,
    std::uint64_t sequence) noexcept
{
    SourceSnapshotResult result = {};
    if (!ledger || !epoch_id || !output || !capacity) return result;
    LedgerGuard guard(ledger, LedgerLossReason::ConsumptionContention);
    if (!guard.locked()) {
        result.dropped_mutations = DroppedMutationCount(ledger);
        return result;
    }
    const CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, epoch_id);
    if (!epoch) return result;
    std::size_t count = 0;
    for (std::uint32_t index = 0;
         index < ledger->source_count; ++index) {
        const CustomSourceRecord& record = ledger->sources[index];
        if (record.epoch != epoch_id || !record.active ||
            !record.add_succeeded ||
            !record.loader_owned_custom_content)
            continue;
        ++result.expected_active_count;
        if (count >= capacity) {
            RecordOwnershipMutationLoss(
                ledger, LedgerLossReason::SourceAddCapacity);
            continue;
        }
        SourceResetSnapshot* snapshot = &output[count++];
        std::memset(snapshot, 0, sizeof(*snapshot));
        snapshot->record_index = index;
        snapshot->epoch = record.epoch;
        snapshot->successful_add_ordinal =
            record.successful_add_ordinal;
        snapshot->source = record.source;
        snapshot->flags = record.flags;
        snapshot->kind = record.kind;
        snapshot->exact_handle_available =
            record.exact_handle_available;
        snapshot->path_created_ownership =
            record.path_created_ownership;
        CopyText(snapshot->original_path,
                 sizeof(snapshot->original_path),
                 record.original_path);
        CopyText(snapshot->normalized_path,
                 sizeof(snapshot->normalized_path),
                 record.normalized_path);
    }
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = left + 1;
             right < count; ++right) {
            if (output[right].successful_add_ordinal >
                output[left].successful_add_ordinal) {
                const SourceResetSnapshot temporary = output[left];
                output[left] = output[right];
                output[right] = temporary;
            }
        }
    }
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::FrontendResetBegin,
        epoch_id, static_cast<std::uint32_t>(count),
        "cancelled-presentation-source-snapshot",
        "exact-epoch-reverse-successful-add-order", sequence);
    result.count = count;
    result.dropped_mutations = static_cast<std::uint32_t>(
        ledger->ownership_mutation_loss);
    result.complete = epoch->ownership_complete &&
        !epoch->ownership_mutation_lost &&
        !epoch->project_root_conflict &&
        result.dropped_mutations == 0 &&
        result.count == result.expected_active_count;
    return result;
}

PresentationSourceSetSnapshotResult SnapshotPresentationSourceSet(
    CustomContentLedger* ledger, std::uint32_t epoch_id,
    const char* normalized_project_root,
    PresentationSourceSetSnapshot* output) noexcept
{
    if (!output)
        return PresentationSourceSetSnapshotResult::InvalidArgument;
    std::memset(output, 0, sizeof(*output));
    output->result =
        PresentationSourceSetSnapshotResult::InvalidArgument;
    if (!ledger || !epoch_id || !normalized_project_root ||
        !normalized_project_root[0])
        return output->result;
    if (!NormalizeProjectRoot(
            normalized_project_root,
            output->presenter_root_canonical,
            sizeof(output->presenter_root_canonical))) {
        output->result =
            PresentationSourceSetSnapshotResult::ProjectRootInvalid;
        return output->result;
    }
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) {
        output->result =
            PresentationSourceSetSnapshotResult::LedgerBusy;
        return output->result;
    }
    const CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, epoch_id);
    if (!epoch) {
        output->result =
            PresentationSourceSetSnapshotResult::EpochMissing;
        return output->result;
    }
    output->epoch = epoch_id;
    if (!NormalizeProjectRoot(
            epoch->normalized_project_root,
            output->epoch_root_canonical,
            sizeof(output->epoch_root_canonical))) {
        output->result =
            PresentationSourceSetSnapshotResult::ProjectRootInvalid;
        return output->result;
    }
    output->root_equal = ProjectRootsEqual(
        output->epoch_root_canonical,
        output->presenter_root_canonical);
    if (!output->root_equal) {
        output->result =
            PresentationSourceSetSnapshotResult::ProjectRootMismatch;
        return output->result;
    }
    output->ownership_complete =
        epoch->active && epoch->accepting_records &&
        !epoch->consumed && !epoch->sources_removed &&
        epoch->ownership_complete &&
        !epoch->ownership_mutation_lost &&
        !epoch->project_root_conflict &&
        ledger->ownership_mutation_loss == 0;
    output->exact_handles_complete = true;
    for (std::uint32_t index = 0;
         index < ledger->source_count; ++index) {
        const CustomSourceRecord& record = ledger->sources[index];
        if (record.epoch != epoch_id || !record.active ||
            !record.add_succeeded ||
            !record.loader_owned_custom_content)
            continue;
        ++output->active_source_count;
        if (!record.exact_handle_available ||
            !record.path_created_ownership ||
            !record.source ||
            !PathWithinRoot(record.normalized_path,
                            output->presenter_root_canonical))
            output->exact_handles_complete = false;
        if (record.kind == CustomSourceKind::ProjectRoot)
            ++output->project_root_count;
        else if (record.kind == CustomSourceKind::DataDirectory)
            ++output->data_directory_count;
        else if (record.kind == CustomSourceKind::AssetsPc)
            ++output->assets_pc_count;
    }
    if (output->project_root_count > 1U ||
        output->data_directory_count > 1U ||
        output->assets_pc_count > 1U)
        output->result =
            PresentationSourceSetSnapshotResult::DuplicateSourceKind;
    else if (!output->ownership_complete)
        output->result =
            PresentationSourceSetSnapshotResult::OwnershipIncomplete;
    else if (!output->exact_handles_complete)
        output->result =
            PresentationSourceSetSnapshotResult::ExactHandleIncomplete;
    else if (output->project_root_count != 1U ||
             output->data_directory_count != 1U ||
             output->assets_pc_count != 1U)
        output->result =
            PresentationSourceSetSnapshotResult::
                RequiredSourceKindMissing;
    else
        output->result =
            PresentationSourceSetSnapshotResult::Succeeded;
    output->complete =
        output->result ==
            PresentationSourceSetSnapshotResult::Succeeded;
    return output->result;
}

const char* PresentationSourceSetSnapshotResultName(
    PresentationSourceSetSnapshotResult result) noexcept
{
    switch (result) {
    case PresentationSourceSetSnapshotResult::Succeeded:
        return "Succeeded";
    case PresentationSourceSetSnapshotResult::InvalidArgument:
        return "InvalidArgument";
    case PresentationSourceSetSnapshotResult::LedgerBusy:
        return "LedgerBusy";
    case PresentationSourceSetSnapshotResult::EpochMissing:
        return "EpochMissing";
    case PresentationSourceSetSnapshotResult::ProjectRootInvalid:
        return "ProjectRootInvalid";
    case PresentationSourceSetSnapshotResult::ProjectRootMismatch:
        return "ProjectRootMismatch";
    case PresentationSourceSetSnapshotResult::OwnershipIncomplete:
        return "OwnershipIncomplete";
    case PresentationSourceSetSnapshotResult::ExactHandleIncomplete:
        return "ExactHandleIncomplete";
    case PresentationSourceSetSnapshotResult::DuplicateSourceKind:
        return "DuplicateSourceKind";
    case PresentationSourceSetSnapshotResult::RequiredSourceKindMissing:
        return "RequiredSourceKindMissing";
    default:
        return "InvalidArgument";
    }
}

bool MarkSourceResetResult(CustomContentLedger* ledger,
                           std::uint32_t record_index,
                           bool removed,
                           std::uint64_t sequence,
                           const SourceResetSnapshot* expected) noexcept
{
    LedgerGuard guard(ledger, LedgerLossReason::SourceRemovalContention);
    if (!guard.locked() || !SourceTokenMatches(ledger, record_index, expected))
        return false;
    CustomSourceRecord* record = &ledger->sources[record_index];
    if (removed) {
        record->active = false;
        record->removed_by_reset = true;
        record->reset_failed = false;
    } else {
        record->reset_failed = true;
    }
    PushDiagnosticUnlocked(
        ledger,
        removed ? LedgerDiagnosticType::SourceRemovedByReset
                : LedgerDiagnosticType::SourceResetFailed,
        record->epoch, removed ? 1U : 0U, record->normalized_path,
        record->exact_handle_available ? "exact-handle"
                                       : "path-fallback",
        sequence);
    CustomContentEpoch* epoch = FindEpochUnlocked(ledger, record->epoch);
    if (epoch) {
        bool all_removed = true;
        for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
            const CustomSourceRecord& candidate = ledger->sources[index];
            if (candidate.epoch == record->epoch &&
                candidate.loader_owned_custom_content &&
                candidate.add_succeeded && candidate.active) {
                all_removed = false;
                break;
            }
        }
        epoch->sources_removed = all_removed;
    }
    return true;
}

bool SnapshotSourceMutationToken(CustomContentLedger* ledger,
                                 std::uint32_t record_index,
                                 std::uint32_t expected_epoch,
                                 SourceResetSnapshot* output) noexcept
{
    if (!output || !expected_epoch) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked() || record_index >= ledger->source_count) return false;
    const auto& source = ledger->sources[record_index];
    if (!source.add_succeeded || source.epoch != expected_epoch) return false;
    std::memset(output, 0, sizeof(*output));
    output->record_index = record_index;
    output->epoch = source.epoch;
    output->successful_add_ordinal = source.successful_add_ordinal;
    output->source = source.source;
    output->flags = source.flags;
    output->kind = source.kind;
    output->exact_handle_available = source.exact_handle_available;
    output->path_created_ownership = source.path_created_ownership;
    CopyText(output->original_path, sizeof(output->original_path), source.original_path);
    CopyText(output->normalized_path, sizeof(output->normalized_path), source.normalized_path);
    return true;
}

bool RecordNaturalSourceRemovalExact(CustomContentLedger* ledger,
                                     const SourceResetSnapshot* expected,
                                     bool succeeded,
                                     std::uint64_t sequence) noexcept
{
    if (!expected || !succeeded) return false;
    LedgerGuard guard(ledger, LedgerLossReason::SourceRemovalContention);
    if (!guard.locked() || !SourceTokenMatches(ledger, expected->record_index, expected)) return false;
    auto& source = ledger->sources[expected->record_index];
    if (!source.active) return source.removed_naturally;
    source.active = false;
    source.removed_naturally = true;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::SourceRemovedNaturally,
        source.epoch, 1U, source.normalized_path, "exact-incarnation", sequence,
        reinterpret_cast<std::uintptr_t>(source.source), source.successful_add_ordinal);
    return true;
}

bool SnapshotNaturalSourceMutationToken(CustomContentLedger* ledger,
                                        void* source, const char* path,
                                        SourceResetSnapshot* output) noexcept
{
    if (!output || (!source && (!path || !path[0]))) return false;
    char normalized[kSourcePathCapacity] = {};
    if (path && !NormalizePhysicalPath(path, normalized, sizeof(normalized))) return false;
    LedgerGuard guard(ledger, LedgerLossReason::SourceRemovalContention);
    if (!guard.locked()) return false;
    const CustomSourceRecord* match = nullptr;
    std::uint32_t matched_index = 0;
    for (std::uint32_t i = 0; i < ledger->source_count; ++i) {
        const auto& candidate = ledger->sources[i];
        if (!candidate.active || !candidate.add_succeeded || !candidate.loader_owned_custom_content) continue;
        if (source ? candidate.source != source : !TextEquals(candidate.normalized_path, normalized)) continue;
        if (match) {
            RecordOwnershipMutationLoss(ledger, LedgerLossReason::SourceRemovalContention);
            return false;
        }
        match = &candidate;
        matched_index = i;
    }
    if (!match) return false;
    std::memset(output, 0, sizeof(*output));
    output->record_index = matched_index;
    output->epoch = match->epoch;
    output->successful_add_ordinal = match->successful_add_ordinal;
    output->source = match->source;
    output->flags = match->flags;
    output->kind = match->kind;
    output->exact_handle_available = match->exact_handle_available;
    output->path_created_ownership = match->path_created_ownership;
    CopyText(output->original_path, sizeof(output->original_path), match->original_path);
    CopyText(output->normalized_path, sizeof(output->normalized_path), match->normalized_path);
    return true;
}

bool CloseCancelledEpochConsumed(CustomContentLedger* ledger,
                                 std::uint32_t epoch_id,
                                 std::uint64_t sequence) noexcept
{
    LedgerGuard guard(ledger, LedgerLossReason::ConsumptionContention);
    if (!guard.locked() || !epoch_id) return false;
    CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, epoch_id);
    if (!epoch || epoch->consumed ||
        !epoch->ownership_complete ||
        epoch->ownership_mutation_lost ||
        epoch->project_root_conflict)
        return false;
    for (std::uint32_t index = 0;
         index < ledger->source_count; ++index) {
        const CustomSourceRecord& source = ledger->sources[index];
        if (source.epoch == epoch_id && source.add_succeeded &&
            source.active && source.loader_owned_custom_content)
            return false;
    }
    epoch->active = false;
    epoch->accepting_records = false;
    epoch->frontend_reset_pending = false;
    epoch->sources_removed = true;
    epoch->consumed = true;
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::EpochEnd, epoch_id, 1,
        epoch->normalized_project_root,
        "cancelled-presentation-consumed", sequence);
    return true;
}

bool IsTrackedCustomSource(CustomContentLedger* ledger, void* source,
                           const char* resolved_source_path,
                           std::uint32_t* epoch_output,
                           bool* rejected_closed_epoch_output) noexcept
{
    if (epoch_output) *epoch_output = 0;
    if (rejected_closed_epoch_output)
        *rejected_closed_epoch_output = false;
    char normalized[kSourcePathCapacity];
    normalized[0] = 0;
    if (resolved_source_path)
        NormalizePhysicalPath(resolved_source_path, normalized,
                              sizeof(normalized));
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    const CustomSourceRecord* match = nullptr;
    std::uint32_t match_count = 0;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
        const CustomSourceRecord& record = ledger->sources[index];
        if (!record.active || !record.loader_owned_custom_content) continue;
        const bool same = source
            ? record.source == source
            : normalized[0] &&
                  TextEquals(record.normalized_path, normalized);
        if (!same) continue;
        match = &record;
        ++match_count;
    }
    if (match_count != 1) {
        /* A nonmatching exact source can still be an ordinary base/DLC
         * provider. Latch incompleteness only when its path aliases an active
         * custom record, or when the selected identity is ambiguous. */
        bool aliases_custom_path = false;
        if (source && normalized[0]) {
            for (std::uint32_t index = 0;
                 index < ledger->source_count; ++index) {
                const CustomSourceRecord& record = ledger->sources[index];
                if (record.active && record.loader_owned_custom_content &&
                    TextEquals(record.normalized_path, normalized)) {
                    aliases_custom_path = true;
                    break;
                }
            }
        }
        if (match_count > 1 || aliases_custom_path) {
            RecordDiagnosticObservationLoss(
                ledger, LedgerLossReason::OptionalObservationContention);
            PushDiagnosticUnlocked(
                ledger, LedgerDiagnosticType::FileUnclassified, 0, 0,
                normalized[0] ? normalized : "<unknown-source>",
                match_count > 1
                    ? "ambiguous-custom-source-identity"
                    : "exact-source-mismatch-no-path-fallback",
                static_cast<std::uint64_t>(ledger->sequence));
        }
        return false;
    }
    const CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, match->epoch);
    if (!epoch || !epoch->accepting_records) {
        if (rejected_closed_epoch_output)
            *rejected_closed_epoch_output = true;
        RecordDiagnosticObservationLoss(
            ledger, LedgerLossReason::OptionalObservationContention);
        PushDiagnosticUnlocked(
            ledger, LedgerDiagnosticType::FileUnclassified,
            match->epoch, 0, match->normalized_path,
            "open-rejected-after-epoch-close",
            static_cast<std::uint64_t>(ledger->sequence));
        return false;
    }
    if (epoch_output) *epoch_output = match->epoch;
    return true;
}

bool LookupExactCustomSourceIdentity(
    CustomContentLedger* ledger, void* source,
    const char* resolved_source_path,
    CustomSourceIdentitySnapshot* output) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!ledger || !source || !resolved_source_path ||
        !resolved_source_path[0] || !output)
        return false;
    char normalized[kSourcePathCapacity];
    if (!NormalizePhysicalPath(resolved_source_path, normalized,
                               sizeof(normalized)))
        return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    const CustomSourceRecord* active_match = nullptr;
    const CustomSourceRecord* historical_match = nullptr;
    std::uint32_t active_index = UINT32_MAX;
    std::uint32_t historical_index = UINT32_MAX;
    std::uint32_t active_count = 0;
    std::uint32_t historical_count = 0;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
        const CustomSourceRecord& record = ledger->sources[index];
        if (!record.add_succeeded ||
            !record.loader_owned_custom_content ||
            record.source != source ||
            !TextEquals(record.normalized_path, normalized))
            continue;
        historical_match = &record;
        historical_index = index;
        ++historical_count;
        if (record.active) {
            active_match = &record;
            active_index = index;
            ++active_count;
        }
    }
    const CustomSourceRecord* match = nullptr;
    std::uint32_t match_index = UINT32_MAX;
    if (active_count == 1) {
        match = active_match;
        match_index = active_index;
    } else if (active_count == 0 && historical_count == 1) {
        match = historical_match;
        match_index = historical_index;
    } else {
        return false;
    }
    const CustomContentEpoch* epoch =
        FindEpochUnlocked(ledger, match->epoch);
    if (!epoch) return false;
    output->epoch = match->epoch;
    output->source_record_index = match_index;
    output->successful_add_ordinal = match->successful_add_ordinal;
    output->kind = match->kind;
    output->source_active = match->active;
    output->epoch_active = epoch->active;
    output->epoch_accepting_records = epoch->accepting_records;
    output->epoch_consumed = epoch->consumed;
    CopyText(output->normalized_source_path,
             sizeof(output->normalized_source_path),
             match->normalized_path);
    CopyText(output->normalized_project_root,
             sizeof(output->normalized_project_root),
             epoch->normalized_project_root);
    return true;
}

bool EvaluatePlayerPresetRepairSourceProof(
    CustomContentLedger* ledger, std::uint32_t old_epoch,
    std::uint32_t current_epoch, void* old_source,
    const char* old_provider,
    PlayerPresetRepairSourceProof* output) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!ledger || !output || !old_epoch || !current_epoch ||
        old_epoch == current_epoch || !old_source || !old_provider ||
        !old_provider[0])
        return false;
    char normalized_old_provider[kSourcePathCapacity];
    if (!NormalizePhysicalPath(old_provider, normalized_old_provider,
                               sizeof(normalized_old_provider)))
        return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    const CustomContentEpoch* old_epoch_record =
        FindEpochUnlocked(ledger, old_epoch);
    const CustomContentEpoch* current_epoch_record =
        FindEpochUnlocked(ledger, current_epoch);
    if (!old_epoch_record || !current_epoch_record) return false;
    output->old_epoch = old_epoch;
    output->current_epoch = current_epoch;
    output->current_epoch_active = current_epoch_record->active &&
        current_epoch_record->accepting_records &&
        !current_epoch_record->consumed;
    output->ownership_complete =
        current_epoch_record->ownership_complete &&
        !current_epoch_record->ownership_mutation_lost &&
        ledger->ownership_mutation_loss == 0;
    bool all_old_data_inactive = true;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
        const CustomSourceRecord& source = ledger->sources[index];
        if (!source.add_succeeded ||
            !source.loader_owned_custom_content)
            continue;
        if (source.epoch == old_epoch &&
            (source.kind == CustomSourceKind::DataDirectory ||
             source.kind == CustomSourceKind::DataWorkshopPak)) {
            if (source.active) all_old_data_inactive = false;
            if (source.source == old_source &&
                TextEquals(source.normalized_path,
                           normalized_old_provider))
                output->old_data_source_identified = true;
        }
        if (source.epoch == current_epoch && source.active &&
            source.exact_handle_available &&
            source.path_created_ownership)
            ++output->current_active_exact_source_count;
    }
    output->old_data_sources_inactive =
        output->old_data_source_identified && all_old_data_inactive &&
        !old_epoch_record->active && old_epoch_record->consumed;
    output->current_epoch_sources_ready =
        output->current_epoch_active && output->ownership_complete &&
        current_epoch_record->successful_source_count != 0 &&
        output->current_active_exact_source_count != 0;
    return true;
}

bool RecordCustomFileOpen(CustomContentLedger* ledger,
                          std::uint32_t epoch, const char* logical_path,
                          const char* resolved_source_path, void* source,
                          std::uintptr_t caller_address,
                          std::uint32_t caller_rva,
                          const char* caller_module,
                          std::uint32_t thread_id,
                          std::uint64_t sequence,
                          std::uint32_t* record_index_output,
                          const CustomSourceIdentitySnapshot* attribution,
                          std::uint64_t transition_generation,
                          ContentOpenStagingPhase staging_phase,
                          std::uint16_t update_dlc_depth) noexcept
{
    if (record_index_output) *record_index_output = UINT32_MAX;
    char normalized_logical[kLogicalPathCapacity];
    if (!NormalizeLogicalPath(logical_path, normalized_logical,
                              sizeof(normalized_logical)))
        return false;
    char normalized_provider[kSourcePathCapacity];
    normalized_provider[0] = 0;
    if (resolved_source_path &&
        !NormalizePhysicalPath(resolved_source_path, normalized_provider,
                               sizeof(normalized_provider)))
        CopyText(normalized_provider, sizeof(normalized_provider),
                 resolved_source_path);
    const std::uintptr_t source_address =
        reinterpret_cast<std::uintptr_t>(source);
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) {
        RecordBroadFileObservationLoss(
            ledger, LedgerLossReason::BroadFileObservationContention,
            sequence);
        return false;
    }
    if (ledger->retirement_history.epochs && !FindEpochUnlocked(ledger, epoch)) return false;
    if (attribution) {
        if (attribution->epoch != epoch || attribution->source_record_index >= ledger->source_count) return false;
        const auto& owned_source = ledger->sources[attribution->source_record_index];
        if (!owned_source.add_succeeded || owned_source.epoch != epoch ||
            owned_source.successful_add_ordinal != attribution->successful_add_ordinal ||
            owned_source.source != source) return false;
    }
    for (std::uint32_t index = 0; index < ledger->file_count; ++index) {
        CustomFileLoadRecord* record = &ledger->files[index];
        if (!FileKeyEquals(*record, epoch, normalized_logical, caller_rva,
                           caller_module ? caller_module : "<unknown>"))
            continue;
        bool same_provider = false;
        if (source_address) {
            same_provider = record->last_source == source_address &&
                (!record->last_resolved_source_path[0] ||
                 !normalized_provider[0] ||
                 TextEquals(record->last_resolved_source_path,
                            normalized_provider));
        } else {
            same_provider = record->last_source == 0 &&
                TextEquals(record->last_resolved_source_path,
                           normalized_provider);
        }
        ++record->open_count;
        record->last_open_sequence = sequence;
        record->last_transition_generation = transition_generation;
        record->last_staging_phase = staging_phase;
        record->last_update_dlc_depth = update_dlc_depth;
        if (same_provider) {
            record->last_source = source_address;
            if (normalized_provider[0])
                CopyText(record->last_resolved_source_path,
                         sizeof(record->last_resolved_source_path),
                         normalized_provider);
        } else {
            ++record->provider_change_count;
            record->last_source = source_address;
            CopyText(record->last_resolved_source_path,
                     sizeof(record->last_resolved_source_path),
                     normalized_provider);
            record->lifetime = ContentLifetimeClass::Unknown;
            record->reset_adapter_id = 0;
            record->provider_disposition =
                BaselineProviderDisposition::Unresolved;
            record->fallback_provider_exists = false;
            record->overrides_baseline_provider = false;
            record->classification_complete = false;
            record->baseline_probe_complete = false;
            PushDiagnosticUnlocked(
                ledger, LedgerDiagnosticType::FileUnclassified,
                epoch, record->provider_change_count,
                record->logical_path,
                "provider-changed-within-epoch", sequence);
        }
        if (!record->resolved_source_path[0] && normalized_provider[0])
            CopyText(record->resolved_source_path,
                     sizeof(record->resolved_source_path),
                     normalized_provider);
        if (record_index_output) *record_index_output = index;
        return true;
    }
    std::uint32_t record_index = 0;
    while (record_index < ledger->file_count && ledger->files[record_index].open_count) ++record_index;
    if (record_index >= kMaximumFiles) {
        RecordDiagnosticObservationLoss(
            ledger, LedgerLossReason::OptionalObservationContention);
        RecordBroadFileObservationLoss(
            ledger, LedgerLossReason::BroadFileCapacity, sequence);
        return false;
    }
    if (record_index == ledger->file_count) ++ledger->file_count;
    else ledger->file_slot_reused[record_index] = true;
    CustomFileLoadRecord* record = &ledger->files[record_index];
    std::memset(record, 0, sizeof(*record));
    record->epoch = epoch;
    record->open_count = 1;
    record->source_record_index = attribution
        ? attribution->source_record_index : UINT32_MAX;
    record->source_add_ordinal = attribution
        ? attribution->successful_add_ordinal : 0;
    record->source_kind = attribution
        ? attribution->kind : CustomSourceKind::UnknownProjectSource;
    CopyText(record->logical_path, sizeof(record->logical_path),
             normalized_logical);
    CopyText(record->resolved_source_path,
             sizeof(record->resolved_source_path), normalized_provider);
    CopyText(record->last_resolved_source_path,
             sizeof(record->last_resolved_source_path),
             normalized_provider);
    if (attribution)
        CopyText(record->normalized_project_root,
                 sizeof(record->normalized_project_root),
                 attribution->normalized_project_root);
    record->source = source_address;
    record->last_source = source_address;
    record->caller_address = caller_address;
    record->caller_rva = caller_rva;
    record->thread_id = thread_id;
    CopyText(record->caller_module, sizeof(record->caller_module),
             caller_module ? caller_module : "<unknown>");
    record->lifetime = ContentLifetimeClass::Unknown;
    record->provider_disposition =
        BaselineProviderDisposition::Unresolved;
    record->first_open_sequence = sequence;
    record->last_open_sequence = sequence;
    record->first_transition_generation = transition_generation;
    record->last_transition_generation = transition_generation;
    record->first_staging_phase = staging_phase;
    record->last_staging_phase = staging_phase;
    record->first_update_dlc_depth = update_dlc_depth;
    record->last_update_dlc_depth = update_dlc_depth;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::FileTracked,
                           epoch, 1, record->logical_path,
                           record->resolved_source_path, sequence);
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::FileUnclassified,
                           epoch, 0, record->logical_path,
                           record->caller_module, sequence);
    if (record_index_output) *record_index_output = record_index;
    return true;
}

bool RecordExactMapOwnerEvidence(
    CustomContentLedger* ledger, const char* logical_path,
    const CustomSourceIdentitySnapshot* attribution,
    std::uint64_t transition_generation,
    std::uint64_t open_sequence) noexcept
{
    char map_name[kMapNameCapacity];
    if (!ledger || !attribution || !transition_generation ||
        !ExtractExactMapAnchor(logical_path, map_name,
                               sizeof(map_name)))
        return false;
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    if (!attribution->epoch ||
        attribution->epoch != ledger->current_epoch ||
        attribution->source_record_index >= ledger->source_count)
        return false;
    const CustomContentEpoch* const epoch =
        FindEpochUnlocked(ledger, attribution->epoch);
    const CustomSourceRecord* const source =
        &ledger->sources[attribution->source_record_index];
    if (!epoch || !epoch->active || !epoch->accepting_records ||
        epoch->consumed || epoch->sources_removed ||
        !epoch->ownership_complete || epoch->ownership_mutation_lost ||
        epoch->project_root_conflict ||
        !epoch->normalized_project_root[0] ||
        source->epoch != epoch->id || !source->add_succeeded ||
        !source->active || !source->exact_handle_available ||
        !source->loader_owned_custom_content ||
        !source->path_created_ownership ||
        source->successful_add_ordinal !=
            attribution->successful_add_ordinal ||
        !attribution->source_active || !attribution->epoch_active ||
        !attribution->epoch_accepting_records ||
        attribution->epoch_consumed ||
        !ProjectRootsEqual(attribution->normalized_project_root,
                           epoch->normalized_project_root) ||
        !PathWithinRoot(source->normalized_path,
                        epoch->normalized_project_root))
        return false;

    ExactMapOwnerEvidence* const evidence =
        &ledger->exact_map_owner;
    if (evidence->present &&
        evidence->transition_generation == transition_generation &&
        TextEquals(evidence->map_name, map_name)) {
        const bool same =
            evidence->epoch == attribution->epoch &&
            evidence->source_record_index ==
                attribution->source_record_index &&
            evidence->source_add_ordinal ==
                attribution->successful_add_ordinal &&
            ProjectRootsEqual(evidence->normalized_project_root,
                              epoch->normalized_project_root);
        if (!same) {
            evidence->ambiguous = true;
            evidence->open_sequence = open_sequence;
            return false;
        }
        /* Keep the first exact anchor open immutable for this transition.
         * Repeated opens must not revive a consumed proof. */
        return !evidence->ambiguous && !evidence->consumed;
    }

    std::memset(evidence, 0, sizeof(*evidence));
    evidence->epoch = attribution->epoch;
    evidence->source_record_index =
        attribution->source_record_index;
    evidence->source_add_ordinal =
        attribution->successful_add_ordinal;
    evidence->transition_generation = transition_generation;
    evidence->open_sequence = open_sequence;
    CopyText(evidence->map_name, sizeof(evidence->map_name),
             map_name);
    CopyText(evidence->normalized_project_root,
             sizeof(evidence->normalized_project_root),
             epoch->normalized_project_root);
    evidence->present = true;
    return true;
}

ExactMapOwnerProofStatus SnapshotExactMapOwnerProof(
    CustomContentLedger* ledger, const char* map_name,
    std::uint64_t transition_generation,
    ExactMapOwnerProofSnapshot* output) noexcept
{
    if (!output) return ExactMapOwnerProofStatus::Rejected;
    std::memset(output, 0, sizeof(*output));
    output->status = ExactMapOwnerProofStatus::Rejected;
    output->reason = ExactMapOwnerProofReason::InvalidPath;
    char normalized_map[kMapNameCapacity];
    if (!ledger ||
        !NormalizeMapSegment(map_name, normalized_map,
                             sizeof(normalized_map)))
        return output->status;

    LedgerReadGuard guard(ledger);
    if (!guard.locked()) {
        output->status = ExactMapOwnerProofStatus::Pending;
        output->reason = ExactMapOwnerProofReason::LedgerBusy;
        return output->status;
    }

    const ExactMapOwnerEvidence& evidence =
        ledger->exact_map_owner;
    if (!evidence.present) {
        output->status = ExactMapOwnerProofStatus::Pending;
        output->reason = ExactMapOwnerProofReason::NoEvidence;
        return output->status;
    }
    if (!TextEquals(evidence.map_name, normalized_map)) {
        output->status = ExactMapOwnerProofStatus::Pending;
        output->reason = ExactMapOwnerProofReason::MapMismatch;
        return output->status;
    }
    if (!transition_generation ||
        evidence.transition_generation != transition_generation) {
        output->status = ExactMapOwnerProofStatus::Pending;
        output->reason =
            ExactMapOwnerProofReason::TransitionMismatch;
        return output->status;
    }
    if (evidence.ambiguous) {
        output->reason =
            ExactMapOwnerProofReason::AmbiguousEvidence;
        return output->status;
    }
    if (!evidence.epoch ||
        evidence.epoch != ledger->current_epoch) {
        output->reason = ExactMapOwnerProofReason::NoActiveEpoch;
        return output->status;
    }
    const CustomContentEpoch* const epoch =
        FindEpochUnlocked(ledger, evidence.epoch);
    if (!epoch || !epoch->active || !epoch->accepting_records ||
        epoch->consumed || epoch->sources_removed ||
        !epoch->ownership_complete || epoch->ownership_mutation_lost ||
        epoch->project_root_conflict ||
        !epoch->project_root[0] ||
        !epoch->normalized_project_root[0] ||
        ledger->ownership_mutation_loss != 0) {
        output->reason =
            ExactMapOwnerProofReason::OwnershipIncomplete;
        return output->status;
    }
    if (evidence.source_record_index >= ledger->source_count) {
        output->reason = ExactMapOwnerProofReason::SourceInvalid;
        return output->status;
    }
    const CustomSourceRecord& source =
        ledger->sources[evidence.source_record_index];
    if (source.epoch != epoch->id || !source.add_succeeded ||
        !source.active || !source.exact_handle_available ||
        !source.loader_owned_custom_content ||
        !source.path_created_ownership ||
        source.successful_add_ordinal !=
            evidence.source_add_ordinal ||
        !PathWithinRoot(source.normalized_path,
                        epoch->normalized_project_root) ||
        !ProjectRootsEqual(evidence.normalized_project_root,
                           epoch->normalized_project_root)) {
        output->reason = ExactMapOwnerProofReason::SourceInvalid;
        return output->status;
    }
    if (evidence.consumed) {
        output->status = ExactMapOwnerProofStatus::Pending;
        output->reason = ExactMapOwnerProofReason::Consumed;
        return output->status;
    }

    output->status = ExactMapOwnerProofStatus::Ready;
    output->reason = ExactMapOwnerProofReason::None;
    output->epoch = evidence.epoch;
    output->source_record_index =
        evidence.source_record_index;
    output->source_add_ordinal =
        evidence.source_add_ordinal;
    output->transition_generation =
        evidence.transition_generation;
    CopyText(output->map_name, sizeof(output->map_name),
             evidence.map_name);
    CopyWide(output->project_root,
             sizeof(output->project_root) /
                 sizeof(output->project_root[0]),
             epoch->project_root);
    CopyText(output->normalized_project_root,
             sizeof(output->normalized_project_root),
             epoch->normalized_project_root);
    return output->status;
}

bool ConsumeExactMapOwnerProof(
    CustomContentLedger* ledger,
    const ExactMapOwnerProofSnapshot* proof) noexcept
{
    if (!ledger || !proof ||
        proof->status != ExactMapOwnerProofStatus::Ready)
        return false;
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    ExactMapOwnerEvidence* const evidence =
        &ledger->exact_map_owner;
    if (!evidence->present || evidence->consumed ||
        evidence->ambiguous ||
        evidence->epoch != proof->epoch ||
        evidence->source_record_index !=
            proof->source_record_index ||
        evidence->source_add_ordinal !=
            proof->source_add_ordinal ||
        evidence->transition_generation !=
            proof->transition_generation ||
        !TextEquals(evidence->map_name, proof->map_name) ||
        !ProjectRootsEqual(evidence->normalized_project_root,
                           proof->normalized_project_root))
        return false;
    const CustomContentEpoch* const epoch =
        FindEpochUnlocked(ledger, evidence->epoch);
    if (!epoch || evidence->epoch != ledger->current_epoch ||
        !epoch->active || !epoch->accepting_records ||
        epoch->consumed || epoch->sources_removed ||
        !epoch->ownership_complete ||
        epoch->ownership_mutation_lost ||
        epoch->project_root_conflict ||
        ledger->ownership_mutation_loss != 0 ||
        evidence->source_record_index >=
            ledger->source_count)
        return false;
    const CustomSourceRecord& source =
        ledger->sources[evidence->source_record_index];
    if (source.epoch != epoch->id ||
        !source.add_succeeded || !source.active ||
        !source.exact_handle_available ||
        !source.loader_owned_custom_content ||
        !source.path_created_ownership ||
        source.successful_add_ordinal !=
            evidence->source_add_ordinal ||
        !PathWithinRoot(source.normalized_path,
                        epoch->normalized_project_root) ||
        !ProjectRootsEqual(evidence->normalized_project_root,
                           epoch->normalized_project_root))
        return false;
    evidence->consumed = true;
    return true;
}

bool RecordFileParserObservation(
    CustomContentLedger* ledger, std::uint32_t record_index,
    WatchedParserOutcome outcome, const char* input_hash,
    std::uint64_t input_size, const char* registry_key,
    std::uintptr_t existing_object, std::uintptr_t result_object,
    std::uint64_t registration_generation,
    std::uint32_t parsed_object_epoch,
    std::uint64_t sequence,
    const CustomFileMutationToken* expected) noexcept
{
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked() || !FileTokenMatches(ledger, record_index, expected))
        return false;
    CustomFileLoadRecord* record = &ledger->files[record_index];
    record->parser_outcome = outcome;
    record->parser_input_size = input_size;
    record->parser_existing_object = existing_object;
    record->parser_result_object = result_object;
    record->parser_registration_generation = registration_generation;
    record->parsed_object_epoch = parsed_object_epoch;
    CopyText(record->parser_input_hash,
             sizeof(record->parser_input_hash), input_hash);
    CopyText(record->parser_registry_key,
             sizeof(record->parser_registry_key), registry_key);
    PushDiagnosticUnlocked(
        ledger, LedgerDiagnosticType::FileClassified,
        record->epoch, static_cast<std::uint32_t>(outcome),
        record->logical_path, WatchedParserOutcomeName(outcome), sequence,
        result_object, registration_generation, parsed_object_epoch);
    return true;
}

bool ClassifyFile(CustomContentLedger* ledger, std::uint32_t record_index,
                  ContentLifetimeClass lifetime,
                  std::uint32_t reset_adapter_id,
                  bool complete,
                  std::uint64_t sequence,
                  const CustomFileMutationToken* expected) noexcept
{
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked() || !FileTokenMatches(ledger, record_index, expected))
        return false;
    CustomFileLoadRecord* record = &ledger->files[record_index];
    record->lifetime = lifetime;
    record->reset_adapter_id = reset_adapter_id;
    record->classification_complete = complete;
    PushDiagnosticUnlocked(
        ledger,
        complete ? LedgerDiagnosticType::FileClassified
                 : LedgerDiagnosticType::FileUnclassified,
        record->epoch, reset_adapter_id, record->logical_path,
        LifetimeName(lifetime), sequence);
    return true;
}

bool RecordFileOpenContract(CustomContentLedger* ledger,
                            std::uint32_t record_index,
                            std::int32_t open_mode,
                            std::uint32_t open_flags,
                            const CustomFileMutationToken* expected) noexcept
{
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked() || !FileTokenMatches(ledger, record_index, expected))
        return false;
    ledger->files[record_index].open_mode = open_mode;
    ledger->files[record_index].open_flags = open_flags;
    return true;
}

bool RecordProviderProbe(CustomContentLedger* ledger,
                         std::uint32_t record_index,
                         BaselineProviderDisposition disposition,
                         bool fallback_exists,
                         bool overrides_baseline,
                         std::uint64_t,
                         const CustomFileMutationToken* expected) noexcept
{
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked() || !FileTokenMatches(ledger, record_index, expected))
        return false;
    CustomFileLoadRecord* record = &ledger->files[record_index];
    record->provider_disposition = disposition;
    record->fallback_provider_exists = fallback_exists;
    record->overrides_baseline_provider = overrides_baseline;
    record->baseline_probe_complete = true;
    return true;
}

bool SnapshotFileMutationToken(CustomContentLedger* ledger,
                               std::uint32_t record_index,
                               std::uint32_t expected_epoch,
                               CustomFileMutationToken* output) noexcept
{
    if (!output || !expected_epoch) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked() || record_index >= ledger->file_count) return false;
    const auto& file = ledger->files[record_index];
    if (!file.open_count || file.epoch != expected_epoch) return false;
    std::memset(output, 0, sizeof(*output));
    output->record_index = record_index;
    output->epoch = file.epoch;
    output->source_add_ordinal = file.source_add_ordinal;
    output->source = file.source;
    output->first_open_sequence = file.first_open_sequence;
    return true;
}

bool CaptureEpochRetirementCandidate(CustomContentLedger* ledger,
                                     std::uint32_t epoch_id,
                                     VerifiedEpochRetirement* output) noexcept
{
    if (!output || !epoch_id) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    const auto* epoch = FindEpochUnlocked(ledger, epoch_id);
    if (!epoch) return false;
    std::memset(output, 0, sizeof(*output));
    output->epoch = epoch_id;
    output->begin_sequence = epoch->begin_sequence;
    output->records_fingerprint = EpochRecordsFingerprint(ledger, epoch);
    output->losses = CaptureLossState(ledger);
    return true;
}

static EpochRetirementResult RetireVerifiedEpochImpl(CustomContentLedger* ledger,
                                        const VerifiedEpochRetirement* receipt,
                                        std::uint64_t sequence,
                                        LedgerRetirementSummary* output,bool mutate) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!ledger || !receipt || !output || !receipt->epoch ||
        !receipt->verified_transaction_token || !receipt->observer_quiescence_token ||
        !receipt->transaction_verified || !receipt->observer_admission_closed ||
        receipt->pending_observer_callbacks || receipt->external_active_references || receipt->quarantined)
        return EpochRetirementResult::InvalidReceipt;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return EpochRetirementResult::LedgerBusy;
    auto* epoch = FindEpochUnlocked(ledger, receipt->epoch);
    if (!epoch) return EpochRetirementResult::EpochMissing;
    if (epoch->active || epoch->accepting_records || !epoch->consumed || !epoch->sources_removed ||
        !epoch->audio_quiesced || !epoch->global_data_restored || !epoch->audio_restored)
        return EpochRetirementResult::EpochNotConsumed;
    const auto losses = CaptureLossState(ledger);
    if (!epoch->ownership_complete || epoch->ownership_mutation_lost || epoch->project_root_conflict ||
        losses.ownership_mutation_loss || losses.broad_file_observation_loss)
        return EpochRetirementResult::OwnershipIncomplete;
    if (epoch->begin_sequence != receipt->begin_sequence ||
        EpochRecordsFingerprint(ledger, epoch) != receipt->records_fingerprint ||
        std::memcmp(&losses, &receipt->losses, sizeof(losses)) != 0)
        return EpochRetirementResult::FenceChanged;
    LedgerRetirementSummary summary = {};
    summary.epoch = epoch->id;
    summary.begin_sequence = epoch->begin_sequence;
    summary.retired_sequence = sequence;
    summary.records_fingerprint = receipt->records_fingerprint;
    summary.verified_transaction_token = receipt->verified_transaction_token;
    summary.observer_quiescence_token = receipt->observer_quiescence_token;
    summary.losses = losses;
    for (std::uint32_t i = 0; i < ledger->source_count; ++i) {
        const auto& source = ledger->sources[i];
        if (source.epoch != epoch->id) continue;
        if (!source.loader_owned_custom_content) return EpochRetirementResult::GlobalRecordRetained;
        if (source.active || source.reset_failed || (!source.removed_naturally && !source.removed_by_reset))
            return EpochRetirementResult::ResourcesLive;
        for (std::uint32_t j = 0; source.source && j < ledger->source_count; ++j) {
            const auto& other = ledger->sources[j];
            if (other.epoch != epoch->id && other.add_succeeded && other.active && other.source == source.source)
                return EpochRetirementResult::ResourcesLive;
        }
        ++summary.sources;
    }
    for (std::uint32_t i = 0; i < ledger->pack_count; ++i) {
        const auto& pack = ledger->packs[i];
        if (pack.epoch != epoch->id) continue;
        if (!pack.released_naturally || pack.observed_after_frontend_reset)
            return EpochRetirementResult::ResourcesLive;
        for (std::uint32_t j = 0; pack.pack && j < ledger->pack_count; ++j) {
            const auto& other = ledger->packs[j];
            if (other.epoch && other.epoch != epoch->id && !other.released_naturally && other.pack == pack.pack)
                return EpochRetirementResult::ResourcesLive;
        }
        ++summary.packs;
    }
    for (std::uint32_t i = 0; i < ledger->file_count; ++i) {
        const auto& file = ledger->files[i];
        if (file.epoch != epoch->id) {
            if (file.open_count && file.parsed_object_epoch == epoch->id && file.parser_result_object)
                return EpochRetirementResult::ResourcesLive;
            continue;
        }
        if (!file.classification_complete || file.lifetime == ContentLifetimeClass::Unknown ||
            ((file.lifetime == ContentLifetimeClass::GlobalDataManager || file.lifetime == ContentLifetimeClass::AudioBank) &&
             !file.reset_adapter_id)) return EpochRetirementResult::UnclassifiedObservation;
        ++summary.files;
        summary.file_opens += file.open_count;
    }
    if (ledger->exact_map_owner.present && ledger->exact_map_owner.epoch == epoch->id &&
        !ledger->exact_map_owner.consumed) return EpochRetirementResult::ResourcesLive;

    if(!mutate){*output=summary;return EpochRetirementResult::Ready;}

    /* Archive before changing a slot. No active record is moved, and no loss
     * counter or baseline/global record is cleared to manufacture capacity. */
    auto& history = ledger->retirement_history;
    if (history.epochs >= kMaximumRetirementSummaries) ++history.summaries_overwritten;
    history.recent[history.next_recent] = summary;
    history.next_recent = (history.next_recent + 1U) % static_cast<std::uint32_t>(kMaximumRetirementSummaries);
    ++history.epochs;
    history.sources += summary.sources;
    history.files += summary.files;
    history.packs += summary.packs;
    history.file_opens += summary.file_opens;
    for (std::uint32_t i = 0; i < ledger->source_count; ++i)
        if (ledger->sources[i].epoch == epoch->id) std::memset(&ledger->sources[i], 0, sizeof(ledger->sources[i]));
    for (std::uint32_t i = 0; i < ledger->file_count; ++i)
        if (ledger->files[i].epoch == epoch->id) std::memset(&ledger->files[i], 0, sizeof(ledger->files[i]));
    for (std::uint32_t i = 0; i < ledger->pack_count; ++i)
        if (ledger->packs[i].epoch == epoch->id) std::memset(&ledger->packs[i], 0, sizeof(ledger->packs[i]));
    if (ledger->exact_map_owner.epoch == epoch->id) std::memset(&ledger->exact_map_owner, 0, sizeof(ledger->exact_map_owner));
    if (ledger->current_epoch == epoch->id) ledger->current_epoch = 0;
    std::memset(epoch, 0, sizeof(*epoch));
    *output = summary;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::EpochRetiredVerified, summary.epoch, 1U,
        "verified-epoch-retired", "slots-available-for-reuse", sequence, 0,
        summary.records_fingerprint, summary.verified_transaction_token);
    return EpochRetirementResult::Retired;
}

EpochRetirementResult CheckEpochRetirement(CustomContentLedger* ledger,
    const VerifiedEpochRetirement* receipt) noexcept
{
    LedgerRetirementSummary ignored={};
    return RetireVerifiedEpochImpl(ledger,receipt,0,&ignored,false);
}

EpochRetirementResult RetireVerifiedEpoch(CustomContentLedger* ledger,
    const VerifiedEpochRetirement* receipt,std::uint64_t sequence,
    LedgerRetirementSummary* output) noexcept
{
    return RetireVerifiedEpochImpl(ledger,receipt,sequence,output,true);
}

bool SnapshotRetirementHistory(CustomContentLedger* ledger,
                                LedgerRetirementHistory* output) noexcept
{
    if (!output) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    *output = ledger->retirement_history;
    return true;
}

bool RecordPackLoad(CustomContentLedger* ledger, void* pack,
                    const char* pack_path, std::uint64_t sequence) noexcept
{
    char normalized[kSourcePathCapacity];
    if (!NormalizePhysicalPath(pack_path, normalized, sizeof(normalized)))
        return false;
    if (!CurrentEpochId(ledger)) {
        RecordIrrelevantObservationWithoutEpoch(
            ledger, LedgerLossReason::IrrelevantWithoutEpoch);
        return false;
    }
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    const CustomContentEpoch* epoch = nullptr;
    for (std::uint32_t index = 0; index < ledger->epoch_count; ++index) {
        const CustomContentEpoch& candidate = ledger->epochs[index];
        if (candidate.id && (!epoch || candidate.id > epoch->id) &&
            PathWithinRoot(normalized, candidate.normalized_project_root)) {
            epoch = &candidate;
        }
    }
    if (!epoch)
        return false;
    for (std::uint32_t index = 0; index < ledger->pack_count; ++index) {
        CustomPackRecord* record = &ledger->packs[index];
        const bool same_pointer = !record->released_naturally && pack &&
            record->pack == reinterpret_cast<std::uintptr_t>(pack);
        const bool same_live_path = !record->released_naturally &&
            TextEquals(record->pack_path, normalized);
        if (same_pointer || same_live_path) {
            if (!record->pack)
                record->pack = reinterpret_cast<std::uintptr_t>(pack);
            if (!epoch->active || epoch->frontend_reset_pending)
                record->observed_after_frontend_reset = true;
            return true;
        }
    }
    std::uint32_t pack_slot = 0;
    while (pack_slot < ledger->pack_count && ledger->packs[pack_slot].epoch) ++pack_slot;
    if (pack_slot >= kMaximumPacks) {
        RecordDiagnosticObservationLoss(
            ledger, LedgerLossReason::ReleaseCapacity);
        return false;
    }
    if (pack_slot == ledger->pack_count) ++ledger->pack_count;
    CustomPackRecord* record = &ledger->packs[pack_slot];
    std::memset(record, 0, sizeof(*record));
    record->epoch = epoch->id;
    record->pack = reinterpret_cast<std::uintptr_t>(pack);
    record->first_sequence = sequence;
    record->observed_after_frontend_reset =
        !epoch->active || epoch->frontend_reset_pending;
    CopyText(record->pack_path, sizeof(record->pack_path), normalized);
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::PackTracked,
                           epoch->id, 1, record->pack_path,
                           "custom-project-provider", sequence);
    return true;
}

bool RecordPackRelease(CustomContentLedger* ledger, void* pack,
                       const char* pack_path,
                       std::uint64_t sequence,
                       const CustomPackMutationToken* expected) noexcept
{
    char normalized[kSourcePathCapacity];
    normalized[0] = 0;
    if (pack_path)
        NormalizePhysicalPath(pack_path, normalized, sizeof(normalized));
    if (!CurrentEpochId(ledger)) {
        RecordIrrelevantObservationWithoutEpoch(
            ledger, LedgerLossReason::IrrelevantWithoutEpoch);
        return false;
    }
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return false;
    if (!expected && ledger->retirement_history.epochs) return false;
    for (std::uint32_t index = ledger->pack_count; index > 0; --index) {
        CustomPackRecord* record = &ledger->packs[index - 1U];
        if (!record->epoch || record->released_naturally) continue;
        if (expected && (expected->record_index != index - 1U ||
            expected->epoch != record->epoch || expected->pack != record->pack ||
            expected->first_sequence != record->first_sequence)) continue;
        if (!((pack && record->pack == reinterpret_cast<std::uintptr_t>(pack)) ||
              (normalized[0] && TextEquals(record->pack_path, normalized))))
            continue;
        record->level_owned = true;
        record->released_naturally = true;
        record->release_sequence = sequence;
        CustomContentEpoch* epoch =
            FindEpochUnlocked(ledger, record->epoch);
        if (epoch) epoch->outgoing_level_release_observed = true;
        PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::PackReleased,
                               record->epoch, 1, record->pack_path,
                               "natural-CLevel-ReleasePacks", sequence);
        return true;
    }
    return false;
}

bool SnapshotPackMutationToken(CustomContentLedger* ledger,
                               void* pack, const char* pack_path,
                               std::uint32_t expected_epoch,
                               CustomPackMutationToken* output) noexcept
{
    if (!output || !expected_epoch) return false;
    char normalized[kSourcePathCapacity] = {};
    if (pack_path && !NormalizePhysicalPath(pack_path, normalized, sizeof(normalized))) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    const CustomPackRecord* match = nullptr;
    std::uint32_t matched_index = 0;
    for (std::uint32_t i = 0; i < ledger->pack_count; ++i) {
        const auto& record = ledger->packs[i];
        if (record.epoch != expected_epoch || record.released_naturally) continue;
        if (pack ? record.pack != reinterpret_cast<std::uintptr_t>(pack) :
            !normalized[0] || !TextEquals(record.pack_path, normalized)) continue;
        if (match) return false;
        match = &record;
        matched_index = i;
    }
    if (!match) return false;
    *output = {matched_index, match->epoch, match->pack, match->first_sequence};
    return true;
}

bool MarkOutgoingLevelRelease(CustomContentLedger* ledger,
                              std::uint32_t epoch,
                              std::uint64_t sequence) noexcept
{
    LedgerGuard guard(ledger, LedgerLossReason::ReleaseContention);
    if (!guard.locked()) return false;
    CustomContentEpoch* found = FindEpochUnlocked(ledger, epoch);
    if (!found) return false;
    found->outgoing_level_release_observed = true;
    PushDiagnosticUnlocked(ledger, LedgerDiagnosticType::PackReleased,
                           epoch, 1, "<level-owned-pack-collection>",
                           "natural-CLevel-ReleasePacks-complete",
                           sequence);
    return true;
}

bool FindUniqueActiveEpochByContentName(
    CustomContentLedger* ledger, const char* selected_content_name,
    CustomContentEpoch* output) noexcept
{
    if (!ledger || !selected_content_name || !selected_content_name[0] ||
        !output)
        return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    const CustomContentEpoch* match = nullptr;
    for (std::uint32_t index = 0; index < ledger->epoch_count; ++index) {
        const CustomContentEpoch& candidate = ledger->epochs[index];
        if (!candidate.active || !candidate.accepting_records ||
            !TextEquals(candidate.selected_content_name,
                        selected_content_name))
            continue;
        if (match) return false;
        match = &candidate;
    }
    if (!match) return false;
    *output = *match;
    return true;
}

FrontendEpochSelection SelectFrontendResetEpoch(
    CustomContentLedger* ledger, const char* selected_content_name,
    bool content_name_available) noexcept
{
    FrontendEpochSelection result = {};
    result.status = FrontendEpochSelectionStatus::NoActiveEpoch;
    result.content_name_available = content_name_available;
    if (!ledger) return result;

    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return result;

    const CustomContentEpoch* name_match = nullptr;
    const CustomContentEpoch* single_active = nullptr;
    std::uint32_t name_match_count = 0;
    for (std::uint32_t index = 0; index < ledger->epoch_count; ++index) {
        const CustomContentEpoch& candidate = ledger->epochs[index];
        if (!candidate.active || !candidate.accepting_records ||
            candidate.sources_removed || candidate.consumed ||
            !candidate.ownership_complete ||
            candidate.ownership_mutation_lost ||
            candidate.project_root_conflict ||
            !candidate.normalized_project_root[0])
            continue;
        ++result.active_epoch_count;
        single_active = &candidate;
        if (content_name_available && selected_content_name &&
            selected_content_name[0] &&
            TextEquals(candidate.selected_content_name,
                       selected_content_name)) {
            name_match = &candidate;
            ++name_match_count;
        }
    }

    const CustomContentEpoch* selected = nullptr;
    if (name_match_count == 1) {
        selected = name_match;
        result.status =
            FrontendEpochSelectionStatus::SelectedByContentName;
    } else if (result.active_epoch_count == 1) {
        selected = single_active;
        result.status =
            FrontendEpochSelectionStatus::SelectedSingleActiveFallback;
    } else if (result.active_epoch_count > 1) {
        result.status = FrontendEpochSelectionStatus::AmbiguousEpochs;
        return result;
    } else {
        return result;
    }

    for (std::uint32_t index = 0; index < ledger->source_count; ++index) {
        const CustomSourceRecord& source = ledger->sources[index];
        if (source.epoch != selected->id || !source.active ||
            !source.add_succeeded || !source.loader_owned_custom_content)
            continue;
        ++result.active_source_count;
        if (source.source && source.exact_handle_available &&
            source.path_created_ownership)
            ++result.active_exact_owned_source_count;
    }
    if (!result.active_source_count ||
        (result.status ==
             FrontendEpochSelectionStatus::SelectedSingleActiveFallback &&
         !result.active_exact_owned_source_count)) {
        result.status = FrontendEpochSelectionStatus::NoActiveSources;
        return result;
    }
    result.epoch = *selected;
    return result;
}

bool RecordLifecycleDiagnostic(CustomContentLedger* ledger,
                               LedgerDiagnosticType type,
                               std::uint32_t epoch,
                               std::uint32_t result,
                               std::uintptr_t object,
                               std::uint64_t value0,
                               std::uint64_t value1,
                               const char* text0,
                               const char* text1,
                               std::uint64_t sequence) noexcept
{
    /* A bounded lifecycle annotation is not ownership state. If another
     * producer owns the ledger lock, drop this diagnostic without poisoning
     * the exact-source mutation completeness check. */
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) {
        RecordDiagnosticObservationLoss(
            ledger, LedgerLossReason::OptionalObservationContention);
        return false;
    }
    PushDiagnosticUnlocked(ledger, type, epoch, result, text0, text1,
                           sequence, object, value0, value1);
    return true;
}

std::uint32_t DroppedMutationCount(
    const CustomContentLedger* ledger) noexcept
{
    if (!ledger) return UINT32_MAX;
    return static_cast<std::uint32_t>(dluma_atomic_compare_exchange(
        const_cast<volatile LONG*>(&ledger->ownership_mutation_loss),
        0, 0));
}

std::uint32_t CountActiveCustomSources(CustomContentLedger* ledger) noexcept
{
    return CountActiveCustomSourcesThroughEpoch(ledger, UINT32_MAX);
}

std::uint32_t CountActiveCustomSourcesThroughEpoch(
    CustomContentLedger* ledger, std::uint32_t maximum_epoch) noexcept
{
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return UINT32_MAX;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index)
        if (ledger->sources[index].epoch <= maximum_epoch &&
            ledger->sources[index].active &&
            ledger->sources[index].loader_owned_custom_content)
            ++count;
    return count;
}

std::uint32_t CountActiveCustomSourcesForEpoch(
    CustomContentLedger* ledger, std::uint32_t epoch_id) noexcept
{
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return UINT32_MAX;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0;
         index < ledger->source_count; ++index)
        if (ledger->sources[index].epoch == epoch_id &&
            ledger->sources[index].active &&
            ledger->sources[index].loader_owned_custom_content)
            ++count;
    return count;
}

std::uint32_t CountUnconsumedCustomEpochs(
    CustomContentLedger* ledger) noexcept
{
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return UINT32_MAX;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0;
         index < ledger->epoch_count; ++index) {
        const CustomContentEpoch& epoch = ledger->epochs[index];
        if (!epoch.id || epoch.consumed) continue;
        if (epoch.active || epoch.frontend_reset_pending ||
            !epoch.sources_removed)
            ++count;
    }
    return count;
}

std::uint32_t CountUnknownPersistentFiles(
    CustomContentLedger* ledger) noexcept
{
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return UINT32_MAX;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < ledger->file_count; ++index) {
        const CustomFileLoadRecord& record = ledger->files[index];
        if (!record.open_count) continue;
        if (!record.classification_complete ||
            record.lifetime == ContentLifetimeClass::Unknown ||
            (record.lifetime == ContentLifetimeClass::GlobalDataManager &&
             record.reset_adapter_id == 0) ||
            (record.lifetime == ContentLifetimeClass::AudioBank &&
             record.reset_adapter_id == 0))
            ++count;
    }
    return count;
}

bool CanReportBaselineMatch(CustomContentLedger* ledger) noexcept
{
    LedgerReadGuard guard(ledger);
    if (!guard.locked() || !ledger->baseline_captured ||
        ledger->ownership_mutation_loss != 0 ||
        ledger->broad_file_observation_loss != 0)
        return false;
    for (std::uint32_t index = 0; index < ledger->source_count; ++index)
        if (ledger->sources[index].active &&
            ledger->sources[index].loader_owned_custom_content)
            return false;
    for (std::uint32_t index = 0; index < ledger->file_count; ++index) {
        const CustomFileLoadRecord& record = ledger->files[index];
        if (!record.open_count) continue;
        if (!record.classification_complete ||
            record.lifetime == ContentLifetimeClass::Unknown ||
            (record.lifetime == ContentLifetimeClass::GlobalDataManager &&
             record.reset_adapter_id == 0) ||
            (record.lifetime == ContentLifetimeClass::AudioBank &&
             record.reset_adapter_id == 0))
            return false;
    }
    for (std::uint32_t index = 0; index < ledger->pack_count; ++index)
        if (ledger->packs[index].observed_after_frontend_reset)
            return false;
    return true;
}

bool CaptureSnapshot(CustomContentLedger* ledger,
                     LedgerSnapshot* output) noexcept
{
    if (!output) return false;
    LedgerReadGuard guard(ledger);
    if (!guard.locked()) return false;
    std::memset(output, 0, sizeof(*output));
    output->epoch_count = ledger->epoch_count;
    output->source_count = ledger->source_count;
    output->file_count = ledger->file_count;
    output->pack_count = ledger->pack_count;
    output->current_epoch = ledger->current_epoch;
    output->dropped_mutations =
        static_cast<std::uint32_t>(ledger->ownership_mutation_loss);
    output->losses = CaptureLossState(ledger);
    output->baseline_captured = ledger->baseline_captured;
    std::memcpy(output->epochs, ledger->epochs,
                sizeof(CustomContentEpoch) * ledger->epoch_count);
    std::memcpy(output->sources, ledger->sources,
                sizeof(CustomSourceRecord) * ledger->source_count);
    std::memcpy(output->files, ledger->files,
                sizeof(CustomFileLoadRecord) * ledger->file_count);
    std::memcpy(output->packs, ledger->packs,
                sizeof(CustomPackRecord) * ledger->pack_count);
    return true;
}

std::size_t DrainDiagnostics(CustomContentLedger* ledger,
                             LedgerDiagnosticEvent* output,
                             std::size_t capacity) noexcept
{
    if (!ledger || !output || !capacity) return 0;
    LedgerGuard guard(
        ledger, LedgerLossReason::OptionalObservationContention, false);
    if (!guard.locked()) return 0;
    std::size_t count = 0;
    if (ledger->diagnostic_overflow_latched && count < capacity) {
        LedgerDiagnosticEvent* event = &output[count++];
        std::memset(event, 0, sizeof(*event));
        event->sequence = static_cast<std::uint64_t>(ledger->sequence);
        event->type = LedgerDiagnosticType::Overflow;
        event->result = static_cast<std::uint32_t>(
            ledger->diagnostic_observation_loss);
        CopyText(event->text0, sizeof(event->text0),
                 "bounded-ledger-overflow");
        ledger->diagnostic_overflow_latched = false;
    }
    while (count < capacity &&
           ledger->diagnostic_tail != ledger->diagnostic_head) {
        output[count++] = ledger->diagnostics[ledger->diagnostic_tail];
        ledger->diagnostic_tail =
            (ledger->diagnostic_tail + 1U) %
            static_cast<std::uint32_t>(kMaximumDiagnostics);
    }
    return count;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::content
