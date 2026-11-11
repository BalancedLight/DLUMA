/* State transition contracts use the same core included by the native host. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/dlw_editor_git_lease.inc"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    GitProjectLease lease = { 0 };
    GitProjectArm arm = { 0 };
    const char *token = "0123456789abcdef0123456789abcdef";
    const char *other = "1123456789abcdef0123456789abcdef";
    CHECK(!git_arm_begin(&arm, token, 0, 17, "stage"));
    CHECK(!git_arm_begin(&arm, token, 1, 17, "pull"));
    CHECK(git_arm_begin(&arm, token, 1, 17, "stage"));
    CHECK(git_arm_matches(&arm, token, 17, "stage"));
    CHECK(!git_arm_matches(&arm, other, 17, "stage"));
    CHECK(!git_arm_matches(&arm, token, 18, "stage"));
    CHECK(!git_arm_matches(&arm, token, 17, "commit"));
    CHECK(!git_arm_clear(&arm, 0) && arm.active);
    CHECK(!git_arm_promote(&arm, 2) && arm.active);
    CHECK(git_arm_promote(&arm, 1) && !arm.active);
    CHECK(git_arm_begin(&arm, token, 2, 17, "stage"));
    CHECK(git_arm_clear(&arm, 1) && !arm.active);
    CHECK(git_lease_acquire(&lease, 0, 1, 1, 17, "stage", token, 0, 1) == GIT_LEASE_UNAVAILABLE);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "pull", token, 0, 1) == GIT_LEASE_UNAVAILABLE);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "Stage", token, 0, 1) == GIT_LEASE_UNAVAILABLE);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "stageExtra", token, 0, 1) == GIT_LEASE_UNAVAILABLE);
    CHECK(git_lease_acquire(&lease, 1, 1, 0, 17, "stage", token, 0, 1) == GIT_LEASE_UNAVAILABLE);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "stage", "bad", 0, 1) == GIT_LEASE_INVALID);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "stage", token, 1, 2) == GIT_LEASE_BUSY);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "stage", token, 0, 2) == GIT_LEASE_BUSY);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "stage", token, 0, 1) == GIT_LEASE_OK);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 17, "commit", other, 0, 1) == GIT_LEASE_BUSY);
    CHECK(!git_lease_matches(&lease, other));
    CHECK(git_lease_matches(&lease, token));
    CHECK(git_lease_release(&lease, other, 1, 1, 0, 1, 0) == GIT_LEASE_INVALID);
    CHECK(git_lease_release(&lease, token, 1, 1, 1, 2, 0) == GIT_LEASE_PENDING);
    CHECK(git_lease_release(&lease, token, 1, 1, 0, 2, 0) == GIT_LEASE_PENDING);
    CHECK(git_lease_release(&lease, token, 1, 1, 0, 1, 0) == GIT_LEASE_READY);
    CHECK(lease.active);
    CHECK(git_lease_release(&lease, token, 1, 1, 0, 1, 1) == GIT_LEASE_OK);
    CHECK(!git_lease_matches(&lease, token));
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 18, "push", other, 0, 1) == GIT_LEASE_OK);
    CHECK(git_lease_release(&lease, other, 0, 1, 0, 1, 0) == GIT_LEASE_RECOVERY);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 18, "stage", token, 0, 1) == GIT_LEASE_RECOVERY);
    git_lease_retire(&lease, 0);
    CHECK(lease.active && lease.recovery);
    git_lease_retire(&lease, 1);
    CHECK(!lease.active && lease.recovery);
    CHECK(git_lease_acquire(&lease, 1, 1, 1, 18, "stage", token, 0, 1) == GIT_LEASE_RECOVERY);
    puts("Editor Git lease state contract passed.");
    return 0;
}
