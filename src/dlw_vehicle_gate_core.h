#ifndef DLUMA_VEHICLE_GATE_CORE_H
#define DLUMA_VEHICLE_GATE_CORE_H

typedef enum DlumaVehicleGateReason {
    DLUMA_DL_BUGGY_GATE_READY = 0,
    DLUMA_DL_BUGGY_GATE_STATIC_ROUTES_CHANGED = 1,
    DLUMA_DL_BUGGY_GATE_ROOT_SLOT_UNREADABLE = 2,
    DLUMA_DL_BUGGY_GATE_ROOT_UNAVAILABLE = 3,
    DLUMA_DL_BUGGY_GATE_ACTOR_FIELD_UNREADABLE = 4,
    DLUMA_DL_BUGGY_GATE_LOCAL_PLAYER_UNAVAILABLE = 5,
    DLUMA_DL_BUGGY_GATE_PLAYERDI_RTTI_INVALID = 6,
    DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_CALL_FAILED = 7,
    DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_UNAVAILABLE = 8,
    DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_CAST_FAILED = 9,
    DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_NOT_BUGGY = 10,
    DLUMA_DL_BUGGY_GATE_CAR_BUGGY_VTABLE_MISMATCH = 11,
    DLUMA_DL_BUGGY_GATE_DRIVER_QUERY_CALL_FAILED = 12,
    DLUMA_DL_BUGGY_GATE_DRIVER_UNAVAILABLE = 13,
    DLUMA_DL_BUGGY_GATE_DRIVER_OWNER_MISMATCH = 14,
    DLUMA_DL_BUGGY_GATE_STOCK_DRIVER_CALL_FAILED = 15,
    DLUMA_DL_BUGGY_GATE_STOCK_BUGGY_UNAVAILABLE = 16,
    DLUMA_DL_BUGGY_GATE_STOCK_BUGGY_DISAGREEMENT = 17,
    DLUMA_DL_BUGGY_GATE_IDENTITY_CHANGED = 18,
    DLUMA_DL_BUGGY_GATE_VELOCITY_UNAVAILABLE = 19,
    DLUMA_DL_BUGGY_GATE_VELOCITY_INVALID = 20
} DlumaVehicleGateReason;

typedef enum DlumaVehicleGateRoute {
    DLUMA_DL_BUGGY_ROUTE_NONE = 0,
    DLUMA_DL_BUGGY_ROUTE_STOCK_AND_VEHICLE_OWNER_AGREE = 1,
    DLUMA_DL_BUGGY_ROUTE_VEHICLE_DRIVER_OWNER_CORROBORATED = 2
} DlumaVehicleGateRoute;

typedef struct DlumaVehicleGateEvidence {
    unsigned int static_routes_intact;
    unsigned int root_slot_readable;
    unsigned int root_present;
    unsigned int actor_field_readable;
    unsigned int actor_present;
    unsigned int playerdi_exact;
    unsigned int current_vehicle_call_valid;
    unsigned int current_vehicle_present;
    unsigned int current_vehicle_cast_call_valid;
    unsigned int current_vehicle_is_buggy;
    unsigned int car_buggy_exact;
    unsigned int driver_query_call_valid;
    unsigned int driver_present;
    unsigned int driver_matches_local_player;
    unsigned int stock_driver_call_valid;
    unsigned int stock_is_driver;
    unsigned int stock_buggy_present;
    unsigned int stock_buggy_matches_vehicle;
    unsigned int identities_stable;
    unsigned int velocity_available;
    unsigned int velocity_finite;
} DlumaVehicleGateEvidence;

typedef struct DlumaVehicleGateDecision {
    unsigned int available;
    unsigned int local_player_present;
    unsigned int is_local_driver;
    unsigned int is_buggy;
    unsigned int reason;
    unsigned int route;
} DlumaVehicleGateDecision;

