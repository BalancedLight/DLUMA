/* Exercise source-control guard dispatch without an Editor or native offsets. */
#include <stdio.h>
#include <stdlib.h>
#define DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC 1
#include "../src/dlw_editor_source_control_ui.c"
static int aborts, armed_stub, session_ready = 1, disarm_allowed = 1, disarms, unsubscribes;
static int authority_stub, job_snapshot_ok, job_active_stub = 1, profile_stub, coverage_stub = 1, binding_stub = 1;
static int coverage_capture_stub = DLUMA_EDITOR_UI_COVERAGE_READY;
static uint64_t binding_generation_stub = 1, binding_creation_stub = 100;
static unsigned int last_readiness_mask = 0xffffffffU;
static unsigned int last_busy_mask = 0xffffffffU;
static unsigned int last_pending_mask = 0xffffffffU;
static HANDLE companion_event;
static HWND fixture_window;
static WCHAR fixture_cleanup[MAX_PATH];
static uint64_t manager_words[24], catalog_words[18];
static uint64_t policy_editor[0x24B8U / sizeof(uint64_t)];
static void *policy_global, *policy_table = (void *)(UINT_PTR)0x12340;
static DlumaEditorGitParsedRequest request_fixture;
static void cleanup_fixture(void)
{
    if (git_ui.project) { CloseHandle(git_ui.project); git_ui.project = NULL; }
    if (companion_event) { CloseHandle(companion_event); companion_event = NULL; }
    if (fixture_window) { DestroyWindow(fixture_window); fixture_window = NULL; }
    if (fixture_cleanup[0]) { RemoveDirectoryW(fixture_cleanup); fixture_cleanup[0] = 0; }
}

