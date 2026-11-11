/* Pure message policy; only scalar menu/accelerator commands may be replayed. */
#include <stdint.h>
typedef struct GitGuardState {
    UINT message; WPARAM wparam;
    uint64_t generation, token;
    uintptr_t main_identity;
} GitGuardState;
enum { GIT_GUARD_PASS = 0, GIT_GUARD_BLOCK = 1, GIT_GUARD_ABORT = 2 };
static int git_guard_decide(GitGuardState *state, int active, int is_main, int is_child,
    UINT message, WPARAM wparam, LPARAM lparam)
{
    if (!state || !active) return GIT_GUARD_PASS;
    /* Child control messages can carry transient HWND/pointer payloads. They
     * are blocked, never retained or replayed through the main frame. */
    if (!is_main) return is_child && message == WM_COMMAND ? GIT_GUARD_BLOCK : GIT_GUARD_PASS;
    if (message == WM_DESTROY || message == WM_NCDESTROY ||
        (message == WM_ENDSESSION && wparam)) return GIT_GUARD_ABORT;
    if (message == WM_QUERYENDSESSION) return GIT_GUARD_BLOCK | GIT_GUARD_ABORT;
    if (message == WM_COMMAND) {
        if (!state->message && !lparam && (uint64_t)wparam <= 0x1ffffU && LOWORD(wparam)) {
            state->message = WM_COMMAND; state->wparam = wparam;
        }
        return GIT_GUARD_BLOCK;
    }
    if (message == WM_CLOSE ||
        (message == WM_SYSCOMMAND && (wparam & 0xfff0U) == SC_CLOSE)) {
        if (!state->message) {
            state->message = message;
            state->wparam = message == WM_SYSCOMMAND ? SC_CLOSE : 0;
        }
        return GIT_GUARD_BLOCK;
    }
    return GIT_GUARD_PASS;
}

/* Capture only stable scalar identity; no borrowed pointer payload is retained. */
static void git_guard_capture(GitGuardState *state, uint64_t generation,
    uintptr_t main_identity, uint64_t token)
{
    if (!state || !state->message) return;
    state->generation = generation;
    state->main_identity = main_identity;
    state->token = token;
}

static int git_guard_replay_valid(const GitGuardState *state, uint64_t generation,
    uintptr_t main_identity, uint64_t token, int session_current, int project_current,
    int lease_active, int stopping)
{
    return state && state->message && state->generation && state->token &&
        state->generation == generation && state->main_identity == main_identity &&
        state->token == token && session_current && project_current && !lease_active && !stopping;
}
