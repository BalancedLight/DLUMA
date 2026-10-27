#define _HAS_EXCEPTIONS 0

#include "CustomMapAssetTrace.h"

extern "C" {
#include "../../dlw_common.h"

__declspec(dllimport) BOOL WINAPI QueryPerformanceCounter(
    LARGE_INTEGER* performance_count);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
}

#include <cstring>
#include <intrin.h>
#include <type_traits>

namespace dluma::fixes::custom_map_asset_isolation::trace {
namespace {

static_assert(std::is_trivial<AssetTraceEvent>::value,
              "trace records must remain POD");
static_assert(std::is_standard_layout<AssetTraceEvent>::value,
              "trace records must retain a stable layout");
static_assert(std::is_trivial<AssetTraceEventPayload>::value,
              "hook payloads must remain POD");

struct AssetTraceQueue {
    AssetTraceEvent records[kQueueCapacity];
    std::size_t head;
    std::size_t count;
    bool overflow_latched;
    bool overflow_sequence_unknown;
    std::size_t overflow_index;
};

struct TextBuilder {
    char* output;
    std::size_t capacity;
    std::size_t length;
    bool valid;
};

AssetTraceQueue g_queue;
volatile LONG g_queue_lock;
volatile LONG g_contention_drops;
volatile LONG g_contention_drop_domains;
volatile LONG g_contention_critical_drops;
volatile LONG g_contention_optional_drops;
volatile LONG g_domain_drop_counts[kTraceLossDomainCount];
volatile LONG g_domain_drop_sequence_unknown[kTraceLossDomainCount];
std::uint64_t g_domain_first_known_sequence[kTraceLossDomainCount];
std::uint64_t g_domain_last_known_sequence[kTraceLossDomainCount];
volatile LONGLONG g_global_generation;
std::uint64_t g_next_sequence;
std::uint64_t g_total_dropped;
volatile LONGLONG g_critical_trace_loss;
volatile LONGLONG g_optional_trace_loss;
volatile LONGLONG g_transaction_premount_decisions;
volatile LONGLONG g_transaction_premount_allowed;
volatile LONGLONG g_transaction_allowed_tokens;
volatile LONGLONG g_transaction_project_root_bindings;
volatile LONGLONG g_transaction_data_directory_bindings;
volatile LONGLONG g_transaction_assets_pc_bindings;
volatile LONGLONG g_transaction_source_removals;
volatile LONGLONG g_transaction_rollback_barriers;
volatile LONGLONG g_transaction_frontend_resets;
volatile LONGLONG g_transaction_frontend_verifications;
volatile LONGLONG g_transaction_last_token_generation;
volatile LONG g_transaction_last_bound_epoch;
volatile LONG g_transaction_last_admission_result;
volatile LONG g_transaction_observed_flags;
volatile LONG g_transaction_first_failure_state;
char g_transaction_first_failure_reason[
    kTransactionFailureReasonCapacity];
AssetTracePlatform g_platform;

thread_local std::uint64_t g_thread_generation;

std::uint64_t DefaultReadQpc() noexcept
{
    LARGE_INTEGER value = {};
    if (!QueryPerformanceCounter(&value) || value.QuadPart < 0) return 0;
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint32_t DefaultReadThreadId() noexcept
{
    return static_cast<std::uint32_t>(GetCurrentThreadId());
}

bool DefaultIsReadable(const void* address, std::size_t size) noexcept
{
    return dluma_is_readable(address, static_cast<SIZE_T>(size)) != 0;
}

std::uint64_t AtomicIncrementGeneration() noexcept
{
#if defined(__clang__)
    return static_cast<std::uint64_t>(
        __atomic_add_fetch(&g_global_generation, 1, __ATOMIC_SEQ_CST));
#elif defined(_MSC_VER)
    LONGLONG observed = _InterlockedCompareExchange64(
        &g_global_generation, 0, 0);
    for (;;) {
        const LONGLONG desired = observed + 1;
        const LONGLONG previous = _InterlockedCompareExchange64(
            &g_global_generation, desired, observed);
        if (previous == observed)
            return static_cast<std::uint64_t>(desired);
        observed = previous;
    }
#else
#error Unsupported compiler for asset trace atomics.
#endif
}

void AtomicIncrementLoss(volatile LONGLONG* value) noexcept
{
    if (!value) return;
    LONGLONG observed = _InterlockedCompareExchange64(value, 0, 0);
    for (;;) {
        const LONGLONG desired = observed + 1;
        const LONGLONG previous =
            _InterlockedCompareExchange64(value, desired, observed);
        if (previous == observed) return;
        observed = previous;
    }
}

std::uint64_t AtomicLoadU64(
    volatile LONGLONG* value) noexcept
{
    if (!value) return 0;
    return static_cast<std::uint64_t>(
        _InterlockedCompareExchange64(value, 0, 0));
}

void AtomicStoreU64(
    volatile LONGLONG* value,
    std::uint64_t desired) noexcept
{
    if (value)
        _InterlockedExchange64(
            value, static_cast<LONGLONG>(desired));
}

std::uint64_t AtomicLoadGeneration() noexcept
{
#if defined(__clang__)
    return static_cast<std::uint64_t>(
        __atomic_load_n(&g_global_generation, __ATOMIC_SEQ_CST));
#elif defined(_MSC_VER)
    return static_cast<std::uint64_t>(
        _InterlockedCompareExchange64(&g_global_generation, 0, 0));
#else
#error Unsupported compiler for asset trace atomics.
#endif
}

bool TryQueueLock() noexcept
{
    return dluma_atomic_compare_exchange(&g_queue_lock, 1, 0) == 0;
}

void UnlockQueue() noexcept
{
    dluma_atomic_exchange(&g_queue_lock, 0);
}

std::size_t BoundedLength(const char* text, std::size_t capacity) noexcept
{
    std::size_t length = 0;
    if (!text) return 0;
    while (length < capacity && text[length]) ++length;
    return length;
}

bool BoundedStringsEqual(const char* left, const char* right,
                         std::size_t capacity) noexcept
{
    if (!left || !right) return left == right;
    for (std::size_t i = 0; i < capacity; ++i) {
        if (left[i] != right[i]) return false;
        if (!left[i]) return true;
    }
    return false;
}

void CopyBounded(char* output, std::size_t output_capacity,
                 const char* input, std::size_t input_capacity) noexcept
{
    std::size_t length;
    if (!output || !output_capacity) return;
    output[0] = 0;
    if (!input || !input_capacity) return;
    length = BoundedLength(input, input_capacity);
    if (length >= output_capacity) length = output_capacity - 1;
    if (length) std::memcpy(output, input, length);
    output[length] = 0;
}

void CopyLiteral(char* output, std::size_t output_capacity,
                 const char* literal) noexcept
{
    CopyBounded(output, output_capacity, literal,
                literal ? std::strlen(literal) + 1 : 0);
}

void AtomicOrLong(volatile LONG* destination, LONG mask) noexcept
{
    if (!destination || !mask) return;
    LONG observed = dluma_atomic_compare_exchange(destination, 0, 0);
    for (;;) {
        const LONG desired = observed | mask;
        const LONG actual = dluma_atomic_compare_exchange(
            destination, desired, observed);
        if (actual == observed) return;
        observed = actual;
    }
}

enum TransactionEvidenceFlag : LONG {
    TransactionEvidencePreMountDecision = 1L << 0,
    TransactionEvidencePreMountAllowed = 1L << 1,
    TransactionEvidenceAllowedToken = 1L << 2,
    TransactionEvidenceProjectRoot = 1L << 3,
    TransactionEvidenceDataDirectory = 1L << 4,
    TransactionEvidenceAssetsPc = 1L << 5,
    TransactionEvidenceSourceRemoval = 1L << 6,
    TransactionEvidenceRollbackBarrier = 1L << 7,
    TransactionEvidenceFrontendReset = 1L << 8,
    TransactionEvidenceFrontendVerification = 1L << 9,
    TransactionEvidenceFirstFailure = 1L << 10,
};

void RecordTransactionFirstFailure(
    const char* failure_reason) noexcept
{
    if (!failure_reason || !failure_reason[0] ||
        dluma_atomic_compare_exchange(
            &g_transaction_first_failure_state, 1, 0) != 0)
        return;
    CopyBounded(
        g_transaction_first_failure_reason,
        sizeof(g_transaction_first_failure_reason),
        failure_reason,
        BoundedLength(
            failure_reason,
            kTransactionFailureReasonCapacity) + 1U);
    AtomicOrLong(
        &g_transaction_observed_flags,
        TransactionEvidenceFirstFailure);
    dluma_atomic_exchange(
        &g_transaction_first_failure_state, 2);
}

void RecordMandatoryEvent(
    const AssetTraceEventPayload& payload) noexcept
{
    switch (payload.type) {
    case AssetTraceEventType::FsRemoveSourceExit:
        if (payload.value0) {
            AtomicIncrementLoss(
                &g_transaction_source_removals);
            AtomicOrLong(
                &g_transaction_observed_flags,
                TransactionEvidenceSourceRemoval);
        }
        break;
    case AssetTraceEventType::SessionOnDestroyEnter:
        if (payload.value1) {
            AtomicIncrementLoss(
                &g_transaction_rollback_barriers);
            AtomicOrLong(
                &g_transaction_observed_flags,
                TransactionEvidenceRollbackBarrier);
        }
        break;
    case AssetTraceEventType::FrontendSourceResetEnd:
        if (payload.value1) {
            AtomicIncrementLoss(
                &g_transaction_frontend_resets);
            AtomicOrLong(
                &g_transaction_observed_flags,
                TransactionEvidenceFrontendReset);
        }
        break;
    case AssetTraceEventType::FirstFrontendPackOrModuleLookup:
        if ((payload.value1 & 1U) != 0) {
            AtomicIncrementLoss(
                &g_transaction_frontend_verifications);
            AtomicOrLong(
                &g_transaction_observed_flags,
                TransactionEvidenceFrontendVerification);
        }
        break;
    default:
        break;
    }
}

void RecordContentionDropStats(std::uint32_t domain_mask) noexcept
{
    for (std::size_t index = 0; index < kTraceLossDomainCount; ++index) {
        const std::uint32_t bit = 1U << index;
        if ((domain_mask & bit) == 0) continue;
        dluma_atomic_increment(&g_domain_drop_counts[index]);
        dluma_atomic_exchange(&g_domain_drop_sequence_unknown[index], 1);
    }
}

void RecordKnownDropStatsLocked(std::uint32_t domain_mask,
                                std::uint64_t sequence) noexcept
{
    for (std::size_t index = 0; index < kTraceLossDomainCount; ++index) {
        const std::uint32_t bit = 1U << index;
        if ((domain_mask & bit) == 0) continue;
        dluma_atomic_increment(&g_domain_drop_counts[index]);
        if (!g_domain_first_known_sequence[index])
            g_domain_first_known_sequence[index] = sequence;
        g_domain_last_known_sequence[index] = sequence;
    }
}

AssetTraceEvent MakeOverflowEvent(std::uint64_t dropped,
                                  std::uint32_t domain_mask,
                                  std::uint64_t first_drop_sequence,
                                  std::uint64_t last_drop_sequence,
                                  bool sequence_known) noexcept
{
    AssetTraceEvent event = {};
    event.sequence = ++g_next_sequence;
    event.qpc = g_platform.read_qpc();
    event.transition_generation = CurrentTransitionGeneration();
    event.thread_id = g_platform.read_thread_id();
    event.type = AssetTraceEventType::QueueOverflow;
    event.value0 = dropped;
    event.dropped_domain_mask = domain_mask;
    if (sequence_known) {
        event.first = static_cast<std::uintptr_t>(first_drop_sequence);
        event.second = static_cast<std::uintptr_t>(last_drop_sequence);
    }
    CopyLiteral(event.text0, sizeof(event.text0),
                "bounded_queue_dropped_events");
    return event;
}

void AppendOverflowLocked(std::uint64_t dropped,
                          std::uint32_t domain_mask,
                          std::uint64_t first_drop_sequence,
                          std::uint64_t last_drop_sequence,
                          bool sequence_known) noexcept
{
    if (!dropped) return;
    g_total_dropped += dropped;
    if (g_queue.overflow_latched) {
        AssetTraceEvent& event =
            g_queue.records[g_queue.overflow_index];
        event.value0 += dropped;
        event.dropped_domain_mask |= domain_mask;
        if (!sequence_known || g_queue.overflow_sequence_unknown) {
            event.first = 0;
            event.second = 0;
            g_queue.overflow_sequence_unknown = true;
        } else {
            if (!event.first || first_drop_sequence < event.first)
                event.first = static_cast<std::uintptr_t>(
                    first_drop_sequence);
            if (last_drop_sequence > event.second)
                event.second = static_cast<std::uintptr_t>(
                    last_drop_sequence);
        }
        return;
    }

    /* Normal publication reserves one slot for this record, including after
     * a partial drain, so the overflow report itself cannot be lost. */
    if (g_queue.count < kQueueCapacity) {
        const std::size_t index =
            (g_queue.head + g_queue.count) % kQueueCapacity;
        g_queue.records[index] = MakeOverflowEvent(
            dropped, domain_mask, first_drop_sequence,
            last_drop_sequence, sequence_known);
        g_queue.overflow_latched = true;
        g_queue.overflow_sequence_unknown = !sequence_known;
        g_queue.overflow_index = index;
        ++g_queue.count;
    }
}

void FoldContentionDropsLocked() noexcept
{
    const LONG dropped = dluma_atomic_exchange(&g_contention_drops, 0);
    const LONG domains = dluma_atomic_exchange(
        &g_contention_drop_domains, 0);
    if (dropped > 0)
        AppendOverflowLocked(
            static_cast<std::uint64_t>(dropped),
            static_cast<std::uint32_t>(domains), 0, 0, false);
}

bool AppendPayloadLocked(const AssetTraceEventPayload& payload,
                         std::uint64_t* published_sequence = nullptr) noexcept
{
    AssetTraceEvent event = {};
    const bool critical = IsCriticalEvent(payload.type);
    const std::size_t overflow_reserve =
        g_queue.overflow_latched ? 0U : 1U;
    const std::size_t normal_limit = critical
        ? kQueueCapacity - overflow_reserve
        : kQueueCapacity - kCriticalTraceReservedCapacity -
              overflow_reserve;

    if (g_queue.count >= normal_limit) {
        const std::uint64_t dropped_sequence = ++g_next_sequence;
        if (critical)
            AtomicIncrementLoss(&g_critical_trace_loss);
        else
            AtomicIncrementLoss(&g_optional_trace_loss);
        RecordKnownDropStatsLocked(
            EventDropDomainMask(payload.type), dropped_sequence);
        AppendOverflowLocked(
            1, EventDropDomainMask(payload.type), dropped_sequence,
            dropped_sequence, true);
        if (published_sequence) *published_sequence = 0;
        return false;
    }

    event.sequence = ++g_next_sequence;
    event.qpc = g_platform.read_qpc();
    event.transition_generation = CurrentTransitionGeneration();
    event.content_epoch = payload.content_epoch;
    event.source_record_index = payload.source_record_index;
    event.source_add_ordinal = payload.source_add_ordinal;
    event.caller_rva = payload.caller_rva;
    event.source_kind = payload.source_kind;
    event.attribution_flags = payload.attribution_flags;
    event.update_dlc_depth = payload.update_dlc_depth;
    event.staging_phase = payload.staging_phase;
    event.label_source = payload.label_source;
    event.script_outcome = payload.script_outcome;
    event.campaign_phase = payload.campaign_phase;
    event.campaign_slot_selected = payload.campaign_slot_selected;
    event.dropped_domain_mask = payload.dropped_domain_mask;
    event.observation_generation = payload.observation_generation;
    event.thread_id = g_platform.read_thread_id();
    event.type = payload.type;
    event.object = payload.object;
    event.first = payload.first;
    event.second = payload.second;
    event.third = payload.third;
    event.value0 = payload.value0;
    event.value1 = payload.value1;
    event.detail0 = payload.detail0;
    event.detail1 = payload.detail1;
    event.detail2 = payload.detail2;
    event.detail3 = payload.detail3;
    CopyBounded(event.caller_module, sizeof(event.caller_module),
                payload.caller_module, sizeof(payload.caller_module));
    CopyBounded(event.text0, sizeof(event.text0), payload.text0,
                sizeof(payload.text0));
    CopyBounded(event.text1, sizeof(event.text1), payload.text1,
                sizeof(payload.text1));
    CopyBounded(event.text2, sizeof(event.text2), payload.text2,
                sizeof(payload.text2));
    CopyBounded(event.text3, sizeof(event.text3), payload.text3,
                sizeof(payload.text3));
    CopyBounded(event.text4, sizeof(event.text4), payload.text4,
                sizeof(payload.text4));

    const std::size_t index =
        (g_queue.head + g_queue.count) % kQueueCapacity;
    g_queue.records[index] = event;
    ++g_queue.count;
    if (published_sequence) *published_sequence = event.sequence;
    return true;
}

void BuilderInitialize(TextBuilder* builder, char* output,
                       std::size_t capacity) noexcept
{
    if (!builder) return;
    builder->output = output;
    builder->capacity = capacity;
    builder->length = 0;
    builder->valid = output && capacity;
    if (builder->valid) output[0] = 0;
}

void BuilderAppendChar(TextBuilder* builder, char value) noexcept
{
    if (!builder || !builder->valid) return;
    if (builder->length + 1 >= builder->capacity) {
        builder->valid = false;
        return;
    }
    builder->output[builder->length++] = value;
    builder->output[builder->length] = 0;
}

void BuilderAppendFixed(TextBuilder* builder, const char* text,
                        std::size_t capacity) noexcept
{
    const std::size_t length = BoundedLength(text, capacity);
    for (std::size_t i = 0; i < length; ++i)
        BuilderAppendChar(builder, text[i]);
}

void BuilderAppendLiteral(TextBuilder* builder, const char* text) noexcept
{
    if (!text) return;
    BuilderAppendFixed(builder, text, std::strlen(text) + 1);
}

void BuilderAppendUnsigned(TextBuilder* builder, std::uint64_t value) noexcept
{
    char digits[32];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count) BuilderAppendChar(builder, digits[--count]);
}

void BuilderAppendHex(TextBuilder* builder, std::uintptr_t value) noexcept
{
    static constexpr char kHex[] = "0123456789abcdef";
    char digits[2 * sizeof(std::uintptr_t)];
    std::size_t count = 0;
    BuilderAppendLiteral(builder, "0x");
    do {
        digits[count++] = kHex[value & 0x0FU];
        value >>= 4;
    } while (value && count < sizeof(digits));
    while (count) BuilderAppendChar(builder, digits[--count]);
}

void BuilderAppendQuotedFixed(TextBuilder* builder, const char* text,
                              std::size_t capacity) noexcept
{
    static constexpr char kHex[] = "0123456789abcdef";
    const std::size_t length = BoundedLength(text, capacity);
    BuilderAppendChar(builder, '"');
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char value = static_cast<unsigned char>(text[i]);
        switch (value) {
        case '"': BuilderAppendLiteral(builder, "\\\""); break;
        case '\\': BuilderAppendLiteral(builder, "\\\\"); break;
        case '\t': BuilderAppendLiteral(builder, "\\t"); break;
        case '\r': BuilderAppendLiteral(builder, "\\r"); break;
        case '\n': BuilderAppendLiteral(builder, "\\n"); break;
        default:
            if (value < 0x20U || value == 0x7FU) {
                BuilderAppendLiteral(builder, "\\x");
                BuilderAppendChar(builder, kHex[(value >> 4) & 0x0FU]);
                BuilderAppendChar(builder, kHex[value & 0x0FU]);
            } else {
                BuilderAppendChar(builder, static_cast<char>(value));
            }
            break;
        }
    }
    BuilderAppendChar(builder, '"');
}

void BuilderAppendKeyUnsigned(TextBuilder* builder, const char* key,
                              std::uint64_t value) noexcept
{
    BuilderAppendChar(builder, ' ');
    BuilderAppendLiteral(builder, key);
    BuilderAppendChar(builder, '=');
    BuilderAppendUnsigned(builder, value);
}

void BuilderAppendKeyQuoted(TextBuilder* builder, const char* key,
                            const char* value,
                            std::size_t value_capacity) noexcept
{
    BuilderAppendChar(builder, ' ');
    BuilderAppendLiteral(builder, key);
    BuilderAppendChar(builder, '=');
    BuilderAppendQuotedFixed(builder, value, value_capacity);
}

void EventTextKeys(AssetTraceEventType type, const char** first,
                   const char** second) noexcept
{
    *first = "text0";
    *second = "text1";
    switch (type) {
    case AssetTraceEventType::FsAddSourceEnter:
        *first = "path";
        *second = "normalized";
        break;
    case AssetTraceEventType::FsAddSourceExit:
        *first = "path";
        *second = "classification";
        break;
    case AssetTraceEventType::FsRemoveSourceEnter:
    case AssetTraceEventType::FsRemoveSourceExit:
        *first = "path";
        *second = "classification";
        break;
    case AssetTraceEventType::FsOpenWatchedEnter:
    case AssetTraceEventType::FsOpenWatchedResult:
        *first = "path";
        *second = "source";
        break;
    case AssetTraceEventType::GameLoadPackEnter:
    case AssetTraceEventType::GameLoadPackExit:
    case AssetTraceEventType::GameFreePackEnter:
    case AssetTraceEventType::GameFreePackExit:
    case AssetTraceEventType::AssetRegRpackEnter:
    case AssetTraceEventType::AssetRegRpackExit:
    case AssetTraceEventType::AssetUnregRpackEnter:
    case AssetTraceEventType::AssetUnregRpackExit:
    case AssetTraceEventType::AssetReleasePackEnter:
    case AssetTraceEventType::AssetReleasePackExit:
    case AssetTraceEventType::LevelReleasePacksPack:
    case AssetTraceEventType::OnDemandEvictPackEnter:
    case AssetTraceEventType::OnDemandEvictPackExit:
    case AssetTraceEventType::LevelPackInternalReleaseEnter:
    case AssetTraceEventType::LevelPackInternalReleaseExit:
        *first = "pack";
        *second = "detail";
        break;
    case AssetTraceEventType::LevelReleasePacksEnter:
    case AssetTraceEventType::LevelReleasePacksExit:
    case AssetTraceEventType::LevelReleasePacksCrossCheck:
        *first = "map";
        *second = "pack_collection_state";
        break;
    case AssetTraceEventType::ScriptLoaderWatchedEnter:
    case AssetTraceEventType::ScriptLoaderWatchedCacheHit:
    case AssetTraceEventType::ScriptLoaderWatchedParse:
    case AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict:
    case AssetTraceEventType::ScriptLoaderWatchedExit:
        *first = "path";
        *second = "detail";
        break;
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignManagerSlotSnapshot:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
    case AssetTraceEventType::CampaignCarryoverObserved:
    case AssetTraceEventType::CampaignObservationUnknown:
        *first = "campaign";
        *second = "manifest";
        break;
    case AssetTraceEventType::CampaignSnapshotMap:
    case AssetTraceEventType::CampaignAddMapEnter:
    case AssetTraceEventType::CampaignAddMapExit:
        *first = "map";
        *second = "level";
        break;
    case AssetTraceEventType::CampaignRewardsMutationObserved:
        *first = "rewards";
        *second = "previous_rewards";
        break;
    case AssetTraceEventType::CampaignAfterRespawnMutationObserved:
        *first = "after_respawn0";
        *second = "after_respawn1";
        break;
    case AssetTraceEventType::ActiveLevelObserved:
        *first = "map";
        *second = "project";
        break;
    case AssetTraceEventType::ChangeActiveLevelEnter:
    case AssetTraceEventType::ChangeAfterFadeEnter:
        *first = "destination";
        *second = "current_level";
        break;
    case AssetTraceEventType::ChangeActiveLevelBranch:
        *first = "selected_path";
        *second = "detail";
        break;
    case AssetTraceEventType::UnloadLevelEnter:
    case AssetTraceEventType::UnloadLevelExit:
    case AssetTraceEventType::DestroyActiveLevelEnter:
    case AssetTraceEventType::DestroyActiveLevelExit:
    case AssetTraceEventType::LoadLevelEnter:
    case AssetTraceEventType::LoadLevelExit:
        *first = "level";
        *second = "detail";
        break;
    default:
        break;
    }
}

void EventExtraTextKeys(AssetTraceEventType type, const char** third,
                        const char** fourth) noexcept
{
    *third = "text2";
    *fourth = "text3";
    switch (type) {
    case AssetTraceEventType::FsOpenWatchedEnter:
    case AssetTraceEventType::FsOpenWatchedResult:
        *third = "project";
        *fourth = "detail";
        break;
    case AssetTraceEventType::ScriptLoaderWatchedEnter:
    case AssetTraceEventType::ScriptLoaderWatchedCacheHit:
    case AssetTraceEventType::ScriptLoaderWatchedParse:
    case AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict:
    case AssetTraceEventType::ScriptLoaderWatchedExit:
        *third = "content_hash";
        *fourth = "registry_key";
        break;
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignManagerSlotSnapshot:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
    case AssetTraceEventType::CampaignCarryoverObserved:
    case AssetTraceEventType::CampaignObservationUnknown:
        *third = "project";
        *fourth = "manifest_hash";
        break;
    default:
        break;
    }
}

void EventValueKeys(AssetTraceEventType type, const char** first,
                    const char** second) noexcept
{
    *first = "result";
    *second = "flags";
    switch (type) {
    case AssetTraceEventType::ChangeAfterFadeEnter:
        *first = "generation_owner";
        *second = "evidence";
        break;
    case AssetTraceEventType::ChangeActiveLevelEnter:
        *first = "generation_origin";
        *second = "evidence";
        break;
    case AssetTraceEventType::ChangeActiveLevelBranch:
        *first = "vslot_958_result";
        *second = "evidence";
        break;
    case AssetTraceEventType::FsAddSourceEnter:
        *first = "source_flags";
        *second = "evidence";
        break;
    case AssetTraceEventType::FsOpenWatchedEnter:
        *first = "mode";
        *second = "open_flags";
        break;
    case AssetTraceEventType::ScriptLoaderWatchedEnter:
    case AssetTraceEventType::ScriptLoaderWatchedCacheHit:
    case AssetTraceEventType::ScriptLoaderWatchedParse:
    case AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict:
    case AssetTraceEventType::ScriptLoaderWatchedExit:
        *first = "content_size";
        *second = "open_sequence";
        break;
    case AssetTraceEventType::GameLoadPackEnter:
        *first = "first_flag";
        *second = "second_flag";
        break;
    case AssetTraceEventType::LevelReleasePacksPack:
        *first = "pack_index";
        *second = "evidence";
        break;
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignManagerSlotSnapshot:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
        *first = "logical_fingerprint";
        *second = "selected_index_table_count";
        break;
    case AssetTraceEventType::CampaignSnapshotMap:
    case AssetTraceEventType::CampaignAddMapEnter:
    case AssetTraceEventType::CampaignAddMapExit:
        *first = "registration_order";
        *second = "map_id";
        break;
    case AssetTraceEventType::SessionOnDestroyEnter:
    case AssetTraceEventType::OnDestroyOwnedUnloadObserved:
    case AssetTraceEventType::OwnedFrontendUnloadCompleted:
    case AssetTraceEventType::OwnedReleasePacksEnter:
    case AssetTraceEventType::OwnedReleasePacksCompleted:
    case AssetTraceEventType::OutgoingLevelReleaseObserved:
    case AssetTraceEventType::SessionOnDestroyOriginalReturned:
    case AssetTraceEventType::CancelledPresentationDetected:
    case AssetTraceEventType::CancelledCleanupBegin:
    case AssetTraceEventType::CancelledCleanupSourceRemoval:
    case AssetTraceEventType::CancelledCleanupVerification:
    case AssetTraceEventType::CancelledEpochConsumed:
    case AssetTraceEventType::FreshPresentationEpochCreated:
    case AssetTraceEventType::ChangeAfterFadeAttestation:
    case AssetTraceEventType::PendingPostC10TokenPublished:
    case AssetTraceEventType::C10OriginalReturned:
    case AssetTraceEventType::PostC10SessionSlotSampled:
    case AssetTraceEventType::PendingPostC10TokenConsumed:
    case AssetTraceEventType::PostC10PreflightBegin:
    case AssetTraceEventType::PostC10PlayerPass1:
    case AssetTraceEventType::PostC10PlayerPass2:
    case AssetTraceEventType::PostC10PlayerAssessment:
    case AssetTraceEventType::PostC10AudioAssessment:
    case AssetTraceEventType::PostC10TransactionPreparationResult:
    case AssetTraceEventType::PostC10PreflightResult:
    case AssetTraceEventType::C10ScopeCleared:
    case AssetTraceEventType::FrontendSourceResetBegin:
    case AssetTraceEventType::FrontendSourceResetEnd:
    case AssetTraceEventType::FirstFrontendPackOrModuleLookup:
    case AssetTraceEventType::FirstFrontendLookupDisposition:
    case AssetTraceEventType::ShowMenuMainDisposition:
    case AssetTraceEventType::FrontendLookupAfterUnboundOnDestroy:
        *first = "epoch";
        *second = "barrier_evidence";
        break;
    case AssetTraceEventType::QueueOverflow:
        *first = "dropped_events";
        *second = "evidence";
        break;
    default:
        break;
    }
}

AssetTraceGenerationSummary* FindOrAddGeneration(
    AssetTraceSummary* summary, std::uint64_t generation) noexcept
{
    if (!summary || !generation) return nullptr;
    for (std::uint32_t i = 0; i < summary->transition_count; ++i) {
        if (summary->transitions[i].generation == generation)
            return &summary->transitions[i];
    }
    if (summary->transition_count >= kMaxSummaryTransitions) {
        summary->transitions_truncated = true;
        return nullptr;
    }
    AssetTraceGenerationSummary* result =
        &summary->transitions[summary->transition_count++];
    std::memset(result, 0, sizeof(*result));
    result->generation = generation;
    result->script_outcome = AssetTraceScriptOutcome::Unknown;
    return result;
}

AssetTraceRollbackBarrierSummary* FindOrAddRollbackBarrier(
    AssetTraceSummary* summary, std::uint32_t epoch) noexcept
{
    if (!summary || !epoch) return nullptr;
    for (std::uint32_t i = 0;
         i < summary->rollback_barrier_count; ++i) {
        if (summary->rollback_barriers[i].epoch == epoch)
            return &summary->rollback_barriers[i];
    }
    if (summary->rollback_barrier_count >=
        kMaxSummaryRollbackBarriers) {
        summary->rollback_barriers_truncated = true;
        return nullptr;
    }
    AssetTraceRollbackBarrierSummary* result =
        &summary->rollback_barriers[
            summary->rollback_barrier_count++];
    std::memset(result, 0, sizeof(*result));
    result->epoch = epoch;
    return result;
}

AssetTraceCampaignLoadSummary* FindOrAddCampaignLoad(
    AssetTraceSummary* summary, std::uint64_t generation) noexcept
{
    if (!summary || !generation) return nullptr;
    for (std::uint32_t index = 0;
         index < summary->campaign_load_count; ++index)
        if (summary->campaign_loads[index].observation_generation ==
            generation)
            return &summary->campaign_loads[index];
    if (summary->campaign_load_count >= kMaxSummaryCampaignLoads) {
        summary->campaign_loads_truncated = true;
        return nullptr;
    }
    AssetTraceCampaignLoadSummary* result =
        &summary->campaign_loads[summary->campaign_load_count++];
    std::memset(result, 0, sizeof(*result));
    result->observation_generation = generation;
    result->outcome =
        CampaignLoadPersistenceOutcome::ObservationIncomplete;
    return result;
}

AssetTracePackReleaseSummary* FindOrAddPackRelease(
    AssetTraceSummary* summary, std::uint64_t generation) noexcept
{
    if (!summary || !generation) return nullptr;
    for (std::uint32_t index = 0;
         index < summary->pack_release_count; ++index)
        if (summary->pack_releases[index].observation_generation ==
            generation)
            return &summary->pack_releases[index];
    if (summary->pack_release_count >= kMaxSummaryPackReleases) {
        summary->pack_releases_truncated = true;
        return nullptr;
    }
    AssetTracePackReleaseSummary* result =
        &summary->pack_releases[summary->pack_release_count++];
    std::memset(result, 0, sizeof(*result));
    result->observation_generation = generation;
    result->collection_state = PackCollectionState::Unknown;
    result->cross_check =
        PackCrossCheckOutcome::LayoutObservationIncomplete;
    return result;
}

bool PackPointerKnown(const AssetTraceGenerationSummary& summary,
                      std::uintptr_t pointer) noexcept
{
    if (!pointer) return false;
    for (std::uint32_t i = 0;
         i < summary.outgoing_pack_pointer_count; ++i) {
        if (summary.outgoing_pack_pointers[i] == pointer) return true;
    }
    return false;
}

void RememberPackPointer(AssetTraceGenerationSummary* summary,
                         std::uintptr_t pointer) noexcept
{
    if (!summary || !pointer || PackPointerKnown(*summary, pointer)) return;
    if (summary->outgoing_pack_pointer_count < kMaxSummaryPackPointers) {
        summary->outgoing_pack_pointers[
            summary->outgoing_pack_pointer_count++] = pointer;
    } else {
        summary->pack_pointer_correlation_truncated = true;
    }
}

void UpdateLastCleanup(AssetTraceGenerationSummary* summary,
                       std::uint64_t sequence) noexcept
{
    if (summary && sequence > summary->last_cleanup_sequence)
        summary->last_cleanup_sequence = sequence;
}

const char* TriStateName(AssetTraceTriState state) noexcept
{
    switch (state) {
    case AssetTraceTriState::No: return "no";
    case AssetTraceTriState::Yes: return "yes";
    default: return "unknown";
    }
}

const char* BoolEvidenceName(bool value, bool overflow) noexcept
{
    if (value) return "yes";
    return overflow ? "unknown" : "no";
}

const char* ScriptOutcomeName(AssetTraceScriptOutcome outcome) noexcept
{
    switch (outcome) {
    case AssetTraceScriptOutcome::Parsed: return "parsed";
    case AssetTraceScriptOutcome::CacheHit: return "cache_hit";
    case AssetTraceScriptOutcome::RegistrationConflict:
        return "registration_conflict";
    default: return "unknown";
    }
}

const char* DropDomainName(std::uint32_t mask) noexcept
{
    switch (mask) {
    case AssetTraceDropTransition: return "transition";
    case AssetTraceDropFilesystem: return "source_removal";
    case AssetTraceDropWatchedOpen: return "watched_open";
    case AssetTraceDropResource: return "resource";
    case AssetTraceDropRollback: return "rollback";
    case AssetTraceDropParserCache: return "parser";
    case AssetTraceDropAnnotation: return "annotation";
    case AssetTraceDropDiagnostic: return "diagnostic";
    case AssetTraceDropCampaign: return "campaign";
    case AssetTraceDropPackCollection: return "pack_collection";
    case AssetTraceDropPackDynamic: return "pack_dynamic";
    case AssetTraceDropBroadFileOpen: return "broad_file_open";
    case AssetTraceDropMaterial: return "material";
    case AssetTraceDropPlayerProgression: return "player_progression";
    default: return "unknown";
    }
}

void BuilderAppendSequenceOrUnknown(TextBuilder* builder,
                                    std::uint64_t sequence) noexcept
{
    if (sequence)
        BuilderAppendUnsigned(builder, sequence);
    else
        BuilderAppendLiteral(builder, "unknown");
}

AssetTraceTriState EvictionState(const AssetTraceSummary& aggregate,
                                 const AssetTraceGenerationSummary& item) noexcept
{
    if (item.outgoing_pack_evicted) return AssetTraceTriState::Yes;
    if (!aggregate.on_demand_hook_available ||
        (aggregate.dropped_domain_mask & AssetTraceDropResource) != 0 ||
        aggregate.trace_output_incomplete ||
        item.pack_pointer_correlation_truncated ||
        !item.outgoing_level_pack_observed ||
        !item.level_release_packs_exited)
        return AssetTraceTriState::Unknown;
    return AssetTraceTriState::No;
}

bool BuilderFinish(TextBuilder* builder, std::size_t* written) noexcept
{
    if (written) *written = 0;
    if (!builder || !builder->valid) {
        if (builder && builder->output && builder->capacity)
            builder->output[0] = 0;
        return false;
    }
    if (written) *written = builder->length;
    return true;
}

}  // namespace