int dluma_editor_git_session_abort(void) { ++aborts; return 1; }
void dluma_log_line(const char *level, const char *message) {
    unsigned int value; (void)level;
    if (sscanf(message, "Git readiness mask=%u", &value) == 1) last_readiness_mask = value;
    if (sscanf(message, "Git admission busy mask=%u", &value) == 1) last_busy_mask = value;
    if (sscanf(message, "Git admission armed mask=%u", &value) == 1) last_pending_mask = value;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv)
{
    LRESULT result = -1;
    HWND main_window = (HWND)(UINT_PTR)1;
    WCHAR temp_dir[MAX_PATH], ini[MAX_PATH];
    if (argc == 2) {
        CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, git_ui.root, GIT_PATH_CAP));
        git_ui.diagnostic_git_dir = git_diagnostic_open_git_dir(git_ui.root);
        CHECK(git_ui.diagnostic_git_dir && git_diagnostic_metadata_valid(1));
        git_diagnostic_close(); puts("Diagnostic metadata scan passed."); return 0;
    }
    CHECK(argc == 1);
    git_ui.main = main_window; git_ui.thread = (LONG)GetCurrentThreadId(); git_ui.generation = 17;
    git_ui.lease.active = 1;
    CHECK(git_operation_guard(main_window, DLUMA_EDITOR_UI_BEFORE, WM_COMMAND, 9, 0, &result, NULL));
    CHECK(result == 0 && git_ui.deferred.message == WM_COMMAND && git_ui.deferred.wparam == 9 &&
        git_ui.deferred.generation == 17 && git_ui.deferred.main_identity == (uintptr_t)main_window && git_ui.deferred.token != 0 && !aborts);
    result = -1;
    CHECK(git_operation_guard(main_window, DLUMA_EDITOR_UI_BEFORE, WM_QUERYENDSESSION, 0, 0, &result, NULL));
    CHECK(result == 0 && aborts == 1 && git_ui.aborting && git_ui.closing && git_ui.lease.recovery && git_ui.lease.active);
    result = -1;
    CHECK(!git_operation_guard(main_window, DLUMA_EDITOR_UI_BEFORE, WM_DESTROY, 0, 0, &result, NULL));
    CHECK(aborts == 1 && result == -1);
    git_ui.closing = git_ui.aborting = git_ui.lease.recovery = 0; aborts = 0;
    CHECK(!git_operation_guard(main_window, DLUMA_EDITOR_UI_COVERAGE_LOST, 0, 0, 0, &result, NULL));
    CHECK(aborts == 1 && git_ui.closing && git_ui.lease.recovery && git_ui.lease.active);
    git_ui.lease.active = 0; git_ui.closing = 0; git_ui.guard_registered = 1; git_ui.session = 0;
    git_replay_message = WM_APP + 113; result = -1;
    CHECK(on_window(main_window, DLUMA_EDITOR_UI_BEFORE, git_replay_message,
        (WPARAM)git_ui.deferred.token, 0, &result, NULL));
    CHECK(result == 0 && !git_ui.deferred.message);
    ZeroMemory(&git_ui.deferred, sizeof(git_ui.deferred));
    git_ui.closing = 0; git_ui.aborting = 0; git_ui.lease.active = 0; armed_stub = 1; result = -1;
    CHECK(git_operation_guard(main_window, DLUMA_EDITOR_UI_BEFORE, WM_COMMAND, 10, 0, &result, NULL));
    CHECK(result == 0 && git_ui.deferred.message == WM_COMMAND && git_ui.deferred.wparam == 10);
    armed_stub = 0;
    {
        HWND parent = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
        HWND child;
        CHECK(parent);
        child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1, parent, NULL, GetModuleHandleW(NULL), NULL);
        CHECK(child);
        git_ui.main = parent; armed_stub = 1; ZeroMemory(&git_ui.deferred, sizeof(git_ui.deferred)); result = -1;
        CHECK(git_operation_guard(child, DLUMA_EDITOR_UI_BEFORE, WM_COMMAND, 3100, (LPARAM)child, &result, NULL));
        CHECK(result == 0 && !git_ui.deferred.message);
        armed_stub = 0; git_ui.main = main_window;
        CHECK(DestroyWindow(child)); CHECK(DestroyWindow(parent));
    }
    git_ui.session = 1; git_ui.guard_registered = 1; git_ui.guard_epoch = 7;
    session_ready = 0; disarm_allowed = 0; disarms = unsubscribes = 0;
    CHECK(!close_session() && !disarms && !unsubscribes && git_ui.guard_epoch == 7);
    session_ready = 1;
    CHECK(!close_session() && disarms == 1 && !unsubscribes && git_ui.guard_epoch == 7);
    disarm_allowed = 1;
    CHECK(close_session() && disarms == 2 && unsubscribes == 1 && !git_ui.guard_epoch && !git_ui.guard_registered);
    {
        WCHAR fixture_dir[MAX_PATH], short_dir[MAX_PATH], diagnostic_ini[MAX_PATH], metadata_dir[MAX_PATH], redirect_path[MAX_PATH];
        WCHAR objects_dir[MAX_PATH], refs_dir[MAX_PATH], head_path[MAX_PATH];
        char fixture_path[MAX_PATH], response[1024];
        size_t used = 0;
        unsigned int k;
        HWND window;
        CHECK(GetTempPathW(MAX_PATH, temp_dir) && GetTempFileNameW(temp_dir, L"dgr", 0, fixture_dir));
        CHECK(DeleteFileW(fixture_dir) && CreateDirectoryW(fixture_dir, NULL));
        lstrcpyW(fixture_cleanup, fixture_dir); CHECK(atexit(cleanup_fixture) == 0);
        if (GetShortPathNameW(fixture_dir, short_dir, MAX_PATH)) lstrcpyW(fixture_dir, short_dir);
        CHECK(WideCharToMultiByte(CP_UTF8, 0, fixture_dir, -1, fixture_path, MAX_PATH, NULL, NULL));
        for (k = 0; fixture_path[k]; ++k) CHECK((unsigned char)fixture_path[k] < 0x80U);
        window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
        CHECK(window); fixture_window = window; companion_event = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(companion_event);
        ZeroMemory(manager_words, sizeof(manager_words)); ZeroMemory(catalog_words, sizeof(catalog_words));
        manager_words[4] = (uint64_t)(uintptr_t)catalog_words; ((unsigned int *)manager_words)[10] = 1;
        ((int *)manager_words)[46] = 0; catalog_words[8] = (uint64_t)(uintptr_t)fixture_path;
        profile_stub = authority_stub = 1; armed_stub = 0;
        ZeroMemory(policy_editor, sizeof(policy_editor));
        policy_global = policy_editor; *(void **)policy_editor = policy_table;
        CHECK(!git_policy_read_off(&policy_global, policy_table, GetCurrentThreadId() + 1U, NULL, NULL));
        git_ui.coverage_generation = 1;
        git_ui.main = window; git_ui.thread = (LONG)GetCurrentThreadId(); git_ui.generation = 42;
        CHECK(GetTempFileNameW(temp_dir, L"dgd", 0, diagnostic_ini));
        CHECK(WritePrivateProfileStringW(L"loader", L"developer_mode", L"0", diagnostic_ini));
        CHECK(WritePrivateProfileStringW(L"loader", L"editor_git_local_diagnostic", L"1", diagnostic_ini));
        CHECK(WritePrivateProfileStringW(L"loader", L"editor_git_diagnostic_project", fixture_dir, diagnostic_ini));
        git_diagnostic_configure(diagnostic_ini);
        git_ui.project = selected_project(git_ui.root, GIT_PATH_CAP); CHECK(git_ui.project);
        CHECK(!git_ui.diagnostic_requested && git_ordinary_local_qualified() && git_mutations_qualified());
        CHECK(WritePrivateProfileStringW(L"loader", L"developer_mode", L"1", diagnostic_ini));
        CHECK(WritePrivateProfileStringW(L"loader", L"editor_git_local_diagnostic", L"0", diagnostic_ini));
        git_diagnostic_configure(diagnostic_ini); CHECK(!git_ui.diagnostic_requested && git_ordinary_local_qualified());
        CHECK(WritePrivateProfileStringW(L"loader", L"editor_git_local_diagnostic", L"1", diagnostic_ini));
        git_diagnostic_configure(diagnostic_ini);
        CHECK(git_ui.diagnostic_requested && !git_ordinary_local_active() && !git_mutations_qualified());
        lstrcpyW(metadata_dir, fixture_dir); lstrcatW(metadata_dir, L"\\.git");
        { HANDLE linked = CreateFileW(metadata_dir, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
          CHECK(linked != INVALID_HANDLE_VALUE); CloseHandle(linked); }
        git_diagnostic_configure(diagnostic_ini); CHECK(!git_mutations_qualified()); CHECK(DeleteFileW(metadata_dir));
        CHECK(CreateDirectoryW(metadata_dir, NULL));
        git_diagnostic_configure(diagnostic_ini);
        CHECK(!git_mutations_qualified());
        lstrcpyW(objects_dir, metadata_dir); lstrcatW(objects_dir, L"\\objects");
        lstrcpyW(refs_dir, metadata_dir); lstrcatW(refs_dir, L"\\refs");
        lstrcpyW(head_path, metadata_dir); lstrcatW(head_path, L"\\HEAD");
        CHECK(CreateDirectoryW(objects_dir, NULL) && CreateDirectoryW(refs_dir, NULL));
        { HANDLE head = CreateFileW(head_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
          CHECK(head != INVALID_HANDLE_VALUE); CloseHandle(head); }
        CHECK(git_mutations_qualified());
        lstrcpyW(redirect_path, metadata_dir); lstrcatW(redirect_path, L"\\commondir");
        { HANDLE redirect = CreateFileW(redirect_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
          CHECK(redirect != INVALID_HANDLE_VALUE); CloseHandle(redirect); }
        CHECK(!git_mutations_qualified()); CHECK(DeleteFileW(redirect_path)); CHECK(git_mutations_qualified());
        { WCHAR alias_path[MAX_PATH]; HANDLE original;
          lstrcpyW(redirect_path, metadata_dir); lstrcatW(redirect_path, L"\\sample");
          lstrcpyW(alias_path, metadata_dir); lstrcatW(alias_path, L"\\sample-link");
          original = CreateFileW(redirect_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
          CHECK(original != INVALID_HANDLE_VALUE); CloseHandle(original);
          CHECK(CreateHardLinkW(alias_path, redirect_path, NULL)); CHECK(!git_mutations_qualified());
          CHECK(DeleteFileW(alias_path) && DeleteFileW(redirect_path)); CHECK(git_mutations_qualified()); }
        { HANDLE actual = git_ui.project;
          HANDLE different = CreateFileW(temp_dir, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          CHECK(different != INVALID_HANDLE_VALUE); git_ui.project = different;
          CHECK(!git_mutations_qualified()); git_ui.project = actual; CloseHandle(different); }
        CHECK(git_local_operation_allowed("stage") && git_local_operation_allowed("unstage") &&
              git_local_operation_allowed("commit"));
        CHECK(!git_local_operation_allowed("initialize") && !git_local_operation_allowed("createBranch") &&
              !git_local_operation_allowed("addRemote") && !git_local_operation_allowed("fetch") &&
              !git_local_operation_allowed("push") && !git_local_operation_allowed("clone"));
        CHECK(git_local_operation_permitted("createBranch", 1) && git_local_operation_permitted("stage", 1) &&
              !git_local_operation_permitted("initialize", 1) && !git_local_operation_permitted("switchBranch", 1) &&
              !git_local_operation_permitted("fetch", 1) && !git_local_operation_permitted("push", 1));
        CHECK(git_ordinary_mode_allowed(1, 0) && !git_ordinary_mode_allowed(1, 1));
        CHECK(git_ui.diagnostic_requested && !git_ordinary_local_active());
        ZeroMemory(&git_ui.lease, sizeof(git_ui.lease));
        git_ui.session = git_ui.guard_registered = 1; git_ui.guard_epoch = 0;
        git_ui.panel.content_window = (HWND)(UINT_PTR)5;
        git_ui.closing = git_ui.aborting = 0; git_ui.executable = companion_event; job_snapshot_ok = 1;
        git_ui.guard_diagnostics = 1; last_readiness_mask = 0xffffffffU;
        git_observe_readiness(1); CHECK(last_readiness_mask == 0U);
        authority_stub = 0; git_observe_readiness(1); CHECK(last_readiness_mask & 8U); authority_stub = 1;
        ((BYTE *)policy_editor)[0x1057] = 1; git_observe_readiness(1); CHECK(last_readiness_mask & 16U);
        ((BYTE *)policy_editor)[0x1057] = 0;
        lstrcpyW(redirect_path, metadata_dir); lstrcatW(redirect_path, L"\\commondir");
        { HANDLE redirect = CreateFileW(redirect_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
          CHECK(redirect != INVALID_HANDLE_VALUE); CloseHandle(redirect); }
        git_observe_readiness(1); CHECK(last_readiness_mask & 4U); CHECK(DeleteFileW(redirect_path));
        ZeroMemory(&request_fixture, sizeof(request_fixture)); request_fixture.kind = DLUMA_GIT_PROJECT_ACQUIRE;
        request_fixture.project_generation = 42;
        CHECK(WideCharToMultiByte(CP_UTF8, 0, git_ui.root, -1, request_fixture.project_root,
                                  sizeof(request_fixture.project_root), NULL, NULL));
        lstrcpyA(request_fixture.operation, "fetch"); used = 0;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.arm.active && !git_ui.lease.active && strstr(response, "\"granted\":false") &&
              strstr(response, "\"code\":\"unavailable\""));
        lstrcpyA(request_fixture.operation, "stage"); used = 0;
        coverage_capture_stub = DLUMA_EDITOR_UI_COVERAGE_FAILED;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.arm.active && strstr(response, "\"code\":\"unavailable\"") && strstr(response, "Close extra windows"));
        coverage_capture_stub = DLUMA_EDITOR_UI_COVERAGE_READY; used = 0;
        job_active_stub = 2;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.arm.active && !git_ui.lease.active && strstr(response, "\"code\":\"busy\"") &&
              last_busy_mask != 0xffffffffU && (last_busy_mask & 2U));
        job_active_stub = 1; used = 0;
        git_ui.children[0] = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(git_ui.children[0]);
        last_busy_mask = 0xffffffffU;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.arm.active && last_busy_mask != 0xffffffffU && (last_busy_mask & 1U));
        CHECK(SetEvent(git_ui.children[0])); used = 0;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(git_ui.arm.active && !git_ui.lease.active && strstr(response, "\"code\":\"pending\""));
        git_observe_pending(1U, 2U); CHECK(last_pending_mask != 0xffffffffU && (last_pending_mask & 32U) && (last_pending_mask & 64U));
        request_fixture.has_arm = 1; memcpy(request_fixture.arm_id, git_ui.arm.token, 33); used = 0;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.arm.active && git_ui.lease.active && strstr(response, "\"granted\":true"));
        ZeroMemory(&request_fixture, sizeof(request_fixture)); request_fixture.kind = DLUMA_GIT_LEASE_RELEASE;
        request_fixture.has_lease = 1; memcpy(request_fixture.lease_id, git_ui.lease.token, 33); used = 0;
        CHECK(request("gitLeaseRelease", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.lease.active && !git_ui.guard_epoch && strstr(response, "\"released\":true"));
        git_ui.executable = NULL;
        git_ui.session = git_ui.guard_registered = 1; git_ui.guard_epoch = 7;
        git_ui.closing = git_ui.aborting = 0; git_ui.coverage_generation = 1;
        CHECK(git_bound_foreign(&git_ui.coverage_foreign));
        ZeroMemory(&git_ui.lease, sizeof(git_ui.lease)); ZeroMemory(&git_ui.arm, sizeof(git_ui.arm));
        CHECK(git_arm_begin(&git_ui.arm, "fedcba9876543210fedcba9876543210", 7, 42, "stage"));
        ((BYTE *)policy_editor)[0x1057] = 1;
        CHECK(!git_promote_project_lease(1, "stage", "0123456789abcdef0123456789abcdef", 0, 1));
        CHECK(git_ui.arm.active && !git_ui.lease.active);
        ((BYTE *)policy_editor)[0x1057] = 0;
        *(void **)((BYTE *)policy_editor + 0x24B0) = policy_table;
        CHECK(!git_promote_project_lease(1, "stage", "0123456789abcdef0123456789abcdef", 0, 1));
        CHECK(git_ui.arm.active && !git_ui.lease.active);
        *(void **)((BYTE *)policy_editor + 0x24B0) = NULL;
        CHECK(git_promote_project_lease(1, "stage", "0123456789abcdef0123456789abcdef", 0, 1));
        CHECK(!git_ui.arm.active && git_ui.lease.active && git_ui.policy_editor == policy_editor);
        ZeroMemory(&git_ui.lease, sizeof(git_ui.lease)); git_ui.policy_editor = NULL;
        CloseHandle(git_ui.project); git_ui.project = NULL;
        for (k = 0; k < 2; ++k) {
            ZeroMemory(&git_ui.lease, sizeof(git_ui.lease));
            git_ui.project = selected_project(git_ui.root, GIT_PATH_CAP); CHECK(git_ui.project);
            git_ui.session = git_ui.guard_registered = 1; git_ui.guard_epoch = 7; git_ui.coverage_generation = 1; git_ui.closing = git_ui.aborting = 0;
            job_snapshot_ok = 1; CHECK(git_bound_foreign(&git_ui.coverage_foreign));
            coverage_stub = 0; CHECK(!git_guard_authority_valid());
            coverage_stub = 1; CHECK(git_guard_authority_valid());
            ((BYTE *)policy_editor)[0x1057] = 1;
            CHECK(!git_guard_authority_valid());
            ((BYTE *)policy_editor)[0x1057] = 0;
            *(void **)((BYTE *)policy_editor + 0x24B0) = policy_table;
            CHECK(!git_guard_authority_valid());
            *(void **)((BYTE *)policy_editor + 0x24B0) = NULL;
            CHECK(git_guard_authority_valid());
            CHECK(git_lease_acquire(&git_ui.lease, 1, 1, 1, 42, "stage", "0123456789abcdef0123456789abcdef", 0, 1) == GIT_LEASE_OK);
            git_ui.policy_editor = policy_editor;
            CHECK(git_guard_authority_valid());
            policy_global = manager_words;
            CHECK(!git_guard_authority_valid());
            policy_global = policy_editor;
            CHECK(git_guard_authority_valid());
            ++binding_generation_stub; CHECK(!git_guard_authority_valid()); --binding_generation_stub;
            ++binding_creation_stub; CHECK(!git_guard_authority_valid()); --binding_creation_stub;
            binding_stub = 0; CHECK(!git_guard_authority_valid()); binding_stub = 1;
            CHECK(git_guard_authority_valid());
            ZeroMemory(&request_fixture, sizeof(request_fixture)); request_fixture.kind = DLUMA_GIT_LEASE_RELEASE;
            request_fixture.has_lease = 1; memcpy(request_fixture.lease_id, "0123456789abcdef0123456789abcdef", 33);
            job_snapshot_ok = (int)k; disarm_allowed = 0; session_ready = 0; aborts = disarms = unsubscribes = 0;
            CHECK(request("gitLeaseRelease", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
            CHECK(strstr(response, "\"requiresRecovery\":true") != NULL);
            CHECK(git_ui.closing && git_ui.lease.active && git_ui.lease.recovery && aborts == 1 && git_ui.guard_registered && git_ui.guard_epoch == 7);
            result = -1; CHECK(on_window(window, DLUMA_EDITOR_UI_BEFORE, WM_TIMER, GIT_TIMER, 0, &result, NULL));
            CHECK(git_ui.guard_registered && git_ui.guard_epoch == 7 && !unsubscribes);
            session_ready = disarm_allowed = 1;
            CHECK(on_window(window, DLUMA_EDITOR_UI_BEFORE, WM_TIMER, GIT_TIMER, 0, &result, NULL));
            CHECK(!git_ui.guard_registered && !git_ui.guard_epoch && !git_ui.lease.active && git_ui.lease.recovery && unsubscribes == 1);
        }
        git_ui.project = selected_project(git_ui.root, GIT_PATH_CAP); CHECK(git_ui.project);
        git_ui.session = git_ui.guard_registered = 1; git_ui.guard_epoch = 7;
        git_ui.closing = git_ui.aborting = 0; git_ui.coverage_generation = 1; git_ui.policy_editor = policy_editor;
        job_snapshot_ok = 1; CHECK(git_bound_foreign(&git_ui.coverage_foreign));
        ZeroMemory(&git_ui.lease, sizeof(git_ui.lease));
        CHECK(git_lease_acquire(&git_ui.lease, 1, 1, 1, 42, "stage", "0123456789abcdef0123456789abcdef", 0, 1) == GIT_LEASE_OK);
        ((BYTE *)policy_editor)[0x1057] = 1; session_ready = 0; aborts = 0;
        CHECK(on_window(window, DLUMA_EDITOR_UI_BEFORE, WM_TIMER, GIT_TIMER, 0, &result, NULL));
        CHECK(aborts == 1 && git_ui.closing && git_ui.lease.active && git_ui.lease.recovery && git_ui.guard_epoch == 7);
        ((BYTE *)policy_editor)[0x1057] = 0; session_ready = 1; disarm_allowed = 1;
        CHECK(close_session() && !git_ui.lease.active && git_ui.lease.recovery && !git_ui.policy_editor);
        git_ui.project = selected_project(git_ui.root, GIT_PATH_CAP); CHECK(git_ui.project);
        git_ui.session = git_ui.guard_registered = 1; git_ui.closing = git_ui.aborting = 0;
        ZeroMemory(&request_fixture, sizeof(request_fixture)); request_fixture.kind = DLUMA_GIT_STATE;
        request_fixture.project_generation = 42;
        CHECK(WideCharToMultiByte(CP_UTF8, 0, git_ui.root, -1, request_fixture.project_root,
                                  sizeof(request_fixture.project_root), NULL, NULL));
        git_ui.guard_diagnostics = 1; last_readiness_mask = 0xffffffffU; used = 0;
        CHECK(request("gitState", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(strstr(response, "\"mutationsAvailable\":false") && strstr(response, "\"localWritesOnly\":false") &&
              strstr(response, "Git cleanup needs attention"));
        CHECK(last_readiness_mask != 0xffffffffU && (last_readiness_mask & 2U));
        CHECK(close_session());
        git_diagnostic_close();
        ZeroMemory(&git_ui.lease, sizeof(git_ui.lease)); git_ui.closing = git_ui.aborting = 0;
        git_ui.project = selected_project(git_ui.root, GIT_PATH_CAP); CHECK(git_ui.project);
        git_ui.session = git_ui.guard_registered = 1; git_ui.guard_epoch = 0;
        CHECK(git_ordinary_local_qualified() && git_local_operation_allowed("createBranch"));
        CHECK(!git_local_operation_allowed("initialize") && !git_local_operation_allowed("switchBranch") &&
              !git_local_operation_allowed("pull") && !git_local_operation_allowed("push"));
        { HANDLE redirect = CreateFileW(redirect_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
          CHECK(redirect != INVALID_HANDLE_VALUE); CloseHandle(redirect); }
        CHECK(git_mutations_qualified()); CHECK(DeleteFileW(redirect_path));
        ZeroMemory(&request_fixture, sizeof(request_fixture)); request_fixture.kind = DLUMA_GIT_STATE;
        request_fixture.project_generation = 42;
        CHECK(WideCharToMultiByte(CP_UTF8, 0, git_ui.root, -1, request_fixture.project_root,
                                  sizeof(request_fixture.project_root), NULL, NULL));
        used = 0; CHECK(request("gitState", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(strstr(response, "\"mutationsAvailable\":true") && strstr(response, "\"branchCreateAvailable\":true") &&
              strstr(response, "\"diagnosticFixtureOnly\":false") && strstr(response, "\"worktreeAvailable\":false"));
        ((BYTE *)policy_editor)[0x1057] = 1;
        used = 0; CHECK(request("gitState", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(strstr(response, "\"mutationsAvailable\":false") && strstr(response, "Turn off Source control enabled"));
        ((BYTE *)policy_editor)[0x1057] = 0;
        *(void **)((BYTE *)policy_editor + 0x24B0) = policy_table;
        CHECK(!git_ordinary_local_qualified()); *(void **)((BYTE *)policy_editor + 0x24B0) = NULL;
        profile_stub = 0; CHECK(!git_ordinary_local_qualified()); profile_stub = 1;
        request_fixture.kind = DLUMA_GIT_PROJECT_ACQUIRE; lstrcpyA(request_fixture.operation, "createBranch");
        git_ui.executable = companion_event; job_snapshot_ok = 1; used = 0;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(git_ui.arm.active && !git_ui.lease.active && strstr(response, "\"code\":\"pending\""));
        request_fixture.has_arm = 1; memcpy(request_fixture.arm_id, git_ui.arm.token, 33); used = 0;
        CHECK(request("gitProjectAcquire", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.arm.active && git_ui.lease.active && strstr(response, "\"granted\":true"));
        ZeroMemory(&request_fixture, sizeof(request_fixture)); request_fixture.kind = DLUMA_GIT_LEASE_RELEASE;
        request_fixture.has_lease = 1; memcpy(request_fixture.lease_id, git_ui.lease.token, 33); used = 0;
        CHECK(request("gitLeaseRelease", "{}", 2, response, sizeof(response), &used, NULL) == DLUMA_EDITOR_GIT_REQUEST_OK);
        CHECK(!git_ui.lease.active && !git_ui.guard_epoch && strstr(response, "\"released\":true"));
        git_ui.executable = NULL; CHECK(close_session());
        CHECK(DeleteFileW(head_path)); CHECK(RemoveDirectoryW(objects_dir));
        CHECK(RemoveDirectoryW(refs_dir)); CHECK(RemoveDirectoryW(metadata_dir)); CHECK(DeleteFileW(diagnostic_ini));
        CHECK(CloseHandle(companion_event)); companion_event = NULL;
        CHECK(DestroyWindow(window)); fixture_window = NULL; CHECK(RemoveDirectoryW(fixture_dir)); fixture_cleanup[0] = 0;
        profile_stub = authority_stub = 0; git_ui.main = main_window;
    }
    CHECK(GetTempPathW(MAX_PATH, temp_dir) && GetTempFileNameW(temp_dir, L"dgt", 0, ini));
    CHECK(WritePrivateProfileStringW(L"loader", L"developer_mode", L"0", ini));
    CHECK(WritePrivateProfileStringW(L"loader", L"editor_git_guard_diagnostics", L"1", ini));
    CHECK(!dluma_editor_source_control_host_start(ini) && !git_ui.guard_diagnostics);
    CHECK(WritePrivateProfileStringW(L"loader", L"developer_mode", L"1", ini));
    CHECK(!dluma_editor_source_control_host_start(ini) && git_ui.guard_diagnostics);
    CHECK(DeleteFileW(ini));
    puts("Editor Git host guard contract passed.");
    return 0;
}
int dluma_editor_is_main_window(HWND w, DWORD p) { (void)w; (void)p; return 0; }
int dluma_editor_source_control_parse_request(const char *t,const char *p,size_t n,DlumaEditorGitParsedRequest *r) { (void)t;(void)p;(void)n; *r = request_fixture; return 1; }
int dluma_editor_source_control_verify_child(HANDLE c,HANDLE j,HANDLE e,DWORD p,uint64_t t,uint64_t h,HANDLE *o) { (void)c;(void)j;(void)e;(void)p;(void)t;(void)h;(void)o; return 0; }
int dluma_editor_ui_subscribe(unsigned int o,DlumaEditorUiListener l,void *c) { (void)o;(void)l;(void)c; return 0; }
int dluma_editor_ui_unsubscribe(unsigned int o) { (void)o; return 1; }
int dluma_editor_ui_guard_subscribe(DlumaEditorUiListener l,void *c) { (void)l;(void)c; return 0; }
int dluma_editor_ui_guard_unsubscribe(void) { ++unsubscribes; return 1; }
int dluma_editor_ui_guard_ready(DlumaEditorUiListener l,void *c) { (void)l;(void)c; return authority_stub; }
uint64_t dluma_editor_ui_guard_arm(DlumaEditorUiListener l,void *c) { (void)l;(void)c; return 7; }
int dluma_editor_ui_guard_armed(DlumaEditorUiListener l,void *c) { (void)l;(void)c; return armed_stub || authority_stub; }
int dluma_editor_ui_guard_arm_ready(DlumaEditorUiListener l,void *c,uint64_t e) { (void)l;(void)c;(void)e; return authority_stub; }
int dluma_editor_ui_guard_coverage_current(DlumaEditorUiListener l,void *c,HWND w,uint64_t g) { (void)l;(void)c;(void)w;(void)g; return coverage_stub; }
int dluma_editor_ui_guard_coverage_capture(DlumaEditorUiListener l,void *c,HWND w,uint64_t *g) { (void)l;(void)c;(void)w; *g = 1; return DLUMA_EDITOR_UI_COVERAGE_READY; }
int dluma_editor_ui_guard_coverage_current_foreign(DlumaEditorUiListener l,void *c,HWND w,const DlumaEditorUiCoverageForeign *f,uint64_t g) { (void)l;(void)c;(void)w;(void)f;(void)g; return coverage_stub; }
int dluma_editor_ui_guard_coverage_capture_foreign(DlumaEditorUiListener l,void *c,HWND w,const DlumaEditorUiCoverageForeign *f,uint64_t *g,DlumaEditorUiCoverageDetail *d) { (void)l;(void)c;(void)w;(void)f; *g = 1; if (d) { ZeroMemory(d, sizeof(*d)); d->reason = 14U; } return coverage_capture_stub; }

int dluma_editor_ui_guard_disarm(DlumaEditorUiListener l,void *c,uint64_t e) { (void)l;(void)c;(void)e; ++disarms; return disarm_allowed; }
int dluma_editor_ui_request_enroll(HWND w) { (void)w; return 0; }
int dluma_editor_panel_host_create_view(HWND w,DlumaEditorPanelNotify n,void *c,DlumaEditorPanelRef *p) { (void)w;(void)n;(void)c;(void)p; return 0; }
int dluma_editor_panel_host_layout(const DlumaEditorPanelRef *p) { (void)p; return 0; }
uint64_t dluma_editor_panel_host_binding_generation(const DlumaEditorPanelRef *p) { (void)p; return 0; }
int dluma_editor_panel_host_close(const DlumaEditorPanelRef *p) { (void)p; return 1; }
int dluma_editor_git_session_open(const DlumaEditorGitSessionSpecV1 *p) { (void)p; return 0; }
int dluma_editor_git_session_dispatch(HWND w,UINT m,WPARAM p,LPARAM l,LRESULT *r) { (void)w;(void)m;(void)p;(void)l;(void)r; return 0; }
int dluma_editor_git_session_detach(void) { return 1; }
int dluma_editor_git_session_close(void) { return session_ready; }
int dluma_editor_git_session_drained(void) { return session_ready; }
int dluma_editor_git_session_job_snapshot(DlumaEditorGitJobSnapshotV1 *p) { if (!job_snapshot_ok) return 0; p->companion_process = companion_event; p->companion_pid = 4242; p->active_processes = (unsigned int)job_active_stub; return 1; }
int dluma_editor_panel_host_bound_child(const DlumaEditorPanelRef *p,DWORD pid,HANDLE process,HWND *child,uint64_t *generation,uint64_t *creation) {
    (void)p;(void)pid;(void)process; if (!binding_stub) return 0; *child = (HWND)(UINT_PTR)7;
    *generation = binding_generation_stub; *creation = binding_creation_stub; return 1;
}
int dluma_editor_theme_profile_admit(unsigned int p) { (void)p; return profile_stub; }
void *dluma_editor_theme_profile_address(unsigned int p,uintptr_t r,size_t n) {
    (void)p;(void)n; if (!profile_stub) return NULL;
    if (r == 0x1B70238U) return &policy_global;
    if (r == 0x1504F98U) return policy_table;
    return manager_words;
}
int dluma_editor_dock_show(DlumaEditorDockRef *d,HWND w,unsigned int i,const char *t,const void *c,int v) { (void)d;(void)w;(void)i;(void)t;(void)c;(void)v; return 0; }
