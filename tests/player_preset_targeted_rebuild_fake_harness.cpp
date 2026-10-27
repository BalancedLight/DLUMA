#define _HAS_EXCEPTIONS 0

extern "C" {
#include "../src/dlw_common.h"
}
#include "../src/fixes/custom_map_asset_isolation/PlayerPresetTargetedRebuild.h"

#include <cstdio>
#include <cstring>

using namespace dluma::fixes::custom_map_asset_isolation::playerpreset;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

PendingPlayerPresetProviderRepair MakeToken() noexcept
{
    PendingPlayerPresetProviderRepair token = {};
    token.created_sequence = 17;
    token.old_content_epoch = 1;
    token.current_content_epoch = 2;
    token.cache_generation = 19;
    token.player_state = 0x1000;
    token.player_state_generation = 7;
    token.player_variables = 0x2000;
    token.old_data_source = 0x3000;
    token.old_type0_definition_vector = 0xA080BA0;
    token.old_type0_definition_count = 385;
    token.old_type0_fingerprint = 0xC0EE5;
    token.owner_thread_id = 7412;
    token.selector_equal = true;
    token.content_identity_changed = true;
    token.old_data_source_inactive = true;
    token.current_epoch_sources_ready = true;
    std::strcpy(token.progression_selector, "default_levels.xml");
    std::strcpy(token.class_selector, "");
    std::strcpy(token.old_provider, "X:\\workshop\\first_project run\\data");
    return token;
}

bool Matches(const PendingPlayerPresetProviderRepair& token,
             PreflightFailure* failure = nullptr) noexcept
{
    return TokenMatchesLoad(
        token, 0x1000, 7, 0x2000, 2, 19, 7412,
        "default_levels.xml", "", failure);
}

bool CasePendingTokenIsGenerationSafeAndSingleUse()
{
    PendingRepairStore store = {};
    Initialize(&store);
    PendingPlayerPresetProviderRepair first = MakeToken();
    CHECK(PublishPendingRepair(&store, &first));
    CHECK(first.token_generation == 1);
    PendingPlayerPresetProviderRepair observed = {};
    CHECK(PeekPendingRepair(&store, &observed));
    CHECK(observed.token_generation == first.token_generation);
    CHECK(ConsumePendingRepair(&store, first.token_generation, &observed));
    CHECK(observed.consumed);
    CHECK(!PeekPendingRepair(&store, &observed));
    CHECK(!ConsumePendingRepair(&store, first.token_generation, nullptr));
    PendingPlayerPresetProviderRepair second = MakeToken();
    CHECK(PublishPendingRepair(&store, &second));
    CHECK(second.token_generation == 2);
    ExpirePendingRepair(&store);
    CHECK(!PeekPendingRepair(&store, &observed));
    return true;
}

bool CaseExactTokenMatchesLoad()
{
    PendingPlayerPresetProviderRepair token = MakeToken();
    PreflightFailure failure = PreflightFailure::UnsupportedImage;
    CHECK(Matches(token, &failure));
    CHECK(failure == PreflightFailure::None);
    return true;
}

bool CaseDifferentStateGenerationRejects()
{
    PendingPlayerPresetProviderRepair token = MakeToken();
    PreflightFailure failure = PreflightFailure::None;
    CHECK(!TokenMatchesLoad(token, 0x1000, 8, 0x2000, 2, 19, 7412,
                            "default_levels.xml", "", &failure));
    CHECK(failure == PreflightFailure::StateGenerationMismatch);
    return true;
}

bool CaseWrongStateOrVariablesRejects()
{
    PendingPlayerPresetProviderRepair token = MakeToken();
    PreflightFailure failure = PreflightFailure::None;
    CHECK(!TokenMatchesLoad(token, 0x1100, 7, 0x2000, 2, 19, 7412,
                            "default_levels.xml", "", &failure));
    CHECK(failure == PreflightFailure::StateMismatch);
    CHECK(!TokenMatchesLoad(token, 0x1000, 7, 0x2100, 2, 19, 7412,
                            "default_levels.xml", "", &failure));
    CHECK(failure == PreflightFailure::StateMismatch);
    return true;
}

bool CaseThreadSelectorEpochAndCacheReject()
{
    PendingPlayerPresetProviderRepair token = MakeToken();
    PreflightFailure failure = PreflightFailure::None;
    CHECK(!TokenMatchesLoad(token, 0x1000, 7, 0x2000, 2, 19, 7000,
                            "default_levels.xml", "", &failure));
    CHECK(failure == PreflightFailure::ThreadMismatch);
    CHECK(!TokenMatchesLoad(token, 0x1000, 7, 0x2000, 2, 19, 7412,
                            "other.xml", "", &failure));
    CHECK(failure == PreflightFailure::SelectorMismatch);
    CHECK(!TokenMatchesLoad(token, 0x1000, 7, 0x2000, 3, 19, 7412,
                            "default_levels.xml", "", &failure));
    CHECK(failure == PreflightFailure::ContentEpochMismatch);
    CHECK(!TokenMatchesLoad(token, 0x1000, 7, 0x2000, 2, 20, 7412,
                            "default_levels.xml", "", &failure));
    CHECK(failure == PreflightFailure::CacheGenerationMismatch);
    return true;
}