void Initialize(const AssetTracePlatform* platform) noexcept
{
    g_platform.read_qpc =
        platform && platform->read_qpc ? platform->read_qpc : DefaultReadQpc;
    g_platform.read_thread_id =
        platform && platform->read_thread_id
            ? platform->read_thread_id
            : DefaultReadThreadId;
    g_platform.is_readable =
        platform && platform->is_readable
            ? platform->is_readable
            : DefaultIsReadable;
    Reset();
}

void Reset() noexcept
{
    std::memset(&g_queue, 0, sizeof(g_queue));
    dluma_atomic_exchange(&g_queue_lock, 0);
    dluma_atomic_exchange(&g_contention_drops, 0);
    dluma_atomic_exchange(&g_contention_drop_domains, 0);
    for (std::size_t index = 0; index < kTraceLossDomainCount; ++index) {
        dluma_atomic_exchange(&g_domain_drop_counts[index], 0);
        dluma_atomic_exchange(&g_domain_drop_sequence_unknown[index], 0);
        g_domain_first_known_sequence[index] = 0;
        g_domain_last_known_sequence[index] = 0;
    }
#if defined(__clang__)
    __atomic_store_n(&g_global_generation, 0, __ATOMIC_SEQ_CST);
#elif defined(_MSC_VER)
    _InterlockedExchange64(&g_global_generation, 0);
#endif
    g_next_sequence = 0;
    g_total_dropped = 0;
    _InterlockedExchange64(&g_critical_trace_loss, 0);
    _InterlockedExchange64(&g_optional_trace_loss, 0);
    _InterlockedExchange64(
        &g_transaction_premount_decisions, 0);
    _InterlockedExchange64(
        &g_transaction_premount_allowed, 0);
    _InterlockedExchange64(
        &g_transaction_allowed_tokens, 0);
    _InterlockedExchange64(
        &g_transaction_project_root_bindings, 0);
    _InterlockedExchange64(
        &g_transaction_data_directory_bindings, 0);
    _InterlockedExchange64(
        &g_transaction_assets_pc_bindings, 0);
    _InterlockedExchange64(
        &g_transaction_source_removals, 0);
    _InterlockedExchange64(
        &g_transaction_rollback_barriers, 0);
    _InterlockedExchange64(
        &g_transaction_frontend_resets, 0);
    _InterlockedExchange64(
        &g_transaction_frontend_verifications, 0);
    _InterlockedExchange64(
        &g_transaction_last_token_generation, 0);
    dluma_atomic_exchange(
        &g_transaction_last_bound_epoch, 0);
    dluma_atomic_exchange(
        &g_transaction_last_admission_result, 0);
    dluma_atomic_exchange(
        &g_transaction_observed_flags, 0);
    dluma_atomic_exchange(
        &g_transaction_first_failure_state, 0);
    std::memset(
        g_transaction_first_failure_reason, 0,
        sizeof(g_transaction_first_failure_reason));
    dluma_atomic_exchange(&g_contention_critical_drops, 0);
    dluma_atomic_exchange(&g_contention_optional_drops, 0);
    g_thread_generation = 0;
}

void ClearPayload(AssetTraceEventPayload* payload,
                  AssetTraceEventType type) noexcept
{
    if (!payload) return;
    std::memset(payload, 0, sizeof(*payload));
    payload->type = type;
}

bool Publish(const AssetTraceEventPayload& payload) noexcept
{
    return PublishWithSequence(payload, nullptr);
}

bool PublishWithSequence(const AssetTraceEventPayload& payload,
                         std::uint64_t* published_sequence) noexcept
{
    if (published_sequence) *published_sequence = 0;
    RecordMandatoryEvent(payload);
    if (!TryQueueLock()) {
        const std::uint32_t domain = EventDropDomainMask(payload.type);
        if (IsCriticalEvent(payload.type))
            AtomicIncrementLoss(&g_critical_trace_loss);
        else
            AtomicIncrementLoss(&g_optional_trace_loss);
        dluma_atomic_increment(&g_contention_drops);
        AtomicOrLong(
            &g_contention_drop_domains,
            static_cast<LONG>(domain));
        RecordContentionDropStats(domain);
        return false;
    }
    FoldContentionDropsLocked();
    const bool published = AppendPayloadLocked(
        payload, published_sequence);
    UnlockQueue();
    return published;
}

bool PublishThenCommit(const AssetTraceEventPayload& payload,
                       AssetTraceCommit commit,
                       void* context) noexcept
{
    if (!commit) return Publish(payload);
    RecordMandatoryEvent(payload);
    if (!TryQueueLock()) {
        const std::uint32_t domain = EventDropDomainMask(payload.type);
        if (IsCriticalEvent(payload.type))
            AtomicIncrementLoss(&g_critical_trace_loss);
        else
            AtomicIncrementLoss(&g_optional_trace_loss);
        dluma_atomic_increment(&g_contention_drops);
        AtomicOrLong(
            &g_contention_drop_domains,
            static_cast<LONG>(domain));
        RecordContentionDropStats(domain);
        commit(context);
        return false;
    }
    FoldContentionDropsLocked();
    const bool published = AppendPayloadLocked(payload);
    /* A lookup that races this short critical section may update the barrier,
     * but its own non-blocking trace publish then becomes a recorded
     * contention drop. Thus the trace is either authoritatively ordered or
     * explicitly incomplete; it never reports a contradictory order. */
    commit(context);
    UnlockQueue();
    return published;
}

bool PrepareThenPublish(AssetTraceEventPayload* payload,
                        AssetTracePrepare prepare,
                        void* context) noexcept
{
    if (!payload || !prepare) return false;
    if (!TryQueueLock()) {
        const bool prepared = prepare(context, payload);
        if (prepared) {
            RecordMandatoryEvent(*payload);
            const std::uint32_t domain =
                EventDropDomainMask(payload->type);
            if (IsCriticalEvent(payload->type))
                AtomicIncrementLoss(&g_critical_trace_loss);
            else
                AtomicIncrementLoss(&g_optional_trace_loss);
            dluma_atomic_increment(&g_contention_drops);
            AtomicOrLong(
                &g_contention_drop_domains,
                static_cast<LONG>(domain));
            RecordContentionDropStats(domain);
        }
        return false;
    }
    FoldContentionDropsLocked();
    const bool prepared = prepare(context, payload);
    if (prepared) RecordMandatoryEvent(*payload);
    const bool published = prepared && AppendPayloadLocked(*payload);
    UnlockQueue();
    return published;
}

bool Publish(AssetTraceEventType type, std::uintptr_t object,
             std::uintptr_t first, std::uintptr_t second,
             std::uint64_t value0, std::uint64_t value1,
             const char* text0, const char* text1) noexcept
{
    AssetTraceEventPayload payload;
    ClearPayload(&payload, type);
    payload.object = object;
    payload.first = first;
    payload.second = second;
    payload.value0 = value0;
    payload.value1 = value1;
    if (text0)
        CopyReadableCString(payload.text0, sizeof(payload.text0), text0);
    if (text1)
        CopyReadableCString(payload.text1, sizeof(payload.text1), text1);
    return Publish(payload);
}

std::size_t Drain(AssetTraceEvent* output, std::size_t capacity) noexcept
{
    if (!output || !capacity || !TryQueueLock()) return 0;
    FoldContentionDropsLocked();
    const std::size_t count =
        g_queue.count < capacity ? g_queue.count : capacity;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = (g_queue.head + i) % kQueueCapacity;
        output[i] = g_queue.records[index];
        if (g_queue.overflow_latched && index == g_queue.overflow_index) {
            g_queue.overflow_latched = false;
            g_queue.overflow_sequence_unknown = false;
            g_queue.overflow_index = 0;
        }
    }
    g_queue.head = (g_queue.head + count) % kQueueCapacity;
    g_queue.count -= count;
    UnlockQueue();
    return count;
}

std::size_t PendingCount() noexcept
{
    if (!TryQueueLock()) return 0;
    FoldContentionDropsLocked();
    const std::size_t result = g_queue.count;
    UnlockQueue();
    return result;
}

