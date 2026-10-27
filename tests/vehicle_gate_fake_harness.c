#include "../src/dlw_vehicle_gate_core.h"

#include <stdio.h>
#include <string.h>

static DlumaVehicleGateEvidence ready_evidence(void)
{
    DlumaVehicleGateEvidence evidence;
    memset(&evidence, 0, sizeof(evidence));
    evidence.static_routes_intact = 1U;
    evidence.root_slot_readable = 1U;
    evidence.root_present = 1U;
    evidence.actor_field_readable = 1U;
    evidence.actor_present = 1U;
    evidence.playerdi_exact = 1U;
    evidence.current_vehicle_call_valid = 1U;
    evidence.current_vehicle_present = 1U;
    evidence.current_vehicle_cast_call_valid = 1U;
    evidence.current_vehicle_is_buggy = 1U;
    evidence.car_buggy_exact = 1U;
    evidence.driver_query_call_valid = 1U;
    evidence.driver_present = 1U;
    evidence.driver_matches_local_player = 1U;
    evidence.stock_driver_call_valid = 1U;
    evidence.stock_is_driver = 1U;
    evidence.stock_buggy_present = 1U;
    evidence.stock_buggy_matches_vehicle = 1U;
    evidence.identities_stable = 1U;
    evidence.velocity_available = 1U;
    evidence.velocity_finite = 1U;
    return evidence;
}

static int check(
    DlumaVehicleGateEvidence *evidence,
    unsigned int available,
    unsigned int local_driver,
    unsigned int reason)
{
    DlumaVehicleGateDecision decision;
    memset(&decision, 0, sizeof(decision));
    dluma_vehicle_gate_evaluate(evidence, &decision);
    return decision.available == available &&
        decision.is_local_driver == local_driver &&
        decision.reason == reason;
}

int main(void)
{
    DlumaVehicleGateEvidence evidence = ready_evidence();
    unsigned int passed = 0U;
    if (!check(&evidence, 1U, 1U, DLUMA_DL_BUGGY_GATE_READY)) return 1;
    ++passed; puts("[PASS] matching vehicle and driver evidence");
    evidence = ready_evidence(); evidence.driver_matches_local_player = 0U;
    if (!check(&evidence, 1U, 0U,
            DLUMA_DL_BUGGY_GATE_DRIVER_OWNER_MISMATCH)) return 1;
    ++passed; puts("[PASS] passenger and remote driver rejected");
    evidence = ready_evidence(); evidence.identities_stable = 0U;
    if (!check(&evidence, 0U, 0U,
            DLUMA_DL_BUGGY_GATE_IDENTITY_CHANGED)) return 1;
    ++passed; puts("[PASS] identity change fails closed");
    evidence = ready_evidence(); evidence.velocity_finite = 0U;
    if (!check(&evidence, 0U, 0U,
            DLUMA_DL_BUGGY_GATE_VELOCITY_INVALID)) return 1;
    ++passed; puts("[PASS] invalid velocity fails closed");
    evidence = ready_evidence(); evidence.static_routes_intact = 0U;
    if (!check(&evidence, 0U, 0U,
            DLUMA_DL_BUGGY_GATE_STATIC_ROUTES_CHANGED)) return 1;
    ++passed; puts("[PASS] changed exact-build route fails closed");
    printf("Vehicle gate fake cases passed: %u\n", passed);
    return 0;
}
