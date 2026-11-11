#include <windows.h>
#include <stdio.h>
#include "../src/dlw_editor_git_guard.inc"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    GitGuardState s = {0};
    CHECK(git_guard_decide(&s, 0, 1, 0, WM_COMMAND, 7, 0) == GIT_GUARD_PASS);
    CHECK(git_guard_decide(&s, 1, 0, 0, WM_COMMAND, 7, 0) == GIT_GUARD_PASS);
    CHECK(git_guard_decide(&s, 1, 0, 1, WM_COMMAND, 3100, (LPARAM)123) == GIT_GUARD_BLOCK);
    CHECK(!s.message); /* A child HWND/payload is never replayed to main. */
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_COMMAND, 7, 0) == GIT_GUARD_BLOCK);
    CHECK(s.message == WM_COMMAND && s.wparam == 7);
    git_guard_capture(&s, 17, (uintptr_t)100, 5);
    CHECK(git_guard_replay_valid(&s, 17, (uintptr_t)100, 5, 1, 1, 0, 0));
    CHECK(!git_guard_replay_valid(&s, 18, (uintptr_t)100, 5, 1, 1, 0, 0));
    CHECK(!git_guard_replay_valid(&s, 17, (uintptr_t)101, 5, 1, 1, 0, 0));
    CHECK(!git_guard_replay_valid(&s, 17, (uintptr_t)100, 6, 1, 1, 0, 0));
    CHECK(!git_guard_replay_valid(&s, 17, (uintptr_t)100, 5, 0, 1, 0, 0));
    CHECK(!git_guard_replay_valid(&s, 17, (uintptr_t)100, 5, 1, 0, 0, 0));
    CHECK(!git_guard_replay_valid(&s, 17, (uintptr_t)100, 5, 1, 1, 1, 0));
    CHECK(!git_guard_replay_valid(&s, 17, (uintptr_t)100, 5, 1, 1, 0, 1));
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_CLOSE, 0, 0) == GIT_GUARD_BLOCK && s.message == WM_COMMAND);
    s.message = 0;
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_COMMAND, 12, 123) == GIT_GUARD_BLOCK && !s.message);
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_COMMAND, ((WPARAM)2 << 16) | 12, 0) == GIT_GUARD_BLOCK && !s.message);
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_SYSCOMMAND, SC_CLOSE, 42) == GIT_GUARD_BLOCK && s.message == WM_SYSCOMMAND && s.wparam == SC_CLOSE);
    s.message = 0;
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_QUERYENDSESSION, 0, 0) == (GIT_GUARD_BLOCK | GIT_GUARD_ABORT));
    CHECK(!s.message);
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_ENDSESSION, 1, 0) == GIT_GUARD_ABORT);
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_DESTROY, 0, 0) == GIT_GUARD_ABORT);
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_NCDESTROY, 0, 0) == GIT_GUARD_ABORT);
    CHECK(git_guard_decide(&s, 1, 1, 0, WM_TIMER, 1, 0) == GIT_GUARD_PASS);
    puts("Editor Git guard decision contract passed.");
    return 0;
}
