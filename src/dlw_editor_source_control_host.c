#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "core/dlw_json_reader.h"
#include "dlw_editor_source_control_ui.h"

static int git_string_compare(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

static int git_nullable_string(DlumaJsonReader *reader, char *buffer,
    unsigned int capacity, int *present)
{
    dluma_json_space(reader);
    if ((size_t)(reader->end - reader->cursor) >= 4U &&
        memcmp(reader->cursor, "null", 4U) == 0) {
        reader->cursor += 4; *present = 0; buffer[0] = 0; return 1;
    }
    *present = 1;
    return dluma_json_string(reader, buffer, capacity);
}

static int git_absolute_path(const char *path)
{
    int length;
    unsigned char a, b;
    if (!path || !path[0] || !path[1] || !path[2]) return 0;
    a = (unsigned char)path[0]; b = (unsigned char)path[1];
    if (!(((a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z')) &&
          b == ':' && (path[2] == '\\' || path[2] == '/')) &&
        !((a == '\\' || a == '/') && (b == '\\' || b == '/') && path[2] != '\\' && path[2] != '/'))
        return 0;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    return length > 1 && length <= 32769;
}

static int git_lease_id(const char *id)
{
    size_t i;
    if (strlen(id) != DLUMA_EDITOR_GIT_LEASE_HEX_LENGTH) return 0;
    for (i = 0; i < DLUMA_EDITOR_GIT_LEASE_HEX_LENGTH; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

static int git_operation(const char *name)
{
    static const char *const names[] = {
        "initialize", "stage", "unstage", "commit", "createBranch", "switchBranch",
        "fetch", "pull", "publishBranch", "push", "addRemote", "clone"
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!git_string_compare(name, names[i])) return 1;
    return 0;
}

int dluma_editor_source_control_parse_request(const char *type, const char *payload,
    size_t size, DlumaEditorGitParsedRequest *out)
{
    DlumaJsonReader reader;
    unsigned int fields = 0, required = 0, allowed = 0;
    int first = 1, next;
    uint64_t number;
    if (!type || !payload || !out || !size || size > DLUMA_EDITOR_GIT_MAX_PAYLOAD) return 0;
    ZeroMemory(out, sizeof(*out));
    if (!git_string_compare(type, "gitState")) {
        out->kind = DLUMA_GIT_STATE; required = 3U; allowed = 7U;
    } else if (!git_string_compare(type, "gitProjectAcquire")) {
        out->kind = DLUMA_GIT_PROJECT_ACQUIRE; required = 11U; allowed = 1051U;
    } else if (!git_string_compare(type, "gitProjectCancel")) {
        out->kind = DLUMA_GIT_PROJECT_CANCEL; required = allowed = 1027U;
    } else if (!git_string_compare(type, "gitWorktreeAcquire")) {
        out->kind = DLUMA_GIT_WORKTREE_ACQUIRE; required = allowed = 43U;
    } else if (!git_string_compare(type, "gitChildStarted")) {
        out->kind = DLUMA_GIT_CHILD_STARTED; required = 960U; allowed = 992U;
    } else if (!git_string_compare(type, "gitLeaseRelease")) {
        out->kind = DLUMA_GIT_LEASE_RELEASE; required = allowed = 32U;
    } else return 0;
    dluma_json_reader_initialize(&reader, payload, (unsigned int)size);
    if (!dluma_json_object_begin(&reader)) return 0;
    while ((next = dluma_json_object_next(&reader, &first)) > 0) {
        char key[32]; unsigned int bit;
        if (!dluma_json_string(&reader, key, sizeof(key)) || !dluma_json_take(&reader, ':')) return 0;
        if (!git_string_compare(key, "projectRoot")) bit = 1U;
        else if (!git_string_compare(key, "projectGeneration")) bit = 2U;
        else if (!git_string_compare(key, "gitExecutable")) bit = 4U;
        else if (!git_string_compare(key, "operation")) bit = 8U;
        else if (!git_string_compare(key, "destination")) bit = 16U;
        else if (!git_string_compare(key, out->kind == DLUMA_GIT_WORKTREE_ACQUIRE ? "projectLeaseId" : "leaseId")) bit = 32U;
        else if (!git_string_compare(key, "processId")) bit = 64U;
        else if (!git_string_compare(key, "creationTime")) bit = 128U;
        else if (!git_string_compare(key, "processHandle")) bit = 256U;
        else if (!git_string_compare(key, "mutating")) bit = 512U;
        else if (!git_string_compare(key, "armId")) bit = 1024U;
        else return 0;
        if (!(allowed & bit) || (fields & bit)) return 0;
        fields |= bit;
        switch (bit) {
        case 1U: if (!dluma_json_string(&reader, out->project_root, sizeof(out->project_root))) return 0; break;
        case 2U: if (!dluma_json_u64(&reader, &out->project_generation)) return 0; break;
        case 4U: if (!git_nullable_string(&reader, out->git_executable, sizeof(out->git_executable), &out->has_executable)) return 0; break;
        case 8U: if (!dluma_json_string(&reader, out->operation, sizeof(out->operation))) return 0; break;
        case 16U: if (!git_nullable_string(&reader, out->destination, sizeof(out->destination), &out->has_destination)) return 0; break;
        case 32U: if (!git_nullable_string(&reader, out->lease_id, sizeof(out->lease_id), &out->has_lease)) return 0; break;
        case 64U:
            if (!dluma_json_u64(&reader, &number) || !number || number > INT_MAX) return 0;
            out->process_id = (DWORD)number; break;
        case 128U: if (!dluma_json_u64(&reader, &out->creation_time)) return 0; break;
        case 256U: if (!dluma_json_u64(&reader, &out->process_handle)) return 0; break;
        case 512U: if (!dluma_json_boolean(&reader, &out->mutating)) return 0; break;
        case 1024U: if (!git_nullable_string(&reader, out->arm_id, sizeof(out->arm_id), &out->has_arm)) return 0; break;
        default: return 0;
        }
    }
    if (next != 0 || !dluma_json_object_end(&reader) || !dluma_json_finished(&reader) ||
        (fields & required) != required) return 0;
    if ((fields & 1U) && (!git_absolute_path(out->project_root) || !out->project_generation)) return 0;
    if (out->has_executable && !git_absolute_path(out->git_executable)) return 0;
    if (out->kind == DLUMA_GIT_PROJECT_ACQUIRE &&
        (!git_operation(out->operation) ||
         (!git_string_compare(out->operation, "clone") ? !out->has_destination || !git_absolute_path(out->destination) : out->has_destination))) return 0;
    if (out->has_arm && (!git_lease_id(out->arm_id) ||
        (out->kind == DLUMA_GIT_PROJECT_ACQUIRE && !git_string_compare(out->operation, "clone")))) return 0;
    if (out->kind == DLUMA_GIT_PROJECT_CANCEL && !out->has_arm) return 0;
    if (out->kind == DLUMA_GIT_WORKTREE_ACQUIRE &&
        (git_string_compare(out->operation, "pull") && git_string_compare(out->operation, "switchBranch"))) return 0;
    if ((out->kind == DLUMA_GIT_WORKTREE_ACQUIRE || out->kind == DLUMA_GIT_LEASE_RELEASE) && !out->has_lease) return 0;
    if (out->has_lease && !git_lease_id(out->lease_id)) return 0;
    if (out->kind == DLUMA_GIT_CHILD_STARTED &&
        (!out->creation_time || !out->process_handle || out->process_handle > INT64_MAX ||
         (out->mutating && !out->has_lease))) return 0;
    return 1;
}

int dluma_editor_source_control_verify_child(HANDLE companion, HANDLE job,
    HANDLE expected_executable, DWORD process_id, uint64_t creation_time,
    uint64_t companion_handle, HANDLE *verified_process)
{
    HANDLE process = NULL, image = INVALID_HANDLE_VALUE;
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER observed;
    BY_HANDLE_FILE_INFORMATION expected, actual;
    WCHAR path[32768]; DWORD length = (DWORD)(sizeof(path) / sizeof(path[0]));
    BOOL member = FALSE; int valid = 0;
    if (!verified_process) return 0;
    *verified_process = NULL;
    if (!companion || !job || !expected_executable || expected_executable == INVALID_HANDLE_VALUE ||
        !process_id || !creation_time || !companion_handle || companion_handle > INT64_MAX) return 0;
    if (!DuplicateHandle(companion, (HANDLE)(ULONG_PTR)companion_handle, GetCurrentProcess(), &process,
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) return 0;
    if (GetProcessId(process) != process_id || !GetProcessTimes(process, &created, &exited, &kernel, &user)) goto done;
    observed.LowPart = created.dwLowDateTime; observed.HighPart = created.dwHighDateTime;
    if (observed.QuadPart != creation_time || !IsProcessInJob(process, job, &member) || !member ||
        !QueryFullProcessImageNameW(process, 0, path, &length) || !length ||
        GetFileType(expected_executable) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(expected_executable, &expected) ||
        (expected.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) goto done;
    image = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (image == INVALID_HANDLE_VALUE || GetFileType(image) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(image, &actual) || (actual.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        expected.dwVolumeSerialNumber != actual.dwVolumeSerialNumber ||
        expected.nFileIndexHigh != actual.nFileIndexHigh || expected.nFileIndexLow != actual.nFileIndexLow) goto done;
    *verified_process = process; process = NULL; valid = 1;
done:
    if (image != INVALID_HANDLE_VALUE) CloseHandle(image);
    if (process) CloseHandle(process);
    return valid;
}