static __forceinline void dluma_vehicle_gate_evaluate(
    const DlumaVehicleGateEvidence* evidence,
    DlumaVehicleGateDecision* decision)
{
    if (!decision) return;
    decision->available = 0U;
    decision->local_player_present = 0U;
    decision->is_local_driver = 0U;
    decision->is_buggy = 0U;
    decision->reason = DLUMA_DL_BUGGY_GATE_STATIC_ROUTES_CHANGED;
    decision->route = DLUMA_DL_BUGGY_ROUTE_NONE;
    if (!evidence || !evidence->static_routes_intact) return;
    decision->available = 1U;
    if (!evidence->root_slot_readable) {
        decision->available = 0U;
        decision->reason = DLUMA_DL_BUGGY_GATE_ROOT_SLOT_UNREADABLE;
        return;
    }
    if (!evidence->root_present) {
        decision->reason = DLUMA_DL_BUGGY_GATE_ROOT_UNAVAILABLE;
        return;
    }
    if (!evidence->actor_field_readable) {
        decision->available = 0U;
        decision->reason = DLUMA_DL_BUGGY_GATE_ACTOR_FIELD_UNREADABLE;
        return;
    }
    if (!evidence->actor_present) {
        decision->reason = DLUMA_DL_BUGGY_GATE_LOCAL_PLAYER_UNAVAILABLE;
        return;
    }
    if (!evidence->playerdi_exact) {
        decision->available = 0U;
        decision->reason = DLUMA_DL_BUGGY_GATE_PLAYERDI_RTTI_INVALID;
        return;
    }
    decision->local_player_present = 1U;
    if (!evidence->current_vehicle_call_valid) {
        decision->available = 0U;
        decision->reason =
            DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_CALL_FAILED;
        return;
    }
    if (!evidence->current_vehicle_present) {
        decision->reason =
            DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_UNAVAILABLE;
        return;
    }
    if (!evidence->current_vehicle_cast_call_valid) {
        decision->available = 0U;
        decision->reason =
            DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_CAST_FAILED;
        return;
    }
    if (!evidence->current_vehicle_is_buggy) {
        decision->reason =
            DLUMA_DL_BUGGY_GATE_CURRENT_VEHICLE_NOT_BUGGY;
        return;
    }
    if (!evidence->car_buggy_exact) {
        decision->available = 0U;
        decision->reason =
            DLUMA_DL_BUGGY_GATE_CAR_BUGGY_VTABLE_MISMATCH;
        return;
    }
    decision->is_buggy = 1U;
    if (!evidence->driver_query_call_valid) {
        decision->available = 0U;
        decision->reason =
            DLUMA_DL_BUGGY_GATE_DRIVER_QUERY_CALL_FAILED;
        return;
    }
    if (!evidence->driver_present) {
        decision->reason = DLUMA_DL_BUGGY_GATE_DRIVER_UNAVAILABLE;
        return;
    }
    if (!evidence->driver_matches_local_player) {
        decision->reason =
            DLUMA_DL_BUGGY_GATE_DRIVER_OWNER_MISMATCH;
        return;
    }
    if (!evidence->stock_driver_call_valid) {
        decision->available = 0U;
        decision->reason =
            DLUMA_DL_BUGGY_GATE_STOCK_DRIVER_CALL_FAILED;
        return;
    }
    if (evidence->stock_is_driver) {
        if (!evidence->stock_buggy_present) {
            decision->available = 0U;
            decision->reason =
                DLUMA_DL_BUGGY_GATE_STOCK_BUGGY_UNAVAILABLE;
            return;
        }
        if (!evidence->stock_buggy_matches_vehicle) {
            decision->available = 0U;
            decision->reason =
                DLUMA_DL_BUGGY_GATE_STOCK_BUGGY_DISAGREEMENT;
            return;
        }
    }
    if (!evidence->identities_stable) {
        decision->available = 0U;
        decision->reason = DLUMA_DL_BUGGY_GATE_IDENTITY_CHANGED;
        return;
    }
    if (!evidence->velocity_available) {
        decision->available = 0U;
        decision->reason = DLUMA_DL_BUGGY_GATE_VELOCITY_UNAVAILABLE;
        return;
    }
    if (!evidence->velocity_finite) {
        decision->available = 0U;
        decision->reason = DLUMA_DL_BUGGY_GATE_VELOCITY_INVALID;
        return;
    }
    decision->is_local_driver = 1U;
    decision->reason = DLUMA_DL_BUGGY_GATE_READY;
    decision->route = evidence->stock_is_driver
        ? DLUMA_DL_BUGGY_ROUTE_STOCK_AND_VEHICLE_OWNER_AGREE
        : DLUMA_DL_BUGGY_ROUTE_VEHICLE_DRIVER_OWNER_CORROBORATED;
}

#endif