std::uint64_t DroppedEventCount() noexcept
{
    if (!TryQueueLock()) return g_total_dropped;
    FoldContentionDropsLocked();
    const std::uint64_t result = g_total_dropped;
    UnlockQueue();
    return result;
}

std::uint64_t CriticalTraceLossCount() noexcept
{
    return static_cast<std::uint64_t>(
        _InterlockedCompareExchange64(&g_critical_trace_loss, 0, 0));
}

std::uint64_t OptionalTraceLossCount() noexcept
{
    return static_cast<std::uint64_t>(
        _InterlockedCompareExchange64(&g_optional_trace_loss, 0, 0));
}

void RecordPreMountDecision(
    bool allowed, std::uint64_t token_generation,
    std::uint32_t admission_result,
    const char* failure_reason) noexcept
{
    AtomicIncrementLoss(
        &g_transaction_premount_decisions);
    dluma_atomic_exchange(
        &g_transaction_last_admission_result,
        static_cast<LONG>(admission_result));
    AtomicOrLong(
        &g_transaction_observed_flags,
        TransactionEvidencePreMountDecision);
    if (allowed) {
        AtomicIncrementLoss(
            &g_transaction_premount_allowed);
        AtomicOrLong(
            &g_transaction_observed_flags,
            TransactionEvidencePreMountAllowed);
        if (token_generation) {
            AtomicIncrementLoss(
                &g_transaction_allowed_tokens);
            AtomicStoreU64(
                &g_transaction_last_token_generation,
                token_generation);
            AtomicOrLong(
                &g_transaction_observed_flags,
                TransactionEvidenceAllowedToken);
        }
    } else {
        RecordTransactionFirstFailure(failure_reason);
    }
}

void RecordSourceBinding(
    std::uint32_t source_kind,
    std::uint64_t token_generation,
    std::uint32_t epoch,
    bool accepted,
    const char* failure_reason) noexcept
{
    if (!accepted) {
        RecordTransactionFirstFailure(failure_reason);
        return;
    }
    AtomicStoreU64(
        &g_transaction_last_token_generation,
        token_generation);
    dluma_atomic_exchange(
        &g_transaction_last_bound_epoch,
        static_cast<LONG>(epoch));
    switch (source_kind) {
    case DLUMA_PROJECT_MOUNT_SOURCE_PROJECT_ROOT:
        AtomicIncrementLoss(
            &g_transaction_project_root_bindings);
        AtomicOrLong(
            &g_transaction_observed_flags,
            TransactionEvidenceProjectRoot);
        break;
    case DLUMA_PROJECT_MOUNT_SOURCE_DATA_DIRECTORY:
        AtomicIncrementLoss(
            &g_transaction_data_directory_bindings);
        AtomicOrLong(
            &g_transaction_observed_flags,
            TransactionEvidenceDataDirectory);
        break;
    case DLUMA_PROJECT_MOUNT_SOURCE_ASSETS_PC:
        AtomicIncrementLoss(
            &g_transaction_assets_pc_bindings);
        AtomicOrLong(
            &g_transaction_observed_flags,
            TransactionEvidenceAssetsPc);
        break;
    default:
        RecordTransactionFirstFailure(
            failure_reason && failure_reason[0]
                ? failure_reason
                : "unrelated_custom_project_source");
        break;
    }
}

bool SnapshotTransactionEvidence(
    AssetTraceTransactionEvidence* output) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    output->premount_decision_count =
        AtomicLoadU64(&g_transaction_premount_decisions);
    output->premount_allowed_count =
        AtomicLoadU64(&g_transaction_premount_allowed);
    output->allowed_token_count =
        AtomicLoadU64(&g_transaction_allowed_tokens);
    output->project_root_binding_count =
        AtomicLoadU64(&g_transaction_project_root_bindings);
    output->data_directory_binding_count =
        AtomicLoadU64(&g_transaction_data_directory_bindings);
    output->assets_pc_binding_count =
        AtomicLoadU64(&g_transaction_assets_pc_bindings);
    output->source_removal_count =
        AtomicLoadU64(&g_transaction_source_removals);
    output->rollback_barrier_count =
        AtomicLoadU64(&g_transaction_rollback_barriers);
    output->frontend_reset_count =
        AtomicLoadU64(&g_transaction_frontend_resets);
    output->frontend_verification_count =
        AtomicLoadU64(&g_transaction_frontend_verifications);
    output->last_token_generation =
        AtomicLoadU64(&g_transaction_last_token_generation);
    output->last_bound_epoch =
        static_cast<std::uint32_t>(
            dluma_atomic_compare_exchange(
                &g_transaction_last_bound_epoch, 0, 0));
    output->last_admission_result =
        static_cast<std::uint32_t>(
            dluma_atomic_compare_exchange(
                &g_transaction_last_admission_result, 0, 0));
    output->observed_flags =
        static_cast<std::uint32_t>(
            dluma_atomic_compare_exchange(
                &g_transaction_observed_flags, 0, 0));
    output->transaction_evidence_loss = 0;
    if (dluma_atomic_compare_exchange(
            &g_transaction_first_failure_state, 0, 0) == 2)
        CopyBounded(
            output->first_failure_reason,
            sizeof(output->first_failure_reason),
            g_transaction_first_failure_reason,
            sizeof(g_transaction_first_failure_reason));
    return true;
}

std::uint64_t TransactionEvidenceLossCount() noexcept
{
    return 0;
}

std::size_t CriticalTraceReservedCapacity() noexcept
{
    return kCriticalTraceReservedCapacity;
}

bool IsCriticalEvent(AssetTraceEventType type) noexcept
{
    switch (type) {
    case AssetTraceEventType::FsRemoveSourceEnter:
    case AssetTraceEventType::FsRemoveSourceExit:
    case AssetTraceEventType::SessionOnDestroyEnter:
    case AssetTraceEventType::OnDestroyOwnedUnloadObserved:
    case AssetTraceEventType::OutgoingLevelReleaseObserved:
    case AssetTraceEventType::SessionOnDestroyOriginalReturned:
    case AssetTraceEventType::CancelledPresentationDetected:
    case AssetTraceEventType::CancelledCleanupBegin:
    case AssetTraceEventType::CancelledCleanupSourceRemoval:
    case AssetTraceEventType::CancelledCleanupVerification:
    case AssetTraceEventType::CancelledEpochConsumed:
    case AssetTraceEventType::FreshPresentationEpochCreated:
    case AssetTraceEventType::ChangeAfterFadeAttestation:
    case AssetTraceEventType::PendingPostC10TokenPublished:
    case AssetTraceEventType::C10OriginalReturned:
    case AssetTraceEventType::PostC10SessionSlotSampled:
    case AssetTraceEventType::PendingPostC10TokenConsumed:
    case AssetTraceEventType::PostC10PreflightBegin:
    case AssetTraceEventType::PostC10PlayerPass1:
    case AssetTraceEventType::PostC10PlayerPass2:
    case AssetTraceEventType::PostC10PlayerAssessment:
    case AssetTraceEventType::PostC10AudioAssessment:
    case AssetTraceEventType::PostC10TransactionPreparationResult:
    case AssetTraceEventType::PostC10PreflightResult:
    case AssetTraceEventType::C10ScopeCleared:
    case AssetTraceEventType::FrontendSourceResetBegin:
    case AssetTraceEventType::FrontendSourceResetEnd:
    case AssetTraceEventType::FirstFrontendLookupDisposition:
    case AssetTraceEventType::ShowMenuMainDisposition:
    case AssetTraceEventType::PlayerPresetProviderRepairPending:
    case AssetTraceEventType::PlayerPresetRebuildPreflightFailed:
    case AssetTraceEventType::PlayerPresetTargetRecordMembership:
    case AssetTraceEventType::ExperimentalPlayerPresetRebuildBegin:
    case AssetTraceEventType::ExperimentalPlayerPresetRecordRebuilt:
    case AssetTraceEventType::ExperimentalPlayerPresetRebuildEnd:
    case AssetTraceEventType::PlayerPresetNaturalClearApplied:
    case AssetTraceEventType::PlayerPresetProviderRestored:
        return true;
    default:
        return false;
    }
}

AssetTraceContextToken BeginTransitionContext() noexcept
{
    AssetTraceContextToken token = {};
    token.previous_generation = g_thread_generation;
    token.generation = AtomicIncrementGeneration();
    g_thread_generation = token.generation;
    return token;
}

AssetTraceContextToken PushTransitionContext(std::uint64_t generation) noexcept
{
    AssetTraceContextToken token = {};
    token.previous_generation = g_thread_generation;
    token.generation = generation;
    g_thread_generation = generation;
    return token;
}

void EndTransitionContext(const AssetTraceContextToken& token) noexcept
{
    g_thread_generation = token.previous_generation;
}

std::uint64_t CurrentTransitionGeneration() noexcept
{
    return g_thread_generation ? g_thread_generation
                               : AtomicLoadGeneration();
}

std::uint64_t GlobalTransitionGeneration() noexcept
{
    return AtomicLoadGeneration();
}

AssetTraceStringCopyResult CopyReadableCString(
    char* output, std::size_t output_capacity, const char* source,
    std::size_t maximum_scan) noexcept
{
    if (!output || !output_capacity) return AssetTraceStringCopyResult::Null;
    output[0] = 0;
    if (!source) {
        CopyLiteral(output, output_capacity, "<null>");
        return AssetTraceStringCopyResult::Null;
    }
    if (!maximum_scan) {
        CopyLiteral(output, output_capacity, "<unterminated>");
        return AssetTraceStringCopyResult::Unterminated;
    }

    std::size_t length = 0;
    bool terminated = false;
    while (length < maximum_scan) {
        const char* current = source + length;
        if (!g_platform.is_readable(current, 1)) {
            CopyLiteral(output, output_capacity, "<unreadable>");
            return AssetTraceStringCopyResult::Unreadable;
        }
        if (!*current) {
            terminated = true;
            break;
        }
        ++length;
    }
    if (!terminated) {
        CopyLiteral(output, output_capacity, "<unterminated>");
        return AssetTraceStringCopyResult::Unterminated;
    }
    if (!g_platform.is_readable(source, length + 1)) {
        CopyLiteral(output, output_capacity, "<unreadable>");
        return AssetTraceStringCopyResult::Unreadable;
    }

    const bool truncated = length >= output_capacity;
    const std::size_t copied =
        truncated ? output_capacity - 1 : length;
    if (copied) std::memcpy(output, source, copied);
    output[copied] = 0;
    return truncated ? AssetTraceStringCopyResult::Truncated
                     : AssetTraceStringCopyResult::Copied;
}

AssetTraceStringCopyResult CopyRawResourcePackPath(
    char* output, std::size_t output_capacity,
    const void* resource_data_pack) noexcept
{
    const void* holder = nullptr;
    const char* path = nullptr;
    if (!output || !output_capacity) return AssetTraceStringCopyResult::Null;
    output[0] = 0;
    if (!resource_data_pack) {
        CopyLiteral(output, output_capacity, "<null>");
        return AssetTraceStringCopyResult::Null;
    }
    const unsigned char* pack =
        static_cast<const unsigned char*>(resource_data_pack);
    if (!g_platform.is_readable(pack + 0xC0, sizeof(holder))) {
        CopyLiteral(output, output_capacity, "<unreadable>");
        return AssetTraceStringCopyResult::Unreadable;
    }
    std::memcpy(&holder, pack + 0xC0, sizeof(holder));
    if (!holder || !g_platform.is_readable(holder, sizeof(path))) {
        CopyLiteral(output, output_capacity, "<unreadable>");
        return AssetTraceStringCopyResult::Unreadable;
    }
    std::memcpy(&path, holder, sizeof(path));
    return CopyReadableCString(output, output_capacity, path, 4096);
}

bool NormalizeWatchedPath(const char* input, char* output,
                          std::size_t output_capacity) noexcept
{
    if (!output || !output_capacity) return false;
    output[0] = 0;
    if (!input) return false;

    std::size_t source = 0;
    std::size_t destination = 0;
    while (input[source] == '/' || input[source] == '\\') ++source;

    bool previous_separator = false;
    for (; input[source]; ++source) {
        unsigned char value = static_cast<unsigned char>(input[source]);
        const bool separator = value == '/' || value == '\\';
        if (separator) {
            if (previous_separator) continue;
            value = '\\';
            previous_separator = true;
        } else {
            previous_separator = false;
            if (value >= 'A' && value <= 'Z')
                value = static_cast<unsigned char>(value - 'A' + 'a');
        }
        if (destination + 1 >= output_capacity) {
            output[0] = 0;
            return false;
        }
        output[destination++] = static_cast<char>(value);
    }
    while (destination && output[destination - 1] == '\\') --destination;
    output[destination] = 0;
    return destination != 0;
}

void ResetWatchList(AssetTraceWatchList* watch_list) noexcept
{
    if (watch_list) std::memset(watch_list, 0, sizeof(*watch_list));
}

bool AddWatchedPath(AssetTraceWatchList* watch_list,
                    const char* logical_path) noexcept
{
    char normalized[kEventTextCapacity];
    if (!watch_list ||
        !NormalizeWatchedPath(logical_path, normalized, sizeof(normalized)))
        return false;
    for (std::uint32_t i = 0; i < watch_list->count; ++i) {
        if (BoundedStringsEqual(
                watch_list->paths[i], normalized, sizeof(normalized)))
            return true;
    }
    if (watch_list->count >= kMaxWatchedPaths) return false;
    CopyBounded(watch_list->paths[watch_list->count], kEventTextCapacity,
                normalized, sizeof(normalized));
    ++watch_list->count;
    return true;
}

bool MatchWatchedPath(const AssetTraceWatchList& watch_list,
                      const char* logical_path, std::uint32_t* matched_index,
                      char* normalized_output,
                      std::size_t normalized_capacity) noexcept
{
    char normalized[kEventTextCapacity];
    if (matched_index) *matched_index = 0;
    if (normalized_output && normalized_capacity) normalized_output[0] = 0;
    if (!NormalizeWatchedPath(logical_path, normalized, sizeof(normalized)))
        return false;
    if (normalized_output && normalized_capacity)
        CopyBounded(normalized_output, normalized_capacity, normalized,
                    sizeof(normalized));
    for (std::uint32_t i = 0; i < watch_list.count; ++i) {
        if (BoundedStringsEqual(
                watch_list.paths[i], normalized, sizeof(normalized))) {
            if (matched_index) *matched_index = i;
            return true;
        }
    }
    return false;
}

const char* EventTypeName(AssetTraceEventType type) noexcept
{
#define DLUMA_TRACE_EVENT_NAME(value) \
    case AssetTraceEventType::value: return #value
    switch (type) {
    DLUMA_TRACE_EVENT_NAME(ChangeActiveLevelEnter);
    DLUMA_TRACE_EVENT_NAME(ChangeActiveLevelBranch);
    DLUMA_TRACE_EVENT_NAME(ChangeActiveLevelExit);
    DLUMA_TRACE_EVENT_NAME(ChangeAfterFadeEnter);
    DLUMA_TRACE_EVENT_NAME(ChangeAfterFadePostUnload);
    DLUMA_TRACE_EVENT_NAME(ChangeAfterFadeExit);
    DLUMA_TRACE_EVENT_NAME(UnloadLevelEnter);
    DLUMA_TRACE_EVENT_NAME(UnloadLevelExit);
    DLUMA_TRACE_EVENT_NAME(DestroyActiveLevelEnter);
    DLUMA_TRACE_EVENT_NAME(DestroyActiveLevelExit);
    DLUMA_TRACE_EVENT_NAME(UpdateDLCContentEnter);
    DLUMA_TRACE_EVENT_NAME(UpdateDLCContentExit);
    DLUMA_TRACE_EVENT_NAME(LoadLevelEnter);
    DLUMA_TRACE_EVENT_NAME(LoadModuleBeginCall);
    DLUMA_TRACE_EVENT_NAME(LoadModuleBeginReturn);
    DLUMA_TRACE_EVENT_NAME(LoadModuleEndCall);
    DLUMA_TRACE_EVENT_NAME(LoadModuleEndReturn);
    DLUMA_TRACE_EVENT_NAME(LoadLevelExit);
    DLUMA_TRACE_EVENT_NAME(FsAddSourceEnter);
    DLUMA_TRACE_EVENT_NAME(FsAddSourceExit);
    DLUMA_TRACE_EVENT_NAME(FsRemoveSourceEnter);
    DLUMA_TRACE_EVENT_NAME(FsRemoveSourceExit);
    DLUMA_TRACE_EVENT_NAME(FsOpenWatchedEnter);
    DLUMA_TRACE_EVENT_NAME(FsOpenWatchedResult);
    DLUMA_TRACE_EVENT_NAME(GameLoadPackEnter);
    DLUMA_TRACE_EVENT_NAME(GameLoadPackExit);
    DLUMA_TRACE_EVENT_NAME(GameFreePackEnter);
    DLUMA_TRACE_EVENT_NAME(GameFreePackExit);
    DLUMA_TRACE_EVENT_NAME(AssetRegRpackEnter);
    DLUMA_TRACE_EVENT_NAME(AssetRegRpackExit);
    DLUMA_TRACE_EVENT_NAME(AssetUnregRpackEnter);
    DLUMA_TRACE_EVENT_NAME(AssetUnregRpackExit);
    DLUMA_TRACE_EVENT_NAME(AssetReleasePackEnter);
    DLUMA_TRACE_EVENT_NAME(AssetReleasePackExit);
    DLUMA_TRACE_EVENT_NAME(LevelReleasePacksEnter);
    DLUMA_TRACE_EVENT_NAME(LevelReleasePacksPack);
    DLUMA_TRACE_EVENT_NAME(LevelReleasePacksExit);
    DLUMA_TRACE_EVENT_NAME(OnDemandEvictPackEnter);
    DLUMA_TRACE_EVENT_NAME(OnDemandEvictPackExit);
    DLUMA_TRACE_EVENT_NAME(LevelPackInternalReleaseEnter);
    DLUMA_TRACE_EVENT_NAME(LevelPackInternalReleaseExit);
    DLUMA_TRACE_EVENT_NAME(LevelReleasePacksCrossCheck);
    DLUMA_TRACE_EVENT_NAME(ScriptLoaderWatchedEnter);
    DLUMA_TRACE_EVENT_NAME(ScriptLoaderWatchedCacheHit);
    DLUMA_TRACE_EVENT_NAME(ScriptLoaderWatchedParse);
    DLUMA_TRACE_EVENT_NAME(ScriptLoaderWatchedRegistrationConflict);
    DLUMA_TRACE_EVENT_NAME(ScriptLoaderWatchedExit);
    DLUMA_TRACE_EVENT_NAME(CampaignManagerLoadEnter);
    DLUMA_TRACE_EVENT_NAME(CampaignManagerSelection);
    DLUMA_TRACE_EVENT_NAME(CampaignManagerSlotSnapshot);
    DLUMA_TRACE_EVENT_NAME(CampaignStateBeforeLoad);
    DLUMA_TRACE_EVENT_NAME(CampaignSnapshotMap);
    DLUMA_TRACE_EVENT_NAME(CampaignAddMapEnter);
    DLUMA_TRACE_EVENT_NAME(CampaignAddMapExit);
    DLUMA_TRACE_EVENT_NAME(CampaignRewardsMutationObserved);
    DLUMA_TRACE_EVENT_NAME(CampaignAfterRespawnMutationObserved);
    DLUMA_TRACE_EVENT_NAME(CampaignStateAfterLoad);
    DLUMA_TRACE_EVENT_NAME(CampaignManagerLoadExit);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalInitEnter);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalInitExit);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalReloadEnter);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalReloadExit);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalClearEnter);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalClearExit);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalDestroyEnter);
    DLUMA_TRACE_EVENT_NAME(CampaignNaturalDestroyExit);
    DLUMA_TRACE_EVENT_NAME(CampaignFrontendSnapshot);
    DLUMA_TRACE_EVENT_NAME(CampaignCarryoverObserved);
    DLUMA_TRACE_EVENT_NAME(CampaignObservationUnknown);
    DLUMA_TRACE_EVENT_NAME(SessionOnDestroyEnter);
    DLUMA_TRACE_EVENT_NAME(OnDestroyOwnedUnloadObserved);
    DLUMA_TRACE_EVENT_NAME(OwnedFrontendUnloadCompleted);
    DLUMA_TRACE_EVENT_NAME(OwnedReleasePacksEnter);
    DLUMA_TRACE_EVENT_NAME(OwnedReleasePacksCompleted);
    DLUMA_TRACE_EVENT_NAME(OutgoingLevelReleaseObserved);
    DLUMA_TRACE_EVENT_NAME(SessionOnDestroyOriginalReturned);
    DLUMA_TRACE_EVENT_NAME(CancelledPresentationDetected);
    DLUMA_TRACE_EVENT_NAME(CancelledCleanupBegin);
    DLUMA_TRACE_EVENT_NAME(CancelledCleanupSourceRemoval);
    DLUMA_TRACE_EVENT_NAME(CancelledCleanupVerification);
    DLUMA_TRACE_EVENT_NAME(CancelledEpochConsumed);
    DLUMA_TRACE_EVENT_NAME(FreshPresentationEpochCreated);
    DLUMA_TRACE_EVENT_NAME(ChangeAfterFadeAttestation);
    DLUMA_TRACE_EVENT_NAME(PendingPostC10TokenPublished);
    DLUMA_TRACE_EVENT_NAME(C10OriginalReturned);
    DLUMA_TRACE_EVENT_NAME(PostC10SessionSlotSampled);
    DLUMA_TRACE_EVENT_NAME(PendingPostC10TokenConsumed);
    DLUMA_TRACE_EVENT_NAME(PostC10PreflightBegin);
    DLUMA_TRACE_EVENT_NAME(PostC10PlayerPass1);
    DLUMA_TRACE_EVENT_NAME(PostC10PlayerPass2);
    DLUMA_TRACE_EVENT_NAME(PostC10PlayerAssessment);
    DLUMA_TRACE_EVENT_NAME(PostC10AudioAssessment);
    DLUMA_TRACE_EVENT_NAME(PostC10TransactionPreparationResult);
    DLUMA_TRACE_EVENT_NAME(PostC10PreflightResult);
    DLUMA_TRACE_EVENT_NAME(C10ScopeCleared);
    DLUMA_TRACE_EVENT_NAME(FrontendSourceResetBegin);
    DLUMA_TRACE_EVENT_NAME(FrontendSourceResetEnd);
    DLUMA_TRACE_EVENT_NAME(FirstFrontendPackOrModuleLookup);
    DLUMA_TRACE_EVENT_NAME(FirstFrontendLookupDisposition);
    DLUMA_TRACE_EVENT_NAME(ShowMenuMainDisposition);
    DLUMA_TRACE_EVENT_NAME(FrontendLookupAfterUnboundOnDestroy);
    DLUMA_TRACE_EVENT_NAME(MaterialReloadEnter);
    DLUMA_TRACE_EVENT_NAME(MaterialLocalPackOpen);
    DLUMA_TRACE_EVENT_NAME(MaterialPackLoadEnter);
    DLUMA_TRACE_EVENT_NAME(MaterialPackLoadExit);
    DLUMA_TRACE_EVENT_NAME(MaterialPackSelected);
    DLUMA_TRACE_EVENT_NAME(MaterialExistingObjectRebind);
    DLUMA_TRACE_EVENT_NAME(MaterialGlobalFallback);
    DLUMA_TRACE_EVENT_NAME(MaterialOldPackDestructorEnter);
    DLUMA_TRACE_EVENT_NAME(MaterialOldPackDestructorExit);
    DLUMA_TRACE_EVENT_NAME(MaterialReloadExit);
    DLUMA_TRACE_EVENT_NAME(PostSupersessionAssetsOpen);
    DLUMA_TRACE_EVENT_NAME(PlayerTemplateSelection);
    DLUMA_TRACE_EVENT_NAME(ProgressionXmlLoadEnter);
    DLUMA_TRACE_EVENT_NAME(ProgressionXmlLoadExit);
    DLUMA_TRACE_EVENT_NAME(ProgressionDefinitionOverlay);
    DLUMA_TRACE_EVENT_NAME(PlayerVariablesAfterTemplateApply);
    DLUMA_TRACE_EVENT_NAME(PlayerStateOwnerObserved);
    DLUMA_TRACE_EVENT_NAME(PlayerStateDestroyed);
    DLUMA_TRACE_EVENT_NAME(PlayerStateClearEnter);
    DLUMA_TRACE_EVENT_NAME(PlayerStateClearExit);
    DLUMA_TRACE_EVENT_NAME(PlayerStateRemovePrecachedEnter);
    DLUMA_TRACE_EVENT_NAME(PlayerStateRemovePrecachedExit);
    DLUMA_TRACE_EVENT_NAME(PlayerStateInitializeEnter);
    DLUMA_TRACE_EVENT_NAME(PlayerStateInitializeExit);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetCacheAppend);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetRecordRebuild);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetCacheResetEnter);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetCacheResetExit);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetMembershipClassified);
    DLUMA_TRACE_EVENT_NAME(SameSelectorProviderChanged);
    DLUMA_TRACE_EVENT_NAME(SameSelectorProviderChangeRebuildSkipped);
    DLUMA_TRACE_EVENT_NAME(StaleProgressionDefinitionRetained);
    DLUMA_TRACE_EVENT_NAME(PlayerAppearanceParseEnter);
    DLUMA_TRACE_EVENT_NAME(PlayerAppearanceParseExit);
    DLUMA_TRACE_EVENT_NAME(ReloadPlayerScriptsEnter);
    DLUMA_TRACE_EVENT_NAME(ReloadPlayerScriptsExit);
    DLUMA_TRACE_EVENT_NAME(RecreateRepresentationEnter);
    DLUMA_TRACE_EVENT_NAME(RecreateRepresentationExit);
    DLUMA_TRACE_EVENT_NAME(AudioScriptExecuteEnter);
    DLUMA_TRACE_EVENT_NAME(AudioScriptExecuteExit);
    DLUMA_TRACE_EVENT_NAME(SpeechRegistryMutationEnter);
    DLUMA_TRACE_EVENT_NAME(SpeechRegistryMutationExit);
    DLUMA_TRACE_EVENT_NAME(PlayerStateLoadEnter);
    DLUMA_TRACE_EVENT_NAME(PlayerStateLoadClearCall);
    DLUMA_TRACE_EVENT_NAME(PlayerStateLoadAfterClear);
    DLUMA_TRACE_EVENT_NAME(PlayerStateLoadExit);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetProviderRepairPending);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetRebuildPreflightFailed);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetTargetRecordMembership);
    DLUMA_TRACE_EVENT_NAME(ExperimentalPlayerPresetRebuildBegin);
    DLUMA_TRACE_EVENT_NAME(ExperimentalPlayerPresetRecordRebuilt);
    DLUMA_TRACE_EVENT_NAME(ExperimentalPlayerPresetRebuildEnd);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetNaturalClearApplied);
    DLUMA_TRACE_EVENT_NAME(PlayerPresetProviderRestored);
    DLUMA_TRACE_EVENT_NAME(SharedStateLiveValueUnknown);
    DLUMA_TRACE_EVENT_NAME(ActiveLevelObserved);
    DLUMA_TRACE_EVENT_NAME(QueueOverflow);
    default: return "Unknown";
    }
