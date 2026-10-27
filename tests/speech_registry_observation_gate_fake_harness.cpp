#define _HAS_EXCEPTIONS 0

#include "fixes/custom_map_asset_isolation/SpeechRegistryObservationGate.h"

#include <cstdio>
#include <type_traits>

namespace speech =
    dluma::fixes::custom_map_asset_isolation::speech_registry;

static_assert(std::is_trivially_copyable<
                  speech::SpeechRegistryObservationGate>::value,
              "speech registry gate must remain bounded POD");
static_assert(std::is_standard_layout<
                  speech::SpeechRegistryObservationGate>::value,
              "speech registry gate must remain standard-layout POD");
static_assert(std::is_trivially_copyable<
                  speech::SpeechRegistryMutationToken>::value,
              "writer token must remain POD");
static_assert(std::is_trivially_copyable<
                  speech::SpeechRegistrySnapshotToken>::value,
              "snapshot token must remain POD");
static_assert(std::is_trivially_copyable<
                  speech::SpeechRegistryRevisionCountSnapshot>::value,
              "revision/count snapshot must remain POD");

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

bool CaseInitialRevisionAndCountSnapshot()
{
    speech::SpeechRegistryObservationGate gate = {};
    speech::InitializeSpeechRegistryObservationGate(&gate);
    CHECK(gate.admission_state == 0);
    CHECK(gate.revision == 1);
    CHECK(gate.completed_mutation_count == 0);

    speech::SpeechRegistrySnapshotToken token = {};
    CHECK(speech::TryAcquireSpeechRegistrySnapshot(&gate, &token));
    speech::SpeechRegistryRevisionCountSnapshot snapshot = {};
    CHECK(speech::CompleteSpeechRegistrySnapshot(
        &token, 17, &snapshot));
    CHECK(snapshot.revision == 1);
    CHECK(snapshot.completed_mutation_count == 0);
    CHECK(snapshot.observed_entry_count == 17);
    CHECK(gate.admission_state == 0);
    return true;
}

bool CaseFailedSnapshotIsNonblocking()
{
    speech::SpeechRegistryObservationGate gate = {};
    speech::InitializeSpeechRegistryObservationGate(&gate);
    speech::SpeechRegistryMutationToken writer = {};
    CHECK(speech::BeginSpeechRegistryMutation(&gate, &writer));

    /* This call executes on the owning writer thread.  Waiting would deadlock,
     * so reaching the next assertion deterministically proves try semantics. */
    speech::SpeechRegistrySnapshotToken blocked = {};
    CHECK(!speech::TryAcquireSpeechRegistrySnapshot(&gate, &blocked));
    CHECK(!blocked.active);
    CHECK(speech::EndSpeechRegistryMutation(&writer));
    return true;
}

struct WaitingWriterContext {
    speech::SpeechRegistryObservationGate* gate;
    HANDLE attempted;
    HANDLE admitted;
    HANDLE release;
    volatile LONG* reader_copy_active;
    volatile LONG overlap_observed;
};

DWORD WINAPI WaitForSnapshotThenMutate(void* opaque)
{
    WaitingWriterContext* context =
        static_cast<WaitingWriterContext*>(opaque);
    if (!context || !context->gate) return 1;
    if (!SetEvent(context->attempted)) return 2;
    speech::SpeechRegistryMutationToken writer = {};
    if (!speech::BeginSpeechRegistryMutation(context->gate, &writer))
        return 3;
    if (dluma_atomic_compare_exchange(
            context->reader_copy_active, 0, 0) != 0)
        dluma_atomic_exchange(&context->overlap_observed, 1);
    if (!SetEvent(context->admitted)) return 4;
    if (WaitForSingleObject(context->release, 30000) != WAIT_OBJECT_0)
        return 5;
    return speech::EndSpeechRegistryMutation(&writer) ? 0 : 6;
}

