#include "dlw_mod_package_runtime.h"

#include "../dlw_path_security.h"
#include "../dlw_sha256_internal.h"

typedef struct DlumaPackageRead {
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    WCHAR final_path[DLUMA_MAX_WPATH];
    unsigned char* bytes;
    unsigned int size;
    char sha256[DLUMA_MOD_SHA256_CAPACITY];
} DlumaPackageRead;

static void dluma_package_error(
    DlumaModPackageSnapshot* output,
    const char* message)
{
    if (!output) return;
    dluma_str_copy(
        output->diagnostic,
        DLUMA_ARRAY_COUNT(output->diagnostic),
        message);
}

static void dluma_artifact_error(
    DlumaModArtifactSnapshot* output,
    const char* message)
{
    if (!output) return;
    dluma_str_copy(
        output->diagnostic,
        DLUMA_ARRAY_COUNT(output->diagnostic),
        message);
}

static int dluma_package_identity_equal(
    const BY_HANDLE_FILE_INFORMATION* left,
    const BY_HANDLE_FILE_INFORMATION* right)
{
    return left && right &&
        left->dwVolumeSerialNumber == right->dwVolumeSerialNumber &&
        left->nFileIndexHigh == right->nFileIndexHigh &&
        left->nFileIndexLow == right->nFileIndexLow &&
        left->nFileSizeHigh == right->nFileSizeHigh &&
        left->nFileSizeLow == right->nFileSizeLow &&
        left->ftLastWriteTime.dwHighDateTime ==
            right->ftLastWriteTime.dwHighDateTime &&
        left->ftLastWriteTime.dwLowDateTime ==
            right->ftLastWriteTime.dwLowDateTime &&
        left->nNumberOfLinks == right->nNumberOfLinks;
}

static int dluma_package_path_under(
    const WCHAR* path,
    const WCHAR* root)
{
    SIZE_T index;
    SIZE_T root_length;
    if (!path || !root) return 0;
    root_length = wcslen(root);
    if (!root_length) return 0;
    for (index = 0; index < root_length; ++index)
        if (!path[index] ||
            dluma_wascii_lower(path[index]) !=
                dluma_wascii_lower(root[index]))
            return 0;
    return !path[root_length] ||
        path[root_length] == L'\\' || path[root_length] == L'/';
}

static void dluma_package_read_close(DlumaPackageRead* read)
{
    if (!read) return;
    if (read->file && read->file != INVALID_HANDLE_VALUE)
        CloseHandle(read->file);
    dluma_free(read->bytes);
    memset(read, 0, sizeof(*read));
    read->file = INVALID_HANDLE_VALUE;
}

