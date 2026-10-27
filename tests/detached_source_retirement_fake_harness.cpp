#include "fixes/custom_map_asset_isolation/DetachedSourceRetirement.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace retire = dluma::fixes::custom_map_asset_isolation::provider_retirement;
using retire::RetirementResult;
using retire::SourcePhase;
using retire::MutationOutcome;
static_assert(std::is_trivially_copyable<retire::DetachedSourceRetirementLedger>::value,
    "retirement ledger must stay bounded POD");

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return false; } } while (0)

namespace {
struct FakeSource {
    bool mounted = true;
    bool alive = true;
    int contents = 73;
    unsigned detach_calls = 0;
    unsigned destroy_calls = 0;
    std::uint64_t registry_generation = 10;
    MutationOutcome detach_result = MutationOutcome::Succeeded;
    MutationOutcome destroy_result = MutationOutcome::Succeeded;
    bool reject_attached_verification = false;
    bool reject_detached_verification = false;
    bool late_during_detach = false;
    bool late_during_destroy_verification = false;
    bool late_during_destroy = false;
    retire::DetachedSourceRetirementLedger* ledger = nullptr;
    retire::SourceHandle handle = {};
    unsigned rejected_consumers = 0;
};
bool Verify(void* user, const retire::SourceIdentity* identity,
    retire::SourceLocation location, std::uint64_t* generation) noexcept {
    auto& source = *static_cast<FakeSource*>(user);
    if (identity->source != reinterpret_cast<std::uintptr_t>(&source) || !source.alive)
        return false;
    *generation = source.registry_generation;
    if (location == retire::SourceLocation::Mounted)
        return source.mounted && !source.reject_attached_verification;
    if (source.late_during_destroy_verification) {
        retire::ConsumerToken token = {};
        if (retire::BeginSourceConsumer(source.ledger, source.handle, &token) ==
            RetirementResult::LateConsumer) ++source.rejected_consumers;
    }
    return !source.mounted && !source.reject_detached_verification;
}
MutationOutcome Detach(void* user, const retire::SourceIdentity*) noexcept {
    auto& source = *static_cast<FakeSource*>(user);
    ++source.detach_calls;
    if (source.detach_result == MutationOutcome::Succeeded) {
        source.mounted = false;
        ++source.registry_generation;
    }
    if (source.late_during_detach) {
        retire::ConsumerToken token = {};
        if (retire::BeginSourceConsumer(source.ledger, source.handle, &token) ==
            RetirementResult::LateConsumer) ++source.rejected_consumers;
    }
    return source.detach_result;
}
MutationOutcome Destroy(void* user, const retire::SourceIdentity*) noexcept {
    auto& source = *static_cast<FakeSource*>(user);
    ++source.destroy_calls;
    if (source.late_during_destroy) {
        retire::ConsumerToken token = {};
        if (retire::BeginSourceConsumer(source.ledger, source.handle, &token) ==
            RetirementResult::LateConsumer) ++source.rejected_consumers;
    }
    if (source.destroy_result == MutationOutcome::Succeeded) source.alive = false;
    return source.destroy_result;
}
struct Fixture {
    retire::DetachedSourceRetirementLedger ledger = {};
    FakeSource source;
    retire::SourceIdentity identity = {};
    retire::SourceHandle handle = {};
    retire::RetirementContext context = {31, 10, 4};
    retire::RetirementCallbacks callbacks = {Verify, Detach, Destroy, &source, true, true};
    Fixture() {
        (void)retire::InitializeLedger(&ledger, 7);
        identity.source = reinterpret_cast<std::uintptr_t>(&source);
        identity.source_vtable = reinterpret_cast<std::uintptr_t>(&callbacks);
        identity.original_registry_link = reinterpret_cast<std::uintptr_t>(&identity);
        identity.source_generation = 3;
        identity.owner_generation = 2;
        identity.epoch = 4;
        identity.successful_add_ordinal = 1;
        std::strcpy(identity.canonical_path, "generic/project/data");
        (void)retire::RegisterSource(&ledger, &identity,
            retire::SourceOwnership::ExclusivePathCreated, &handle);
        source.ledger = &ledger;
        source.handle = handle;
    }
    retire::SourceRecord record() {
        retire::SourceRecord output = {};
        (void)retire::SnapshotSource(&ledger, handle, &output);
        return output;
    }
    retire::ConsumerReleaseProof proof() {
        auto value = record();
        return {{context.transaction_generation, source.registry_generation, context.through_epoch},
            context.transaction_generation, value.consumer_revision, true};
    }
    RetirementResult detach() { return retire::DetachSource(&ledger, handle, &context, &callbacks); }
    RetirementResult destroy() {
        auto value = proof();
        return retire::DestroyDetachedSource(&ledger, handle, &value, &callbacks);
    }
};

bool LiveConsumerSurvivesDetachAndReleasesBeforeDestruction() {
    Fixture f;
    retire::ConsumerToken token = {};
    CHECK(retire::BeginSourceConsumer(&f.ledger, f.handle, &token) == RetirementResult::Succeeded);
    CHECK(f.detach() == RetirementResult::Succeeded);
    CHECK(!f.source.mounted && f.source.alive && f.source.contents == 73);
    CHECK(f.source.destroy_calls == 0 && f.record().active_consumers == 1);
    CHECK(f.destroy() == RetirementResult::ActiveConsumers);
    CHECK(retire::EndSourceConsumer(&f.ledger, &token) == RetirementResult::Succeeded);
    auto incomplete = f.proof();
    incomplete.all_participating_consumers_released = false;
    CHECK(retire::DestroyDetachedSource(&f.ledger, f.handle, &incomplete, &f.callbacks) ==
        RetirementResult::ConsumerReleaseIncomplete);
    CHECK(f.source.alive && f.source.destroy_calls == 0);
    CHECK(f.destroy() == RetirementResult::Succeeded);
    CHECK(!f.source.alive && f.source.destroy_calls == 1);
    CHECK(f.destroy() == RetirementResult::AlreadyComplete);
    CHECK(f.detach() == RetirementResult::AlreadyComplete);
    CHECK(f.source.destroy_calls == 1 && f.source.detach_calls == 1);
    return true;
}
bool RefusesBaselineSharedBorrowedAndUnknown() {
    for (auto ownership : {retire::SourceOwnership::Baseline, retire::SourceOwnership::Shared,
        retire::SourceOwnership::Borrowed, retire::SourceOwnership::Unknown}) {
        Fixture f;
        retire::SourceHandle handle = {};
        CHECK(retire::RegisterSource(&f.ledger, &f.identity, ownership, &handle) ==
            RetirementResult::OwnershipRejected);
        CHECK(f.source.detach_calls == 0 && f.source.destroy_calls == 0);
    }
    Fixture f;
    retire::SourceHandle handle = {};
    auto changed = f.identity;
    changed.source_generation++;
    CHECK(retire::RegisterSource(&f.ledger, &changed,
        retire::SourceOwnership::ExclusivePathCreated, &handle) == RetirementResult::IdentityConflict);
    changed = f.identity;
    changed.source_vtable = 0;
    CHECK(retire::RegisterSource(&f.ledger, &changed,
        retire::SourceOwnership::ExclusivePathCreated, &handle) == RetirementResult::InvalidArgument);
    CHECK(!retire::ExactSourceIdentityEqual(f.identity, changed));
    return true;
}
bool NativeMutationEvidenceDefaultsClosed() {
    Fixture f;
    f.callbacks.exact_detach_without_destroy_validated = false;
    CHECK(f.detach() == RetirementResult::EvidenceUnavailable && f.source.detach_calls == 0);
    f.callbacks.exact_detach_without_destroy_validated = true;
    CHECK(f.detach() == RetirementResult::Succeeded);
    f.callbacks.exact_destroy_ownership_validated = false;
    CHECK(f.destroy() == RetirementResult::EvidenceUnavailable);
    CHECK(f.source.alive && f.source.destroy_calls == 0);
    return true;
}
bool DetachedSourceRetainedAfterConsumerResetFailure() {
    Fixture a;
    Fixture b;
    CHECK(a.detach() == RetirementResult::Succeeded);
    b.source.detach_result = MutationOutcome::FailedWithoutMutation;
    CHECK(b.detach() == RetirementResult::MutationFailed);
    auto barrier = a.proof();
    barrier.all_participating_consumers_released = false;
    CHECK(retire::DestroyDetachedSource(&a.ledger, a.handle, &barrier, &a.callbacks) ==
        RetirementResult::ConsumerReleaseIncomplete);
    CHECK(a.record().phase == SourcePhase::Detached && a.source.alive);
    CHECK(b.record().phase == SourcePhase::Mounted && b.source.alive);
    CHECK(a.source.destroy_calls == 0 && b.source.destroy_calls == 0);
    CHECK(retire::InitializeLedger(&a.ledger, 7) == RetirementResult::AlreadyComplete);
    CHECK(a.record().phase == SourcePhase::Detached);
    CHECK(retire::InitializeLedger(&a.ledger, 8) == RetirementResult::GenerationMismatch);
    CHECK(a.record().phase == SourcePhase::Detached);
    return true;
}
bool UncertainDetachNeverDestroysOrRetries() {
    Fixture f;
    f.source.detach_result = MutationOutcome::Uncertain;
    CHECK(f.detach() == RetirementResult::MutationUncertain);
    CHECK(f.record().phase == SourcePhase::Quarantined && f.source.alive);
    CHECK(f.detach() == RetirementResult::InvalidPhase);
    CHECK(f.destroy() == RetirementResult::InvalidPhase);
    CHECK(f.source.detach_calls == 1 && f.source.destroy_calls == 0);
    Fixture g;
    g.source.reject_detached_verification = true;
    CHECK(g.detach() == RetirementResult::MutationUncertain);
    CHECK(!g.source.mounted && g.source.alive);
    CHECK(g.record().phase == SourcePhase::Quarantined && g.source.destroy_calls == 0);
    return true;
}
bool FailedDestroyRetainsOwnershipAndUncertainDestroyNeverRepeats() {
    Fixture f;
    CHECK(f.detach() == RetirementResult::Succeeded);
    f.source.destroy_result = MutationOutcome::FailedWithoutMutation;
    CHECK(f.destroy() == RetirementResult::MutationFailed);
    CHECK(f.record().phase == SourcePhase::Detached && f.source.alive);
    f.source.destroy_result = MutationOutcome::Uncertain;
    CHECK(f.destroy() == RetirementResult::MutationUncertain);
    CHECK(f.record().phase == SourcePhase::Quarantined);
    CHECK(f.destroy() == RetirementResult::InvalidPhase);
    CHECK(f.source.destroy_calls == 2);
    return true;
}
bool LateConsumersBeforeDestroyPreventFree() {
    Fixture f;
    CHECK(f.detach() == RetirementResult::Succeeded);
    f.source.late_during_destroy_verification = true;
    CHECK(f.destroy() == RetirementResult::LateConsumer);
    CHECK(f.source.rejected_consumers == 1 && f.source.destroy_calls == 0 && f.source.alive);
    CHECK(f.destroy() == RetirementResult::LateConsumer);
    Fixture g;
    g.source.late_during_detach = true;
    CHECK(g.detach() == RetirementResult::MutationUncertain);
    CHECK(g.record().phase == SourcePhase::Quarantined && g.source.destroy_calls == 0);
    return true;
}
bool AdmissionDuringDestructionIsRejectedAndCannotDoubleFree() {
    Fixture f;
    CHECK(f.detach() == RetirementResult::Succeeded);
    f.source.late_during_destroy = true;
    CHECK(f.destroy() == RetirementResult::LateConsumer);
    CHECK(f.source.rejected_consumers == 1 && f.source.destroy_calls == 1);
    CHECK(f.record().phase == SourcePhase::Destroyed);
    CHECK(f.destroy() == RetirementResult::AlreadyComplete && f.source.destroy_calls == 1);
    return true;
}
bool StaleReservationsCannotConsumeOtherConsumers() {
    Fixture f;
    retire::ConsumerToken first = {}, second = {};
    CHECK(retire::BeginSourceConsumer(&f.ledger, f.handle, &first) == RetirementResult::Succeeded);
    auto copy = first;
    CHECK(retire::BeginSourceConsumer(&f.ledger, f.handle, &first) == RetirementResult::InvalidArgument);
    CHECK(first.reservation == copy.reservation && f.record().active_consumers == 1);
    CHECK(retire::EndSourceConsumer(&f.ledger, &first) == RetirementResult::Succeeded);
    CHECK(retire::BeginSourceConsumer(&f.ledger, f.handle, &second) == RetirementResult::Succeeded);
    CHECK(retire::EndSourceConsumer(&f.ledger, &copy) == RetirementResult::StaleHandle);
    CHECK(f.record().active_consumers == 1);
    CHECK(f.detach() == RetirementResult::Succeeded);
    CHECK(f.destroy() == RetirementResult::ActiveConsumers);
    CHECK(retire::EndSourceConsumer(&f.ledger, &second) == RetirementResult::Succeeded);
    CHECK(f.destroy() == RetirementResult::Succeeded);
    return true;
}
bool GenerationAndRevisionMismatchPreventCallbacks() {
    Fixture f;
    ++f.context.source_registry_generation;
    CHECK(f.detach() == RetirementResult::VerificationFailed && f.source.detach_calls == 0);
    --f.context.source_registry_generation;
    CHECK(f.detach() == RetirementResult::Succeeded);
    auto p = f.proof();
    ++p.context.transaction_generation;
    CHECK(retire::DestroyDetachedSource(&f.ledger, f.handle, &p, &f.callbacks) == RetirementResult::GenerationMismatch);
    p = f.proof(); ++p.source_consumer_revision;
    CHECK(retire::DestroyDetachedSource(&f.ledger, f.handle, &p, &f.callbacks) == RetirementResult::ConsumerReleaseIncomplete);
    p = f.proof(); ++p.completed_release_generation;
    CHECK(retire::DestroyDetachedSource(&f.ledger, f.handle, &p, &f.callbacks) == RetirementResult::ConsumerReleaseIncomplete);
    p = f.proof(); ++p.context.source_registry_generation;
    CHECK(retire::DestroyDetachedSource(&f.ledger, f.handle, &p, &f.callbacks) == RetirementResult::VerificationFailed);
    CHECK(f.source.destroy_calls == 0 && f.source.alive);
    return true;
}
bool ReusedAddressesAndSlotsRejectOldHandles() {
    Fixture f;
    const auto old = f.handle;
    for (unsigned index = 0; index < 2 * retire::kMaximumRetirementSources; ++index) {
        CHECK(f.detach() == RetirementResult::Succeeded);
        CHECK(f.destroy() == RetirementResult::Succeeded);
        retire::SourceHandle repeated_registration = {};
        CHECK(retire::RegisterSource(&f.ledger, &f.identity,
            retire::SourceOwnership::ExclusivePathCreated, &repeated_registration) ==
            RetirementResult::IdentityConflict);
        f.source.alive = true; f.source.mounted = true;
        ++f.identity.source_generation;
        ++f.identity.epoch;
        ++f.context.transaction_generation;
        f.context.through_epoch = f.identity.epoch;
        f.context.source_registry_generation = f.source.registry_generation;
        CHECK(retire::RegisterSource(&f.ledger, &f.identity,
            retire::SourceOwnership::ExclusivePathCreated, &f.handle) == RetirementResult::Succeeded);
        f.source.handle = f.handle;
    }
    retire::SourceRecord record = {};
    CHECK(retire::SnapshotSource(&f.ledger, old, &record) == RetirementResult::StaleHandle);
    CHECK(f.record().phase == SourcePhase::Mounted);
    return true;
}
bool LostObservationRetainsEverySource() {
    Fixture f;
    retire::ConsumerToken token = {};
    CHECK(retire::BeginSourceConsumer(&f.ledger, f.handle, &token) == RetirementResult::Succeeded);
    f.ledger.lock = 1;
    CHECK(retire::EndSourceConsumer(&f.ledger, &token) == RetirementResult::ObservationLost);
    f.ledger.lock = 0;
    CHECK(retire::EndSourceConsumer(&f.ledger, &token) == RetirementResult::Succeeded);
    CHECK(f.detach() == RetirementResult::ObservationLost);
    CHECK(f.source.alive && f.source.mounted && f.source.detach_calls == 0);
    return true;
}
bool CapacityFailureDoesNotEvictRetainedSources() {
    Fixture f;
    for (std::size_t index = 1; index < retire::kMaximumRetirementSources; ++index) {
        auto key = f.identity;
        key.source += index;
        retire::SourceHandle handle = {};
        CHECK(retire::RegisterSource(&f.ledger, &key,
            retire::SourceOwnership::ExclusivePathCreated, &handle) == RetirementResult::Succeeded);
    }
    auto key = f.identity; key.source += retire::kMaximumRetirementSources;
    retire::SourceHandle handle = {};
    CHECK(retire::RegisterSource(&f.ledger, &key,
        retire::SourceOwnership::ExclusivePathCreated, &handle) == RetirementResult::CapacityExceeded);
    CHECK(f.record().phase == SourcePhase::Mounted && f.source.alive);
    return true;
}
}  // namespace

