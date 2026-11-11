/* Owner-thread project lease state. This file makes no native calls and does
 * not establish Editor admission; the host supplies verified observations. */
#include <stdint.h>
#include <string.h>

typedef enum GitLeaseResult {
    GIT_LEASE_OK = 0, GIT_LEASE_UNAVAILABLE, GIT_LEASE_BUSY,
    GIT_LEASE_INVALID, GIT_LEASE_PENDING, GIT_LEASE_RECOVERY, GIT_LEASE_READY
} GitLeaseResult;
typedef struct GitProjectLease {
    uint64_t generation;
    char token[33];
    int active, recovery;
} GitProjectLease;

static int git_lease_operation(const char *operation)
{
    static const char *const allowed[] = {
        "initialize", "stage", "unstage", "commit", "createBranch",
        "addRemote", "fetch", "publishBranch", "push"
    };
    unsigned int i;
    size_t length;
    if (!operation) return 0;
    length = strlen(operation);
    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strlen(allowed[i]) == length && !memcmp(operation, allowed[i], length)) return 1;
    return 0;
}

static int git_lease_token_valid(const char *token)
{
    unsigned int i;
    if (!token || strlen(token) != 32U) return 0;
    for (i = 0; i < 32U; ++i) {
        unsigned char c = (unsigned char)token[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static GitLeaseResult git_lease_acquire(GitProjectLease *lease, int qualified,
    int current, int guard_ready, uint64_t generation, const char *operation,
    const char *token, unsigned int pending_children, unsigned int active_processes)
{
    if (!lease) return GIT_LEASE_INVALID;
    if (lease->recovery) return GIT_LEASE_RECOVERY;
    if (lease->active) return GIT_LEASE_BUSY;
    if (!qualified || !current || !guard_ready || !generation || !git_lease_operation(operation))
        return GIT_LEASE_UNAVAILABLE;
    if (!git_lease_token_valid(token)) return GIT_LEASE_INVALID;
    if (pending_children || active_processes != 1U) return GIT_LEASE_BUSY;
    memcpy(lease->token, token, sizeof(lease->token));
    lease->generation = generation;
    lease->active = 1;
    return GIT_LEASE_OK;
}

static int git_lease_matches(const GitProjectLease *lease, const char *token)
{
    return lease && lease->active && git_lease_token_valid(token) &&
        memcmp(lease->token, token, sizeof(lease->token)) == 0;
}

/* Caller must establish companion liveness and query the owned job. The idle
 * authenticated companion is the only process permitted at normal release. */
static GitLeaseResult git_lease_release(GitProjectLease *lease, const char *token,
    int current, int authority_valid, unsigned int pending_children, unsigned int active_processes, int guard_disarmed)
{
    if (!git_lease_matches(lease, token)) return GIT_LEASE_INVALID;
    if (lease->recovery || !current || !authority_valid || !active_processes) {
        lease->recovery = 1;
        return GIT_LEASE_RECOVERY;
    }
    if (pending_children || active_processes != 1U) return GIT_LEASE_PENDING;
    if (!guard_disarmed) return GIT_LEASE_READY;
    memset(lease, 0, sizeof(*lease));
    return GIT_LEASE_OK;
}

/* Never drop authority until the session's owned job is proven quiescent. */
static void git_lease_retire(GitProjectLease *lease, int job_quiescent)
{
    if (!lease) return;
    int recovery = lease->recovery || lease->active;
    if (!job_quiescent) { if (lease->active) lease->recovery = 1; return; }
    memset(lease, 0, sizeof(*lease));
    lease->recovery = recovery;
}

/* Pre-grant identity is distinct from a lease. The host owns and disarms the
 * matching broker epoch before clearing this state. */
typedef struct GitProjectArm {
    uint64_t epoch, generation;
    char token[33], operation[32];
    int active;
} GitProjectArm;

static int git_arm_begin(GitProjectArm *arm, const char *token, uint64_t epoch,
    uint64_t generation, const char *operation)
{
    size_t length;
    if (!arm || arm->active || !epoch || !generation || !git_lease_token_valid(token) ||
        !git_lease_operation(operation)) return 0;
    length = strlen(operation);
    if (length >= sizeof(arm->operation)) return 0;
    memcpy(arm->token, token, sizeof(arm->token));
    memcpy(arm->operation, operation, length + 1U);
    arm->epoch = epoch; arm->generation = generation; arm->active = 1;
    return 1;
}

static int git_arm_matches(const GitProjectArm *arm, const char *token,
    uint64_t generation, const char *operation)
{
    size_t length;
    if (!arm || !arm->active || !git_lease_token_valid(token) || !operation ||
        arm->generation != generation || memcmp(arm->token, token, sizeof(arm->token))) return 0;
    length = strlen(operation);
    return strlen(arm->operation) == length && !memcmp(arm->operation, operation, length);
}

static int git_arm_clear(GitProjectArm *arm, int disarmed)
{
    if (!arm || !arm->active || !disarmed) return 0;
    memset(arm, 0, sizeof(*arm));
    return 1;
}

/* Transfer the armed epoch to the granted lease without disarming it. */
static int git_arm_promote(GitProjectArm *arm, uint64_t epoch)
{
    if (!arm || !arm->active || !epoch || arm->epoch != epoch) return 0;
    memset(arm, 0, sizeof(*arm));
    return 1;
}