#undef DLUMA_TRACE_EVENT_NAME
}

const char* EventCategoryName(AssetTraceEventType type) noexcept
{
    switch (type) {
    case AssetTraceEventType::FsAddSourceEnter:
    case AssetTraceEventType::FsAddSourceExit:
    case AssetTraceEventType::FsRemoveSourceEnter:
    case AssetTraceEventType::FsRemoveSourceExit:
    case AssetTraceEventType::FsOpenWatchedEnter:
    case AssetTraceEventType::FsOpenWatchedResult:
        return "filesystem";
    case AssetTraceEventType::GameLoadPackEnter:
    case AssetTraceEventType::GameLoadPackExit:
    case AssetTraceEventType::GameFreePackEnter:
    case AssetTraceEventType::GameFreePackExit:
    case AssetTraceEventType::AssetRegRpackEnter:
    case AssetTraceEventType::AssetRegRpackExit:
    case AssetTraceEventType::AssetUnregRpackEnter:
    case AssetTraceEventType::AssetUnregRpackExit:
    case AssetTraceEventType::AssetReleasePackEnter:
    case AssetTraceEventType::AssetReleasePackExit:
    case AssetTraceEventType::LevelReleasePacksEnter:
    case AssetTraceEventType::LevelReleasePacksPack:
    case AssetTraceEventType::LevelReleasePacksExit:
    case AssetTraceEventType::OnDemandEvictPackEnter:
    case AssetTraceEventType::OnDemandEvictPackExit:
    case AssetTraceEventType::LevelPackInternalReleaseEnter:
    case AssetTraceEventType::LevelPackInternalReleaseExit:
    case AssetTraceEventType::LevelReleasePacksCrossCheck:
        return "resource";
    case AssetTraceEventType::ScriptLoaderWatchedEnter:
    case AssetTraceEventType::ScriptLoaderWatchedCacheHit:
    case AssetTraceEventType::ScriptLoaderWatchedParse:
    case AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict:
    case AssetTraceEventType::ScriptLoaderWatchedExit:
        return "script";
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignManagerSlotSnapshot:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignSnapshotMap:
    case AssetTraceEventType::CampaignAddMapEnter:
    case AssetTraceEventType::CampaignAddMapExit:
    case AssetTraceEventType::CampaignRewardsMutationObserved:
    case AssetTraceEventType::CampaignAfterRespawnMutationObserved:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignNaturalInitEnter:
    case AssetTraceEventType::CampaignNaturalInitExit:
    case AssetTraceEventType::CampaignNaturalReloadEnter:
    case AssetTraceEventType::CampaignNaturalReloadExit:
    case AssetTraceEventType::CampaignNaturalClearEnter:
    case AssetTraceEventType::CampaignNaturalClearExit:
    case AssetTraceEventType::CampaignNaturalDestroyEnter:
    case AssetTraceEventType::CampaignNaturalDestroyExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
    case AssetTraceEventType::CampaignCarryoverObserved:
    case AssetTraceEventType::CampaignObservationUnknown:
        return "campaign";
    case AssetTraceEventType::OutgoingLevelReleaseObserved:
    case AssetTraceEventType::OwnedFrontendUnloadCompleted:
    case AssetTraceEventType::OwnedReleasePacksEnter:
    case AssetTraceEventType::OwnedReleasePacksCompleted:
    case AssetTraceEventType::CancelledPresentationDetected:
    case AssetTraceEventType::CancelledCleanupBegin:
    case AssetTraceEventType::CancelledCleanupSourceRemoval:
    case AssetTraceEventType::CancelledCleanupVerification:
    case AssetTraceEventType::CancelledEpochConsumed:
    case AssetTraceEventType::FreshPresentationEpochCreated:
    case AssetTraceEventType::ChangeAfterFadeAttestation:
    case AssetTraceEventType::PendingPostC10TokenPublished:
    case AssetTraceEventType::C10OriginalReturned:
    case AssetTraceEventType::PostC10SessionSlotSampled:
    case AssetTraceEventType::PendingPostC10TokenConsumed:
    case AssetTraceEventType::PostC10PreflightBegin:
    case AssetTraceEventType::PostC10PlayerPass1:
    case AssetTraceEventType::PostC10PlayerPass2:
    case AssetTraceEventType::PostC10PlayerAssessment:
    case AssetTraceEventType::PostC10AudioAssessment:
    case AssetTraceEventType::PostC10TransactionPreparationResult:
    case AssetTraceEventType::PostC10PreflightResult:
    case AssetTraceEventType::C10ScopeCleared:
    case AssetTraceEventType::FrontendSourceResetBegin:
    case AssetTraceEventType::FrontendSourceResetEnd:
    case AssetTraceEventType::FirstFrontendPackOrModuleLookup:
    case AssetTraceEventType::FirstFrontendLookupDisposition:
    case AssetTraceEventType::ShowMenuMainDisposition:
    case AssetTraceEventType::FrontendLookupAfterUnboundOnDestroy:
        return "rollback";
    case AssetTraceEventType::MaterialReloadEnter:
    case AssetTraceEventType::MaterialLocalPackOpen:
    case AssetTraceEventType::MaterialPackLoadEnter:
    case AssetTraceEventType::MaterialPackLoadExit:
    case AssetTraceEventType::MaterialPackSelected:
    case AssetTraceEventType::MaterialExistingObjectRebind:
    case AssetTraceEventType::MaterialGlobalFallback:
    case AssetTraceEventType::MaterialOldPackDestructorEnter:
    case AssetTraceEventType::MaterialOldPackDestructorExit:
    case AssetTraceEventType::MaterialReloadExit:
    case AssetTraceEventType::PostSupersessionAssetsOpen:
        return "material";
    case AssetTraceEventType::PlayerTemplateSelection:
    case AssetTraceEventType::ProgressionXmlLoadEnter:
    case AssetTraceEventType::ProgressionXmlLoadExit:
    case AssetTraceEventType::ProgressionDefinitionOverlay:
    case AssetTraceEventType::PlayerVariablesAfterTemplateApply:
    case AssetTraceEventType::PlayerStateOwnerObserved:
    case AssetTraceEventType::PlayerStateDestroyed:
    case AssetTraceEventType::PlayerStateClearEnter:
    case AssetTraceEventType::PlayerStateClearExit:
    case AssetTraceEventType::PlayerStateRemovePrecachedEnter:
    case AssetTraceEventType::PlayerStateRemovePrecachedExit:
    case AssetTraceEventType::PlayerStateInitializeEnter:
    case AssetTraceEventType::PlayerStateInitializeExit:
    case AssetTraceEventType::PlayerPresetCacheAppend:
    case AssetTraceEventType::PlayerPresetRecordRebuild:
    case AssetTraceEventType::PlayerPresetCacheResetEnter:
    case AssetTraceEventType::PlayerPresetCacheResetExit:
    case AssetTraceEventType::PlayerPresetMembershipClassified:
    case AssetTraceEventType::SameSelectorProviderChanged:
    case AssetTraceEventType::SameSelectorProviderChangeRebuildSkipped:
    case AssetTraceEventType::StaleProgressionDefinitionRetained:
    case AssetTraceEventType::PlayerAppearanceParseEnter:
    case AssetTraceEventType::PlayerAppearanceParseExit:
    case AssetTraceEventType::ReloadPlayerScriptsEnter:
    case AssetTraceEventType::ReloadPlayerScriptsExit:
    case AssetTraceEventType::RecreateRepresentationEnter:
    case AssetTraceEventType::RecreateRepresentationExit:
    case AssetTraceEventType::PlayerStateLoadEnter:
    case AssetTraceEventType::PlayerStateLoadClearCall:
    case AssetTraceEventType::PlayerStateLoadAfterClear:
    case AssetTraceEventType::PlayerStateLoadExit:
    case AssetTraceEventType::PlayerPresetProviderRepairPending:
    case AssetTraceEventType::PlayerPresetRebuildPreflightFailed:
    case AssetTraceEventType::PlayerPresetTargetRecordMembership:
    case AssetTraceEventType::ExperimentalPlayerPresetRebuildBegin:
    case AssetTraceEventType::ExperimentalPlayerPresetRecordRebuilt:
    case AssetTraceEventType::ExperimentalPlayerPresetRebuildEnd:
    case AssetTraceEventType::PlayerPresetNaturalClearApplied:
    case AssetTraceEventType::PlayerPresetProviderRestored:
    case AssetTraceEventType::SharedStateLiveValueUnknown:
        return "player_progression";
    case AssetTraceEventType::AudioScriptExecuteEnter:
    case AssetTraceEventType::AudioScriptExecuteExit:
    case AssetTraceEventType::SpeechRegistryMutationEnter:
    case AssetTraceEventType::SpeechRegistryMutationExit:
        return "audio";
    case AssetTraceEventType::ActiveLevelObserved:
        return "annotation";
    case AssetTraceEventType::QueueOverflow:
        return "diagnostic";
    default:
        return "transition";
    }
}

const char* SourceKindName(AssetTraceSourceKind kind) noexcept
{
    switch (kind) {
    case AssetTraceSourceKind::ProjectRoot: return "ProjectRoot";
    case AssetTraceSourceKind::DataDirectory: return "DataDirectory";
    case AssetTraceSourceKind::DataWorkshopPak: return "DataWorkshopPak";
    case AssetTraceSourceKind::LocaleDirectory: return "LocaleDirectory";
    case AssetTraceSourceKind::LocalePak: return "LocalePak";
    case AssetTraceSourceKind::SpeechDirectory: return "SpeechDirectory";
    case AssetTraceSourceKind::SpeechPak: return "SpeechPak";
    case AssetTraceSourceKind::AssetsPc: return "AssetsPc";
    case AssetTraceSourceKind::UnknownProjectSource:
        return "UnknownProjectSource";
    default: return "Unknown";
    }
}

const char* StagingPhaseName(AssetTraceStagingPhase phase) noexcept
{
    switch (phase) {
    case AssetTraceStagingPhase::CustomContentStaging:
        return "custom_content_staging";
    case AssetTraceStagingPhase::TransitionActive:
        return "transition_active";
    case AssetTraceStagingPhase::MapActive: return "map_active";
    case AssetTraceStagingPhase::Frontend: return "frontend";
    default: return "unknown";
    }
}

const char* LabelSourceName(AssetTraceLabelSource source) noexcept
{
    switch (source) {
    case AssetTraceLabelSource::OnDestroyBinding:
        return "on_destroy_binding";
    case AssetTraceLabelSource::TransitionBinding:
        return "transition_binding";
    case AssetTraceLabelSource::ActiveMapAnnotation:
        return "active_map_annotation";
    default: return "unknown";
    }
}

const char* WatchedScriptOutcomeName(WatchedScriptOutcome outcome) noexcept
{
    switch (outcome) {
    case WatchedScriptOutcome::ParsedNew: return "parsed_new";
    case WatchedScriptOutcome::ParsedReplaced: return "parsed_replaced";
    case WatchedScriptOutcome::ParsedMerged: return "parsed_merged";
    case WatchedScriptOutcome::CacheHitSameEpoch:
        return "cache_hit_same_epoch";
    case WatchedScriptOutcome::CacheHitPriorEpoch:
        return "cache_hit_prior_epoch";
    case WatchedScriptOutcome::RegistrationConflict:
        return "registration_conflict";
    case WatchedScriptOutcome::ParseFailed: return "parse_failed";
    case WatchedScriptOutcome::CorrelationUnknown:
        return "correlation_unknown";
    default: return "unknown";
    }
}

WatchedScriptOutcome ClassifyWatchedScriptObservation(
    const WatchedScriptClassificationSignals& signals) noexcept
{
    if (!signals.correlation_exact)
        return WatchedScriptOutcome::CorrelationUnknown;
    if (signals.registration_conflict)
        return WatchedScriptOutcome::RegistrationConflict;
    if (signals.cache_hit)
        return signals.prior_epoch_object
            ? WatchedScriptOutcome::CacheHitPriorEpoch
            : WatchedScriptOutcome::CacheHitSameEpoch;
    if (!signals.parser_entered || !signals.parser_succeeded)
        return WatchedScriptOutcome::ParseFailed;
    if (signals.registration_merged)
        return WatchedScriptOutcome::ParsedMerged;
    if (signals.registration_replaced)
        return WatchedScriptOutcome::ParsedReplaced;
    return WatchedScriptOutcome::ParsedNew;
}

namespace {

void CampaignHashBytes(std::uint64_t* hash, const void* data,
                       std::size_t size) noexcept
{
    if (!hash || (!data && size)) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        *hash ^= bytes[index];
        *hash *= 1099511628211ULL;
    }
}

void CampaignHashText(std::uint64_t* hash, const char* text,
                      std::size_t capacity) noexcept
{
    if (!hash || !text) return;
    std::size_t length = 0;
    while (length < capacity && text[length]) ++length;
    CampaignHashBytes(hash, &length, sizeof(length));
    CampaignHashBytes(hash, text, length);
}

bool CampaignMapEquals(const BoundedCampaignMap& left,
                       const BoundedCampaignMap& right) noexcept
{
    return left.complete && right.complete &&
        left.map_id == right.map_id &&
        BoundedStringsEqual(left.map_name, right.map_name,
                            sizeof(left.map_name)) &&
        BoundedStringsEqual(left.level_name, right.level_name,
                            sizeof(left.level_name));
}

bool CampaignSnapshotAuthoritative(
    const CampaignStateSnapshot& snapshot) noexcept
{
    return (snapshot.completeness_flags & CampaignSnapshotComplete) != 0 &&
        snapshot.map_count <= kMaximumObservedCampaignMaps;
}

}  // namespace

std::uint64_t FingerprintCampaignState(
    const CampaignStateSnapshot& snapshot) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    CampaignHashBytes(&hash, &snapshot.campaign_id,
                      sizeof(snapshot.campaign_id));
    CampaignHashBytes(&hash, &snapshot.campaign_flags,
                      sizeof(snapshot.campaign_flags));
    CampaignHashBytes(&hash, &snapshot.campaign_flag0,
                      sizeof(snapshot.campaign_flag0));
    CampaignHashBytes(&hash, &snapshot.campaign_flag1,
                      sizeof(snapshot.campaign_flag1));
    CampaignHashBytes(&hash, &snapshot.map_count,
                      sizeof(snapshot.map_count));
    CampaignHashBytes(&hash, &snapshot.rewards_count,
                      sizeof(snapshot.rewards_count));
    CampaignHashBytes(&hash, &snapshot.after_respawn_count,
                      sizeof(snapshot.after_respawn_count));
    CampaignHashText(&hash, snapshot.campaign_name,
                     sizeof(snapshot.campaign_name));
    CampaignHashText(&hash, snapshot.campaign_source,
                     sizeof(snapshot.campaign_source));
    CampaignHashText(&hash, snapshot.rewards,
                     sizeof(snapshot.rewards));
    CampaignHashText(&hash, snapshot.after_respawn0,
                     sizeof(snapshot.after_respawn0));
    CampaignHashText(&hash, snapshot.after_respawn1,
                     sizeof(snapshot.after_respawn1));
    const std::uint32_t count =
        snapshot.map_count < kMaximumObservedCampaignMaps
            ? snapshot.map_count
            : static_cast<std::uint32_t>(kMaximumObservedCampaignMaps);
    for (std::uint32_t index = 0; index < count; ++index) {
        const BoundedCampaignMap& map = snapshot.maps[index];
        CampaignHashBytes(&hash, &map.registration_order,
                          sizeof(map.registration_order));
        CampaignHashBytes(&hash, &map.map_id, sizeof(map.map_id));
        CampaignHashText(&hash, map.map_name, sizeof(map.map_name));
        CampaignHashText(&hash, map.level_name, sizeof(map.level_name));
    }
    return hash;
}

CampaignLoadPersistenceOutcome ClassifyCampaignPersistence(
    const CampaignStateSnapshot& before,
    const CampaignStateSnapshot& after) noexcept
{
    if (!CampaignSnapshotAuthoritative(before) ||
        !CampaignSnapshotAuthoritative(after))
        return CampaignLoadPersistenceOutcome::ObservationIncomplete;
    if (before.selected_campaign_index != after.selected_campaign_index)
        return CampaignLoadPersistenceOutcome::DifferentSelectedCampaign;
    if (before.selected_campaign != after.selected_campaign)
        return CampaignLoadPersistenceOutcome::NewCampaignObject;
    if (before.map_count == 0 && before.rewards_count == 0 &&
        before.after_respawn_count == 0)
        return CampaignLoadPersistenceOutcome::ReusedEmptyCampaign;
    if (after.map_count == 0 && after.rewards_count == 0 &&
        after.after_respawn_count == 0)
        return CampaignLoadPersistenceOutcome::ReusedClearedCampaign;
    if (before.logical_fingerprint == after.logical_fingerprint)
        return CampaignLoadPersistenceOutcome::ReusedMergedCampaign;

    std::uint32_t overlap = 0;
    bool ordered_prefix = before.map_count <= after.map_count;
    for (std::uint32_t left = 0; left < before.map_count; ++left) {
        bool found = false;
        for (std::uint32_t right = 0; right < after.map_count; ++right) {
            if (CampaignMapEquals(before.maps[left], after.maps[right])) {
                found = true;
                ++overlap;
                break;
            }
        }
        if (!found || left >= after.map_count ||
            !CampaignMapEquals(before.maps[left], after.maps[left]))
            ordered_prefix = false;
    }
    if (overlap == before.map_count && ordered_prefix &&
        after.map_count > before.map_count)
        return CampaignLoadPersistenceOutcome::ReusedAppendedCampaign;
    if (overlap)
        return CampaignLoadPersistenceOutcome::ReusedMergedCampaign;
    return CampaignLoadPersistenceOutcome::ReusedReplacedCampaign;
}

