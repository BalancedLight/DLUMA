#ifndef DLUMA_PLAYER_ACTOR_BINDING_H
#define DLUMA_PLAYER_ACTOR_BINDING_H

#include <stdint.h>

typedef struct DlumaPlayerVitalsSnapshot {
    float health;
    float max_health;
    uint32_t alive;
} DlumaPlayerVitalsSnapshot;

/* Implemented beside the exact Player profile so SHA/RTTI/owner validation
 * has one authority. Only copied values cross into the shared game service. */
int dluma_player_actor_binding_vitals_ready(void);
int dluma_player_actor_binding_read_vitals(DlumaPlayerVitalsSnapshot* snapshot);

#endif