static int dluma_package_open_regular(
    const WCHAR* path,
    uint64_t maximum,
    int read_bytes,
    DlumaPackageRead* output)
{
    LARGE_INTEGER zero;
    LARGE_INTEGER size;
    DWORD read_size = 0;
    BY_HANDLE_FILE_INFORMATION after;
    if (!path || !output || !maximum) return 0;
    memset(output, 0, sizeof(*output));
    output->file = INVALID_HANDLE_VALUE;
    output->file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (output->file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(output->file, &output->identity) ||
        (output->identity.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        output->identity.nNumberOfLinks != 1U ||
        !GetFileSizeEx(output->file, &size) ||
        size.QuadPart < 0 ||
        (uint64_t)size.QuadPart > maximum ||
        (uint64_t)size.QuadPart > 0xFFFFFFFFULL ||
        !dluma_get_final_path_checked(
            output->file, output->final_path,
            DLUMA_ARRAY_COUNT(output->final_path))) {
        dluma_package_read_close(output);
        return 0;
    }
    output->size = (unsigned int)size.QuadPart;
    if (read_bytes) {
        if (!output->size) {
            dluma_package_read_close(output);
            return 0;
        }
        output->bytes = (unsigned char*)dluma_alloc(output->size);
        zero.QuadPart = 0;
        if (!output->bytes ||
            !SetFilePointerEx(output->file, zero, 0, FILE_BEGIN) ||
            !ReadFile(
                output->file, output->bytes, output->size,
                &read_size, 0) ||
            read_size != output->size) {
            dluma_package_read_close(output);
            return 0;
        }
    }
    if (!dluma_sha256_handle_hex(
            output->file, maximum, output->sha256) ||
        !GetFileInformationByHandle(output->file, &after) ||
        !dluma_package_identity_equal(&output->identity, &after)) {
        dluma_package_read_close(output);
        return 0;
    }
    return 1;
}

static int dluma_package_open_root(
    const WCHAR* root,
    WCHAR* final_root,
    unsigned int final_root_capacity)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION identity;
    DWORD attributes;
    if (!root || !root[0] || !final_root || !final_root_capacity)
        return 0;
    attributes = GetFileAttributesW(root);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    directory = CreateFileW(
        root, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (directory == INVALID_HANDLE_VALUE) return 0;
    memset(&identity, 0, sizeof(identity));
    if (!GetFileInformationByHandle(directory, &identity) ||
        !(identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            directory, final_root, final_root_capacity)) {
        CloseHandle(directory);
        return 0;
    }
    CloseHandle(directory);
    return 1;
}

static int dluma_package_layout_matches(
    const WCHAR* final_root,
    const DlumaModManifest* manifest)
{
    WCHAR id[128];
    WCHAR map[128];
    WCHAR expected[512];
    const WCHAR* language;
    const WCHAR* scope;
    SIZE_T path_length;
    SIZE_T expected_length;
    if (!final_root || !manifest ||
        !dluma_utf8_to_wide(
            manifest->id, id, DLUMA_ARRAY_COUNT(id)))
        return 0;
    language =
        manifest->language == DLUMA_MOD_LANGUAGE_LUA
            ? L"data\\scripts\\lua" :
        manifest->language == DLUMA_MOD_LANGUAGE_CSHARP
            ? L"mods\\csharp" :
        manifest->language == DLUMA_MOD_LANGUAGE_NATIVE
            ? L"mods\\native" : 0;
    scope =
        manifest->scope == DLUMA_SCOPE_GLOBAL ? L"global" :
        manifest->scope == DLUMA_SCOPE_PROJECT ? L"project" : 0;
    expected[0] = 0;
    if (!language) return 0;
    dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), language);
    dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), L"\\");
    if (manifest->scope == DLUMA_SCOPE_MAP) {
        if (!dluma_utf8_to_wide(
                manifest->map, map, DLUMA_ARRAY_COUNT(map)))
            return 0;
        dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), L"maps\\");
        dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), map);
        dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), L"\\");
    } else {
        dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), scope);
        dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), L"\\");
    }
    dluma_wstr_append(expected, DLUMA_ARRAY_COUNT(expected), id);
    path_length = wcslen(final_root);
    expected_length = wcslen(expected);
    if (path_length < expected_length ||
        dluma_wstricmp(
            final_root + path_length - expected_length,
            expected) != 0)
        return 0;
    return path_length == expected_length ||
        final_root[path_length - expected_length - 1U] == L'\\' ||
        final_root[path_length - expected_length - 1U] == L'/';
}

static const DlumaModLockedArtifact* dluma_package_find_artifact(
    const DlumaModLock* lock,
    const char* path)
{
    unsigned int index;
    if (!lock || !path) return 0;
    for (index = 0; index < lock->artifact_count; ++index)
        if (dluma_stricmp(lock->artifacts[index].path, path) == 0)
            return &lock->artifacts[index];
    return 0;
}

