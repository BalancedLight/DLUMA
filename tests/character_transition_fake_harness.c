#include "../src/core/dlw_character_transition.c"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
enum { ENTER = 1, PREPARE, PHASE, APPLY, VERIFY_TARGET, CAPTURE_TARGET, RESTORE, VERIFY_SOURCE, FINISH, LOAD };
typedef struct Fake {
    DlumaCharacterJournal disk;
    int exists, held, fail, corrupt_world, entered, left, applies, restores;
    int phase_calls, fail_phase_call;
    int target_readiness, readiness_calls, prepares;
    /* Durable fake journal metadata, survives a new coordinator object. */
    int world_boundary_open, world_boundary_original;
    uint64_t current_profile;
    int character[6], source[6], target[6], world, protected_world;
    DlumaCharacterTransition *reenter;
    const DlumaCharacterBackend *backend;
    int reentry_result;
} Fake;

static int enter(void *u, const DlumaCharacterRequest *r, uint64_t id)
{
    Fake *f = u; (void)id;
    if (f->fail == ENTER || r->scope_id != 7 || r->source_profile != 1 ||
        r->target_profile != 2 || f->held) return 0;
    f->held = 1; f->entered++; f->protected_world = f->world; return 1;
}
static void leave(void *u) { Fake *f = u; f->held = 0; f->left++; }
static int prepare(void *u, const DlumaCharacterRequest *r, uint64_t *id)
{
    Fake *f = u; f->prepares++;
    if (f->fail == PREPARE || f->exists || f->current_profile != r->source_profile) return 0;
    memcpy(f->source, f->character, sizeof(f->source));
    f->disk.version = 1; f->disk.id = 11; f->disk.request = *r;
    f->disk.phase = DLUMA_CHARACTER_PREPARED; f->exists = 1; *id = 11;
    return 1;
}
static int phase(void *u, uint64_t id, DlumaCharacterPhase p)
{
    Fake *f = u; (void)id;
    f->phase_calls++;
    if (f->fail == PHASE || f->phase_calls == f->fail_phase_call) return 0;
    f->disk.phase = p; return 1;
}
static int apply(void *u, uint64_t id)
{
    Fake *f = u; (void)id; f->applies++;
    if (f->reenter) f->reentry_result = dluma_character_transition_restore(f->reenter, f->backend);
    memcpy(f->character, f->target, sizeof(f->target));
    f->current_profile = 2;
    if (f->corrupt_world) f->world++;
    return f->fail != APPLY;
}
static int verify(void *u, uint64_t id, int target)
{
    Fake *f = u; (void)id;
    return f->fail != (target ? VERIFY_TARGET : VERIFY_SOURCE) &&
        f->current_profile == (target ? 2u : 1u) &&
        f->world == f->protected_world &&
        !memcmp(f->character, target ? f->target : f->source, sizeof(f->source));
}
static int capture_returning(void *u, uint64_t id)
{
    Fake *f = u; (void)id;
    if (f->fail == CAPTURE_TARGET || f->current_profile != 2) return 0;
    memcpy(f->target, f->character, sizeof(f->target));
    f->disk.phase = DLUMA_CHARACTER_RETURNING; return 1;
}
static int restore(void *u, uint64_t id)
{
    Fake *f = u; (void)id; f->restores++;
    if (f->fail == RESTORE) return 0;
    memcpy(f->character, f->source, sizeof(f->source)); f->current_profile = 1; return 1;
}
static int finish(void *u, uint64_t id)
{
    Fake *f = u; (void)id;
    if (f->fail == FINISH || f->world_boundary_open) return 0;
    f->exists = 0; return 1;
}
static int load(void *u, uint64_t id, DlumaCharacterJournal *out)
{
    Fake *f = u;
    if (f->fail == LOAD || !f->exists || id != f->disk.id) return 0;
    *out = f->disk; return 1;
}
static int world_boundary(void *u, uint64_t id, int begin)
{
    Fake *f = u; (void)id;
    if (!f->world_boundary_open) {
        if (!begin) return 0;
        f->world_boundary_original = f->world;
        f->world_boundary_open = 1;
    }
    if (f->world != f->world_boundary_original) return 0;
    if (!begin) f->world_boundary_open = 0;
    return 1;
}
static DlumaCharacterTargetReadiness target_readiness(void* u,const DlumaCharacterRequest* request)
{
    Fake* f=u;(void)request;f->readiness_calls++;
    return (DlumaCharacterTargetReadiness)f->target_readiness;
}
static DlumaCharacterBackend backend(Fake *f)
{
    DlumaCharacterBackend b = {3, DLUMA_CHARACTER_REQUIRED_COMPONENTS, f,
        enter, leave, prepare, phase, apply, verify, capture_returning, restore, finish, load, world_boundary,target_readiness};
    return b;
}
static void setup(Fake *f)
{
    unsigned i; memset(f, 0, sizeof(*f)); f->world = 100; f->current_profile = 1;
    f->target_readiness=DLUMA_CHARACTER_TARGET_SAVED;
    for (i = 0; i < 6; ++i) { f->character[i] = (int)i + 10; f->target[i] = (int)i + 50; }
}
static int configuration_regression(void)
{
    Fake f;DlumaCharacterBackend b;DlumaCharacterTransition t={0},reboot={0};
    DlumaCharacterRequest request={7,1,2};int current[6],target[6];
    setup(&f);b=backend(&f);f.target_readiness=DLUMA_CHARACTER_TARGET_CONFIGURATION_REQUIRED;
    memcpy(current,f.character,sizeof(current));memcpy(target,f.target,sizeof(target));
    CHECK(dluma_character_transition_begin(&t,&b,&request)==DLUMA_CHARACTER_CONFIGURATION_REQUIRED);
    CHECK(!f.exists&&!t.journal.id&&!f.prepares&&!f.applies&&!f.restores&&!f.world_boundary_open);
    CHECK(f.entered==1&&f.left==1&&!memcmp(current,f.character,sizeof(current))&&!memcmp(target,f.target,sizeof(target)));
    /* Explicit user or authenticated disposable test configuration is distinct
     * from an automatic source clone. The coordinator never creates defaults. */
    f.target_readiness=DLUMA_CHARACTER_TARGET_EXPLICIT_CONFIGURATION;
    CHECK(dluma_character_transition_begin(&t,&b,&request)==DLUMA_CHARACTER_OK);
    CHECK(f.prepares==1&&f.source[0]==10&&f.character[0]==50);
    f.target_readiness=DLUMA_CHARACTER_TARGET_CONFIGURATION_REQUIRED;
    CHECK(dluma_character_transition_recover(&reboot,&b,11)==DLUMA_CHARACTER_OK);
    CHECK(f.readiness_calls==2&&f.character[0]==10&&!f.exists);
    f.target_readiness=99;
    CHECK(dluma_character_transition_begin(&t,&b,&request)==DLUMA_CHARACTER_PENDING);
    memset(&t,0,sizeof(t));
    CHECK(dluma_character_transition_begin(&t,&b,&request)==DLUMA_CHARACTER_INVALID&&!f.exists&&f.prepares==1);
    return 0;
}
static int capture_crash_regression(void)
{
    Fake f; DlumaCharacterBackend b; DlumaCharacterTransition t = {0}, reboot = {0};
    DlumaCharacterRequest request = {7, 1, 2};
    setup(&f); b = backend(&f);
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
    f.character[0] = 77;
    f.fail_phase_call = f.phase_calls + 1;
    CHECK(dluma_character_transition_restore(&t, &b) != DLUMA_CHARACTER_OK);
    CHECK(f.target[0] == 77 && f.exists);
    /* Simulate checkpoint reload replacing the live target after its durable
     * capture, then recover using only the persisted journal. */
    f.character[0] = 10; f.current_profile = 1;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_OK);
    CHECK(f.target[0] == 77 && f.character[0] == 10);
    /* The exact crash point after the atomic callback commits RETURNING but
     * before the coordinator observes its return also skips target recapture. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t)); memset(&reboot, 0, sizeof(reboot));
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
    f.character[0] = 91;
    CHECK(world_boundary(&f, 11, 1)); CHECK(capture_returning(&f, 11));
    f.character[0] = 10; f.current_profile = 1;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_OK);
    CHECK(f.target[0] == 91 && f.character[0] == 10);
    /* An ACTIVE checkpoint containing a different live character is refused
     * before it can replace the stored target payload. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t)); memset(&reboot, 0, sizeof(reboot));
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
    f.current_profile = 1; f.character[0] = 10;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_CAPTURE_FAILED);
    CHECK(f.target[0] == 50 && f.exists && f.disk.phase == DLUMA_CHARACTER_ACTIVE);
    return 0;
}
static int world_retry_regression(void)
{
    Fake f; DlumaCharacterBackend b; DlumaCharacterTransition t = {0}, reboot = {0};
    DlumaCharacterRequest request = {7, 1, 2};
    setup(&f); b = backend(&f); f.corrupt_world = 1;
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_RECOVERY_REQUIRED);
    CHECK(f.world == 101 && f.exists);
    CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_RECOVERY_REQUIRED);
    CHECK(f.exists && t.journal.id);
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_RECOVERY_REQUIRED);
    CHECK(f.exists && reboot.journal.id && f.world == 101);
    /* Independent trusted repair restores the original evidence; no coordinator
     * retry, enter or load is allowed to replace this baseline. */
    f.world = 100;
    CHECK(dluma_character_transition_restore(&reboot, &b) == DLUMA_CHARACTER_OK);
    CHECK(!f.exists && !f.world_boundary_open && f.character[0] == 10);
    return 0;
}
int main(int argc, char **argv)
{
    Fake f; DlumaCharacterBackend b; DlumaCharacterTransition t = {0}, reboot = {0};
    DlumaCharacterRequest request = {7, 1, 2}; int failure; unsigned cycle;
    if (argc == 2) {
        int result = !strcmp(argv[1], "configuration") ? configuration_regression() :
            !strcmp(argv[1], "capture-crash") ? capture_crash_regression() : world_retry_regression();
        if (!result) puts("PASS: character transition recovery");
        return result;
    }
    setup(&f); b = backend(&f); f.backend = &b;
    b.qualified_components = 0;
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_UNSUPPORTED);
    CHECK(!f.entered && !f.exists); b.qualified_components = DLUMA_CHARACTER_REQUIRED_COMPONENTS;
    for (cycle = 0; cycle < 4; ++cycle) {
        CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
        CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_PENDING);
        f.character[0]++; f.world++; /* Target progress and shared world advance independently. */
        CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_OK);
        CHECK(f.character[0] == 10 && f.target[0] == 51 + (int)cycle);
        CHECK(f.world == 101 + (int)cycle && !f.exists && !t.journal.id);
    }
    for (failure = ENTER; failure <= VERIFY_TARGET; ++failure) {
        setup(&f); b = backend(&f); memset(&t, 0, sizeof(t)); f.fail = failure;
        CHECK(dluma_character_transition_begin(&t, &b, &request) != DLUMA_CHARACTER_OK);
        CHECK(f.character[0] == 10 && (f.applies == 0) == (failure <= PHASE));
        CHECK(f.entered == f.left && !f.held);
        f.fail = 0;
        if (t.journal.id) CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_OK);
        CHECK(!f.exists);
    }
    for (failure = CAPTURE_TARGET; failure <= FINISH; ++failure) {
        setup(&f); b = backend(&f); memset(&t, 0, sizeof(t));
        CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
        f.character[0] = 77; f.fail = failure;
        CHECK(dluma_character_transition_restore(&t, &b) != DLUMA_CHARACTER_OK);
        CHECK(f.exists && t.journal.id && f.entered == f.left);
        CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_PENDING);
        f.fail = 0; memset(&reboot, 0, sizeof(reboot));
        CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_OK);
        CHECK(!f.exists && f.character[0] == 10 && f.target[0] == 77);
    }
    /* Crash after durable APPLYING but before the result is known. */
    setup(&f); b = backend(&f); memset(&reboot, 0, sizeof(reboot));
    CHECK(prepare(&f, &request, &f.disk.id)); f.disk.phase = DLUMA_CHARACTER_APPLYING;
    memcpy(f.character, f.target, sizeof(f.character));
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_OK);
    CHECK(f.character[0] == 10 && !f.exists);
    /* ACTIVE publication failure rolls back the already-applied target. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t)); f.fail_phase_call = 2;
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_STORAGE_FAILED);
    CHECK(f.applies == 1 && f.restores == 1 && f.character[0] == 10 && !f.exists);
    /* Capture+RETURNING is atomic; later phase failure cannot recapture target. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t));
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
    f.character[0] = 88; f.fail_phase_call = f.phase_calls + 1;
    CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_RECOVERY_REQUIRED);
    CHECK(f.character[0] == 88 && f.target[0] == 88 && !f.restores);
    CHECK(f.disk.phase == DLUMA_CHARACTER_RETURNING);
    CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_OK);
    CHECK(f.character[0] == 10 && f.target[0] == 88 && !f.exists);
    /* Corrupt/foreign durable records and read failures never touch state. */
    setup(&f); b = backend(&f); memset(&reboot, 0, sizeof(reboot));
    CHECK(prepare(&f, &request, &f.disk.id)); f.disk.version = 2;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_INVALID);
    CHECK(!f.entered && !f.applies && !f.restores && f.exists);
    f.disk.version = 1; f.fail = LOAD;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_STORAGE_FAILED);
    CHECK(!f.entered && !reboot.journal.id && f.exists);
    f.fail = 0; f.disk.phase = (DlumaCharacterPhase)99;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_INVALID);
    f.disk.phase = DLUMA_CHARACTER_PREPARED; f.disk.request.target_profile = 3;
    CHECK(dluma_character_transition_recover(&reboot, &b, 11) == DLUMA_CHARACTER_OWNER_CHANGED);
    CHECK(f.exists && !f.applies && !f.restores);
    /* Refuse foreign save/map identity without touching its active character. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t));
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
    t.journal.request.scope_id = 8;
    CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_OWNER_CHANGED);
    CHECK(f.character[0] == 50 && f.exists);
    /* Reentrancy during backend mutation must not start restoration. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t)); f.reenter = &t; f.backend = &b;
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_OK);
    CHECK(f.reentry_result == DLUMA_CHARACTER_BUSY && !f.restores);
    CHECK(dluma_character_transition_restore(&t, &b) == DLUMA_CHARACTER_OK);
    /* Wrong world ownership is observable and retains a recovery journal. */
    setup(&f); b = backend(&f); memset(&t, 0, sizeof(t)); f.corrupt_world = 1;
    CHECK(dluma_character_transition_begin(&t, &b, &request) == DLUMA_CHARACTER_RECOVERY_REQUIRED);
    CHECK(f.character[0] == 10 && f.exists && f.world == 101);
    puts("PASS: character transition recovery"); return 0;
}