bool CaseSourceReadinessRejects()
{
    PendingPlayerPresetProviderRepair token = MakeToken();
    PreflightFailure failure = PreflightFailure::None;
    token.old_data_source_inactive = false;
    CHECK(!Matches(token, &failure));
    CHECK(failure == PreflightFailure::OldDataSourceStillActive);
    token.old_data_source_inactive = true;
    token.current_epoch_sources_ready = false;
    CHECK(!Matches(token, &failure));
    CHECK(failure == PreflightFailure::CurrentEpochSourcesIncomplete);
    return true;
}

CacheVectorBounds MakeBounds(std::uintptr_t records, std::uint32_t type,
                             std::uint32_t count = 4) noexcept
{
    CacheVectorBounds bounds = {};
    bounds.records = records;
    bounds.count = count;
    bounds.capacity = count;
    bounds.logical_type = type;
    bounds.cache_generation = 19;
    bounds.descriptor_readable = true;
    return bounds;
}

bool CaseExactRecordPlusTwentyMaps()
{
    const CacheVectorBounds bounds = MakeBounds(0x10000, 3);
    std::uintptr_t record = 0;
    CHECK(MapDefinitionVectorToValidatedCacheRecord(
        0x10000 + 2 * kPresetRecordSize + kPresetDefinitionVectorOffset,
        3, bounds, &record));
    CHECK(record == 0x10000 + 2 * kPresetRecordSize);
    return true;
}

bool CaseBlindSubtractionAndWrongTypeReject()
{
    const CacheVectorBounds bounds = MakeBounds(0x10000, 3);
    std::uintptr_t record = 0;
    PreflightFailure failure = PreflightFailure::None;
    CHECK(!MapDefinitionVectorToValidatedCacheRecord(
        0x20020, 3, bounds, &record, &failure));
    CHECK(failure == PreflightFailure::RecordMembershipMissing);
    CHECK(!MapDefinitionVectorToValidatedCacheRecord(
        0x10020, 2, bounds, &record, &failure));
    CHECK(failure == PreflightFailure::CacheVectorInvalid);
    return true;
}

bool CaseMisalignedAndOutOfBoundsReject()
{
    const CacheVectorBounds bounds = MakeBounds(0x10000, 0, 2);
    std::uintptr_t record = 0;
    PreflightFailure failure = PreflightFailure::None;
    CHECK(!MapDefinitionVectorToValidatedCacheRecord(
        0x10021, 0, bounds, &record, &failure));
    CHECK(failure == PreflightFailure::RecordMembershipMissing);
    CHECK(!MapDefinitionVectorToValidatedCacheRecord(
        0x10000 + 2 * kPresetRecordSize + kPresetDefinitionVectorOffset,
        0, bounds, &record, &failure));
    CHECK(failure == PreflightFailure::RecordMembershipMissing);
    return true;
}

bool CaseInvalidVectorBoundsReject()
{
    CacheVectorBounds bounds = MakeBounds(0x10000, 0);
    bounds.capacity = 3;
    std::uintptr_t record = 0;
    PreflightFailure failure = PreflightFailure::None;
    CHECK(!MapDefinitionVectorToValidatedCacheRecord(
        0x10020, 0, bounds, &record, &failure));
    CHECK(failure == PreflightFailure::CacheVectorInvalid);
    bounds = MakeBounds(0x10000, 0, 4097);
    CHECK(!MapDefinitionVectorToValidatedCacheRecord(
        0x10020, 0, bounds, &record, &failure));
    CHECK(failure == PreflightFailure::CacheVectorInvalid);
    return true;
}

bool CaseDuplicateMappingIsDeduplicatedAndSorted()
{
    TargetedRebuildPlan plan = {};
    PreflightFailure failure = PreflightFailure::None;
    CHECK(AppendValidatedRecord(&plan, 0, 0x3000, 0x3020, 3, &failure));
    CHECK(AppendValidatedRecord(&plan, 1, 0x1000, 0x1020, 1, &failure));
    CHECK(AppendValidatedRecord(&plan, 2, 0x3000, 0x3020, 3, &failure));
    CHECK(plan.distinct_count == 2);
    CHECK(plan.records[0].logical_type == 1);
    CHECK(plan.records[1].logical_type == 3);
    CHECK(plan.row_to_record[0] == 1);
    CHECK(plan.row_to_record[1] == 0);
    CHECK(plan.row_to_record[2] == 1);
    return true;
}

bool CaseAmbiguousDuplicateRejects()
{
    TargetedRebuildPlan plan = {};
    PreflightFailure failure = PreflightFailure::None;
    CHECK(AppendValidatedRecord(&plan, 0, 0x1000, 0x1020, 0, &failure));
    CHECK(!AppendValidatedRecord(&plan, 1, 0x1000, 0x1020, 1, &failure));
    CHECK(failure == PreflightFailure::RecordMembershipAmbiguous);
    return true;
}