static int dluma_package_artifact_path(
    const WCHAR* root,
    const char* relative,
    WCHAR* output,
    unsigned int output_capacity)
{
    WCHAR wide[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    unsigned int index;
    if (!root || !relative || !output ||
        !dluma_utf8_to_wide(relative, wide, DLUMA_ARRAY_COUNT(wide)))
        return 0;
    for (index = 0; wide[index]; ++index)
        if (wide[index] == L'/') wide[index] = L'\\';
    dluma_path_join(output, output_capacity, root, wide);
    return output[0] != 0;
}

static int dluma_package_verify_artifacts(
    const WCHAR* final_root,
    const DlumaModLock* lock,
    const DlumaModManifest* manifest,
    const char* manifest_sha256,
    DlumaModPackageSnapshot* output)
{
    unsigned int index;
    unsigned int manifest_count = 0;
    unsigned int entry_count = 0;
    for (index = 0; index < lock->artifact_count; ++index) {
        const DlumaModLockedArtifact* artifact = &lock->artifacts[index];
        WCHAR path[DLUMA_MAX_WPATH];
        DlumaPackageRead read;
        if (!dluma_package_artifact_path(
                final_root, artifact->path, path,
                DLUMA_ARRAY_COUNT(path)) ||
            !dluma_package_open_regular(
                path, 1073741824ULL, 0, &read))
            return 0;
        if (!dluma_package_path_under(read.final_path, final_root) ||
            read.size != artifact->size ||
            dluma_stricmp(read.sha256, artifact->sha256) != 0) {
            dluma_package_read_close(&read);
            return 0;
        }
        if (artifact->kind == DLUMA_MOD_ARTIFACT_MANIFEST) {
            ++manifest_count;
            if (dluma_stricmp(artifact->path, "mod.dluma.json") != 0 ||
                dluma_stricmp(artifact->sha256, manifest_sha256) != 0) {
                dluma_package_read_close(&read);
                return 0;
            }
        }
        if (artifact->kind == DLUMA_MOD_ARTIFACT_ENTRY) {
            ++entry_count;
            if (dluma_stricmp(artifact->path, manifest->entry) != 0) {
                dluma_package_read_close(&read);
                return 0;
            }
            dluma_wstr_copy(
                output->entry_path,
                DLUMA_ARRAY_COUNT(output->entry_path),
                read.final_path);
            dluma_str_copy(
                output->entry_sha256,
                DLUMA_ARRAY_COUNT(output->entry_sha256),
                read.sha256);
            output->entry_size = read.size;
            output->entry_volume_serial =
                read.identity.dwVolumeSerialNumber;
            output->entry_file_index_high =
                read.identity.nFileIndexHigh;
            output->entry_file_index_low =
                read.identity.nFileIndexLow;
        }
        dluma_package_read_close(&read);
    }
    return manifest_count == 1U && entry_count == 1U;
}

static int dluma_package_relative_from_root(
    const WCHAR* final_root,
    const WCHAR* final_path,
    char* output,
    unsigned int output_capacity)
{
    const WCHAR* relative;
    if (!dluma_package_path_under(final_path, final_root)) return 0;
    relative = final_path + wcslen(final_root);
    while (*relative == L'\\' || *relative == L'/') ++relative;
    if (!*relative ||
        !dluma_wide_to_utf8(relative, output, output_capacity))
        return 0;
    {
        unsigned int index;
        for (index = 0; output[index]; ++index)
            if (output[index] == '\\') output[index] = '/';
    }
    return 1;
}

static int dluma_package_verify_tree_recursive(
    const WCHAR* final_root,
    const WCHAR* directory,
    const DlumaModLock* lock,
    unsigned int depth,
    unsigned int* file_count)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!final_root || !directory || !lock || !file_count ||
        depth > DLUMA_MOD_PACKAGE_MAX_TREE_DEPTH)
        return 0;
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        WCHAR requested[DLUMA_MAX_WPATH];
        if ((data.cFileName[0] == L'.' &&
             (!data.cFileName[1] ||
              (data.cFileName[1] == L'.' && !data.cFileName[2]))) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                FindClose(find);
                return 0;
            }
            continue;
        }
        dluma_path_join(
            requested, DLUMA_ARRAY_COUNT(requested),
            directory, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!dluma_package_verify_tree_recursive(
                    final_root, requested, lock, depth + 1U,
                    file_count)) {
                FindClose(find);
                return 0;
            }
        } else {
            DlumaPackageRead read;
            char relative[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
            memset(&read, 0, sizeof(read));
            read.file = INVALID_HANDLE_VALUE;
            if (++*file_count > DLUMA_MOD_PACKAGE_MAX_TREE_FILES ||
                !dluma_package_open_regular(
                    requested, 1073741824ULL, 0, &read) ||
                !dluma_package_relative_from_root(
                    final_root, read.final_path, relative,
                    DLUMA_ARRAY_COUNT(relative))) {
                dluma_package_read_close(&read);
                FindClose(find);
                return 0;
            }
            dluma_package_read_close(&read);
            if (dluma_stricmp(relative, "mod.dluma.lock.json") != 0 &&
                !dluma_package_find_artifact(lock, relative)) {
                FindClose(find);
                return 0;
            }
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return 1;
}

DlumaStatus dluma_mod_package_discover(
    const WCHAR* package_root,
    DlumaModPackageSnapshot* output)
{
    DlumaPackageRead manifest_read;
    DlumaPackageRead lock_read;
    DlumaModLockedArtifact* artifacts = 0;
    DlumaModLock lock;
    DlumaModManifestDiagnostic diagnostic;
    WCHAR requested[DLUMA_MAX_WPATH];
    unsigned int file_count = 0;
    DlumaStatus status = DLUMA_STATUS_INVALID_ARGUMENT;
    if (!package_root || !output ||
        output->struct_size < sizeof(*output))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->struct_size = (unsigned int)sizeof(*output);
    output->struct_version = DLUMA_MOD_MANIFEST_VERSION;
    memset(&manifest_read, 0, sizeof(manifest_read));
    memset(&lock_read, 0, sizeof(lock_read));
    manifest_read.file = INVALID_HANDLE_VALUE;
    lock_read.file = INVALID_HANDLE_VALUE;
    if (!dluma_package_open_root(
            package_root, output->package_root,
            DLUMA_ARRAY_COUNT(output->package_root))) {
        dluma_package_error(output, "package root is missing, redirected, or unsafe");
        goto done;
    }
    dluma_path_join(
        requested, DLUMA_ARRAY_COUNT(requested),
        output->package_root, L"mod.dluma.json");
    if (!dluma_package_open_regular(
            requested, DLUMA_MOD_MANIFEST_MAX_BYTES, 1,
            &manifest_read) ||
        !dluma_package_path_under(
            manifest_read.final_path, output->package_root)) {
        dluma_package_error(output, "mod.dluma.json is missing or unsafe");
        goto done;
    }
    output->manifest.struct_size =
        (unsigned int)sizeof(output->manifest);
    status = dluma_mod_manifest_parse(
        manifest_read.bytes, manifest_read.size,
        &output->manifest, &diagnostic);
    if (status != DLUMA_STATUS_OK ||
        !dluma_package_layout_matches(
            output->package_root, &output->manifest)) {
        dluma_package_error(output, "manifest or folder-derived layout is invalid");
        status = DLUMA_STATUS_INVALID_ARGUMENT;
        goto done;
    }
    dluma_wstr_copy(
        output->manifest_path,
        DLUMA_ARRAY_COUNT(output->manifest_path),
        manifest_read.final_path);
    dluma_str_copy(
        output->manifest_sha256,
        DLUMA_ARRAY_COUNT(output->manifest_sha256),
        manifest_read.sha256);
    dluma_path_join(
        requested, DLUMA_ARRAY_COUNT(requested),
        output->package_root, L"mod.dluma.lock.json");
    if (!dluma_package_open_regular(
            requested, DLUMA_MOD_LOCK_MAX_BYTES, 1, &lock_read) ||
        !dluma_package_path_under(
            lock_read.final_path, output->package_root)) {
        dluma_package_error(output, "mod.dluma.lock.json is missing or unsafe");
        status = DLUMA_STATUS_NOT_FOUND;
        goto done;
    }
    artifacts = (DlumaModLockedArtifact*)dluma_alloc(
        (SIZE_T)DLUMA_MOD_LOCK_MAX_ARTIFACTS * sizeof(*artifacts));
    if (!artifacts) {
        dluma_package_error(output, "lock artifact allocation failed");
        status = DLUMA_STATUS_INTERNAL_ERROR;
        goto done;
    }
    memset(&lock, 0, sizeof(lock));
    lock.struct_size = (unsigned int)sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = DLUMA_MOD_LOCK_MAX_ARTIFACTS;
    status = dluma_mod_lock_parse(
        lock_read.bytes, lock_read.size, &lock, &diagnostic);
    if (status != DLUMA_STATUS_OK ||
        !dluma_mod_lock_catalog_compatible(&lock, &output->manifest) ||
        dluma_stricmp(lock.mod_id, output->manifest.id) != 0 ||
        dluma_stricmp(
            lock.manifest_sha256,
            output->manifest_sha256) != 0 ||
        !dluma_package_verify_artifacts(
            output->package_root, &lock, &output->manifest,
            output->manifest_sha256, output) ||
        !dluma_package_verify_tree_recursive(
            output->package_root, output->package_root,
            &lock, 0U, &file_count)) {
        dluma_package_error(output, "lock or exact artifact tree is invalid");
        status = status == DLUMA_STATUS_OK
            ? DLUMA_STATUS_CHANGED_SINCE_APPROVAL : status;
        goto done;
    }
    dluma_wstr_copy(
        output->lock_path,
        DLUMA_ARRAY_COUNT(output->lock_path),
        lock_read.final_path);
    dluma_str_copy(
        output->lock_sha256,
        DLUMA_ARRAY_COUNT(output->lock_sha256),
        lock_read.sha256);
    output->valid = 1U;
    output->diagnostic[0] = 0;
    status = DLUMA_STATUS_OK;
done:
    dluma_package_read_close(&manifest_read);
    dluma_package_read_close(&lock_read);
    dluma_free(artifacts);
    return status;
}

DlumaStatus dluma_mod_package_revalidate(
    const DlumaModPackageSnapshot* approved,
    DlumaModPackageSnapshot* current)
{
    DlumaStatus status;
    if (!approved || !approved->valid || !current ||
        current->struct_size < sizeof(*current))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_mod_package_discover(
        approved->package_root, current);
    if (status != DLUMA_STATUS_OK)
        return status;
    if (dluma_stricmp(
            approved->manifest_sha256,
            current->manifest_sha256) != 0 ||
        dluma_stricmp(
            approved->lock_sha256,
            current->lock_sha256) != 0 ||
        dluma_stricmp(
            approved->entry_sha256,
            current->entry_sha256) != 0 ||
        approved->entry_size != current->entry_size ||
        approved->entry_volume_serial !=
            current->entry_volume_serial ||
        approved->entry_file_index_high !=
            current->entry_file_index_high ||
        approved->entry_file_index_low !=
            current->entry_file_index_low ||
        dluma_stricmp(
            approved->manifest.id,
            current->manifest.id) != 0) {
        current->valid = 0;
        dluma_package_error(
            current, "package bytes or physical identity changed");
        return DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
    }
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_mod_package_read_artifact(
    const DlumaModPackageSnapshot* approved,
    const char* relative_path,
    unsigned int expected_kind,
    unsigned int maximum_bytes,
    DlumaModArtifactSnapshot* output)
{
    DlumaPackageRead lock_read;
    DlumaPackageRead artifact_read;
    DlumaModLockedArtifact* artifacts = 0;
    DlumaModLock lock;
    DlumaModManifestDiagnostic diagnostic;
    const DlumaModLockedArtifact* locked;
    WCHAR final_root[DLUMA_MAX_WPATH];
    WCHAR requested[DLUMA_MAX_WPATH];
    char normalized[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    DlumaStatus status = DLUMA_STATUS_INVALID_ARGUMENT;
    if (!approved || !approved->valid || !relative_path ||
        !maximum_bytes || !output ||
        output->struct_size < sizeof(*output))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    output->struct_size = (unsigned int)sizeof(*output);
    output->struct_version = DLUMA_MOD_MANIFEST_VERSION;
    memset(&lock_read, 0, sizeof(lock_read));
    memset(&artifact_read, 0, sizeof(artifact_read));
    lock_read.file = INVALID_HANDLE_VALUE;
    artifact_read.file = INVALID_HANDLE_VALUE;
    if (!dluma_mod_relative_path_validate_and_normalize(
            relative_path, normalized, DLUMA_ARRAY_COUNT(normalized))) {
        dluma_artifact_error(output, "artifact relative path is invalid");
        goto done;
    }
    if (!dluma_package_open_root(
            approved->package_root, final_root,
            DLUMA_ARRAY_COUNT(final_root)) ||
        dluma_wstricmp(final_root, approved->package_root) != 0) {
        dluma_artifact_error(output, "package root identity changed");
        status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
        goto done;
    }
    dluma_path_join(
        requested, DLUMA_ARRAY_COUNT(requested),
        final_root, L"mod.dluma.lock.json");
    if (!dluma_package_open_regular(
            requested, DLUMA_MOD_LOCK_MAX_BYTES, 1, &lock_read) ||
        !dluma_package_path_under(lock_read.final_path, final_root) ||
        dluma_stricmp(lock_read.sha256, approved->lock_sha256) != 0) {
        dluma_artifact_error(output, "approved lock bytes changed");
        status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
        goto done;
    }
    artifacts = (DlumaModLockedArtifact*)dluma_alloc(
        (SIZE_T)DLUMA_MOD_LOCK_MAX_ARTIFACTS * sizeof(*artifacts));
    if (!artifacts) {
        dluma_artifact_error(output, "lock artifact allocation failed");
        status = DLUMA_STATUS_INTERNAL_ERROR;
        goto done;
    }
    memset(&lock, 0, sizeof(lock));
    lock.struct_size = (unsigned int)sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = DLUMA_MOD_LOCK_MAX_ARTIFACTS;
    status = dluma_mod_lock_parse(
        lock_read.bytes, lock_read.size, &lock, &diagnostic);
    if (status != DLUMA_STATUS_OK ||
        !dluma_mod_lock_catalog_compatible(&lock, &approved->manifest) ||
        dluma_stricmp(lock.mod_id, approved->manifest.id) != 0 ||
        dluma_stricmp(
            lock.manifest_sha256,
            approved->manifest_sha256) != 0) {
        dluma_artifact_error(output, "approved lock is invalid");
        status = status == DLUMA_STATUS_OK
            ? DLUMA_STATUS_CHANGED_SINCE_APPROVAL : status;
        goto done;
    }
    locked = dluma_package_find_artifact(&lock, normalized);
    if (!locked) {
        dluma_artifact_error(output, "artifact is not present in the lock");
        status = DLUMA_STATUS_NOT_FOUND;
        goto done;
    }
    if (expected_kind && locked->kind != expected_kind) {
        dluma_artifact_error(output, "artifact kind differs from the request");
        status = DLUMA_STATUS_INVALID_ARGUMENT;
        goto done;
    }
    if (locked->size > maximum_bytes) {
        dluma_artifact_error(output, "artifact exceeds the requested byte limit");
        status = DLUMA_STATUS_LIMIT_EXCEEDED;
        goto done;
    }
    if (!dluma_package_artifact_path(
            final_root, normalized, requested,
            DLUMA_ARRAY_COUNT(requested)) ||
        !dluma_package_open_regular(
            requested, maximum_bytes, 1, &artifact_read) ||
        !dluma_package_path_under(artifact_read.final_path, final_root) ||
        artifact_read.size != locked->size ||
        dluma_stricmp(artifact_read.sha256, locked->sha256) != 0) {
        dluma_artifact_error(output, "locked artifact bytes changed");
        status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
        goto done;
    }
    /*
     * The lock remains open without write/delete sharing while the target is
     * read. Re-hash it after the target snapshot is complete so an already
     * open writer cannot race a different authorization record into use.
     */
    {
        BY_HANDLE_FILE_INFORMATION after;
        char lock_sha256[DLUMA_MOD_SHA256_CAPACITY];
        if (!dluma_sha256_handle_hex(
                lock_read.file, DLUMA_MOD_LOCK_MAX_BYTES, lock_sha256) ||
            dluma_stricmp(lock_sha256, approved->lock_sha256) != 0 ||
            !GetFileInformationByHandle(lock_read.file, &after) ||
            !dluma_package_identity_equal(&lock_read.identity, &after)) {
            dluma_artifact_error(output, "lock changed while reading artifact");
            status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
            goto done;
        }
    }
    output->kind = locked->kind;
    output->size = artifact_read.size;
    output->bytes = artifact_read.bytes;
    artifact_read.bytes = 0;
    dluma_wstr_copy(
        output->final_path, DLUMA_ARRAY_COUNT(output->final_path),
        artifact_read.final_path);
    dluma_str_copy(
        output->relative_path,
        DLUMA_ARRAY_COUNT(output->relative_path), normalized);
    dluma_str_copy(
        output->sha256, DLUMA_ARRAY_COUNT(output->sha256),
        artifact_read.sha256);
    output->volume_serial =
        artifact_read.identity.dwVolumeSerialNumber;
    output->file_index_high =
        artifact_read.identity.nFileIndexHigh;
    output->file_index_low =
        artifact_read.identity.nFileIndexLow;
    output->valid = 1U;
    output->diagnostic[0] = 0;
    status = DLUMA_STATUS_OK;
done:
    dluma_package_read_close(&artifact_read);
    dluma_package_read_close(&lock_read);
    dluma_free(artifacts);
    return status;
}

DlumaStatus dluma_mod_package_list_artifacts(
    const DlumaModPackageSnapshot* approved,
    DlumaModLockedArtifact* artifacts,
    unsigned int artifact_capacity,
    unsigned int* artifact_count)
{
    DlumaPackageRead lock_read;
    DlumaModLock lock;
    DlumaModManifestDiagnostic diagnostic;
    WCHAR final_root[DLUMA_MAX_WPATH];
    WCHAR requested[DLUMA_MAX_WPATH];
    DlumaStatus status = DLUMA_STATUS_INVALID_ARGUMENT;
    if (artifact_count) *artifact_count = 0;
    if (!approved || !approved->valid || !artifacts ||
        !artifact_capacity ||
        artifact_capacity > DLUMA_MOD_LOCK_MAX_ARTIFACTS ||
        !artifact_count)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(
        artifacts, 0,
        (SIZE_T)artifact_capacity * sizeof(*artifacts));
    memset(&lock_read, 0, sizeof(lock_read));
    lock_read.file = INVALID_HANDLE_VALUE;
    if (!dluma_package_open_root(
            approved->package_root, final_root,
            DLUMA_ARRAY_COUNT(final_root)) ||
        dluma_wstricmp(final_root, approved->package_root) != 0) {
        status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
        goto done;
    }
    dluma_path_join(
        requested, DLUMA_ARRAY_COUNT(requested),
        final_root, L"mod.dluma.lock.json");
    if (!dluma_package_open_regular(
            requested, DLUMA_MOD_LOCK_MAX_BYTES, 1, &lock_read) ||
        !dluma_package_path_under(lock_read.final_path, final_root) ||
        dluma_stricmp(
            lock_read.sha256, approved->lock_sha256) != 0) {
        status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
        goto done;
    }
    memset(&lock, 0, sizeof(lock));
    lock.struct_size = (unsigned int)sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = artifact_capacity;
    status = dluma_mod_lock_parse(
        lock_read.bytes, lock_read.size, &lock, &diagnostic);
    if (status != DLUMA_STATUS_OK ||
        !dluma_mod_lock_catalog_compatible(&lock, &approved->manifest) ||
        dluma_stricmp(lock.mod_id, approved->manifest.id) != 0 ||
        dluma_stricmp(
            lock.manifest_sha256,
            approved->manifest_sha256) != 0) {
        if (status == DLUMA_STATUS_OK)
            status = DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
        goto done;
    }
    *artifact_count = lock.artifact_count;
done:
    if (status != DLUMA_STATUS_OK)
        memset(
            artifacts, 0,
            (SIZE_T)artifact_capacity * sizeof(*artifacts));
    dluma_package_read_close(&lock_read);
    return status;
}

void dluma_mod_artifact_release(DlumaModArtifactSnapshot* artifact)
{
    if (!artifact) return;
    dluma_free(artifact->bytes);
    memset(artifact, 0, sizeof(*artifact));
    artifact->struct_size = (unsigned int)sizeof(*artifact);
    artifact->struct_version = DLUMA_MOD_MANIFEST_VERSION;
}