int main() {
    const struct { const char* name; bool (*run)(); } cases[] = {
        {"consumer lifetime across detach", LiveConsumerSurvivesDetachAndReleasesBeforeDestruction},
        {"ownership refusals", RefusesBaselineSharedBorrowedAndUnknown},
        {"native evidence gate", NativeMutationEvidenceDefaultsClosed},
        {"failed transaction retains detached source", DetachedSourceRetainedAfterConsumerResetFailure},
        {"uncertain detach quarantines", UncertainDetachNeverDestroysOrRetries},
        {"destruction failure retains ownership", FailedDestroyRetainsOwnershipAndUncertainDestroyNeverRepeats},
        {"late consumer before retirement", LateConsumersBeforeDestroyPreventFree},
        {"admission during destruction", AdmissionDuringDestructionIsRejectedAndCannotDoubleFree},
        {"reservation ABA", StaleReservationsCannotConsumeOtherConsumers},
        {"generation and barrier revision", GenerationAndRevisionMismatchPreventCallbacks},
        {"repeated lifetime and address reuse", ReusedAddressesAndSlotsRejectOldHandles},
        {"lost observation refuses mutation", LostObservationRetainsEverySource},
        {"bounded capacity preserves ownership", CapacityFailureDoesNotEvictRetainedSources},
    };
    unsigned passed = 0;
    for (const auto& test : cases) {
        if (!test.run()) { std::printf("FAILED: %s\n", test.name); return 1; }
        ++passed;
    }
    std::printf("Detached source retirement: %u scenarios passed\n", passed);
    return 0;
}
