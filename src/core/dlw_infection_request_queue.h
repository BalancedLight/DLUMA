#ifndef DLW_INFECTION_REQUEST_QUEUE_H
#define DLW_INFECTION_REQUEST_QUEUE_H
#include <stdint.h>

#define DLUMA_INFECTION_QUEUE_CAPACITY 32u
#define DLUMA_INFECTION_QUEUE_MAX_TIMEOUT_MS 60000u

typedef enum DlumaInfectionQueueResult {
    DLUMA_IQ_OK, DLUMA_IQ_INVALID, DLUMA_IQ_FOREIGN, DLUMA_IQ_STALE,
    DLUMA_IQ_FULL, DLUMA_IQ_EXHAUSTED, DLUMA_IQ_TOO_LATE,
    DLUMA_IQ_ALREADY_COMPLETED, DLUMA_IQ_NO_WORK, DLUMA_IQ_UNAVAILABLE,
    DLUMA_IQ_BUSY
} DlumaInfectionQueueResult;
typedef enum DlumaInfectionQueueState {
    DLUMA_IQ_QUEUED = 1, DLUMA_IQ_APPLYING, DLUMA_IQ_APPLIED,
    DLUMA_IQ_CANCELLED, DLUMA_IQ_FAILED, DLUMA_IQ_EXPIRED
} DlumaInfectionQueueState;
typedef enum DlumaInfectionQueueOutcome {
    DLUMA_IQ_NO_OUTCOME = 0, DLUMA_IQ_ACKNOWLEDGED,
    DLUMA_IQ_NOT_APPLIED, DLUMA_IQ_UNCERTAIN
} DlumaInfectionQueueOutcome;
typedef struct DlumaInfectionQueueOwner { uint64_t id, generation; } DlumaInfectionQueueOwner;
typedef struct DlumaInfectionQueueSnapshot {
    uint64_t token;
    DlumaInfectionQueueOwner owner;
    uint32_t map_generation;
    uint64_t target_generation;
    uint64_t requested_ms, deadline_ms, attempt_frame_sequence;
    DlumaInfectionQueueState state;
    DlumaInfectionQueueOutcome outcome;
    uint32_t retired, may_have_applied;
} DlumaInfectionQueueSnapshot;
typedef struct DlumaInfectionQueuedRequest {
    uint64_t token;
    DlumaInfectionQueueOwner owner;
    uint32_t map_generation;
    uint64_t frame_sequence;
    uint64_t target_generation;
} DlumaInfectionQueuedRequest;
/* Supplied only by a separately qualified native callback adapter. These
 * copied scheduling flags are not a native object lifetime pin. */
typedef struct DlumaInfectionQueueCallback {
    uint32_t map_generation;
    uint64_t frame_sequence;
    uint32_t native_borrow_qualified, local_owner_ready;
    uint64_t target_generation;
} DlumaInfectionQueueCallback;
typedef struct DlumaInfectionQueueRecord {
    DlumaInfectionQueueSnapshot value;
    uint32_t occupied;
} DlumaInfectionQueueRecord;
typedef struct DlumaInfectionQueue {
    uint32_t initialized, service_generation, next_sequence, current_map;
    uint32_t last_frame_map;
    uint64_t last_frame_sequence, applying_token;
    uint64_t target_generation, highest_target_generation;
    DlumaInfectionQueueRecord records[DLUMA_INFECTION_QUEUE_CAPACITY];
} DlumaInfectionQueue;

/* Zero initialize once; service_generation must be nonzero and unique across
 * service resets in the same process. No token is reused in that generation.
 * All records contain copied integers only, with no native pointer persistence.
 * The host authenticates owner credentials and serializes every core operation.
 * Do not hold its state lock across native calls: take returns a POD copy,
 * then the host executes under a qualified native borrow and calls complete.
 * A bounded receiver field primitive that makes no native calls may stay
 * serialized with the core; it must not block, forward, or reenter the host.
 * Cancel/retire may interleave there. Missing completion remains Applying;
 * after an interrupted callback the host must explicitly complete Uncertain.
 * Never infer Applied from admission, frame identity, or an expired deadline.
 * Terminal records remain until release/host teardown; never silently evict them.
 * Requests bind to the qualified current local target at admission. The host
 * publishes a strictly increasing native ctor/retirement generation, never a
 * constant/map-only value or cached-address lookup. Zero invalidates the target.
 * Respawn/replacement retires old Queued requests; Applying becomes uncertain.
 * A map transition invalidates the target until a qualified native identity is
 * published again. Target generation exhaustion fails closed.
 */
DlumaInfectionQueueResult dluma_infection_queue_initialize(DlumaInfectionQueue *, uint32_t service_generation, uint32_t map_generation);
DlumaInfectionQueueResult dluma_infection_queue_bind_map(DlumaInfectionQueue *, uint32_t map_generation);
DlumaInfectionQueueResult dluma_infection_queue_bind_target(DlumaInfectionQueue *, uint64_t target_generation);
DlumaInfectionQueueResult dluma_infection_queue_request(DlumaInfectionQueue *, const DlumaInfectionQueueOwner *, uint32_t map_generation, uint64_t now_ms, uint32_t timeout_ms, uint64_t *token);
DlumaInfectionQueueResult dluma_infection_queue_status(DlumaInfectionQueue *, const DlumaInfectionQueueOwner *, uint64_t token, uint64_t now_ms, DlumaInfectionQueueSnapshot *);
DlumaInfectionQueueResult dluma_infection_queue_cancel(DlumaInfectionQueue *, const DlumaInfectionQueueOwner *, uint64_t token, uint64_t now_ms);
DlumaInfectionQueueResult dluma_infection_queue_release(DlumaInfectionQueue *, const DlumaInfectionQueueOwner *, uint64_t token);
DlumaInfectionQueueResult dluma_infection_queue_retire_owner(DlumaInfectionQueue *, const DlumaInfectionQueueOwner *);
DlumaInfectionQueueResult dluma_infection_queue_take(DlumaInfectionQueue *, const DlumaInfectionQueueCallback *, uint64_t now_ms, DlumaInfectionQueuedRequest *);
DlumaInfectionQueueResult dluma_infection_queue_complete(DlumaInfectionQueue *, const DlumaInfectionQueuedRequest *, DlumaInfectionQueueOutcome);
#endif
