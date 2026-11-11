#include "dlw_infection_request_queue.h"
#include <string.h>

static int iq_valid(const DlumaInfectionQueue *q) { return q && q->initialized == 1 && q->service_generation; }
static int iq_owner(const DlumaInfectionQueueOwner *o) { return o && o->id && o->generation; }
static void iq_expire(DlumaInfectionQueueRecord *r, uint64_t now)
{
    if (r->occupied && r->value.state == DLUMA_IQ_QUEUED && now >= r->value.deadline_ms) {
        r->value.state = DLUMA_IQ_EXPIRED; r->value.outcome = DLUMA_IQ_NOT_APPLIED;
    }
}
static void iq_retire(DlumaInfectionQueueRecord *r)
{
    r->value.retired = 1;
    if (r->value.state == DLUMA_IQ_QUEUED) {
        r->value.state = DLUMA_IQ_CANCELLED; r->value.outcome = DLUMA_IQ_NOT_APPLIED;
    }
}
static DlumaInfectionQueueResult iq_find(DlumaInfectionQueue *q,
    const DlumaInfectionQueueOwner *owner, uint64_t token, DlumaInfectionQueueRecord **out)
{
    unsigned i;
    if (!iq_valid(q) || !iq_owner(owner) || !token || !out) return DLUMA_IQ_INVALID;
    if ((uint32_t)(token >> 32) != q->service_generation) return DLUMA_IQ_STALE;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i) {
        DlumaInfectionQueueRecord *r = &q->records[i];
        if (!r->occupied || r->value.token != token) continue;
        if (r->value.owner.id != owner->id) return DLUMA_IQ_FOREIGN;
        if (r->value.owner.generation != owner->generation) return DLUMA_IQ_STALE;
        *out = r; return DLUMA_IQ_OK;
    }
    return DLUMA_IQ_STALE;
}
DlumaInfectionQueueResult dluma_infection_queue_initialize(DlumaInfectionQueue *q,
    uint32_t generation, uint32_t map)
{
    if (!q || q->initialized || !generation) return DLUMA_IQ_INVALID;
    memset(q, 0, sizeof(*q)); q->initialized = 1;
    q->service_generation = generation; q->current_map = map; return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_bind_map(DlumaInfectionQueue *q, uint32_t map)
{
    unsigned i;
    if (!iq_valid(q)) return DLUMA_IQ_INVALID;
    if (q->current_map == map) return DLUMA_IQ_OK;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i)
        if (q->records[i].occupied && q->records[i].value.map_generation != map)
            iq_retire(&q->records[i]);
    q->current_map = map; q->target_generation = 0;
    q->last_frame_map = 0; q->last_frame_sequence = 0;
    return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_bind_target(DlumaInfectionQueue *q, uint64_t target)
{
    unsigned i;
    if (!iq_valid(q)) return DLUMA_IQ_INVALID;
    if (target == q->target_generation) return DLUMA_IQ_OK;
    if (target && !q->current_map) return DLUMA_IQ_UNAVAILABLE;
    if (target && q->highest_target_generation == UINT64_MAX) return DLUMA_IQ_EXHAUSTED;
    if (target && target <= q->highest_target_generation) return DLUMA_IQ_STALE;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i)
        if (q->records[i].occupied && q->records[i].value.target_generation != target)
            iq_retire(&q->records[i]);
    q->target_generation = target;
    if (target) q->highest_target_generation = target;
    return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_request(DlumaInfectionQueue *q,
    const DlumaInfectionQueueOwner *owner, uint32_t map, uint64_t now,
    uint32_t timeout, uint64_t *token)
{
    unsigned i;
    if (token) *token = 0;
    if (!iq_valid(q) || !iq_owner(owner) || !token || !timeout ||
        timeout > DLUMA_INFECTION_QUEUE_MAX_TIMEOUT_MS || now > UINT64_MAX - timeout)
        return DLUMA_IQ_INVALID;
    if (!map || map != q->current_map || !q->target_generation) return DLUMA_IQ_UNAVAILABLE;
    if (q->next_sequence == UINT32_MAX) return DLUMA_IQ_EXHAUSTED;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i) {
        DlumaInfectionQueueRecord *r = &q->records[i];
        if (r->occupied) continue;
        memset(r, 0, sizeof(*r)); r->occupied = 1;
        r->value.token = ((uint64_t)q->service_generation << 32) | ++q->next_sequence;
        r->value.owner = *owner; r->value.map_generation = map;
        r->value.target_generation = q->target_generation;
        r->value.requested_ms = now; r->value.deadline_ms = now + timeout;
        r->value.state = DLUMA_IQ_QUEUED; *token = r->value.token;
        return DLUMA_IQ_OK;
    }
    return DLUMA_IQ_FULL;
}
DlumaInfectionQueueResult dluma_infection_queue_status(DlumaInfectionQueue *q,
    const DlumaInfectionQueueOwner *owner, uint64_t token, uint64_t now,
    DlumaInfectionQueueSnapshot *out)
{
    DlumaInfectionQueueRecord *r = 0; DlumaInfectionQueueResult result;
    if (!out) return DLUMA_IQ_INVALID;
    memset(out, 0, sizeof(*out)); result = iq_find(q, owner, token, &r);
    if (result != DLUMA_IQ_OK) return result;
    iq_expire(r, now); *out = r->value; return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_cancel(DlumaInfectionQueue *q,
    const DlumaInfectionQueueOwner *owner, uint64_t token, uint64_t now)
{
    DlumaInfectionQueueRecord *r = 0;
    DlumaInfectionQueueResult result = iq_find(q, owner, token, &r);
    if (result != DLUMA_IQ_OK) return result;
    iq_expire(r, now);
    if (r->value.state == DLUMA_IQ_CANCELLED) return DLUMA_IQ_OK;
    if (r->value.state != DLUMA_IQ_QUEUED) return DLUMA_IQ_TOO_LATE;
    r->value.state = DLUMA_IQ_CANCELLED; r->value.outcome = DLUMA_IQ_NOT_APPLIED;
    return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_release(DlumaInfectionQueue *q,
    const DlumaInfectionQueueOwner *owner, uint64_t token)
{
    DlumaInfectionQueueRecord *r = 0;
    DlumaInfectionQueueResult result = iq_find(q, owner, token, &r);
    if (result != DLUMA_IQ_OK) return result;
    if (r->value.state == DLUMA_IQ_QUEUED || r->value.state == DLUMA_IQ_APPLYING)
        return DLUMA_IQ_TOO_LATE;
    memset(r, 0, sizeof(*r)); return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_retire_owner(DlumaInfectionQueue *q,
    const DlumaInfectionQueueOwner *owner)
{
    unsigned i;
    if (!iq_valid(q) || !iq_owner(owner)) return DLUMA_IQ_INVALID;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i) {
        DlumaInfectionQueueRecord *r = &q->records[i];
        if (r->occupied && r->value.owner.id == owner->id &&
            r->value.owner.generation == owner->generation) iq_retire(r);
    }
    return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_take(DlumaInfectionQueue *q,
    const DlumaInfectionQueueCallback *ctx, uint64_t now, DlumaInfectionQueuedRequest *out)
{
    unsigned i; DlumaInfectionQueueRecord *next = 0;
    if (!out) return DLUMA_IQ_INVALID;
    memset(out, 0, sizeof(*out));
    if (!iq_valid(q) || !ctx || !ctx->frame_sequence) return DLUMA_IQ_INVALID;
    if (q->applying_token) return DLUMA_IQ_BUSY;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i) iq_expire(&q->records[i], now);
    if (!ctx->native_borrow_qualified || !ctx->local_owner_ready ||
        !ctx->map_generation || ctx->map_generation != q->current_map ||
        !ctx->target_generation || ctx->target_generation != q->target_generation)
        return DLUMA_IQ_UNAVAILABLE;
    if (q->last_frame_map == ctx->map_generation &&
        ctx->frame_sequence <= q->last_frame_sequence) return DLUMA_IQ_NO_WORK;
    for (i = 0; i < DLUMA_INFECTION_QUEUE_CAPACITY; ++i) {
        DlumaInfectionQueueRecord *r = &q->records[i];
        if (!r->occupied || r->value.state != DLUMA_IQ_QUEUED ||
            r->value.map_generation != ctx->map_generation ||
            r->value.target_generation != ctx->target_generation || now < r->value.requested_ms) continue;
        if (!next || r->value.token < next->value.token) next = r;
    }
    if (!next) return DLUMA_IQ_NO_WORK;
    next->value.state = DLUMA_IQ_APPLYING;
    next->value.attempt_frame_sequence = ctx->frame_sequence;
    q->applying_token = next->value.token; q->last_frame_map = ctx->map_generation;
    q->last_frame_sequence = ctx->frame_sequence;
    out->token = next->value.token; out->owner = next->value.owner;
    out->map_generation = next->value.map_generation; out->frame_sequence = ctx->frame_sequence;
    out->target_generation = next->value.target_generation;
    return DLUMA_IQ_OK;
}
DlumaInfectionQueueResult dluma_infection_queue_complete(DlumaInfectionQueue *q,
    const DlumaInfectionQueuedRequest *work, DlumaInfectionQueueOutcome outcome)
{
    DlumaInfectionQueueRecord *r = 0; DlumaInfectionQueueResult result;
    if (!work || outcome < DLUMA_IQ_ACKNOWLEDGED || outcome > DLUMA_IQ_UNCERTAIN)
        return DLUMA_IQ_INVALID;
    result = iq_find(q, &work->owner, work->token, &r);
    if (result != DLUMA_IQ_OK) return result;
    if (work->map_generation != r->value.map_generation ||
        work->frame_sequence != r->value.attempt_frame_sequence ||
        work->target_generation != r->value.target_generation) return DLUMA_IQ_STALE;
    if (r->value.state != DLUMA_IQ_APPLYING) return DLUMA_IQ_ALREADY_COMPLETED;
    if (q->applying_token != work->token) return DLUMA_IQ_STALE;
    if (r->value.retired) outcome = DLUMA_IQ_UNCERTAIN;
    r->value.outcome = outcome;
    r->value.state = outcome == DLUMA_IQ_ACKNOWLEDGED ? DLUMA_IQ_APPLIED : DLUMA_IQ_FAILED;
    r->value.may_have_applied = outcome == DLUMA_IQ_UNCERTAIN;
    q->applying_token = 0; return DLUMA_IQ_OK;
}
