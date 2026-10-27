#include "fixes/custom_map_asset_isolation/CustomContentLedger.h"
#include <cstdio>
#include <cstring>

namespace content = dluma::fixes::custom_map_asset_isolation::content;
#define CHECK(value) do { if (!(value)) { std::printf("FAIL %d: %s\n", __LINE__, #value); return false; } } while (0)
static content::CustomContentLedger ledger;
static content::LedgerDiagnosticEvent diagnostics[content::kMaximumDiagnostics];
static content::LedgerSnapshot snapshot;
static void* Pointer(std::uintptr_t value) { return reinterpret_cast<void*>(value); }
static std::uint64_t Sequence() { return content::NextSequence(&ledger); }

static std::uint32_t Begin(const char* name, const char* root)
{
    return content::BeginEpoch(&ledger, name, nullptr, root, Sequence());
}

static bool Source(std::uint32_t epoch, const char* root, void* pointer, bool owned,
                   content::SourceResetSnapshot* token)
{
    CHECK(content::RecordSuccessfulSourceAddForEpoch(&ledger, epoch, pointer, root, root,
        0, content::CustomSourceKind::UnknownProjectSource, owned, Sequence(), true));
    for (std::uint32_t i = 0; i < ledger.source_count; ++i)
        if (ledger.sources[i].epoch == epoch && ledger.sources[i].source == pointer)
            return content::SnapshotSourceMutationToken(&ledger, i, epoch, token);
    return false;
}

static bool File(std::uint32_t epoch, const char* path, const content::SourceResetSnapshot& source,
                 content::CustomFileMutationToken* token)
{
    content::CustomSourceIdentitySnapshot identity = {};
    CHECK(content::LookupExactCustomSourceIdentity(&ledger, source.source, source.normalized_path, &identity));
    std::uint32_t index = UINT32_MAX;
    CHECK(content::RecordCustomFileOpen(&ledger, epoch, path, source.normalized_path, source.source,
        0x1000, 0x1000, "fixture.dll", 1, Sequence(), &index, &identity));
    CHECK(content::SnapshotFileMutationToken(&ledger, index, epoch, token));
    CHECK(content::ClassifyFile(&ledger, index, content::ContentLifetimeClass::SourceOnly, 0, true, Sequence(), token));
    return true;
}

static bool Finish(std::uint32_t epoch, const content::SourceResetSnapshot& source)
{
    CHECK(content::CloseEpochForFrontend(&ledger, epoch, Sequence()));
    CHECK(content::MarkSourceResetResult(&ledger, source.record_index, true, Sequence(), &source));
    CHECK(content::MarkEpochConsumed(&ledger, epoch, Sequence()));
    CHECK(content::MarkEpochComponentResults(&ledger, epoch, true, true, true, true, Sequence()));
    return true;
}

static bool Candidate(std::uint32_t epoch, content::VerifiedEpochRetirement* proof)
{
    CHECK(content::CaptureEpochRetirementCandidate(&ledger, epoch, proof));
    proof->verified_transaction_token = 10000U + epoch;
    proof->observer_quiescence_token = 20000U + epoch;
    proof->transaction_verified = true;
    proof->observer_admission_closed = true;
    return true;
}