const char* CampaignPersistenceOutcomeName(
    CampaignLoadPersistenceOutcome outcome) noexcept
{
    switch (outcome) {
    case CampaignLoadPersistenceOutcome::NewCampaignObject:
        return "new_campaign_object";
    case CampaignLoadPersistenceOutcome::ReusedEmptyCampaign:
        return "reused_empty_campaign";
    case CampaignLoadPersistenceOutcome::ReusedClearedCampaign:
        return "reused_cleared_campaign";
    case CampaignLoadPersistenceOutcome::ReusedReplacedCampaign:
        return "reused_replaced_campaign";
    case CampaignLoadPersistenceOutcome::ReusedMergedCampaign:
        return "reused_merged_campaign";
    case CampaignLoadPersistenceOutcome::ReusedAppendedCampaign:
        return "reused_appended_campaign";
    case CampaignLoadPersistenceOutcome::DifferentSelectedCampaign:
        return "different_selected_campaign";
    case CampaignLoadPersistenceOutcome::PriorEpochEntriesRemain:
        return "prior_epoch_entries_remain";
    case CampaignLoadPersistenceOutcome::NaturalResetObserved:
        return "natural_reset_observed";
    default: return "observation_incomplete";
    }
}

const char* CampaignSnapshotPhaseName(CampaignSnapshotPhase phase) noexcept
{
    switch (phase) {
    case CampaignSnapshotPhase::BeforeLoad: return "before_load";
    case CampaignSnapshotPhase::AfterLoad: return "after_load";
    case CampaignSnapshotPhase::Frontend: return "frontend";
    default: return "unknown";
    }
}

const char* PackCollectionStateName(PackCollectionState state) noexcept
{
    switch (state) {
    case PackCollectionState::Empty: return "empty";
    case PackCollectionState::ReadableNonempty:
        return "readable_nonempty";
    case PackCollectionState::CountOutOfBounds:
        return "count_out_of_bounds";
    case PackCollectionState::DataPointerUnreadable:
        return "data_pointer_unreadable";
    case PackCollectionState::ElementUnreadable:
        return "element_unreadable";
    case PackCollectionState::LayoutValidationFailed:
        return "layout_validation_failed";
    case PackCollectionState::SnapshotTruncated:
        return "snapshot_truncated";
    default: return "unknown";
    }
}

const char* PackCrossCheckOutcomeName(PackCrossCheckOutcome outcome) noexcept
{
    switch (outcome) {
    case PackCrossCheckOutcome::CollectionAndDynamicMatch:
        return "collection_and_dynamic_match";
    case PackCrossCheckOutcome::ValidatedEmptyCollection:
        return "validated_empty_collection";
    case PackCrossCheckOutcome::DynamicPackMissingFromSnapshot:
        return "dynamic_pack_missing_from_snapshot";
    case PackCrossCheckOutcome::SnapshotPackWithoutDynamicRelease:
        return "snapshot_pack_without_dynamic_release";
    default: return "layout_observation_incomplete";
    }
}

PackCrossCheckOutcome ClassifyPackCrossCheck(
    PackCollectionState state, std::uint32_t static_count,
    std::uint32_t dynamic_eviction_count,
    std::uint32_t dynamic_release_count,
    std::uint32_t dynamic_missing_from_snapshot,
    std::uint32_t static_missing_release,
    bool dynamic_observation_complete) noexcept
{
    if (state == PackCollectionState::Empty && !static_count &&
        !dynamic_eviction_count && !dynamic_release_count &&
        dynamic_observation_complete)
        return PackCrossCheckOutcome::ValidatedEmptyCollection;
    if (state != PackCollectionState::ReadableNonempty ||
        !dynamic_observation_complete)
        return PackCrossCheckOutcome::LayoutObservationIncomplete;
    if (dynamic_missing_from_snapshot)
        return PackCrossCheckOutcome::DynamicPackMissingFromSnapshot;
    if (static_missing_release)
        return PackCrossCheckOutcome::SnapshotPackWithoutDynamicRelease;
    return PackCrossCheckOutcome::CollectionAndDynamicMatch;
}

std::uint32_t EventDropDomainMask(AssetTraceEventType type) noexcept
{
    switch (type) {
    case AssetTraceEventType::FsOpenWatchedEnter:
    case AssetTraceEventType::FsOpenWatchedResult:
        return AssetTraceDropWatchedOpen;
    case AssetTraceEventType::FsAddSourceEnter:
    case AssetTraceEventType::FsAddSourceExit:
    case AssetTraceEventType::FsRemoveSourceEnter:
    case AssetTraceEventType::FsRemoveSourceExit:
        return AssetTraceDropFilesystem;
    case AssetTraceEventType::GameLoadPackEnter:
    case AssetTraceEventType::GameLoadPackExit:
    case AssetTraceEventType::GameFreePackEnter:
    case AssetTraceEventType::GameFreePackExit:
    case AssetTraceEventType::AssetRegRpackEnter:
    case AssetTraceEventType::AssetRegRpackExit:
    case AssetTraceEventType::AssetUnregRpackEnter:
    case AssetTraceEventType::AssetUnregRpackExit:
    case AssetTraceEventType::AssetReleasePackEnter:
    case AssetTraceEventType::AssetReleasePackExit:
        return AssetTraceDropResource;
    case AssetTraceEventType::LevelReleasePacksEnter:
    case AssetTraceEventType::LevelReleasePacksPack:
    case AssetTraceEventType::LevelReleasePacksExit:
    case AssetTraceEventType::LevelReleasePacksCrossCheck:
        return AssetTraceDropPackCollection;
    case AssetTraceEventType::OnDemandEvictPackEnter:
    case AssetTraceEventType::OnDemandEvictPackExit:
    case AssetTraceEventType::LevelPackInternalReleaseEnter:
    case AssetTraceEventType::LevelPackInternalReleaseExit:
        return AssetTraceDropPackDynamic;
    case AssetTraceEventType::ScriptLoaderWatchedEnter:
    case AssetTraceEventType::ScriptLoaderWatchedCacheHit:
    case AssetTraceEventType::ScriptLoaderWatchedParse:
    case AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict:
    case AssetTraceEventType::ScriptLoaderWatchedExit:
        return AssetTraceDropParserCache;
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignManagerSlotSnapshot:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignSnapshotMap:
    case AssetTraceEventType::CampaignAddMapEnter:
    case AssetTraceEventType::CampaignAddMapExit:
    case AssetTraceEventType::CampaignRewardsMutationObserved:
    case AssetTraceEventType::CampaignAfterRespawnMutationObserved:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignNaturalInitEnter:
    case AssetTraceEventType::CampaignNaturalInitExit:
    case AssetTraceEventType::CampaignNaturalReloadEnter:
    case AssetTraceEventType::CampaignNaturalReloadExit:
    case AssetTraceEventType::CampaignNaturalClearEnter:
    case AssetTraceEventType::CampaignNaturalClearExit:
    case AssetTraceEventType::CampaignNaturalDestroyEnter:
    case AssetTraceEventType::CampaignNaturalDestroyExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
    case AssetTraceEventType::CampaignCarryoverObserved:
    case AssetTraceEventType::CampaignObservationUnknown:
        return AssetTraceDropCampaign;
    case AssetTraceEventType::SessionOnDestroyEnter:
    case AssetTraceEventType::OnDestroyOwnedUnloadObserved:
    case AssetTraceEventType::OwnedFrontendUnloadCompleted:
    case AssetTraceEventType::OwnedReleasePacksEnter:
    case AssetTraceEventType::OwnedReleasePacksCompleted:
    case AssetTraceEventType::OutgoingLevelReleaseObserved:
    case AssetTraceEventType::SessionOnDestroyOriginalReturned:
    case AssetTraceEventType::CancelledPresentationDetected:
    case AssetTraceEventType::CancelledCleanupBegin:
    case AssetTraceEventType::CancelledCleanupSourceRemoval:
    case AssetTraceEventType::CancelledCleanupVerification:
    case AssetTraceEventType::CancelledEpochConsumed:
    case AssetTraceEventType::FreshPresentationEpochCreated:
    case AssetTraceEventType::ChangeAfterFadeAttestation:
    case AssetTraceEventType::PendingPostC10TokenPublished:
    case AssetTraceEventType::C10OriginalReturned:
    case AssetTraceEventType::PostC10SessionSlotSampled:
    case AssetTraceEventType::PendingPostC10TokenConsumed:
    case AssetTraceEventType::PostC10PreflightBegin:
    case AssetTraceEventType::PostC10PlayerPass1:
    case AssetTraceEventType::PostC10PlayerPass2:
    case AssetTraceEventType::PostC10PlayerAssessment:
    case AssetTraceEventType::PostC10AudioAssessment:
    case AssetTraceEventType::PostC10TransactionPreparationResult:
    case AssetTraceEventType::PostC10PreflightResult:
    case AssetTraceEventType::C10ScopeCleared:
    case AssetTraceEventType::FrontendSourceResetBegin:
    case AssetTraceEventType::FrontendSourceResetEnd:
    case AssetTraceEventType::FirstFrontendPackOrModuleLookup:
    case AssetTraceEventType::FirstFrontendLookupDisposition:
    case AssetTraceEventType::ShowMenuMainDisposition:
    case AssetTraceEventType::FrontendLookupAfterUnboundOnDestroy:
        return AssetTraceDropRollback;
    case AssetTraceEventType::MaterialReloadEnter:
    case AssetTraceEventType::MaterialLocalPackOpen:
    case AssetTraceEventType::MaterialPackLoadEnter:
    case AssetTraceEventType::MaterialPackLoadExit:
    case AssetTraceEventType::MaterialPackSelected:
    case AssetTraceEventType::MaterialExistingObjectRebind:
    case AssetTraceEventType::MaterialGlobalFallback:
    case AssetTraceEventType::MaterialOldPackDestructorEnter:
    case AssetTraceEventType::MaterialOldPackDestructorExit:
    case AssetTraceEventType::MaterialReloadExit:
    case AssetTraceEventType::PostSupersessionAssetsOpen:
        return AssetTraceDropMaterial;
    case AssetTraceEventType::PlayerTemplateSelection:
    case AssetTraceEventType::ProgressionXmlLoadEnter:
    case AssetTraceEventType::ProgressionXmlLoadExit:
    case AssetTraceEventType::ProgressionDefinitionOverlay:
    case AssetTraceEventType::PlayerVariablesAfterTemplateApply:
    case AssetTraceEventType::PlayerStateOwnerObserved:
    case AssetTraceEventType::PlayerStateDestroyed:
    case AssetTraceEventType::PlayerStateClearEnter:
    case AssetTraceEventType::PlayerStateClearExit:
    case AssetTraceEventType::PlayerStateRemovePrecachedEnter:
    case AssetTraceEventType::PlayerStateRemovePrecachedExit:
    case AssetTraceEventType::PlayerStateInitializeEnter:
    case AssetTraceEventType::PlayerStateInitializeExit:
    case AssetTraceEventType::PlayerPresetCacheAppend:
    case AssetTraceEventType::PlayerPresetRecordRebuild:
    case AssetTraceEventType::PlayerPresetCacheResetEnter:
    case AssetTraceEventType::PlayerPresetCacheResetExit:
    case AssetTraceEventType::PlayerPresetMembershipClassified:
    case AssetTraceEventType::SameSelectorProviderChanged:
    case AssetTraceEventType::SameSelectorProviderChangeRebuildSkipped:
    case AssetTraceEventType::StaleProgressionDefinitionRetained:
    case AssetTraceEventType::PlayerAppearanceParseEnter:
    case AssetTraceEventType::PlayerAppearanceParseExit:
    case AssetTraceEventType::ReloadPlayerScriptsEnter:
    case AssetTraceEventType::ReloadPlayerScriptsExit:
    case AssetTraceEventType::RecreateRepresentationEnter:
    case AssetTraceEventType::RecreateRepresentationExit:
    case AssetTraceEventType::PlayerStateLoadEnter:
    case AssetTraceEventType::PlayerStateLoadClearCall:
    case AssetTraceEventType::PlayerStateLoadAfterClear:
    case AssetTraceEventType::PlayerStateLoadExit:
    case AssetTraceEventType::PlayerPresetProviderRepairPending:
    case AssetTraceEventType::PlayerPresetRebuildPreflightFailed:
    case AssetTraceEventType::PlayerPresetTargetRecordMembership:
    case AssetTraceEventType::ExperimentalPlayerPresetRebuildBegin:
    case AssetTraceEventType::ExperimentalPlayerPresetRecordRebuilt:
    case AssetTraceEventType::ExperimentalPlayerPresetRebuildEnd:
    case AssetTraceEventType::PlayerPresetNaturalClearApplied:
    case AssetTraceEventType::PlayerPresetProviderRestored:
    case AssetTraceEventType::SharedStateLiveValueUnknown:
        return AssetTraceDropPlayerProgression;
    case AssetTraceEventType::ActiveLevelObserved:
        return AssetTraceDropAnnotation;
    case AssetTraceEventType::QueueOverflow:
        return AssetTraceDropDiagnostic;
    default:
        return AssetTraceDropTransition;
    }
}

bool FormatEventLine(const AssetTraceEvent& event, char* output,
                     std::size_t output_capacity,
                     std::size_t* written) noexcept
{
    TextBuilder builder;
    const char* text0_key;
    const char* text1_key;
    const char* text2_key;
    const char* text3_key;
    const char* text4_key = "text4";
    const char* value0_key;
    const char* value1_key;
    BuilderInitialize(&builder, output, output_capacity);
    BuilderAppendLiteral(&builder, "seq=");
    BuilderAppendUnsigned(&builder, event.sequence);
    BuilderAppendKeyUnsigned(&builder, "qpc", event.qpc);
    BuilderAppendKeyUnsigned(&builder, "thread", event.thread_id);
    BuilderAppendKeyUnsigned(&builder, "gen", event.transition_generation);
    BuilderAppendKeyUnsigned(&builder, "epoch", event.content_epoch);
    BuilderAppendKeyUnsigned(&builder, "source_record",
                             event.source_record_index);
    BuilderAppendKeyUnsigned(&builder, "source_ordinal",
                             event.source_add_ordinal);
    BuilderAppendKeyQuoted(&builder, "source_kind",
                           SourceKindName(event.source_kind), 32);
    BuilderAppendKeyUnsigned(&builder, "attribution",
                             event.attribution_flags);
    BuilderAppendKeyUnsigned(&builder, "update_dlc_depth",
                             event.update_dlc_depth);
    BuilderAppendKeyQuoted(&builder, "staging",
                           StagingPhaseName(event.staging_phase), 32);
    BuilderAppendKeyQuoted(&builder, "label_source",
                           LabelSourceName(event.label_source), 32);
    BuilderAppendKeyQuoted(&builder, "script_outcome",
                           WatchedScriptOutcomeName(event.script_outcome),
                           40);
    BuilderAppendKeyQuoted(&builder, "campaign_phase",
                           CampaignSnapshotPhaseName(event.campaign_phase),
                           32);
    BuilderAppendKeyUnsigned(&builder, "campaign_slot_selected",
                             event.campaign_slot_selected);
    BuilderAppendKeyUnsigned(&builder, "observation_generation",
                             event.observation_generation);
    BuilderAppendKeyUnsigned(&builder, "dropped_domain_mask",
                             event.dropped_domain_mask);
    BuilderAppendKeyQuoted(&builder, "caller_module",
                           event.caller_module,
                           sizeof(event.caller_module));
    BuilderAppendKeyUnsigned(&builder, "caller_rva", event.caller_rva);
    BuilderAppendKeyQuoted(&builder, "category", EventCategoryName(event.type),
                           32);
    BuilderAppendKeyQuoted(&builder, "event", EventTypeName(event.type), 64);
    BuilderAppendLiteral(&builder, " object=");
    BuilderAppendHex(&builder, event.object);
    BuilderAppendLiteral(&builder, " first=");
    BuilderAppendHex(&builder, event.first);
    BuilderAppendLiteral(&builder, " second=");
    BuilderAppendHex(&builder, event.second);
    BuilderAppendLiteral(&builder, " third=");
    BuilderAppendHex(&builder, event.third);
    EventValueKeys(event.type, &value0_key, &value1_key);
    BuilderAppendKeyUnsigned(&builder, value0_key, event.value0);
    BuilderAppendKeyUnsigned(&builder, value1_key, event.value1);
    BuilderAppendKeyUnsigned(&builder, "detail0", event.detail0);
    BuilderAppendKeyUnsigned(&builder, "detail1", event.detail1);
    BuilderAppendKeyUnsigned(&builder, "detail2", event.detail2);
    BuilderAppendKeyUnsigned(&builder, "detail3", event.detail3);
    EventTextKeys(event.type, &text0_key, &text1_key);
    BuilderAppendKeyQuoted(&builder, text0_key, event.text0,
                           sizeof(event.text0));
    BuilderAppendKeyQuoted(&builder, text1_key, event.text1,
                           sizeof(event.text1));
    EventExtraTextKeys(event.type, &text2_key, &text3_key);
    switch (event.type) {
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignManagerSlotSnapshot:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
    case AssetTraceEventType::CampaignCarryoverObserved:
    case AssetTraceEventType::CampaignObservationUnknown:
        text4_key = "manifest_provider";
        break;
    default:
        break;
    }
    BuilderAppendKeyQuoted(&builder, text2_key, event.text2,
                           sizeof(event.text2));
    BuilderAppendKeyQuoted(&builder, text3_key, event.text3,
                           sizeof(event.text3));
    BuilderAppendKeyQuoted(&builder, text4_key, event.text4,
                           sizeof(event.text4));
    BuilderAppendChar(&builder, '\n');
    return BuilderFinish(&builder, written);
}

