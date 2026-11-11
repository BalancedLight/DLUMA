#ifndef DLW_INFECTION_REQUEST_H
#define DLW_INFECTION_REQUEST_H
#include "../bindings/dlw_player_actor_binding.h"
#include <stddef.h>

/* Ephemeral identity checks, not a native lifetime pin. The driver must hold a
 * separately qualified native lifetime boundary through this entire request.
 * Never retain these addresses afterward or expose them to a mod. */
typedef struct DlumaInfectionFrame {
    uintptr_t game, session, logical, membership, player, controller, level;
    uintptr_t frame_receiver, level_vtable;
    uint64_t frame_token;
    uint32_t counter;
} DlumaInfectionFrame;
typedef struct DlumaInfectionRequestDriver {
    void *user;
    int (*capture)(void *, DlumaInfectionFrame *);
    int (*invoke_original)(void *, void *, size_t);
} DlumaInfectionRequestDriver;

static int dluma_infection_frame_valid(const DlumaInfectionFrame *f)
{
    return f->game && f->session && f->logical && f->membership && f->player &&
        f->controller && f->level && f->frame_receiver && f->level_vtable && f->frame_token;
}
static int dluma_infection_frame_same(const DlumaInfectionFrame *a,
    const DlumaInfectionFrame *b)
{
    return a->game == b->game && a->session == b->session &&
        a->logical == b->logical && a->membership == b->membership &&
        a->player == b->player && a->controller == b->controller &&
        a->level == b->level && a->level_vtable == b->level_vtable &&
        a->frame_receiver == b->frame_receiver && a->frame_token == b->frame_token;
}
static DlumaPlayerInfectionRequestResult dluma_infection_request_perform(
    const DlumaInfectionRequestDriver *d)
{
    DlumaInfectionFrame before = {0}, confirmed = {0}, after = {0};
    /* Exact stock instruction allocation is24 bytes, naturally aligned to8.
     * The qualified leaf reads no vtable or phase and only writes byte+8. */
    uint64_t instruction[3] = {0, 0, 0};
    if (!d || !d->capture || !d->invoke_original ||
        !d->capture(d->user, &before) || !dluma_infection_frame_valid(&before))
        return DLUMA_PLAYER_INFECTION_UNAVAILABLE;
    if (!d->capture(d->user, &confirmed) ||
        !dluma_infection_frame_valid(&confirmed) ||
        !dluma_infection_frame_same(&before, &confirmed) ||
        before.counter != confirmed.counter)
        return DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED;
    if (!d->invoke_original(d->user, instruction, sizeof(instruction)))
        return DLUMA_PLAYER_INFECTION_NATIVE_FAILED;
    if (!d->capture(d->user, &after) || !dluma_infection_frame_valid(&after) ||
        !dluma_infection_frame_same(&before, &after))
        return DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED;
    if (((const unsigned char *)instruction)[8] != 1 ||
        (uint32_t)(after.counter - before.counter) != 1u)
        return DLUMA_PLAYER_INFECTION_NATIVE_FAILED;
    return DLUMA_PLAYER_INFECTION_ACCEPTED;
}
#endif
