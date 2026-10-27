#include "../src/core/dlw_mod_package_runtime.h"
#include <stdio.h>
#include <string.h>

int wmain(int argc, wchar_t** argv)
{
    DlumaModPackageSnapshot approved;
    DlumaModPackageSnapshot current;
    DlumaModArtifactSnapshot artifact;
    DlumaStatus status;
    if (argc != 3) {
        fputs("usage: dluma_mod_package_runtime_harness <root> <valid|invalid>\n",
              stderr);
        return 2;
    }
    memset(&approved, 0, sizeof(approved));
    approved.struct_size = sizeof(approved);
    status = dluma_mod_package_discover(argv[1], &approved);
    if (argv[2][0] == L'v') {
        if (status != DLUMA_STATUS_OK || !approved.valid) {
            fprintf(stderr, "discover failed: %u %s\n",
                    (unsigned int)status, approved.diagnostic);
            return 1;
        }
        memset(&current, 0, sizeof(current));
        current.struct_size = sizeof(current);
        status = dluma_mod_package_revalidate(&approved, &current);
        if (status != DLUMA_STATUS_OK || !current.valid) {
            fprintf(stderr, "revalidation failed: %u %s\n",
                    (unsigned int)status, current.diagnostic);
            return 1;
        }
        memset(&artifact, 0, sizeof(artifact));
        artifact.struct_size = sizeof(artifact);
        status = dluma_mod_package_read_artifact(
            &approved, approved.manifest.entry,
            DLUMA_MOD_ARTIFACT_ENTRY, 1024U * 1024U, &artifact);
        if (status != DLUMA_STATUS_OK || !artifact.valid ||
            !artifact.bytes || !artifact.size ||
            strcmp(artifact.sha256, approved.entry_sha256) != 0) {
            fprintf(stderr, "artifact read failed: %u %s\n",
                    (unsigned int)status, artifact.diagnostic);
            dluma_mod_artifact_release(&artifact);
            return 1;
        }
        dluma_mod_artifact_release(&artifact);
        wprintf(L"valid %S %S\n",
                approved.manifest.id, approved.entry_sha256);
        return 0;
    }
    if (argv[2][0] == L'm') {
        HANDLE changed;
        DWORD written = 0;
        static const char replacement[] = "changed after approval";
        if (status != DLUMA_STATUS_OK || !approved.valid) {
            fprintf(stderr, "discover failed: %u %s\n",
                    (unsigned int)status, approved.diagnostic);
            return 1;
        }
        changed = CreateFileW(
            approved.entry_path, GENERIC_WRITE, FILE_SHARE_READ,
            0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (changed == INVALID_HANDLE_VALUE ||
            !WriteFile(
                changed, replacement,
                (DWORD)(sizeof(replacement) - 1U), &written, 0) ||
            written != sizeof(replacement) - 1U) {
            if (changed != INVALID_HANDLE_VALUE) CloseHandle(changed);
            fputs("could not mutate fixture\n", stderr);
            return 1;
        }
        CloseHandle(changed);
        memset(&artifact, 0, sizeof(artifact));
        artifact.struct_size = sizeof(artifact);
        status = dluma_mod_package_read_artifact(
            &approved, approved.manifest.entry,
            DLUMA_MOD_ARTIFACT_ENTRY, 1024U * 1024U, &artifact);
        dluma_mod_artifact_release(&artifact);
        if (status != DLUMA_STATUS_CHANGED_SINCE_APPROVAL) {
            fprintf(stderr, "changed artifact was accepted: %u\n",
                    (unsigned int)status);
            return 1;
        }
        puts("changed artifact rejected");
        return 0;
    }
    if (status == DLUMA_STATUS_OK) {
        fputs("invalid package was accepted\n", stderr);
        return 1;
    }
    printf("rejected %u\n", (unsigned int)status);
    return 0;
}