bool CaseWriterWaitsOnlyForBoundedReaderAndNeverOverlaps()
{
    speech::SpeechRegistryObservationGate gate = {};
    speech::InitializeSpeechRegistryObservationGate(&gate);
    speech::SpeechRegistrySnapshotToken reader = {};
    CHECK(speech::TryAcquireSpeechRegistrySnapshot(&gate, &reader));

    volatile LONG reader_copy_active = 1;
    WaitingWriterContext context = {};
    context.gate = &gate;
    context.attempted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    context.admitted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    context.release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    context.reader_copy_active = &reader_copy_active;
    CHECK(context.attempted && context.admitted && context.release);

    HANDLE thread = CreateThread(nullptr, 0, &WaitForSnapshotThenMutate,
                                 &context, 0, nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(context.attempted, 30000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(context.admitted, 0) == WAIT_TIMEOUT);

    dluma_atomic_exchange(&reader_copy_active, 0);
    speech::SpeechRegistryRevisionCountSnapshot before = {};
    CHECK(speech::CompleteSpeechRegistrySnapshot(&reader, 4, &before));
    CHECK(WaitForSingleObject(context.admitted, 30000) == WAIT_OBJECT_0);
    CHECK(context.overlap_observed == 0);
    CHECK(SetEvent(context.release));
    CHECK(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0);
    CHECK(static_cast<std::uint64_t>(gate.revision) ==
          before.revision + 1U);
    CHECK(static_cast<std::uint64_t>(gate.completed_mutation_count) ==
          before.completed_mutation_count + 1U);

    CHECK(CloseHandle(thread));
    CHECK(CloseHandle(context.release));
    CHECK(CloseHandle(context.admitted));
    CHECK(CloseHandle(context.attempted));
    return true;
}

bool CaseNestedWritersNeverWaitForEachOther()
{
    speech::SpeechRegistryObservationGate gate = {};
    speech::InitializeSpeechRegistryObservationGate(&gate);
    speech::SpeechRegistryMutationToken outer = {};
    speech::SpeechRegistryMutationToken inner = {};
    CHECK(speech::BeginSpeechRegistryMutation(&gate, &outer));
    CHECK(speech::BeginSpeechRegistryMutation(&gate, &inner));

    speech::SpeechRegistrySnapshotToken blocked = {};
    CHECK(!speech::TryAcquireSpeechRegistrySnapshot(&gate, &blocked));
    CHECK(speech::EndSpeechRegistryMutation(&inner));
    CHECK(gate.revision == 2);
    CHECK(gate.completed_mutation_count == 1);
    CHECK(!speech::TryAcquireSpeechRegistrySnapshot(&gate, &blocked));
    CHECK(speech::EndSpeechRegistryMutation(&outer));
    CHECK(gate.revision == 3);
    CHECK(gate.completed_mutation_count == 2);
    CHECK(speech::TryAcquireSpeechRegistrySnapshot(&gate, &blocked));
    speech::AbandonSpeechRegistrySnapshot(&blocked);
    return true;
}

struct ConcurrentWriterContext {
    speech::SpeechRegistryObservationGate* gate;
    HANDLE admitted;
    HANDLE release;
};

DWORD WINAPI EnterBesideExistingWriter(void* opaque)
{
    ConcurrentWriterContext* context =
        static_cast<ConcurrentWriterContext*>(opaque);
    if (!context || !context->gate) return 1;
    speech::SpeechRegistryMutationToken writer = {};
    if (!speech::BeginSpeechRegistryMutation(context->gate, &writer))
        return 2;
    if (!SetEvent(context->admitted)) return 3;
    if (WaitForSingleObject(context->release, 30000) != WAIT_OBJECT_0)
        return 4;
    return speech::EndSpeechRegistryMutation(&writer) ? 0 : 5;
}

bool CaseConcurrentWriterIsAdmittedBesideWriter()
{
    speech::SpeechRegistryObservationGate gate = {};
    speech::InitializeSpeechRegistryObservationGate(&gate);
    speech::SpeechRegistryMutationToken first = {};
    CHECK(speech::BeginSpeechRegistryMutation(&gate, &first));

    ConcurrentWriterContext context = {};
    context.gate = &gate;
    context.admitted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    context.release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(context.admitted && context.release);
    HANDLE thread = CreateThread(nullptr, 0, &EnterBesideExistingWriter,
                                 &context, 0, nullptr);
    CHECK(thread != nullptr);

    /* The second writer must not wait for the first writer. */
    CHECK(WaitForSingleObject(context.admitted, 30000) == WAIT_OBJECT_0);
    speech::SpeechRegistrySnapshotToken blocked = {};
    CHECK(!speech::TryAcquireSpeechRegistrySnapshot(&gate, &blocked));
    CHECK(speech::EndSpeechRegistryMutation(&first));
    CHECK(!speech::TryAcquireSpeechRegistrySnapshot(&gate, &blocked));
    CHECK(SetEvent(context.release));
    CHECK(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0);
    CHECK(gate.revision == 3);
    CHECK(gate.completed_mutation_count == 2);

    CHECK(CloseHandle(thread));
    CHECK(CloseHandle(context.release));
    CHECK(CloseHandle(context.admitted));
    return true;
}

bool CaseRevisionIsMonotonicAndStampIsStable()
{
    speech::SpeechRegistryObservationGate gate = {};
    speech::InitializeSpeechRegistryObservationGate(&gate);
    std::uint64_t previous_revision = 1;
    for (std::uint32_t index = 0; index < 128; ++index) {
        speech::SpeechRegistryMutationToken writer = {};
        CHECK(speech::BeginSpeechRegistryMutation(&gate, &writer));
        CHECK(speech::EndSpeechRegistryMutation(&writer));

        speech::SpeechRegistrySnapshotToken reader = {};
        CHECK(speech::TryAcquireSpeechRegistrySnapshot(&gate, &reader));
        speech::SpeechRegistryRevisionCountSnapshot snapshot = {};
        CHECK(speech::CompleteSpeechRegistrySnapshot(
            &reader, index + 1U, &snapshot));
        CHECK(snapshot.revision > previous_revision);
        CHECK(snapshot.revision ==
              snapshot.completed_mutation_count + 1U);
        CHECK(snapshot.completed_mutation_count == index + 1U);
        CHECK(snapshot.observed_entry_count == index + 1U);
        previous_revision = snapshot.revision;
    }
    return true;
}

}  // namespace

int main()
{
    struct TestCase {
        const char* name;
        bool (*run)();
    };
    const TestCase cases[] = {
        {"initial_revision_and_count_snapshot",
         &CaseInitialRevisionAndCountSnapshot},
        {"failed_snapshot_is_nonblocking",
         &CaseFailedSnapshotIsNonblocking},
        {"writer_waits_only_for_bounded_reader_and_never_overlaps",
         &CaseWriterWaitsOnlyForBoundedReaderAndNeverOverlaps},
        {"nested_writers_never_wait_for_each_other",
         &CaseNestedWritersNeverWaitForEachOther},
        {"concurrent_writer_is_admitted_beside_writer",
         &CaseConcurrentWriterIsAdmittedBesideWriter},
        {"revision_is_monotonic_and_stamp_is_stable",
         &CaseRevisionIsMonotonicAndStampIsStable},
    };
    std::uint32_t passed = 0;
    for (const TestCase& test : cases) {
        if (!test.run()) return 1;
        ++passed;
        std::printf("[PASS] %s\n", test.name);
    }
    std::printf("speech registry observation gate cases passed: %u\n",
                passed);
    return 0;
}