static bool StressReusePreservesOtherOwners()
{
    content::Initialize(&ledger);
    CHECK(content::MarkBaselineCaptured(&ledger, true, Sequence()));
    const auto baseline = Begin("baseline", "x:/fixture/global");
    content::SourceResetSnapshot global_source = {};
    CHECK(Source(baseline, "x:/fixture/global/data", Pointer(0xA000), false, &global_source));
    const auto saved_global = ledger.sources[global_source.record_index];
    const auto retained = Begin("retained", "x:/fixture/retained");
    content::SourceResetSnapshot retained_source = {};
    CHECK(Source(retained, "x:/fixture/retained/data", Pointer(0xB000), true, &retained_source));
    const auto saved_retained = ledger.sources[retained_source.record_index];
    content::SourceResetSnapshot stale_source = {};
    content::CustomFileMutationToken stale_file = {};
    content::CustomPackMutationToken stale_pack = {};
    std::uint32_t previous_epoch = retained, previous_ordinal = retained_source.successful_add_ordinal;
    std::uint64_t previous_fingerprint = 0;
    for (unsigned int cycle = 0; cycle < 96; ++cycle) {
        const char* root = cycle % 3 == 1 ? "x:/fixture/b" : "x:/fixture/a";
        const auto epoch = Begin(cycle % 3 == 1 ? "B" : "A", root);
        CHECK(epoch > previous_epoch);
        content::SourceResetSnapshot source = {};
        CHECK(Source(epoch, root, Pointer(0xC000), true, &source));
        CHECK(source.successful_add_ordinal > previous_ordinal);
        content::SourceResetSnapshot natural_token = {};
        CHECK(content::SnapshotNaturalSourceMutationToken(&ledger, nullptr, root, &natural_token));
        CHECK(natural_token.epoch == epoch && natural_token.successful_add_ordinal == source.successful_add_ordinal);
        content::CustomFileMutationToken file = {};
        CHECK(File(epoch, "data/scripts/shared.scr", source, &file));
        char hash[65]; std::snprintf(hash, sizeof(hash), "%064x", cycle + 1U);
        CHECK(content::RecordFileParserObservation(&ledger, file.record_index, content::WatchedParserOutcome::ParsedNew,
            hash, cycle + 1U, "shared", 0, 0xD000, cycle + 1U, epoch, Sequence(), &file));
        char pack_path[128]; std::snprintf(pack_path, sizeof(pack_path), "%s/data.pack", root);
        CHECK(content::RecordPackLoad(&ledger, Pointer(0xE000), pack_path, Sequence()));
        content::CustomPackMutationToken pack = {};
        CHECK(content::SnapshotPackMutationToken(&ledger, Pointer(0xE000), pack_path, epoch, &pack));
        if (cycle) {
            CHECK(source.record_index == stale_source.record_index && file.record_index == stale_file.record_index);
            CHECK(!content::MarkSourceResetResult(&ledger, source.record_index, true, Sequence()));
            CHECK(!content::MarkSourceResetResult(&ledger, source.record_index, true, Sequence(), &stale_source));
            CHECK(!content::RecordNaturalSourceRemovalExact(&ledger, &stale_source, true, Sequence()));
            CHECK(!content::RecordNaturalSourceRemoval(&ledger, source.source, root, true, Sequence()));
            CHECK(!content::ClassifyFile(&ledger, file.record_index, content::ContentLifetimeClass::Unknown, 0, false, Sequence()));
            CHECK(!content::ClassifyFile(&ledger, file.record_index, content::ContentLifetimeClass::Unknown, 0, false, Sequence(), &stale_file));
            CHECK(!content::RecordFileOpenContract(&ledger, file.record_index, 2, 0, &stale_file));
            CHECK(!content::RecordProviderProbe(&ledger, file.record_index, content::BaselineProviderDisposition::CustomOnly, false, false, Sequence(), &stale_file));
            CHECK(!content::RecordFileParserObservation(&ledger, file.record_index, content::WatchedParserOutcome::ParseFailed,
                "bad", 0, "stale", 0, 0, 0, stale_file.epoch, Sequence(), &stale_file));
            CHECK(!content::RecordPackRelease(&ledger, Pointer(0xE000), pack_path, Sequence(), &stale_pack));
            CHECK(!content::RecordPackRelease(&ledger, Pointer(0xE000), pack_path, Sequence()));
            CHECK(ledger.sources[source.record_index].active && ledger.files[file.record_index].classification_complete);
        } else { stale_source = source; stale_file = file; stale_pack = pack; }
        CHECK(content::RecordPackRelease(&ledger, Pointer(0xE000), pack_path, Sequence(), &pack));
        CHECK(Finish(epoch, source));
        content::VerifiedEpochRetirement proof = {};
        CHECK(Candidate(epoch, &proof));
        content::LedgerRetirementSummary summary = {};
        const auto previous_retired_epochs=ledger.retirement_history.epochs;
        CHECK(content::CheckEpochRetirement(&ledger,&proof)==content::EpochRetirementResult::Ready);
        content::CustomContentEpoch still_present={};
        CHECK(content::SnapshotEpoch(&ledger,epoch,&still_present) && still_present.consumed);
        CHECK(ledger.retirement_history.epochs==previous_retired_epochs && ledger.files[file.record_index].open_count==1);
        CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::Retired);
        CHECK(summary.epoch == epoch && summary.sources == 1 && summary.files == 1 && summary.packs == 1 && summary.file_opens == 1);
        CHECK(summary.records_fingerprint != previous_fingerprint);
        CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::EpochMissing);
        CHECK(std::memcmp(&saved_global, &ledger.sources[global_source.record_index], sizeof(saved_global)) == 0);
        CHECK(std::memcmp(&saved_retained, &ledger.sources[retained_source.record_index], sizeof(saved_retained)) == 0);
        CHECK(!ledger.files[file.record_index].open_count && !ledger.packs[pack.record_index].epoch);
        CHECK(content::CountUnknownPersistentFiles(&ledger) == 0);
        CHECK(!content::CanReportBaselineMatch(&ledger)); /* retained live owner still blocks */
        CHECK(ledger.epoch_count == 3 && ledger.source_count == 3 && ledger.file_count == 1 && ledger.pack_count == 1);
        CHECK(!ledger.ownership_mutation_loss && !ledger.broad_file_observation_loss);
        previous_epoch = epoch; previous_ordinal = source.successful_add_ordinal; previous_fingerprint = proof.records_fingerprint;
        (void)content::DrainDiagnostics(&ledger, diagnostics, content::kMaximumDiagnostics);
    }
    content::LedgerRetirementHistory history = {};
    CHECK(content::SnapshotRetirementHistory(&ledger, &history));
    CHECK(history.epochs == 96 && history.sources == 96 && history.files == 96 && history.packs == 96 && history.file_opens == 96);
    CHECK(history.summaries_overwritten == 96 - content::kMaximumRetirementSummaries);
    CHECK(content::CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.sources[global_source.record_index].source == saved_global.source);
    return true;
}

