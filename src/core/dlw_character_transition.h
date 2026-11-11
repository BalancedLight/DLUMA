#ifndef DLW_CHARACTER_TRANSITION_H
#define DLW_CHARACTER_TRANSITION_H
#include <stdint.h>

/* Internal coordinator only. No game backend or public capability is installed. */
enum {
    DLUMA_CHARACTER_INVENTORY = 1u << 0,
    DLUMA_CHARACTER_XP = 1u << 1,
    DLUMA_CHARACTER_PROGRESSION = 1u << 2,
    DLUMA_CHARACTER_APPEARANCE = 1u << 3,
    DLUMA_CHARACTER_SOUND = 1u << 4,
    DLUMA_CHARACTER_WORLD_BOUNDARY = 1u << 5,
    DLUMA_CHARACTER_RELOAD_RECOVERY = 1u << 6,
    DLUMA_CHARACTER_REQUIRED_COMPONENTS = (1u << 7) - 1
};
typedef enum DlumaCharacterResult {
    DLUMA_CHARACTER_OK, DLUMA_CHARACTER_INVALID, DLUMA_CHARACTER_UNSUPPORTED,
    DLUMA_CHARACTER_BUSY, DLUMA_CHARACTER_PENDING, DLUMA_CHARACTER_OWNER_CHANGED,
    DLUMA_CHARACTER_STORAGE_FAILED, DLUMA_CHARACTER_APPLY_FAILED,
    DLUMA_CHARACTER_CAPTURE_FAILED, DLUMA_CHARACTER_RECOVERY_REQUIRED,
    DLUMA_CHARACTER_CONFIGURATION_REQUIRED
} DlumaCharacterResult;

typedef enum DlumaCharacterTargetReadiness {
    DLUMA_CHARACTER_TARGET_CONFIGURATION_REQUIRED = 0,
    DLUMA_CHARACTER_TARGET_SAVED,
    DLUMA_CHARACTER_TARGET_EXPLICIT_CONFIGURATION
} DlumaCharacterTargetReadiness;
typedef enum DlumaCharacterPhase {
    DLUMA_CHARACTER_PREPARED = 1, DLUMA_CHARACTER_APPLYING,
    DLUMA_CHARACTER_ACTIVE, DLUMA_CHARACTER_RETURNING, DLUMA_CHARACTER_RESTORING
} DlumaCharacterPhase;
typedef struct DlumaCharacterRequest {
    /* Stable backend identities, never engine pointers. scope binds mod, map and save. */
    uint64_t scope_id, source_profile, target_profile;
} DlumaCharacterRequest;
typedef struct DlumaCharacterJournal {
    uint32_t version;
    uint64_t id;
    DlumaCharacterRequest request;
    DlumaCharacterPhase phase;
} DlumaCharacterJournal;
typedef struct DlumaCharacterTransition {
    DlumaCharacterJournal journal;
    int busy;
} DlumaCharacterTransition;

/* All callbacks are bounded, serialized on the authoritative thread, and return
 * nonzero only on verified success. Durable operations must be atomic: a failed
 * prepare publishes nothing; a failed phase leaves its previous durable value.
 * Snapshots are owned by the backend and contain values, not engine pointers.
 * enter must bind exact live owner/build/thread and exclude save/checkpoint
 * writers for the complete operation. World evidence lives in the durable
 * backend journal, not an enter-time baseline that can be rebased on retry.
 * World progress may advance between successfully closed operations.
 * Every failure after prepare leaves a recoverable, independently stored journal.
 */
typedef struct DlumaCharacterBackend {
    uint32_t version, qualified_components;
    void *user;
    int (*enter)(void *, const DlumaCharacterRequest *, uint64_t journal_id);
    void (*leave)(void *);
    /* Validate target, capture source, persist both identities and complete
     * recovery payload before publishing PREPARED. Never initialize/reset source. */
    int (*prepare)(void *, const DlumaCharacterRequest *, uint64_t *journal_id);
    int (*phase)(void *, uint64_t, DlumaCharacterPhase);
    int (*apply_target)(void *, uint64_t);
    /* Check full character state AND protected current world boundary. */
    int (*verify)(void *, uint64_t, int target);
    /* One atomic durable transaction: validate the live target incarnation,
     * preserve its progression AND publish RETURNING. Failure changes neither.
     * Recovery of RETURNING must never capture a checkpoint's live state again. */
    int (*capture_returning)(void *, uint64_t);
    /* Idempotent full source restoration; must not restore stale shared world. */
    int (*restore_source)(void *, uint64_t);
    /* Delete/complete durable journal only after verified source restoration. */
    int (*finish)(void *, uint64_t);
    int (*load)(void *, uint64_t, DlumaCharacterJournal *);
    /* begin=1 durably opens a classified shared-world boundary before mutation.
     * If already open, validate its ORIGINAL durable evidence; never rebase it.
     * begin=0 verifies that same evidence and atomically closes it on success.
     * Failure retains the original open record across retry/restart, even when
     * phase publication or verification fails. Only independently trusted
     * backend repair may resolve a changed boundary; enter/load cannot clear it.
     * prepare must not mutate world. finish may not clear an open boundary.
     * Backend contract version is 3; journal structure version remains 1. */
    int (*world_boundary)(void *, uint64_t, int begin);
    /* Under enter's lease, distinguish an authenticated existing target from
     * an explicit complete initialization configuration. Missing configuration
     * must not clone source, use chapter defaults, capture or prepare a journal.
     * A disposable test plan requires explicit authorization and save identity;
     * its target-only source copy is never a production default or source edit.
     * prepare revalidates the same configuration/target under this lease.
     * Restore/recovery do not call this gate: source recovery must remain usable
     * after an initialization configuration is removed. */
    DlumaCharacterTargetReadiness (*target_readiness)(void *, const DlumaCharacterRequest *);
} DlumaCharacterBackend;

/* Zero initialize once. A nonzero journal cannot be replaced by another begin.
 * Keep it or recover its durable ID after interruption; never memset active state. */
DlumaCharacterResult dluma_character_transition_begin(DlumaCharacterTransition *,
    const DlumaCharacterBackend *, const DlumaCharacterRequest *);
DlumaCharacterResult dluma_character_transition_restore(DlumaCharacterTransition *,
    const DlumaCharacterBackend *);
DlumaCharacterResult dluma_character_transition_recover(DlumaCharacterTransition *,
    const DlumaCharacterBackend *, uint64_t journal_id);
#endif