struct FakeTransaction {
    std::uint32_t rebuild_calls;
    std::uint32_t original_load_calls;
    std::uint32_t natural_clear_calls;
    std::uint8_t set_variables_mask;
    std::uint32_t order[kLogicalTypeCount];
    std::uint32_t type0_count;
    std::uint32_t fail_after_type;
    bool global_reset;
    bool initialize;
    bool appearance;
    bool recreate;
};

bool RunFakeTransaction(const TargetedRebuildPlan& plan,
                        FakeTransaction* transaction) noexcept
{
    if (!transaction || !plan.distinct_count ||
        plan.distinct_count > kLogicalTypeCount)
        return false;
    bool verified = true;
    for (std::uint32_t index = 0; index < plan.distinct_count; ++index) {
        const TargetRecord& record = plan.records[index];
        transaction->order[transaction->rebuild_calls++] =
            record.logical_type;
        if (record.logical_type == 0) transaction->type0_count = 379;
        if (record.logical_type == transaction->fail_after_type) {
            verified = false;
            break;
        }
    }
    ++transaction->original_load_calls;
    ++transaction->natural_clear_calls;
    transaction->set_variables_mask = 0xFF;
    return verified;
}

TargetedRebuildPlan MakeEightRecordPlan() noexcept
{
    TargetedRebuildPlan plan = {};
    for (std::uint32_t type = 0; type < kLogicalTypeCount; ++type) {
        const std::uintptr_t record = 0x10000 + type * 0x100;
        (void)AppendValidatedRecord(&plan, type, record,
                                    record + kPresetDefinitionVectorOffset,
                                    type, nullptr);
    }
    return plan;
}

bool CaseSuccessfulEightRecordTransaction()
{
    const TargetedRebuildPlan plan = MakeEightRecordPlan();
    FakeTransaction transaction = {};
    transaction.type0_count = 385;
    transaction.fail_after_type = UINT32_MAX;
    CHECK(RunFakeTransaction(plan, &transaction));
    CHECK(transaction.rebuild_calls == 8);
    for (std::uint32_t type = 0; type < 8; ++type)
        CHECK(transaction.order[type] == type);
    CHECK(transaction.type0_count == 379);
    CHECK(transaction.original_load_calls == 1);
    CHECK(transaction.natural_clear_calls == 1);
    CHECK(transaction.set_variables_mask == 0xFF);
    CHECK(!transaction.global_reset && !transaction.initialize &&
          !transaction.appearance && !transaction.recreate);
    return true;
}

bool CasePartialFailureStopsAndStillLoadsOnce()
{
    const TargetedRebuildPlan plan = MakeEightRecordPlan();
    FakeTransaction transaction = {};
    transaction.type0_count = 385;
    transaction.fail_after_type = 3;
    CHECK(!RunFakeTransaction(plan, &transaction));
    CHECK(transaction.rebuild_calls == 4);
    CHECK(transaction.original_load_calls == 1);
    CHECK(transaction.natural_clear_calls == 1);
    CHECK(!transaction.global_reset && !transaction.initialize &&
          !transaction.appearance && !transaction.recreate);
    return true;
}

}  // namespace

int main()
{
    struct NamedCase { const char* name; bool (*run)(); };
    const NamedCase cases[] = {
        {"pending token generation and single use",
         CasePendingTokenIsGenerationSafeAndSingleUse},
        {"exact token matches load", CaseExactTokenMatchesLoad},
        {"different state generation rejects",
         CaseDifferentStateGenerationRejects},
        {"wrong state or variables rejects", CaseWrongStateOrVariablesRejects},
        {"thread selector epoch and cache reject",
         CaseThreadSelectorEpochAndCacheReject},
        {"source readiness rejects", CaseSourceReadinessRejects},
        {"exact record plus twenty maps", CaseExactRecordPlusTwentyMaps},
        {"blind subtraction and wrong type reject",
         CaseBlindSubtractionAndWrongTypeReject},
        {"misaligned and out of bounds reject",
         CaseMisalignedAndOutOfBoundsReject},
        {"invalid vector bounds reject", CaseInvalidVectorBoundsReject},
        {"duplicate mapping deduplicated and sorted",
         CaseDuplicateMappingIsDeduplicatedAndSorted},
        {"ambiguous duplicate rejects", CaseAmbiguousDuplicateRejects},
        {"successful eight record transaction",
         CaseSuccessfulEightRecordTransaction},
        {"partial failure stops and still loads once",
         CasePartialFailureStopsAndStillLoadsOnce},
    };
    for (const NamedCase& item : cases) {
        if (!item.run()) {
            std::fprintf(stderr, "[FAIL] %s\n", item.name);
            return 1;
        }
        std::printf("[PASS] %s\n", item.name);
    }
    std::printf("player preset targeted rebuild cases passed: %zu\n",
                sizeof(cases) / sizeof(cases[0]));
    return 0;
}