static bool RejectUnverifiedAndChangedEvidence()
{
    content::Initialize(&ledger);
    const auto epoch = Begin("candidate", "x:/fixture/candidate");
    content::SourceResetSnapshot source = {};
    CHECK(Source(epoch, "x:/fixture/candidate", Pointer(0x1000), true, &source));
    content::CustomFileMutationToken file = {};
    CHECK(File(epoch, "data/scripts/shared.scr", source, &file));
    content::VerifiedEpochRetirement proof = {};
    content::LedgerRetirementSummary summary = {};
    CHECK(Candidate(epoch, &proof));
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::EpochNotConsumed);
    CHECK(Finish(epoch, source));
    CHECK(Candidate(epoch, &proof));
    proof.transaction_verified = false;
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::InvalidReceipt);
    proof.transaction_verified = true; proof.pending_observer_callbacks = 1;
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::InvalidReceipt);
    proof.pending_observer_callbacks = 0; proof.external_active_references = 1;
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::InvalidReceipt);
    proof.external_active_references = 0; proof.quarantined = true;
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::InvalidReceipt);
    proof.quarantined = false;
    CHECK(content::RecordFileOpenContract(&ledger, file.record_index, 9, 0, &file));
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::FenceChanged);
    CHECK(Candidate(epoch, &proof));
    ledger.lock = 1;
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::LedgerBusy);
    ledger.lock = 0;
    content::RecordOwnershipMutationLoss(&ledger, content::LedgerLossReason::SourceAddContention);
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::OwnershipIncomplete);
    CHECK(ledger.ownership_mutation_loss == 1 && ledger.sources[source.record_index].add_succeeded);
    CHECK(!ledger.retirement_history.epochs);
    return true;
}