bool FormatHeaderLine(const AssetTraceHeader& header,
                      const AssetTraceWatchList& watch_list, char* output,
                      std::size_t output_capacity,
                      std::size_t* written) noexcept
{
    TextBuilder builder;
    BuilderInitialize(&builder, output, output_capacity);
    BuilderAppendLiteral(&builder,
                         "trace=custom_map_asset_trace format=3");
    BuilderAppendKeyQuoted(&builder, "loader_version", header.loader_version,
                           sizeof(header.loader_version));
    BuilderAppendKeyUnsigned(&builder, "process", header.process_id);
    BuilderAppendKeyUnsigned(&builder, "qpc_frequency",
                             header.qpc_frequency);
    BuilderAppendKeyUnsigned(&builder, "trace_start_qpc",
                             header.trace_start_qpc);
    BuilderAppendKeyQuoted(&builder, "trace_start_time",
                           header.trace_start_time,
                           sizeof(header.trace_start_time));
    BuilderAppendKeyQuoted(&builder, "gamedll", header.gamedll_identity,
                           sizeof(header.gamedll_identity));
    BuilderAppendKeyQuoted(&builder, "engine", header.engine_identity,
                           sizeof(header.engine_identity));
    BuilderAppendKeyQuoted(&builder, "filesystem",
                           header.filesystem_identity,
                           sizeof(header.filesystem_identity));
    BuilderAppendKeyQuoted(&builder, "enabled_hooks", header.enabled_hooks,
                           sizeof(header.enabled_hooks));
    BuilderAppendKeyUnsigned(&builder, "watched_count", watch_list.count);
    for (std::uint32_t i = 0; i < watch_list.count; ++i) {
        char key[16] = "watch";
        std::size_t key_length = 5;
        std::uint32_t value = i + 1;
        char reverse[10];
        std::size_t digits = 0;
        do {
            reverse[digits++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value && digits < sizeof(reverse));
        while (digits && key_length + 1 < sizeof(key))
            key[key_length++] = reverse[--digits];
        key[key_length] = 0;
        BuilderAppendKeyQuoted(&builder, key, watch_list.paths[i],
                               kEventTextCapacity);
    }
    BuilderAppendChar(&builder, '\n');
    return BuilderFinish(&builder, written);
}

void ResetSummary(AssetTraceSummary* summary,
                  bool on_demand_hook_available,
                  bool script_hook_available) noexcept
{
    if (!summary) return;
    std::memset(summary, 0, sizeof(*summary));
    summary->on_demand_hook_available = on_demand_hook_available;
    summary->script_hook_available = script_hook_available;
    summary->ownership_ledger_complete = true;
    summary->source_identity_complete = true;
    summary->source_reset_evidence_complete = true;
    summary->broad_file_ledger_complete = true;
    for (std::size_t index = 0; index < kTraceLossDomainCount; ++index)
        summary->loss_domains[index].domain_mask = 1U << index;
}

void ConfigureSummaryObservers(
    AssetTraceSummary* summary, bool campaign_hook_available,
    bool internal_pack_release_hook_available) noexcept
{
    if (!summary) return;
    summary->campaign_hook_available = campaign_hook_available;
    summary->internal_pack_release_hook_available =
        internal_pack_release_hook_available;
}

void ConfigureSummaryReportState(
    AssetTraceSummary* summary,
    const InstalledObserverCapabilities& capabilities,
    const CurrentObserverAdmission& admission,
    std::uint64_t report_generation, bool report_finalized,
    std::uint64_t trace_records_drained_through_sequence,
    std::uint64_t ledger_snapshot_sequence,
    std::uint64_t trace_summary_sequence) noexcept
{
    if (!summary) return;
    summary->installed_capabilities = capabilities;
    summary->current_admission = admission;
    summary->report_generation = report_generation;
    summary->report_finalized = report_finalized;
    summary->trace_records_drained_through_sequence =
        trace_records_drained_through_sequence;
    summary->ledger_snapshot_sequence = ledger_snapshot_sequence;
    summary->trace_summary_sequence = trace_summary_sequence;
}

void ConfigureSummaryLedgerCompleteness(
    AssetTraceSummary* summary, bool ownership_ledger_complete,
    bool source_identity_complete, bool source_reset_evidence_complete,
    std::uint32_t broad_file_loss_count,
    std::uint64_t broad_file_first_loss_sequence,
    std::uint64_t broad_file_last_loss_sequence,
    const char* broad_file_loss_reason) noexcept
{
    if (!summary) return;
    summary->ownership_ledger_complete = ownership_ledger_complete;
    summary->source_identity_complete = source_identity_complete;
    summary->source_reset_evidence_complete =
        source_reset_evidence_complete;
    summary->broad_file_loss_count = broad_file_loss_count;
    summary->broad_file_ledger_complete = broad_file_loss_count == 0;
    summary->broad_file_first_loss_sequence =
        broad_file_first_loss_sequence;
    summary->broad_file_last_loss_sequence =
        broad_file_last_loss_sequence;
    AssetTraceLossDomainSummary& broad =
        summary->loss_domains[11];
    broad.domain_mask = AssetTraceDropBroadFileOpen;
    broad.count += broad_file_loss_count;
    if (broad_file_first_loss_sequence &&
        (!broad.first_known_sequence ||
         broad_file_first_loss_sequence < broad.first_known_sequence))
        broad.first_known_sequence = broad_file_first_loss_sequence;
    if (broad_file_last_loss_sequence > broad.last_known_sequence)
        broad.last_known_sequence = broad_file_last_loss_sequence;
    CopyBounded(summary->broad_file_loss_reason,
                sizeof(summary->broad_file_loss_reason),
                broad_file_loss_reason,
                broad_file_loss_reason
                    ? std::strlen(broad_file_loss_reason) + 1U
                    : 0U);
}

void CaptureLossDomainStats(AssetTraceSummary* summary) noexcept
{
    if (!summary) return;
    summary->critical_trace_loss = CriticalTraceLossCount();
    summary->optional_trace_loss = OptionalTraceLossCount();
    (void)SnapshotTransactionEvidence(
        &summary->transaction_evidence);
    summary->critical_trace_reserved_capacity =
        static_cast<std::uint32_t>(CriticalTraceReservedCapacity());
    const bool locked = TryQueueLock();
    for (std::size_t index = 0; index < kTraceLossDomainCount; ++index) {
        AssetTraceLossDomainSummary& domain =
            summary->loss_domains[index];
        domain.domain_mask = 1U << index;
        domain.count = static_cast<std::uint32_t>(
            dluma_atomic_compare_exchange(
                &g_domain_drop_counts[index], 0, 0));
        domain.sequence_unknown =
            dluma_atomic_compare_exchange(
                &g_domain_drop_sequence_unknown[index], 0, 0) != 0 ||
            (!locked && domain.count != 0);
        if (locked) {
            domain.first_known_sequence =
                g_domain_first_known_sequence[index];
            domain.last_known_sequence =
                g_domain_last_known_sequence[index];
        } else {
            domain.first_known_sequence = 0;
            domain.last_known_sequence = 0;
        }
    }
    if (locked) UnlockQueue();
}

CampaignObservationCompleteness EvaluateCampaignCompleteness(
    const AssetTraceSummary& summary) noexcept
{
    CampaignObservationCompleteness result = {};
    result.observer_installed =
        summary.installed_capabilities.campaign ||
        summary.campaign_hook_available;
    CopyBounded(result.frontend_state, sizeof(result.frontend_state),
                result.observer_installed ? "frontend_manager_unavailable"
                                          : "observer_unavailable",
                result.observer_installed ? 29U : 21U);
    if (!result.observer_installed) return result;

    const bool common_complete =
        !summary.campaign_loads_truncated &&
        (summary.dropped_domain_mask & AssetTraceDropCampaign) == 0;
    bool custom_seen = false;
    bool frontend_seen = false;
    bool base_seen = false;
    bool custom_complete = common_complete;
    bool frontend_complete = common_complete;
    bool base_complete = common_complete;
    bool live_manager_seen = false;
    bool frontend_manager_present = false;
    for (std::uint32_t index = 0;
         index < summary.campaign_load_count; ++index) {
        const AssetTraceCampaignLoadSummary& campaign =
            summary.campaign_loads[index];
        if (!campaign.frontend_snapshot && campaign.manager)
            live_manager_seen = true;
        const bool row_complete =
            campaign.before_complete && campaign.after_complete &&
            !campaign.maps_truncated &&
            campaign.outcome !=
                CampaignLoadPersistenceOutcome::ObservationIncomplete;
        if (campaign.frontend_snapshot) {
            frontend_seen = true;
            frontend_manager_present = campaign.manager != 0;
            if (!campaign.after_complete || campaign.maps_truncated)
                frontend_complete = false;
        } else if (campaign.epoch != 0) {
            custom_seen = true;
            if (!row_complete) custom_complete = false;
        } else {
            base_seen = true;
            if (!row_complete) base_complete = false;
        }
    }
    result.custom_complete = custom_seen && custom_complete;
    result.frontend_complete = frontend_seen && frontend_complete;
    result.base_complete = base_seen && base_complete;
    if (result.frontend_complete) {
        CopyBounded(result.frontend_state, sizeof(result.frontend_state),
                    "complete", 9U);
    } else if (frontend_seen && frontend_manager_present) {
        CopyBounded(result.frontend_state, sizeof(result.frontend_state),
                    "frontend_snapshot_unresolved", 29U);
    } else if (live_manager_seen) {
        CopyBounded(result.frontend_state, sizeof(result.frontend_state),
                    "frontend_manager_destroyed", 27U);
    }
    return result;
}

void ConsumeSummaryEvent(AssetTraceSummary* summary,
                          const AssetTraceEvent& event) noexcept
{
    if (!summary) return;
    if (event.type == AssetTraceEventType::CampaignObservationUnknown)
        summary->campaign_observation_unknown = true;
    if (event.type == AssetTraceEventType::QueueOverflow) {
        summary->queue_overflow_observed = true;
        summary->overflow_dropped_events += event.value0;
        summary->dropped_domain_mask |= event.dropped_domain_mask;
        if (!event.first || !event.second) {
            summary->drop_sequence_unknown = true;
            summary->first_drop_sequence = 0;
            summary->last_drop_sequence = 0;
        } else if (!summary->drop_sequence_unknown) {
            const std::uint64_t first =
                static_cast<std::uint64_t>(event.first);
            const std::uint64_t last =
                static_cast<std::uint64_t>(event.second);
            if (!summary->first_drop_sequence ||
                first < summary->first_drop_sequence)
                summary->first_drop_sequence = first;
            if (last > summary->last_drop_sequence)
                summary->last_drop_sequence = last;
        }
        return;
    }

    switch (event.type) {
    case AssetTraceEventType::MaterialReloadEnter:
        summary->material_reload_entered = true;
        break;
    case AssetTraceEventType::MaterialLocalPackOpen:
        summary->material_local_pack_opened = true;
        break;
    case AssetTraceEventType::MaterialPackSelected:
        summary->material_pack_selected = true;
        break;
    case AssetTraceEventType::MaterialOldPackDestructorExit:
        summary->material_old_pack_destructor_completed = true;
        break;
    case AssetTraceEventType::MaterialReloadExit:
        summary->material_reload_exited = true;
        break;
    case AssetTraceEventType::MaterialExistingObjectRebind:
        ++summary->material_rebind_count;
        break;
    case AssetTraceEventType::MaterialGlobalFallback:
        ++summary->material_global_fallback_count;
        break;
    case AssetTraceEventType::PostSupersessionAssetsOpen:
        summary->post_supersession_assets_opened = true;
        break;
    case AssetTraceEventType::PlayerTemplateSelection:
        summary->player_template_selected = true;
        break;
    case AssetTraceEventType::ProgressionDefinitionOverlay:
        ++summary->progression_overlay_count;
        if (event.detail0 == 0)
            summary->progression_base_overlay_observed = true;
        else if (event.detail0 == 1 || event.detail0 == 2)
            summary->progression_custom_overlay_observed = true;
        break;
    case AssetTraceEventType::PlayerVariablesAfterTemplateApply:
        summary->player_variables_after_template_observed = true;
        break;
    default:
        break;
    }

    if (event.type == AssetTraceEventType::FsOpenWatchedResult) {
        if (summary->watched_open_count < kMaxSummaryWatchedOpens) {
            AssetTraceWatchedResolution* resolution =
                &summary->watched_opens[summary->watched_open_count++];
            std::memset(resolution, 0, sizeof(*resolution));
            resolution->sequence = event.sequence;
            resolution->generation = event.transition_generation;
            resolution->content_epoch = event.content_epoch;
            resolution->source_record_index = event.source_record_index;
            resolution->source_add_ordinal = event.source_add_ordinal;
            resolution->caller_rva = event.caller_rva;
            resolution->file_handle = event.object;
            resolution->source = event.first;
            resolution->source_kind = event.source_kind;
            resolution->staging_phase = event.staging_phase;
            resolution->update_dlc_depth = event.update_dlc_depth;
            resolution->result = event.value0;
            CopyBounded(resolution->caller_module,
                        sizeof(resolution->caller_module),
                        event.caller_module,
                        sizeof(event.caller_module));
            CopyBounded(resolution->logical_path,
                        sizeof(resolution->logical_path), event.text0,
                        sizeof(event.text0));
            CopyBounded(resolution->resolved_source,
                        sizeof(resolution->resolved_source), event.text1,
                        sizeof(event.text1));
            CopyBounded(resolution->project_root,
                        sizeof(resolution->project_root), event.text2,
                        sizeof(event.text2));
        } else {
            summary->watched_opens_truncated = true;
        }
    }

    if ((event.type == AssetTraceEventType::ScriptLoaderWatchedExit ||
         event.type ==
             AssetTraceEventType::ScriptLoaderWatchedCacheHit ||
         event.type ==
             AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict) &&
        event.script_outcome != WatchedScriptOutcome::Unknown) {
        if (summary->script_observation_count <
            kMaxSummaryScriptObservations) {
            AssetTraceScriptObservation* observation =
                &summary->script_observations[
                    summary->script_observation_count++];
            std::memset(observation, 0, sizeof(*observation));
            observation->sequence = event.sequence;
            observation->generation = event.transition_generation;
            observation->parser_observation_generation =
                event.observation_generation;
            observation->open_sequence = event.value1;
            observation->input_size = event.value0;
            observation->content_epoch = event.content_epoch;
            observation->source = event.first;
            observation->existing_object = event.second;
            observation->result_object = event.object;
            observation->outcome = event.script_outcome;
            CopyBounded(observation->logical_path,
                        sizeof(observation->logical_path), event.text0,
                        sizeof(event.text0));
            CopyBounded(observation->provider,
                        sizeof(observation->provider), event.text1,
                        sizeof(event.text1));
            CopyBounded(observation->content_hash,
                        sizeof(observation->content_hash), event.text2,
                        sizeof(event.text2));
            CopyBounded(observation->registry_key,
                        sizeof(observation->registry_key), event.text3,
                        sizeof(event.text3));
        } else {
            summary->script_observations_truncated = true;
        }
    }

    switch (event.type) {
    case AssetTraceEventType::CampaignManagerLoadEnter:
    case AssetTraceEventType::CampaignStateBeforeLoad:
    case AssetTraceEventType::CampaignManagerSelection:
    case AssetTraceEventType::CampaignStateAfterLoad:
    case AssetTraceEventType::CampaignManagerLoadExit:
    case AssetTraceEventType::CampaignFrontendSnapshot:
    case AssetTraceEventType::CampaignAddMapExit:
    case AssetTraceEventType::CampaignSnapshotMap: {
        AssetTraceCampaignLoadSummary* campaign =
            FindOrAddCampaignLoad(summary,
                                  event.observation_generation);
        if (!campaign) break;
        campaign->transition_generation = event.transition_generation;
        if (event.content_epoch) campaign->epoch = event.content_epoch;
        if (event.type == AssetTraceEventType::CampaignFrontendSnapshot)
            campaign->frontend_snapshot = true;
        if (event.type == AssetTraceEventType::CampaignManagerLoadEnter ||
            event.type == AssetTraceEventType::CampaignStateBeforeLoad) {
            campaign->manager = event.object;
            campaign->selected_before = event.first;
            campaign->fingerprint_before = event.value0;
            campaign->selected_index_before =
                static_cast<std::uint32_t>(event.value1);
            campaign->table_count_before =
                static_cast<std::uint32_t>(event.value1 >> 32U);
            campaign->map_count_before = event.detail0;
            campaign->rewards_count_before = event.detail1;
            campaign->respawn_count_before = event.detail2;
            campaign->before_complete =
                (event.detail3 & CampaignSnapshotComplete) != 0;
        } else if (event.type ==
                       AssetTraceEventType::CampaignAddMapExit ||
                   event.type ==
                       AssetTraceEventType::CampaignSnapshotMap) {
            const bool retained_snapshot_map = event.type ==
                    AssetTraceEventType::CampaignSnapshotMap &&
                (event.detail3 == static_cast<std::uint32_t>(
                     CampaignSnapshotPhase::AfterLoad) ||
                 event.detail3 == static_cast<std::uint32_t>(
                     CampaignSnapshotPhase::Frontend));
            if (event.type ==
                    AssetTraceEventType::CampaignAddMapExit ||
                retained_snapshot_map) {
                const std::uint32_t order =
                    static_cast<std::uint32_t>(event.value0);
                if (order < kMaximumObservedCampaignMaps) {
                    BoundedCampaignMap* map = &campaign->maps[order];
                    map->registration_order = order;
                    map->map_id = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(event.value1));
                    map->complete = event.detail0 != 0;
                    CopyBounded(map->map_name, sizeof(map->map_name),
                                event.text0, sizeof(event.text0));
                    CopyBounded(map->level_name,
                                sizeof(map->level_name),
                                event.text1, sizeof(event.text1));
                    if (campaign->map_count_stored <= order)
                        campaign->map_count_stored = order + 1U;
                } else {
                    campaign->maps_truncated = true;
                }
            }
        } else {
            campaign->manager = event.object;
            campaign->selected_after = event.first;
            campaign->parser_result = event.second;
            campaign->fingerprint_after = event.value0;
            campaign->selected_index_after =
                static_cast<std::uint32_t>(event.value1);
            campaign->table_count_after =
                static_cast<std::uint32_t>(event.value1 >> 32U);
            campaign->map_count_after = event.detail0;
            campaign->rewards_count_after = event.detail1;
            campaign->respawn_count_after = event.detail2;
            if (event.type !=
                AssetTraceEventType::CampaignManagerLoadExit)
                campaign->after_complete =
                    (event.detail3 & CampaignSnapshotComplete) != 0;
        }
        if (event.type == AssetTraceEventType::CampaignManagerLoadExit)
            campaign->outcome =
                static_cast<CampaignLoadPersistenceOutcome>(event.detail3);
        const bool map_row = event.type ==
                AssetTraceEventType::CampaignAddMapExit ||
            event.type == AssetTraceEventType::CampaignSnapshotMap;
        if (!map_row) {
            CopyBounded(campaign->campaign_name,
                        sizeof(campaign->campaign_name), event.text0,
                        sizeof(event.text0));
            CopyBounded(campaign->manifest_path,
                        sizeof(campaign->manifest_path), event.text1,
                        sizeof(event.text1));
            if (event.text2[0])
                CopyBounded(campaign->project_root,
                            sizeof(campaign->project_root), event.text2,
                            sizeof(event.text2));
            if (event.text3[0])
                CopyBounded(campaign->manifest_hash,
                            sizeof(campaign->manifest_hash), event.text3,
                            sizeof(event.text3));
            campaign->manifest_source = event.third;
            if (event.text4[0])
                CopyBounded(campaign->manifest_provider,
                            sizeof(campaign->manifest_provider),
                            event.text4, sizeof(event.text4));
        }
        break;
    }
    case AssetTraceEventType::LevelReleasePacksEnter:
    case AssetTraceEventType::LevelReleasePacksPack:
    case AssetTraceEventType::LevelReleasePacksExit:
    case AssetTraceEventType::LevelReleasePacksCrossCheck: {
        AssetTracePackReleaseSummary* pack = FindOrAddPackRelease(
            summary, event.observation_generation);
        if (!pack) break;
        pack->transition_generation = event.transition_generation;
        pack->epoch = event.content_epoch;
        pack->level = event.object;
        if (event.type == AssetTraceEventType::LevelReleasePacksEnter) {
            pack->collection_state =
                static_cast<PackCollectionState>(event.detail3);
            pack->static_count = event.detail0;
            pack->collection_complete = event.detail1 != 0;
        } else if (event.type ==
                   AssetTraceEventType::LevelReleasePacksPack) {
            const std::uint32_t row_index =
                static_cast<std::uint32_t>(event.value0);
            if (row_index < kMaxSummaryPacksPerRelease) {
                AssetTracePackReleaseSummary::PackRow& row =
                    pack->packs[row_index];
                row.pack = event.first;
                row.collection_index = row_index;
                row.custom_project_path = event.detail1 != 0;
                CopyBounded(row.name, sizeof(row.name), event.text0,
                            sizeof(event.text0));
                if (pack->pack_count_stored <= row_index)
                    pack->pack_count_stored = row_index + 1U;
            } else {
                pack->pack_rows_truncated = true;
            }
        } else if (event.type ==
                   AssetTraceEventType::LevelReleasePacksCrossCheck) {
            pack->static_count = event.detail0;
            pack->dynamic_eviction_count = event.detail1;
            pack->dynamic_release_count = event.detail2;
            pack->cross_check =
                static_cast<PackCrossCheckOutcome>(event.detail3);
            pack->dynamic_complete =
                BoundedStringsEqual(
                    event.text3, "dynamic-observers-complete",
                    sizeof(event.text3));
        }
        CopyBounded(pack->map, sizeof(pack->map), event.text0,
                    sizeof(event.text0));
        CopyBounded(pack->label_source,
                    sizeof(pack->label_source),
                    LabelSourceName(event.label_source), 64);
        break;
    }
    default: break;
    }

    switch (event.type) {
    case AssetTraceEventType::SessionOnDestroyEnter:
    case AssetTraceEventType::OnDestroyOwnedUnloadObserved:
    case AssetTraceEventType::OwnedFrontendUnloadCompleted:
    case AssetTraceEventType::OwnedReleasePacksCompleted:
    case AssetTraceEventType::OutgoingLevelReleaseObserved:
    case AssetTraceEventType::SessionOnDestroyOriginalReturned:
    case AssetTraceEventType::FrontendSourceResetBegin:
    case AssetTraceEventType::FrontendSourceResetEnd:
    case AssetTraceEventType::FirstFrontendPackOrModuleLookup: {
        AssetTraceRollbackBarrierSummary* barrier =
            FindOrAddRollbackBarrier(
                summary, static_cast<std::uint32_t>(event.value0));
        if (!barrier) break;
        switch (event.type) {
        case AssetTraceEventType::SessionOnDestroyEnter:
            if (!barrier->on_destroy_enter_sequence)
                barrier->on_destroy_enter_sequence = event.sequence;
            break;
        case AssetTraceEventType::OnDestroyOwnedUnloadObserved:
            if (!barrier->owned_unload_sequence)
                barrier->owned_unload_sequence = event.sequence;
            break;
        case AssetTraceEventType::OwnedFrontendUnloadCompleted:
            if (!barrier->owned_unload_completed_sequence)
                barrier->owned_unload_completed_sequence = event.sequence;
            barrier->release_enter_count =
                static_cast<std::uint32_t>(event.value1 >> 32U);
            barrier->release_exit_count =
                static_cast<std::uint32_t>(event.value1);
            break;
        case AssetTraceEventType::OwnedReleasePacksCompleted:
            barrier->outgoing_release_sequence = event.sequence;
            break;
        case AssetTraceEventType::OutgoingLevelReleaseObserved:
            if (!barrier->outgoing_release_sequence)
                barrier->outgoing_release_sequence = event.sequence;
            break;
        case AssetTraceEventType::SessionOnDestroyOriginalReturned:
            if (!barrier->original_return_sequence)
                barrier->original_return_sequence = event.sequence;
            break;
        case AssetTraceEventType::FrontendSourceResetBegin:
            if (!barrier->reset_begin_sequence)
                barrier->reset_begin_sequence = event.sequence;
            break;
        case AssetTraceEventType::FrontendSourceResetEnd:
            if (!barrier->reset_end_sequence)
                barrier->reset_end_sequence = event.sequence;
            barrier->reset_succeeded = (event.value1 & 1U) != 0;
            break;
        case AssetTraceEventType::FirstFrontendPackOrModuleLookup:
            if (!barrier->first_frontend_lookup_sequence) {
                barrier->first_frontend_lookup_sequence = event.sequence;
                barrier->first_lookup_after_reset =
                    (event.value1 & 1U) != 0;
            }
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }

    AssetTraceGenerationSummary* item =
        FindOrAddGeneration(summary, event.transition_generation);
    if (!item) return;

    switch (event.type) {
    case AssetTraceEventType::ChangeActiveLevelEnter:
        item->transition_entered = true;
        break;
    case AssetTraceEventType::ChangeActiveLevelBranch:
        item->branch_observed = true;
        item->branch_result = event.value0;
        if (event.text0[0]) {
            CopyBounded(item->selected_branch,
                        sizeof(item->selected_branch), event.text0,
                        sizeof(event.text0));
        } else {
            CopyLiteral(item->selected_branch,
                        sizeof(item->selected_branch),
                        event.value0 ? "destroy_active_level"
                                     : "alternate_0x4358C1");
        }
        break;
    case AssetTraceEventType::ChangeActiveLevelExit:
        item->transition_exited = true;
        break;
    case AssetTraceEventType::DestroyActiveLevelEnter:
        if (event.value1 & AssetTraceEvidenceTransitionContext)
            item->destroy_active_level_entered = true;
        break;
    case AssetTraceEventType::DestroyActiveLevelExit:
        if (event.value1 & AssetTraceEvidenceTransitionContext) {
            item->destroy_active_level_exited = true;
            UpdateLastCleanup(item, event.sequence);
        }
        break;
    case AssetTraceEventType::UpdateDLCContentEnter:
        if (event.value1 & AssetTraceEvidenceTransitionContext)
            item->update_dlc_content_entered = true;
        break;
    case AssetTraceEventType::UpdateDLCContentExit:
        if (event.value1 & AssetTraceEvidenceTransitionContext) {
            item->update_dlc_content_exited = true;
            UpdateLastCleanup(item, event.sequence);
        }
        break;
    case AssetTraceEventType::UnloadLevelEnter:
        item->unload_level_entered = true;
        break;
    case AssetTraceEventType::UnloadLevelExit:
        item->unload_level_exited = true;
        UpdateLastCleanup(item, event.sequence);
        break;
    case AssetTraceEventType::FsRemoveSourceEnter:
        item->any_source_remove_entered = true;
        break;
    case AssetTraceEventType::FsRemoveSourceExit:
        if (event.value1 & AssetTraceEvidenceAttributionUnknown)
            item->source_removal_attribution_unknown = true;
        if ((event.value1 & AssetTraceEvidenceOutgoing) &&
            (event.value1 & AssetTraceEvidenceOriginalSucceeded)) {
            item->outgoing_source_remove_succeeded = true;
            UpdateLastCleanup(item, event.sequence);
        }
        break;
    case AssetTraceEventType::LevelReleasePacksEnter:
        item->level_release_packs_entered = true;
        if (event.value1 & AssetTraceEvidenceAttributionUnknown)
            item->level_pack_attribution_unknown = true;
        break;
    case AssetTraceEventType::LevelReleasePacksPack:
        if (event.value1 & AssetTraceEvidenceAttributionUnknown)
            item->level_pack_attribution_unknown = true;
        if ((event.value1 & AssetTraceEvidenceOutgoing) && event.first) {
            item->outgoing_level_pack_observed = true;
            RememberPackPointer(item, event.first);
        }
        break;
    case AssetTraceEventType::LevelReleasePacksExit:
        item->level_release_packs_exited = true;
        if (event.value1 & AssetTraceEvidenceOutgoing)
            UpdateLastCleanup(item, event.sequence);
        break;
    case AssetTraceEventType::OnDemandEvictPackExit: {
        const std::uintptr_t pack = event.first ? event.first : event.object;
        if ((event.value1 & AssetTraceEvidenceOutgoing) ||
            PackPointerKnown(*item, pack)) {
            item->outgoing_pack_evicted = true;
            UpdateLastCleanup(item, event.sequence);
        }
        break;
    }
    case AssetTraceEventType::FsOpenWatchedResult:
        if (!item->first_watched_lookup_sequence)
            item->first_watched_lookup_sequence = event.sequence;
        if ((event.value1 & AssetTraceEvidenceDestination) &&
            !item->first_destination_lookup_sequence)
            item->first_destination_lookup_sequence = event.sequence;
        break;
    case AssetTraceEventType::ScriptLoaderWatchedParse:
        if (item->script_outcome == AssetTraceScriptOutcome::Unknown)
            item->script_outcome = AssetTraceScriptOutcome::Parsed;
        break;
    case AssetTraceEventType::ScriptLoaderWatchedCacheHit:
        if (item->script_outcome !=
            AssetTraceScriptOutcome::RegistrationConflict)
            item->script_outcome = AssetTraceScriptOutcome::CacheHit;
        break;
    case AssetTraceEventType::ScriptLoaderWatchedRegistrationConflict:
        item->script_outcome =
            AssetTraceScriptOutcome::RegistrationConflict;
        break;
    case AssetTraceEventType::ScriptLoaderWatchedExit:
        switch (event.script_outcome) {
        case WatchedScriptOutcome::ParsedNew:
        case WatchedScriptOutcome::ParsedReplaced:
        case WatchedScriptOutcome::ParsedMerged:
            item->script_outcome = AssetTraceScriptOutcome::Parsed;
            break;
        case WatchedScriptOutcome::CacheHitSameEpoch:
        case WatchedScriptOutcome::CacheHitPriorEpoch:
            item->script_outcome = AssetTraceScriptOutcome::CacheHit;
            break;
        case WatchedScriptOutcome::RegistrationConflict:
            item->script_outcome =
                AssetTraceScriptOutcome::RegistrationConflict;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

AssetTraceTriState CleanupBeforeDestinationLookup(
    const AssetTraceGenerationSummary& summary) noexcept
{
    if (!summary.first_destination_lookup_sequence)
        return AssetTraceTriState::Unknown;

    const bool complete =
        summary.unload_level_exited &&
        summary.destroy_active_level_exited &&
        summary.update_dlc_content_exited &&
        summary.outgoing_source_remove_succeeded &&
        summary.outgoing_level_pack_observed &&
        summary.level_release_packs_exited;
    if (complete && summary.last_cleanup_sequence)
        return summary.last_cleanup_sequence <
                       summary.first_destination_lookup_sequence
                   ? AssetTraceTriState::Yes
                   : AssetTraceTriState::No;

    if (summary.last_cleanup_sequence >=
        summary.first_destination_lookup_sequence)
        return AssetTraceTriState::No;
    return AssetTraceTriState::Unknown;
}

bool FormatSummary(const AssetTraceSummary& summary, char* output,
                   std::size_t output_capacity,
                   std::size_t* written) noexcept
{
    TextBuilder builder;
    BuilderInitialize(&builder, output, output_capacity);
    BuilderAppendLiteral(&builder,
                         "custom_map_asset_trace_summary format=4\n");
    BuilderAppendLiteral(&builder, "report_generation=");
    BuilderAppendUnsigned(&builder, summary.report_generation);
    BuilderAppendLiteral(&builder, " report_finalized=");
    BuilderAppendLiteral(&builder, summary.report_finalized ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " trace_records_drained_through_sequence=");
    BuilderAppendUnsigned(
        &builder, summary.trace_records_drained_through_sequence);
    BuilderAppendLiteral(&builder, " ledger_snapshot_sequence=");
    BuilderAppendUnsigned(&builder, summary.ledger_snapshot_sequence);
    BuilderAppendLiteral(&builder, " trace_summary_sequence=");
    BuilderAppendUnsigned(&builder, summary.trace_summary_sequence);
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "installed_observer_capabilities");
#define DLUMA_APPEND_CAPABILITY(field)                                      \
    BuilderAppendLiteral(&builder, " " #field "=");                     \
    BuilderAppendLiteral(                                                  \
        &builder, summary.installed_capabilities.field ? "yes" : "no")
    DLUMA_APPEND_CAPABILITY(watched_open);
    DLUMA_APPEND_CAPABILITY(parser);
    DLUMA_APPEND_CAPABILITY(campaign);
    DLUMA_APPEND_CAPABILITY(pack_collection);
    DLUMA_APPEND_CAPABILITY(pack_dynamic);
    DLUMA_APPEND_CAPABILITY(default_levels);
    DLUMA_APPEND_CAPABILITY(effective_fov);
    DLUMA_APPEND_CAPABILITY(assets_pc_file);
    DLUMA_APPEND_CAPABILITY(assets_pc_resource);
    DLUMA_APPEND_CAPABILITY(material_observer);
    DLUMA_APPEND_CAPABILITY(player_progression_observer);
    DLUMA_APPEND_CAPABILITY(audio_data);
    DLUMA_APPEND_CAPABILITY(audio_bank);
#undef DLUMA_APPEND_CAPABILITY
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "current_observer_admission");
#define DLUMA_APPEND_ADMISSION(field)                                       \
    BuilderAppendLiteral(&builder, " " #field "=");                     \
    BuilderAppendLiteral(                                                  \
        &builder, summary.current_admission.field ? "yes" : "no")
    DLUMA_APPEND_ADMISSION(hook_publication);
    DLUMA_APPEND_ADMISSION(content_tracking);
    DLUMA_APPEND_ADMISSION(trace_output);
    DLUMA_APPEND_ADMISSION(quiescing);
    DLUMA_APPEND_ADMISSION(material_observer_accepting_events);
    DLUMA_APPEND_ADMISSION(player_progression_observer_accepting_events);
#undef DLUMA_APPEND_ADMISSION
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "transition_generations_observed=");
    BuilderAppendUnsigned(&builder, summary.transition_count);
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "rollback_barriers_observed=");
    BuilderAppendUnsigned(&builder, summary.rollback_barrier_count);
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "campaign_loads_observed=");
    BuilderAppendUnsigned(&builder, summary.campaign_load_count);
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "pack_releases_observed=");
    BuilderAppendUnsigned(&builder, summary.pack_release_count);
    BuilderAppendChar(&builder, '\n');
    const bool material_stream_complete =
        summary.installed_capabilities.material_observer &&
        (summary.dropped_domain_mask & AssetTraceDropMaterial) == 0 &&
        summary.material_reload_entered &&
        summary.material_local_pack_opened &&
        summary.material_pack_selected &&
        summary.material_old_pack_destructor_completed &&
        summary.material_reload_exited;
    const bool player_stream_complete =
        summary.installed_capabilities.player_progression_observer &&
        (summary.dropped_domain_mask &
         AssetTraceDropPlayerProgression) == 0 &&
        summary.player_template_selected &&
        summary.progression_base_overlay_observed &&
        summary.progression_custom_overlay_observed &&
        summary.player_variables_after_template_observed;
    BuilderAppendLiteral(&builder,
                         "staged_assets_file_observation_complete=");
    BuilderAppendLiteral(
        &builder,
        summary.material_local_pack_opened &&
                (summary.dropped_domain_mask & AssetTraceDropMaterial) == 0
            ? "yes" : "no");
    BuilderAppendLiteral(&builder,
                         " material_pack_observation_complete=");
    BuilderAppendLiteral(&builder,
                         material_stream_complete ? "yes" : "no");
    BuilderAppendLiteral(&builder,
                         " material_resource_identity_complete=");
    BuilderAppendLiteral(
        &builder,
        material_stream_complete &&
                (summary.material_rebind_count ||
                 summary.material_global_fallback_count)
            ? "yes" : "no");
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder,
                         "player_template_selection_complete=");
    BuilderAppendLiteral(
        &builder,
        summary.player_template_selected &&
                (summary.dropped_domain_mask &
                 AssetTraceDropPlayerProgression) == 0
            ? "yes" : "no");
    BuilderAppendLiteral(&builder,
                         " progression_xml_observation_complete=");
    BuilderAppendLiteral(&builder,
                         player_stream_complete ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " progression_definition_snapshot_complete=");
    BuilderAppendLiteral(&builder,
                         player_stream_complete ? "yes" : "no");
    BuilderAppendLiteral(&builder,
                         " effective_fov_observation_complete=no");
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "material_rebind_count=");
    BuilderAppendUnsigned(&builder, summary.material_rebind_count);
    BuilderAppendLiteral(&builder, " material_global_fallback_count=");
    BuilderAppendUnsigned(&builder,
                          summary.material_global_fallback_count);
    BuilderAppendLiteral(&builder,
                         " post_supersession_assets_open_observed=");
    BuilderAppendLiteral(
        &builder,
        summary.post_supersession_assets_opened ? "yes" : "no");
    BuilderAppendLiteral(&builder, " progression_overlay_count=");
    BuilderAppendUnsigned(&builder, summary.progression_overlay_count);
    BuilderAppendChar(&builder, '\n');
    if (!summary.asset_manager_hook_available)
        BuilderAppendLiteral(
            &builder, "limitation=asset_manager_trace_unresolved\n");
    if (!summary.on_demand_hook_available)
        BuilderAppendLiteral(
            &builder, "limitation=optional_on_demand_unresolved\n");
    if (!summary.script_hook_available)
        BuilderAppendLiteral(
            &builder, "limitation=script_parser_observer_unavailable\n");
    if (!summary.campaign_hook_available)
        BuilderAppendLiteral(
            &builder, "limitation=campaign_manager_observer_unavailable\n");
    if (!summary.internal_pack_release_hook_available)
        BuilderAppendLiteral(
            &builder, "limitation=internal_pack_release_observer_unavailable\n");
    if (!summary.installed_capabilities.material_observer)
        BuilderAppendLiteral(
            &builder, "limitation=material_observer_unavailable\n");
    if (!summary.installed_capabilities.player_progression_observer)
        BuilderAppendLiteral(
            &builder, "limitation=player_progression_observer_unavailable\n");
    BuilderAppendLiteral(
        &builder, "limitation=effective_fov_observation_unavailable\n");
    BuilderAppendLiteral(
        &builder, "limitation=script_registry_owner_unresolved\n");
    BuilderAppendLiteral(
        &builder,
        "limitation=script_cache_trace_unresolved meaning=higher_registry_owner_not_proven\n");
    BuilderAppendLiteral(&builder, "queue_overflow_observed=");
    BuilderAppendLiteral(&builder,
                         summary.queue_overflow_observed ? "yes" : "no");
    BuilderAppendLiteral(&builder, " dropped_events=");
    BuilderAppendUnsigned(&builder, summary.overflow_dropped_events);
    BuilderAppendLiteral(&builder, " critical_trace_loss=");
    BuilderAppendUnsigned(&builder, summary.critical_trace_loss);
    BuilderAppendLiteral(&builder, " optional_trace_loss=");
    BuilderAppendUnsigned(&builder, summary.optional_trace_loss);
    BuilderAppendLiteral(
        &builder, " transaction_evidence_loss=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            transaction_evidence_loss);
    BuilderAppendLiteral(&builder, " critical_trace_reserved_capacity=");
    BuilderAppendUnsigned(
        &builder, summary.critical_trace_reserved_capacity);
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(
        &builder, "transaction_evidence premount_decisions=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            premount_decision_count);
    BuilderAppendLiteral(&builder, " premount_allowed=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            premount_allowed_count);
    BuilderAppendLiteral(&builder, " allowed_tokens=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.allowed_token_count);
    BuilderAppendLiteral(&builder, " project_root_bindings=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            project_root_binding_count);
    BuilderAppendLiteral(&builder, " data_bindings=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            data_directory_binding_count);
    BuilderAppendLiteral(&builder, " assets_pc_bindings=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            assets_pc_binding_count);
    BuilderAppendLiteral(&builder, " source_removals=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.source_removal_count);
    BuilderAppendLiteral(&builder, " rollback_barriers=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            rollback_barrier_count);
    BuilderAppendLiteral(&builder, " frontend_resets=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.frontend_reset_count);
    BuilderAppendLiteral(
        &builder, " frontend_verifications=");
    BuilderAppendUnsigned(
        &builder,
        summary.transaction_evidence.
            frontend_verification_count);
    BuilderAppendLiteral(&builder, " first_failure=");
    BuilderAppendQuotedFixed(
        &builder,
        summary.transaction_evidence.first_failure_reason,
        sizeof(summary.transaction_evidence.
            first_failure_reason));
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "dropped_domain_mask=");
    BuilderAppendUnsigned(&builder, summary.dropped_domain_mask);
    BuilderAppendLiteral(&builder, " dropped_domains=");
    BuilderAppendChar(&builder, '"');
    if (!summary.dropped_domain_mask) {
        BuilderAppendLiteral(&builder, "none");
    } else {
        bool separator = false;
#define DLUMA_APPEND_DROP_DOMAIN(mask, name)                                  \
        if ((summary.dropped_domain_mask & (mask)) != 0) {                  \
            if (separator) BuilderAppendChar(&builder, ',');                \
            BuilderAppendLiteral(&builder, (name));                         \
            separator = true;                                               \
        }
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropTransition, "transition");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropFilesystem, "source_removal");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropWatchedOpen, "watched_open");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropResource, "resource");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropRollback, "rollback");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropParserCache, "parser");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropAnnotation, "annotation");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropDiagnostic, "diagnostic");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropCampaign, "campaign");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropPackCollection,
                               "pack_collection");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropPackDynamic,
                               "pack_dynamic");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropBroadFileOpen,
                               "broad_file_open");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropMaterial, "material");
        DLUMA_APPEND_DROP_DOMAIN(AssetTraceDropPlayerProgression,
                               "player_progression");
