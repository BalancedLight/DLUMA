#include "dlw_character_transition.h"
#include <string.h>

static int valid_request(const DlumaCharacterRequest *r)
{
    return r && r->scope_id && r->source_profile && r->target_profile &&
        r->source_profile != r->target_profile;
}
static int valid_backend(const DlumaCharacterBackend *b)
{
    return b && b->version == 3 && b->enter && b->leave && b->prepare &&
        b->phase && b->apply_target && b->verify && b->capture_returning &&
        b->restore_source && b->finish && b->load && b->world_boundary && b->target_readiness;
}
static int qualified(const DlumaCharacterBackend *b)
{
    return (b->qualified_components & DLUMA_CHARACTER_REQUIRED_COMPONENTS) ==
        DLUMA_CHARACTER_REQUIRED_COMPONENTS;
}
static int valid_journal(const DlumaCharacterJournal *j)
{
    return j->id && j->version == 1 && valid_request(&j->request) &&
        j->phase >= DLUMA_CHARACTER_PREPARED && j->phase <= DLUMA_CHARACTER_RESTORING;
}
static int persist_phase(DlumaCharacterTransition *t, const DlumaCharacterBackend *b,
    DlumaCharacterPhase phase)
{
    if (!b->phase(b->user, t->journal.id, phase)) return 0;
    t->journal.phase = phase; return 1;
}
static DlumaCharacterResult restore_locked(DlumaCharacterTransition *t,
    const DlumaCharacterBackend *b, int boundary_open)
{
    if (!boundary_open && !b->world_boundary(b->user, t->journal.id, 1))
        return DLUMA_CHARACTER_RECOVERY_REQUIRED;
    if (t->journal.phase == DLUMA_CHARACTER_ACTIVE) {
        if (!b->capture_returning(b->user, t->journal.id)) {
            if (!b->world_boundary(b->user, t->journal.id, 0))
                return DLUMA_CHARACTER_RECOVERY_REQUIRED;
            return DLUMA_CHARACTER_CAPTURE_FAILED;
        }
        t->journal.phase = DLUMA_CHARACTER_RETURNING;
    }
    /* PREPARED/APPLYING are also recovery instructions: source must win when
     * target application was interrupted before ACTIVE became durable. */
    if (!persist_phase(t, b, DLUMA_CHARACTER_RESTORING))
        return DLUMA_CHARACTER_RECOVERY_REQUIRED;
    if (!b->restore_source(b->user, t->journal.id) ||
        !b->verify(b->user, t->journal.id, 0))
        return DLUMA_CHARACTER_RECOVERY_REQUIRED;
    if (!b->world_boundary(b->user, t->journal.id, 0))
        return DLUMA_CHARACTER_RECOVERY_REQUIRED;
    if (!b->finish(b->user, t->journal.id))
        return DLUMA_CHARACTER_STORAGE_FAILED;
    memset(&t->journal, 0, sizeof(t->journal));
    return DLUMA_CHARACTER_OK;
}
DlumaCharacterResult dluma_character_transition_begin(DlumaCharacterTransition *t,
    const DlumaCharacterBackend *b, const DlumaCharacterRequest *r)
{
    DlumaCharacterResult result; DlumaCharacterTargetReadiness readiness; uint64_t id = 0;
    if (!t || !valid_backend(b) || !valid_request(r)) return DLUMA_CHARACTER_INVALID;
    if (t->busy) return DLUMA_CHARACTER_BUSY;
    if (t->journal.id) return DLUMA_CHARACTER_PENDING;
    if (!qualified(b)) return DLUMA_CHARACTER_UNSUPPORTED;
    t->busy = 1;
    if (!b->enter(b->user, r, 0)) { t->busy = 0; return DLUMA_CHARACTER_OWNER_CHANGED; }
    readiness = b->target_readiness(b->user, r);
    if (readiness == DLUMA_CHARACTER_TARGET_CONFIGURATION_REQUIRED) {
        result = DLUMA_CHARACTER_CONFIGURATION_REQUIRED; goto done;
    }
    if (readiness != DLUMA_CHARACTER_TARGET_SAVED && readiness != DLUMA_CHARACTER_TARGET_EXPLICIT_CONFIGURATION) {
        result = DLUMA_CHARACTER_INVALID; goto done;
    }
    if (!b->prepare(b->user, r, &id) || !id) {
        result = DLUMA_CHARACTER_STORAGE_FAILED; goto done;
    }
    t->journal.version = 1; t->journal.id = id; t->journal.request = *r;
    t->journal.phase = DLUMA_CHARACTER_PREPARED;
    if (!b->world_boundary(b->user, id, 1)) {
        result = DLUMA_CHARACTER_RECOVERY_REQUIRED; goto done;
    }
    if (!persist_phase(t, b, DLUMA_CHARACTER_APPLYING)) {
        result = DLUMA_CHARACTER_STORAGE_FAILED; goto done;
    }
    if (!b->apply_target(b->user, id) || !b->verify(b->user, id, 1)) {
        result = restore_locked(t, b, 1);
        if (result == DLUMA_CHARACTER_OK) result = DLUMA_CHARACTER_APPLY_FAILED;
        goto done;
    }
    if (!b->world_boundary(b->user, id, 0)) {
        result = restore_locked(t, b, 1);
        if (result == DLUMA_CHARACTER_OK) result = DLUMA_CHARACTER_APPLY_FAILED;
        goto done;
    }
    if (!persist_phase(t, b, DLUMA_CHARACTER_ACTIVE)) {
        result = restore_locked(t, b, 0);
        if (result == DLUMA_CHARACTER_OK) result = DLUMA_CHARACTER_STORAGE_FAILED;
        goto done;
    }
    result = DLUMA_CHARACTER_OK;
done:
    b->leave(b->user); t->busy = 0; return result;
}
DlumaCharacterResult dluma_character_transition_restore(DlumaCharacterTransition *t,
    const DlumaCharacterBackend *b)
{
    DlumaCharacterResult result;
    if (!t || !valid_backend(b)) return DLUMA_CHARACTER_INVALID;
    if (t->busy) return DLUMA_CHARACTER_BUSY;
    if (!qualified(b)) return DLUMA_CHARACTER_UNSUPPORTED;
    if (!t->journal.id) return DLUMA_CHARACTER_OK;
    if (!valid_journal(&t->journal)) return DLUMA_CHARACTER_INVALID;
    t->busy = 1;
    if (!b->enter(b->user, &t->journal.request, t->journal.id)) {
        t->busy = 0; return DLUMA_CHARACTER_OWNER_CHANGED;
    }
    result = restore_locked(t, b, 0);
    b->leave(b->user); t->busy = 0; return result;
}
DlumaCharacterResult dluma_character_transition_recover(DlumaCharacterTransition *t,
    const DlumaCharacterBackend *b, uint64_t id)
{
    DlumaCharacterJournal journal; int loaded;
    if (!t || !id || !valid_backend(b)) return DLUMA_CHARACTER_INVALID;
    if (t->busy) return DLUMA_CHARACTER_BUSY;
    if (t->journal.id) return DLUMA_CHARACTER_PENDING;
    if (!qualified(b)) return DLUMA_CHARACTER_UNSUPPORTED;
    memset(&journal, 0, sizeof(journal)); t->busy = 1;
    loaded = b->load(b->user, id, &journal); t->busy = 0;
    if (!loaded) return DLUMA_CHARACTER_STORAGE_FAILED;
    if (journal.id != id || !valid_journal(&journal)) return DLUMA_CHARACTER_INVALID;
    t->journal = journal;
    return dluma_character_transition_restore(t, b);
}