static bool RejectCrossEpochReferencesAndGlobalHistory()
{
    content::Initialize(&ledger);
    const auto first = Begin("first", "x:/fixture/first");
    content::SourceResetSnapshot source = {};
    CHECK(Source(first, "x:/fixture/first", Pointer(0x1000), true, &source));
    CHECK(Finish(first, source));
    const auto second = Begin("second", "x:/fixture/second");
    content::SourceResetSnapshot second_source = {};
    CHECK(Source(second, "x:/fixture/second", Pointer(0x2000), true, &second_source));
    content::CustomFileMutationToken file = {};
    CHECK(File(second, "data/scripts/shared.scr", second_source, &file));
    CHECK(content::RecordFileParserObservation(&ledger, file.record_index, content::WatchedParserOutcome::CacheHitPriorEpoch,
        "retained", 1, "shared", 0x5000, 0x5000, 1, first, Sequence(), &file));
    content::VerifiedEpochRetirement proof = {};
    content::LedgerRetirementSummary summary = {};
    CHECK(Candidate(first, &proof));
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::ResourcesLive);
    CHECK(ledger.sources[source.record_index].add_succeeded && !ledger.retirement_history.epochs);

    content::Initialize(&ledger);
    const auto global = Begin("global", "x:/fixture/global");
    CHECK(Source(global, "x:/fixture/global", Pointer(0x3000), false, &source));
    CHECK(Finish(global, source));
    CHECK(Candidate(global, &proof));
    CHECK(content::RetireVerifiedEpoch(&ledger, &proof, Sequence(), &summary) == content::EpochRetirementResult::GlobalRecordRetained);
    CHECK(ledger.sources[source.record_index].add_succeeded);

    content::Initialize(&ledger);
    ledger.next_epoch_id = UINT32_MAX;
    const auto last = Begin("last", "x:/fixture/last");
    CHECK(last == UINT32_MAX && ledger.next_epoch_id == 0);
    CHECK(content::CloseEpochForFrontend(&ledger, last, Sequence()));
    CHECK(!Begin("would-wrap", "x:/fixture/new"));
    CHECK(ledger.next_epoch_id == 0 && ledger.ownership_mutation_loss);
    return true;
}

static bool NaturalNestedRemovalUsesOneCapturedIncarnation()
{
    content::Initialize(&ledger);
    const auto epoch = Begin("natural", "x:/fixture/natural");
    content::SourceResetSnapshot source = {}, path_token = {}, object_token = {};
    CHECK(Source(epoch, "x:/fixture/natural/data", Pointer(0x1000), true, &source));
    CHECK(content::SnapshotNaturalSourceMutationToken(&ledger, nullptr, "X:/FIXTURE/NATURAL/DATA", &path_token));
    CHECK(content::SnapshotNaturalSourceMutationToken(&ledger, Pointer(0x1000), nullptr, &object_token));
    CHECK(path_token.successful_add_ordinal == object_token.successful_add_ordinal);
    CHECK(content::RecordNaturalSourceRemovalExact(&ledger, &object_token, true, Sequence()));
    CHECK(content::RecordNaturalSourceRemovalExact(&ledger, &path_token, true, Sequence()));
    CHECK(!ledger.sources[source.record_index].active && !ledger.ownership_mutation_loss);
    CHECK(!content::SnapshotNaturalSourceMutationToken(&ledger, nullptr, "x:/fixture/natural/data", &path_token));
    return true;
}

int main()
{
    if (!StressReusePreservesOtherOwners() || !RejectUnverifiedAndChangedEvidence() ||
        !RejectCrossEpochReferencesAndGlobalHistory() || !NaturalNestedRemovalUsesOneCapturedIncarnation()) return 1;
    std::puts("verified content retirement: 96 A/B/A cycles, same-address changed content, stale token rejection, stable retained indices, archive and fail-closed evidence passed");
    return 0;
}