#undef DLUMA_APPEND_DROP_DOMAIN
    }
    BuilderAppendChar(&builder, '"');
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "first_drop_sequence=");
    if (summary.drop_sequence_unknown ||
        !summary.first_drop_sequence)
        BuilderAppendLiteral(&builder, "unknown");
    else
        BuilderAppendUnsigned(&builder, summary.first_drop_sequence);
    BuilderAppendLiteral(&builder, " last_drop_sequence=");
    if (summary.drop_sequence_unknown ||
        !summary.last_drop_sequence)
        BuilderAppendLiteral(&builder, "unknown");
    else
        BuilderAppendUnsigned(&builder, summary.last_drop_sequence);
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "trace_output_incomplete=");
    BuilderAppendLiteral(&builder,
                         summary.trace_output_incomplete ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " meaning=legacy_file_io_write_incomplete");
    BuilderAppendChar(&builder, '\n');

    const bool transition_incomplete =
        summary.transitions_truncated ||
        (summary.dropped_domain_mask & AssetTraceDropTransition) != 0;
    const bool filesystem_incomplete =
        (summary.dropped_domain_mask & AssetTraceDropFilesystem) != 0;
    const bool watched_open_incomplete =
        summary.watched_opens_truncated ||
        (summary.dropped_domain_mask & AssetTraceDropWatchedOpen) != 0;
    const bool resource_incomplete =
        (summary.dropped_domain_mask & AssetTraceDropResource) != 0;
    const bool rollback_incomplete =
        summary.rollback_barriers_truncated ||
        (summary.dropped_domain_mask & AssetTraceDropRollback) != 0;
    const bool parser_incomplete =
        !summary.script_hook_available ||
        summary.script_observations_truncated ||
        (summary.dropped_domain_mask & AssetTraceDropParserCache) != 0;
    const CampaignObservationCompleteness campaign_completeness =
        EvaluateCampaignCompleteness(summary);
    const bool campaign_incomplete =
        !campaign_completeness.custom_complete ||
        !campaign_completeness.frontend_complete ||
        !campaign_completeness.base_complete;
    bool pack_collection_incomplete =
        summary.pack_releases_truncated ||
        (summary.dropped_domain_mask & AssetTraceDropPackCollection) != 0;
    bool pack_dynamic_incomplete =
        !summary.installed_capabilities.pack_dynamic ||
        (summary.dropped_domain_mask & AssetTraceDropPackDynamic) != 0;
    for (std::uint32_t index = 0;
         index < summary.pack_release_count; ++index) {
        if (!summary.pack_releases[index].collection_complete ||
            summary.pack_releases[index].pack_rows_truncated)
            pack_collection_incomplete = true;
        if (!summary.pack_releases[index].dynamic_complete)
            pack_dynamic_incomplete = true;
    }
    const bool broad_file_incomplete =
        !summary.broad_file_ledger_complete ||
        (summary.dropped_domain_mask & AssetTraceDropBroadFileOpen) != 0;
    const bool material_incomplete = !material_stream_complete;
    const bool player_progression_incomplete = !player_stream_complete;
    const bool ownership_incomplete =
        !summary.ownership_ledger_complete;
    const bool source_identity_incomplete =
        !summary.source_identity_complete || ownership_incomplete;
    const bool source_reset_incomplete =
        !summary.source_reset_evidence_complete ||
        ownership_incomplete || filesystem_incomplete ||
        rollback_incomplete;
    const bool trace_event_stream_incomplete =
        summary.queue_overflow_observed;
    const bool trace_file_incomplete =
        summary.trace_output_incomplete;
    const bool summary_coverage_complete =
        !ownership_incomplete && !source_identity_incomplete &&
        !source_reset_incomplete && !watched_open_incomplete &&
        !parser_incomplete && !campaign_incomplete &&
        !pack_collection_incomplete && !pack_dynamic_incomplete &&
        !broad_file_incomplete && !material_incomplete &&
        !player_progression_incomplete && !transition_incomplete &&
        !trace_event_stream_incomplete && !trace_file_incomplete;

    BuilderAppendLiteral(&builder, "ownership_ledger_complete=");
    BuilderAppendLiteral(&builder, ownership_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " source_identity_complete=");
    BuilderAppendLiteral(&builder,
                         source_identity_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " source_reset_evidence_complete=");
    BuilderAppendLiteral(&builder,
                         source_reset_incomplete ? "no" : "yes");
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder,
                         "watched_open_observation_complete=");
    BuilderAppendLiteral(&builder,
                         watched_open_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " parser_observation_complete=");
    BuilderAppendLiteral(&builder, parser_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " campaign_observation_complete=");
    BuilderAppendLiteral(&builder, campaign_incomplete ? "no" : "yes");
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "campaign_observer_installed=");
    BuilderAppendLiteral(
        &builder,
        campaign_completeness.observer_installed ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " custom_campaign_load_observation_complete=");
    BuilderAppendLiteral(
        &builder, campaign_completeness.custom_complete ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " frontend_campaign_lifecycle_observation_complete=");
    BuilderAppendLiteral(
        &builder, campaign_completeness.frontend_complete ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " base_campaign_observation_complete=");
    BuilderAppendLiteral(
        &builder, campaign_completeness.base_complete ? "yes" : "no");
    BuilderAppendKeyQuoted(
        &builder, "frontend_campaign_lifecycle_state",
        campaign_completeness.frontend_state,
        sizeof(campaign_completeness.frontend_state));
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder,
                         "pack_collection_observation_complete=");
    BuilderAppendLiteral(&builder,
                         pack_collection_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " pack_dynamic_observation_complete=");
    BuilderAppendLiteral(&builder,
                         pack_dynamic_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " broad_file_ledger_complete=");
    BuilderAppendLiteral(&builder,
                         broad_file_incomplete ? "no" : "yes");
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "trace_event_stream_complete=");
    BuilderAppendLiteral(&builder,
                         trace_event_stream_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " trace_file_io_complete=");
    BuilderAppendLiteral(&builder,
                         trace_file_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " summary_coverage_complete=");
    BuilderAppendLiteral(&builder,
                         summary_coverage_complete ? "yes" : "no");
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "ledger_observation_complete=");
    BuilderAppendLiteral(
        &builder,
        (!ownership_incomplete && !broad_file_incomplete) ? "yes" : "no");
    BuilderAppendLiteral(
        &builder, " ledger_observation_incomplete_domains=");
    BuilderAppendChar(&builder, '"');
    bool ledger_separator = false;
    if (ownership_incomplete) {
        BuilderAppendLiteral(&builder, "source_ownership");
        ledger_separator = true;
    }
    if (broad_file_incomplete) {
        if (ledger_separator) BuilderAppendChar(&builder, ',');
        BuilderAppendLiteral(&builder, "broad_file_open");
        ledger_separator = true;
    }
    if (!ledger_separator) BuilderAppendLiteral(&builder, "none");
    BuilderAppendChar(&builder, '"');
    BuilderAppendChar(&builder, '\n');
    BuilderAppendLiteral(&builder, "broad_file_loss count=");
    BuilderAppendUnsigned(&builder, summary.broad_file_loss_count);
    BuilderAppendLiteral(&builder, " first_known_sequence=");
    BuilderAppendSequenceOrUnknown(
        &builder, summary.broad_file_first_loss_sequence);
    BuilderAppendLiteral(&builder, " last_known_sequence=");
    BuilderAppendSequenceOrUnknown(
        &builder, summary.broad_file_last_loss_sequence);
    BuilderAppendKeyQuoted(&builder, "reason",
                           summary.broad_file_loss_reason,
                           sizeof(summary.broad_file_loss_reason));
    BuilderAppendChar(&builder, '\n');
    for (std::size_t index = 0;
         index < kTraceLossDomainCount; ++index) {
        const AssetTraceLossDomainSummary& domain =
            summary.loss_domains[index];
        BuilderAppendLiteral(&builder, "loss_domain name=");
        BuilderAppendQuotedFixed(
            &builder, DropDomainName(domain.domain_mask), 32);
        BuilderAppendKeyUnsigned(&builder, "count", domain.count);
        BuilderAppendLiteral(&builder, " first_known_sequence=");
        BuilderAppendSequenceOrUnknown(
            &builder, domain.first_known_sequence);
        BuilderAppendLiteral(&builder, " last_known_sequence=");
        BuilderAppendSequenceOrUnknown(
            &builder, domain.last_known_sequence);
        BuilderAppendLiteral(&builder, " sequence_unknown=");
        BuilderAppendLiteral(&builder,
                             domain.sequence_unknown ? "yes" : "no");
        BuilderAppendChar(&builder, '\n');
    }
    BuilderAppendLiteral(&builder, "rollback_barrier_coverage=");
    BuilderAppendLiteral(&builder, rollback_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " watched_open_coverage=");
    BuilderAppendLiteral(&builder, watched_open_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " parser_cache_coverage=");
    BuilderAppendLiteral(&builder,
                         !summary.script_hook_available
                             ? "unknown"
                             : (parser_incomplete ? "no" : "yes"));
    BuilderAppendLiteral(&builder, " resource_trace_coverage=");
    BuilderAppendLiteral(&builder, resource_incomplete ? "no" : "yes");
    BuilderAppendLiteral(&builder, " transition_trace_coverage=");
    BuilderAppendLiteral(&builder, transition_incomplete ? "no" : "yes");
    BuilderAppendChar(&builder, '\n');

    for (std::uint32_t i = 0; i < summary.transition_count; ++i) {
        const AssetTraceGenerationSummary& item = summary.transitions[i];
        BuilderAppendLiteral(&builder, "generation=");
        BuilderAppendUnsigned(&builder, item.generation);
        BuilderAppendKeyQuoted(
            &builder, "branch",
            item.branch_observed ? item.selected_branch : "unknown",
            item.branch_observed ? sizeof(item.selected_branch) : 8);
        BuilderAppendLiteral(&builder, " destroy_active_level_called=");
        BuilderAppendLiteral(
            &builder,
            BoolEvidenceName(item.destroy_active_level_entered,
                             transition_incomplete));
        BuilderAppendLiteral(&builder, " update_dlc_content_called=");
        BuilderAppendLiteral(
            &builder,
            BoolEvidenceName(item.update_dlc_content_entered,
                             transition_incomplete));
        BuilderAppendLiteral(&builder, " unload_level_called=");
        BuilderAppendLiteral(
            &builder,
            BoolEvidenceName(item.unload_level_entered,
                             transition_incomplete));
        BuilderAppendLiteral(&builder, " any_source_removal_observed=");
        BuilderAppendLiteral(
            &builder,
            BoolEvidenceName(item.any_source_remove_entered,
                             filesystem_incomplete));
        BuilderAppendLiteral(&builder,
                             " outgoing_source_removal_observed=");
        BuilderAppendLiteral(
            &builder,
            item.outgoing_source_remove_succeeded
                ? "yes"
                : (item.source_removal_attribution_unknown ||
                           filesystem_incomplete
                       ? "unknown"
                       : "no"));
        BuilderAppendLiteral(&builder,
                             " outgoing_level_pack_release_observed=");
        BuilderAppendLiteral(
            &builder,
            item.outgoing_level_pack_observed &&
                    item.level_release_packs_exited
                ? "yes"
                : (item.level_pack_attribution_unknown ||
                           resource_incomplete
                       ? "unknown"
                       : "no"));
        BuilderAppendLiteral(&builder,
                             " on_demand_pack_eviction_observed=");
        BuilderAppendLiteral(&builder,
                             TriStateName(EvictionState(summary, item)));
        if (item.pack_pointer_correlation_truncated)
            BuilderAppendLiteral(
                &builder, " limitation=pack_pointer_correlation_truncated");
        BuilderAppendLiteral(&builder,
                             " first_watched_destination_lookup_sequence=");
        if (item.first_destination_lookup_sequence)
            BuilderAppendUnsigned(&builder,
                                  item.first_destination_lookup_sequence);
        else
            BuilderAppendLiteral(&builder, "unknown");
        BuilderAppendLiteral(&builder, " last_observed_cleanup_sequence=");
        if (item.last_cleanup_sequence)
            BuilderAppendUnsigned(&builder, item.last_cleanup_sequence);
        else
            BuilderAppendLiteral(&builder, "unknown");
        BuilderAppendLiteral(
            &builder, " cleanup_completed_before_first_destination_lookup=");
        const bool cleanup_coverage_complete =
            summary.asset_manager_hook_available &&
            summary.on_demand_hook_available;
        BuilderAppendLiteral(
            &builder,
            transition_incomplete || filesystem_incomplete ||
                    watched_open_incomplete || resource_incomplete ||
                    !cleanup_coverage_complete
                ? "unknown"
                : TriStateName(CleanupBeforeDestinationLookup(item)));
        BuilderAppendLiteral(&builder, " parser_cache_outcome=");
        BuilderAppendLiteral(&builder, ScriptOutcomeName(item.script_outcome));
        if (parser_incomplete &&
            item.script_outcome == AssetTraceScriptOutcome::Unknown)
            BuilderAppendLiteral(&builder,
                                 summary.script_hook_available
                                     ? " limitation=script_registry_owner_unresolved"
                                     : " limitation=script_parser_observer_unavailable");
        BuilderAppendChar(&builder, '\n');
    }

    for (std::uint32_t i = 0;
         i < summary.rollback_barrier_count; ++i) {
        const AssetTraceRollbackBarrierSummary& barrier =
            summary.rollback_barriers[i];
        BuilderAppendLiteral(&builder, "rollback epoch=");
        BuilderAppendUnsigned(&builder, barrier.epoch);
        BuilderAppendLiteral(&builder, " on_destroy_enter_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.on_destroy_enter_sequence);
        BuilderAppendLiteral(&builder, " owned_unload_0x431C83_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.owned_unload_sequence);
        BuilderAppendLiteral(&builder, " owned_unload_completed_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.owned_unload_completed_sequence);
        BuilderAppendLiteral(&builder, " outgoing_release_packs_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.outgoing_release_sequence);
        BuilderAppendLiteral(&builder, " release_enters=");
        BuilderAppendUnsigned(&builder, barrier.release_enter_count);
        BuilderAppendLiteral(&builder, " release_completions=");
        BuilderAppendUnsigned(&builder, barrier.release_exit_count);
        BuilderAppendLiteral(&builder, " original_return_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.original_return_sequence);
        BuilderAppendLiteral(&builder, " source_reset_begin_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.reset_begin_sequence);
        BuilderAppendLiteral(&builder, " source_reset_end_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.reset_end_sequence);
        BuilderAppendLiteral(&builder, " first_frontend_lookup_seq=");
        BuilderAppendSequenceOrUnknown(
            &builder, barrier.first_frontend_lookup_sequence);
        BuilderAppendLiteral(
            &builder, " reset_completed_before_first_frontend_lookup=");
        if (!barrier.reset_end_sequence ||
            !barrier.first_frontend_lookup_sequence) {
            BuilderAppendLiteral(&builder, "unknown");
        } else {
            const bool ordered = barrier.reset_succeeded &&
                barrier.first_lookup_after_reset &&
                barrier.reset_end_sequence <
                    barrier.first_frontend_lookup_sequence;
            BuilderAppendLiteral(&builder, ordered ? "yes" : "no");
        }
        BuilderAppendLiteral(
            &builder,
            " ordering_authority=atomic_frontend_reset_phase");
        BuilderAppendLiteral(&builder, " coverage=");
        BuilderAppendLiteral(
            &builder, rollback_incomplete ? "incomplete" : "complete");
        BuilderAppendChar(&builder, '\n');
    }

    for (std::uint32_t i = 0; i < summary.watched_open_count; ++i) {
        const AssetTraceWatchedResolution& resolution =
            summary.watched_opens[i];
        BuilderAppendLiteral(&builder, "watched_open seq=");
        BuilderAppendUnsigned(&builder, resolution.sequence);
        BuilderAppendKeyUnsigned(&builder, "epoch",
                                 resolution.content_epoch);
        BuilderAppendKeyUnsigned(&builder, "gen", resolution.generation);
        BuilderAppendKeyQuoted(
            &builder, "staging",
            StagingPhaseName(resolution.staging_phase), 32);
        BuilderAppendKeyQuoted(&builder, "path", resolution.logical_path,
                               sizeof(resolution.logical_path));
        BuilderAppendLiteral(&builder, " source_ptr=");
        BuilderAppendHex(&builder, resolution.source);
        BuilderAppendKeyUnsigned(&builder, "source_record",
                                 resolution.source_record_index);
        BuilderAppendKeyUnsigned(&builder, "source_ordinal",
                                 resolution.source_add_ordinal);
        BuilderAppendKeyQuoted(
            &builder, "source_kind",
            SourceKindName(resolution.source_kind), 32);
        BuilderAppendKeyQuoted(&builder, "project",
                               resolution.project_root,
                               sizeof(resolution.project_root));
        BuilderAppendKeyQuoted(&builder, "provider",
                               resolution.resolved_source,
                               sizeof(resolution.resolved_source));
        BuilderAppendKeyUnsigned(&builder, "update_dlc_depth",
                                 resolution.update_dlc_depth);
        BuilderAppendKeyQuoted(&builder, "caller_module",
                               resolution.caller_module,
                               sizeof(resolution.caller_module));
        BuilderAppendKeyUnsigned(&builder, "caller_rva",
                                 resolution.caller_rva);
        BuilderAppendKeyUnsigned(&builder, "result", resolution.result);
        BuilderAppendChar(&builder, '\n');
    }
    for (std::uint32_t i = 0;
         i < summary.script_observation_count; ++i) {
        const AssetTraceScriptObservation& observation =
            summary.script_observations[i];
        BuilderAppendLiteral(&builder, "parser_observation seq=");
        BuilderAppendUnsigned(&builder, observation.sequence);
        BuilderAppendKeyUnsigned(&builder, "epoch",
                                 observation.content_epoch);
        BuilderAppendKeyUnsigned(&builder, "gen",
                                 observation.generation);
        BuilderAppendKeyQuoted(
            &builder, "outcome",
            WatchedScriptOutcomeName(observation.outcome), 40);
        BuilderAppendKeyQuoted(&builder, "path",
                               observation.logical_path,
                               sizeof(observation.logical_path));
        BuilderAppendLiteral(&builder, " source_ptr=");
        BuilderAppendHex(&builder, observation.source);
        BuilderAppendKeyQuoted(&builder, "provider",
                               observation.provider,
                               sizeof(observation.provider));
        BuilderAppendKeyQuoted(&builder, "input_hash",
                               observation.content_hash,
                               sizeof(observation.content_hash));
        BuilderAppendKeyUnsigned(&builder, "input_size",
                                 observation.input_size);
        BuilderAppendKeyUnsigned(&builder, "open_sequence",
                                 observation.open_sequence);
        BuilderAppendKeyQuoted(&builder, "registry_key",
                               observation.registry_key,
                               sizeof(observation.registry_key));
        BuilderAppendLiteral(&builder, " existing_object=");
        BuilderAppendHex(&builder, observation.existing_object);
        BuilderAppendLiteral(&builder, " result_object=");
        BuilderAppendHex(&builder, observation.result_object);
        BuilderAppendKeyUnsigned(
            &builder, "parser_observation_generation",
            observation.parser_observation_generation);
        BuilderAppendKeyUnsigned(
            &builder, "registry_generation",
            observation.registry_generation);
        BuilderAppendChar(&builder, '\n');
    }
    for (std::uint32_t index = 0;
         index < summary.campaign_load_count; ++index) {
        const AssetTraceCampaignLoadSummary& campaign =
            summary.campaign_loads[index];
        BuilderAppendLiteral(&builder,
                             "campaign_load observation_generation=");
        BuilderAppendUnsigned(&builder,
                              campaign.observation_generation);
        BuilderAppendKeyUnsigned(&builder, "epoch", campaign.epoch);
        BuilderAppendKeyUnsigned(&builder, "gen",
                                 campaign.transition_generation);
        BuilderAppendKeyQuoted(
            &builder, "kind",
            campaign.frontend_snapshot ? "frontend_snapshot" : "load",
            24);
        BuilderAppendKeyQuoted(&builder, "project",
                               campaign.project_root,
                               sizeof(campaign.project_root));
        BuilderAppendKeyQuoted(&builder, "manifest",
                               campaign.manifest_path,
                               sizeof(campaign.manifest_path));
        BuilderAppendKeyQuoted(&builder, "manifest_hash",
                               campaign.manifest_hash,
                               sizeof(campaign.manifest_hash));
        BuilderAppendLiteral(&builder, " manifest_source=");
        BuilderAppendHex(&builder, campaign.manifest_source);
        BuilderAppendKeyQuoted(&builder, "manifest_provider",
                               campaign.manifest_provider,
                               sizeof(campaign.manifest_provider));
        BuilderAppendLiteral(&builder, " manager=");
        BuilderAppendHex(&builder, campaign.manager);
        BuilderAppendLiteral(&builder, " selected_before=");
        BuilderAppendHex(&builder, campaign.selected_before);
        BuilderAppendLiteral(&builder, " selected_after=");
        BuilderAppendHex(&builder, campaign.selected_after);
        BuilderAppendKeyUnsigned(&builder, "selected_index_before",
                                 campaign.selected_index_before);
        BuilderAppendKeyUnsigned(&builder, "selected_index_after",
                                 campaign.selected_index_after);
        BuilderAppendKeyUnsigned(&builder, "table_count_before",
                                 campaign.table_count_before);
        BuilderAppendKeyUnsigned(&builder, "table_count_after",
                                 campaign.table_count_after);
        BuilderAppendKeyUnsigned(&builder, "fingerprint_before",
                                 campaign.fingerprint_before);
        BuilderAppendKeyUnsigned(&builder, "fingerprint_after",
                                 campaign.fingerprint_after);
        BuilderAppendKeyUnsigned(&builder, "maps_before",
                                 campaign.map_count_before);
        BuilderAppendKeyUnsigned(&builder, "maps_after",
                                 campaign.map_count_after);
        BuilderAppendKeyUnsigned(&builder, "rewards_before",
                                 campaign.rewards_count_before);
        BuilderAppendKeyUnsigned(&builder, "rewards_after",
                                 campaign.rewards_count_after);
        BuilderAppendKeyUnsigned(&builder, "respawn_before",
                                 campaign.respawn_count_before);
        BuilderAppendKeyUnsigned(&builder, "respawn_after",
                                 campaign.respawn_count_after);
        BuilderAppendKeyQuoted(
            &builder, "outcome",
            CampaignPersistenceOutcomeName(campaign.outcome), 64);
        BuilderAppendLiteral(&builder, " snapshot_before_complete=");
        BuilderAppendLiteral(&builder,
                             campaign.before_complete ? "yes" : "no");
        BuilderAppendLiteral(&builder, " snapshot_after_complete=");
        BuilderAppendLiteral(&builder,
                             campaign.after_complete ? "yes" : "no");
        BuilderAppendLiteral(&builder, " maps_truncated=");
        BuilderAppendLiteral(&builder,
                             campaign.maps_truncated ? "yes" : "no");
        BuilderAppendChar(&builder, '\n');
        for (std::uint32_t map_index = 0;
             map_index < campaign.map_count_stored; ++map_index) {
            const BoundedCampaignMap& map =
                campaign.maps[map_index];
            BuilderAppendLiteral(
                &builder, "campaign_map observation_generation=");
            BuilderAppendUnsigned(
                &builder, campaign.observation_generation);
            BuilderAppendKeyUnsigned(&builder, "order",
                                     map.registration_order);
            BuilderAppendKeyUnsigned(
                &builder, "map_id",
                static_cast<std::uint32_t>(map.map_id));
            BuilderAppendKeyQuoted(&builder, "map", map.map_name,
                                   sizeof(map.map_name));
            BuilderAppendKeyQuoted(&builder, "level", map.level_name,
                                   sizeof(map.level_name));
            BuilderAppendLiteral(&builder, " complete=");
            BuilderAppendLiteral(&builder,
                                 map.complete ? "yes" : "no");
            BuilderAppendChar(&builder, '\n');
        }
    }
    for (std::uint32_t index = 0;
         index < summary.pack_release_count; ++index) {
        const AssetTracePackReleaseSummary& pack =
            summary.pack_releases[index];
        BuilderAppendLiteral(&builder,
                             "pack_release observation_generation=");
        BuilderAppendUnsigned(&builder,
                              pack.observation_generation);
        BuilderAppendKeyUnsigned(&builder, "epoch", pack.epoch);
        BuilderAppendKeyUnsigned(&builder, "gen",
                                 pack.transition_generation);
        BuilderAppendLiteral(&builder, " level=");
        BuilderAppendHex(&builder, pack.level);
        BuilderAppendKeyQuoted(&builder, "map", pack.map,
                               sizeof(pack.map));
        BuilderAppendKeyQuoted(&builder, "label_source",
                               pack.label_source,
                               sizeof(pack.label_source));
        BuilderAppendKeyQuoted(
            &builder, "collection_state",
            PackCollectionStateName(pack.collection_state), 48);
        BuilderAppendKeyUnsigned(&builder, "static_count",
                                 pack.static_count);
        BuilderAppendKeyUnsigned(&builder, "dynamic_eviction_count",
                                 pack.dynamic_eviction_count);
        BuilderAppendKeyUnsigned(&builder, "dynamic_release_count",
                                 pack.dynamic_release_count);
        BuilderAppendKeyQuoted(
            &builder, "cross_check",
            PackCrossCheckOutcomeName(pack.cross_check), 64);
        BuilderAppendLiteral(&builder, " collection_coverage=");
        BuilderAppendLiteral(&builder,
                             pack.collection_complete ? "complete"
                                                      : "incomplete");
        BuilderAppendLiteral(&builder, " dynamic_coverage=");
        BuilderAppendLiteral(&builder,
                             pack.dynamic_complete ? "complete"
                                                   : "incomplete");
        BuilderAppendLiteral(&builder, " pack_rows_truncated=");
        BuilderAppendLiteral(&builder,
                             pack.pack_rows_truncated ? "yes" : "no");
        BuilderAppendChar(&builder, '\n');
        for (std::uint32_t row_index = 0;
             row_index < pack.pack_count_stored; ++row_index) {
            const AssetTracePackReleaseSummary::PackRow& row =
                pack.packs[row_index];
            BuilderAppendLiteral(
                &builder, "pack_release_row observation_generation=");
            BuilderAppendUnsigned(
                &builder, pack.observation_generation);
            BuilderAppendKeyUnsigned(&builder, "index",
                                     row.collection_index);
            BuilderAppendLiteral(&builder, " pack=");
            BuilderAppendHex(&builder, row.pack);
            BuilderAppendKeyQuoted(&builder, "name", row.name,
                                   sizeof(row.name));
            BuilderAppendLiteral(&builder, " custom_project_path=");
            BuilderAppendLiteral(
                &builder, row.custom_project_path ? "yes" : "no");
            BuilderAppendChar(&builder, '\n');
        }
    }
    if (summary.transitions_truncated)
        BuilderAppendLiteral(&builder, "limitation=transition_summary_truncated\n");
    if (summary.watched_opens_truncated)
        BuilderAppendLiteral(&builder, "limitation=watched_open_summary_truncated\n");
    if (summary.rollback_barriers_truncated)
        BuilderAppendLiteral(
            &builder, "limitation=rollback_barrier_summary_truncated\n");
    if (summary.script_observations_truncated)
        BuilderAppendLiteral(
            &builder, "limitation=script_observation_summary_truncated\n");
    if (summary.campaign_loads_truncated)
        BuilderAppendLiteral(
            &builder, "limitation=campaign_summary_truncated\n");
    if (summary.pack_releases_truncated)
        BuilderAppendLiteral(
            &builder, "limitation=pack_release_summary_truncated\n");
    return BuilderFinish(&builder, written);
}

}  // namespace dluma::fixes::custom_map_asset_isolation::trace

extern "C" void
dluma_custom_map_asset_trace_record_premount_decision(
    unsigned int allowed,
    ULONGLONG token_generation,
    unsigned int admission_result,
    const char* failure_reason)
{
    dluma::fixes::custom_map_asset_isolation::trace::
        RecordPreMountDecision(
            allowed != 0,
            static_cast<std::uint64_t>(token_generation),
            admission_result, failure_reason);
}

extern "C" void
dluma_custom_map_asset_trace_record_source_binding(
    unsigned int source_kind,
    ULONGLONG token_generation,
    unsigned int epoch,
    unsigned int accepted,
    const char* failure_reason)
{
    dluma::fixes::custom_map_asset_isolation::trace::
        RecordSourceBinding(
            source_kind,
            static_cast<std::uint64_t>(token_generation),
            epoch, accepted != 0, failure_reason);
}
