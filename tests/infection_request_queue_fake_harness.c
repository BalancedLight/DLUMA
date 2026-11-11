#include "../src/core/dlw_infection_request_queue.c"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static const DlumaInfectionQueueOwner owner = {1, 3};
static const DlumaInfectionQueueOwner other = {2, 1};
static const DlumaInfectionQueueOwner stale_owner = {1, 4};

typedef struct FakeCallbackHost { unsigned native_calls; int counter; } FakeCallbackHost;
static DlumaInfectionQueueResult frame(DlumaInfectionQueue *q,
    DlumaInfectionQueueCallback context, uint64_t now, FakeCallbackHost *host,
    DlumaInfectionQueueOutcome outcome)
{
    DlumaInfectionQueuedRequest work = {0};
    DlumaInfectionQueueResult result = dluma_infection_queue_take(q, &context, now, &work);
    if (result != DLUMA_IQ_OK) return result;
    host->native_calls++; host->counter++;
    return dluma_infection_queue_complete(q, &work, outcome);
}
int main(void)
{
    DlumaInfectionQueue q = {0}, replacement = {0};
    DlumaInfectionQueueSnapshot s = {0};
    DlumaInfectionQueuedRequest work = {0}, forged = {0};
    DlumaInfectionQueueCallback ctx = {11, 100, 1, 1, 1000};
    FakeCallbackHost host = {0};
    uint64_t first = 0, second = 0, foreign = 0, tokens[DLUMA_INFECTION_QUEUE_CAPACITY];
    unsigned i;
    CHECK(dluma_infection_queue_initialize(&q, 7, 11) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 10, 1000, &first) == DLUMA_IQ_UNAVAILABLE);
    CHECK(dluma_infection_queue_bind_target(&q, 1000) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 10, 1000, &first) == DLUMA_IQ_OK);
    CHECK((first >> 32) == 7 && first);
    CHECK(dluma_infection_queue_initialize(&q, 8, 11) == DLUMA_IQ_INVALID);
    CHECK(dluma_infection_queue_status(&q, &other, first, 11, &s) == DLUMA_IQ_FOREIGN);
    CHECK(dluma_infection_queue_cancel(&q, &other, first, 11) == DLUMA_IQ_FOREIGN);
    CHECK(dluma_infection_queue_status(&q, &stale_owner, first, 11, &s) == DLUMA_IQ_STALE);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 11, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_QUEUED && !host.native_calls);
    ctx.native_borrow_qualified = 0;
    CHECK(frame(&q, ctx, 12, &host, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_UNAVAILABLE);
    ctx.native_borrow_qualified = 1; ctx.local_owner_ready = 0;
    CHECK(frame(&q, ctx, 12, &host, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_UNAVAILABLE);
    CHECK(!host.native_calls);
    ctx.local_owner_ready = 1;
    CHECK(dluma_infection_queue_take(&q, &ctx, 12, &work) == DLUMA_IQ_OK);
    CHECK(work.token == first && work.owner.id == owner.id && work.map_generation == 11 && work.target_generation == 1000);
    CHECK(dluma_infection_queue_cancel(&q, &owner, first, 12) == DLUMA_IQ_TOO_LATE);
    CHECK(dluma_infection_queue_release(&q, &owner, first) == DLUMA_IQ_TOO_LATE);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 12, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_APPLYING);
    forged = work; forged.map_generation = 12;
    CHECK(dluma_infection_queue_complete(&q, &forged, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_STALE);
    host.native_calls++; host.counter++;
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_NOT_APPLIED) == DLUMA_IQ_ALREADY_COMPLETED);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 13, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_APPLIED && !s.may_have_applied);
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 13, 1000, &second) == DLUMA_IQ_OK);
    CHECK(frame(&q, ctx, 14, &host, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_NO_WORK);
    CHECK(host.native_calls == 1);
    ctx.frame_sequence++;
    CHECK(frame(&q, ctx, 14, &host, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_OK);
    CHECK(host.native_calls == 2);
    CHECK(dluma_infection_queue_release(&q, &owner, first) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 15, &s) == DLUMA_IQ_STALE);
    /* A new service generation cannot interpret a previous service's token. */
    CHECK(dluma_infection_queue_initialize(&replacement, 8, 11) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_target(&replacement, 1000) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&replacement, &owner, second, 15, &s) == DLUMA_IQ_STALE);
    /* Cancel succeeds only before Applying and is idempotent after cancellation. */
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 20, 1000, &first) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_cancel(&q, &owner, first, 21) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_cancel(&q, &owner, first, 21) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 21, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_CANCELLED);
    /* Retiring the owner during Applying preserves uncertainty, even with a
     * late claimed acknowledgment; it never turns the request Cancelled. */
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 30, 1000, &first) == DLUMA_IQ_OK);
    ctx.frame_sequence++;
    CHECK(dluma_infection_queue_take(&q, &ctx, 31, &work) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_retire_owner(&q, &owner) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_cancel(&q, &owner, first, 31) == DLUMA_IQ_TOO_LATE);
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 32, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_FAILED && s.outcome == DLUMA_IQ_UNCERTAIN && s.may_have_applied && s.retired);
    /* Owner retirement cancels Queued, without affecting another owner. */
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 40, 1000, &first) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&q, &other, 11, 40, 1000, &foreign) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_retire_owner(&q, &owner) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 41, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_CANCELLED && s.retired);
    ctx.frame_sequence++;
    CHECK(dluma_infection_queue_take(&q, &ctx, 41, &work) == DLUMA_IQ_OK);
    CHECK(work.token == foreign);
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_UNCERTAIN) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &other, foreign, 42, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_FAILED && s.may_have_applied);
    /* Map retirement before callback cancels; stale/native-inactive callbacks
     * do not touch a new map's request. */
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 50, 1000, &first) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_map(&q, 12) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 51, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_CANCELLED);
    CHECK(dluma_infection_queue_request(&q, &owner, 11, 51, 1000, &first) == DLUMA_IQ_UNAVAILABLE);
    CHECK(dluma_infection_queue_request(&q, &owner, 12, 51, 1000, &first) == DLUMA_IQ_UNAVAILABLE);
    CHECK(dluma_infection_queue_bind_target(&q, 2000) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&q, &owner, 12, 51, 1000, &first) == DLUMA_IQ_OK);
    CHECK(frame(&q, ctx, 52, &host, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_UNAVAILABLE);
    ctx.map_generation = 12; ctx.target_generation = 2000; ctx.frame_sequence++;
    CHECK(dluma_infection_queue_take(&q, &ctx, 52, &work) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_map(&q, 13) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_NOT_APPLIED) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 53, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_FAILED && s.may_have_applied && s.retired);
    /* Interrupted native callback: no acknowledgment may be invented/retried. */
    CHECK(dluma_infection_queue_bind_target(&q, 3000) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&q, &owner, 13, 60, 1000, &first) == DLUMA_IQ_OK);
    ctx.map_generation = 13; ctx.target_generation = 3000; ctx.frame_sequence++;
    CHECK(dluma_infection_queue_take(&q, &ctx, 61, &work) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_take(&q, &ctx, 61, &forged) == DLUMA_IQ_BUSY);
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_UNCERTAIN) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 62, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_FAILED && s.may_have_applied);
    /* Expiration never enters Applying or calls the fake native host. */
    CHECK(dluma_infection_queue_request(&q, &owner, 13, 70, 5, &first) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 75, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_EXPIRED);
    /* Actor/controller retirement and replacement in the SAME map must not
     * silently redirect an admitted request to a respawned/reused target. */
    CHECK(dluma_infection_queue_request(&q, &owner, 13, 76, 1000, &first) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_target(&q, 0) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 77, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_CANCELLED && s.retired && s.target_generation == 3000);
    CHECK(dluma_infection_queue_request(&q, &owner, 13, 77, 1000, &first) == DLUMA_IQ_UNAVAILABLE);
    CHECK(dluma_infection_queue_bind_target(&q, 3001) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&q, &owner, 13, 78, 1000, &first) == DLUMA_IQ_OK);
    ctx.frame_sequence++;
    CHECK(frame(&q, ctx, 79, &host, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_UNAVAILABLE);
    ctx.target_generation = 3001;
    CHECK(dluma_infection_queue_take(&q, &ctx, 79, &work) == DLUMA_IQ_OK);
    forged = work; forged.target_generation = 3002;
    CHECK(dluma_infection_queue_complete(&q, &forged, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_STALE);
    CHECK(dluma_infection_queue_bind_target(&q, 3002) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_complete(&q, &work, DLUMA_IQ_ACKNOWLEDGED) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_status(&q, &owner, first, 80, &s) == DLUMA_IQ_OK);
    CHECK(s.state == DLUMA_IQ_FAILED && s.may_have_applied && s.retired);
    CHECK(dluma_infection_queue_bind_target(&q, 3001) == DLUMA_IQ_STALE);
    CHECK(q.target_generation == 3002);
    /* Capacity and sequence exhaustion fail without replacing saved records. */
    memset(&replacement, 0, sizeof(replacement));
    CHECK(dluma_infection_queue_initialize(&replacement, 9, 13) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_target(&replacement, 3002) == DLUMA_IQ_OK);
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i)
        CHECK(dluma_infection_queue_request(&replacement, &owner, 13, 80, 100, &tokens[i]) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_request(&replacement, &owner, 13, 80, 100, &first) == DLUMA_IQ_FULL);
    CHECK(dluma_infection_queue_cancel(&replacement, &owner, tokens[0], 81) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_release(&replacement, &owner, tokens[0]) == DLUMA_IQ_OK);
    replacement.next_sequence = UINT32_MAX;
    CHECK(dluma_infection_queue_request(&replacement, &owner, 13, 82, 100, &first) == DLUMA_IQ_EXHAUSTED);
    CHECK(dluma_infection_queue_bind_target(&replacement, UINT64_MAX) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_target(&replacement, 0) == DLUMA_IQ_OK);
    CHECK(dluma_infection_queue_bind_target(&replacement, UINT64_MAX - 1) == DLUMA_IQ_EXHAUSTED);
    CHECK(!replacement.target_generation);
    puts("PASS: copied infection queue lifecycle and callback acknowledgments"); return 0;
}
