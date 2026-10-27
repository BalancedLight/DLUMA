#include "dlw_common.h"
#include "dlw_project_owner_identity.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define CHECK(expression)                                              \
    do {                                                               \
        if (!(expression)) {                                           \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,   \
                    __LINE__, #expression);                            \
            return 1;                                                  \
        }                                                              \
    } while (0)

WCHAR dluma_wascii_lower(WCHAR value)
{
    return value >= L'A' && value <= L'Z'
        ? (WCHAR)(value + (L'a' - L'A'))
        : value;
}

char dluma_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A'))
        : value;
}

int dluma_stricmp(const char* left, const char* right)
{
    char a;
    char b;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    do {
        a = dluma_ascii_lower(*left++);
        b = dluma_ascii_lower(*right++);
        if (a != b) return a < b ? -1 : 1;
    } while (a);
    return 0;
}

int dluma_wstricmp(const WCHAR* left, const WCHAR* right)
{
    WCHAR a;
    WCHAR b;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    do {
        a = dluma_wascii_lower(*left++);
        b = dluma_wascii_lower(*right++);
        if (a != b) return a < b ? -1 : 1;
    } while (a);
    return 0;
}

void dluma_wstr_copy(
    WCHAR* output, unsigned int capacity, const WCHAR* input)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    if (input)
        while (input[index] && index + 1 < capacity) {
            output[index] = input[index];
            ++index;
        }
    output[index] = 0;
}

void dluma_wstr_append(
    WCHAR* output, unsigned int capacity, const WCHAR* input)
{
    unsigned int index;
    unsigned int source = 0;
    if (!output || !capacity || !input) return;
    index = (unsigned int)wcslen(output);
    while (input[source] && index + 1 < capacity)
        output[index++] = input[source++];
    output[index] = 0;
}

int dluma_utf8_to_wide(
    const char* input, WCHAR* output, unsigned int capacity)
{
    int result;
    if (!input || !output || !capacity) return 0;
    result = MultiByteToWideChar(
        CP_UTF8, 0, input, -1, output, (int)capacity);
    if (!result) output[0] = 0;
    return result != 0;
}

static int ConvertArgument(
    const char* input, WCHAR* output, unsigned int capacity)
{
    return dluma_utf8_to_wide(input, output, capacity);
}

int main(int argc, char** argv)
{
    WCHAR discovery[DLUMA_MAX_WPATH];
    WCHAR project[DLUMA_MAX_WPATH];
    WCHAR alias[DLUMA_MAX_WPATH];
    WCHAR other[DLUMA_MAX_WPATH];
    WCHAR escape[DLUMA_MAX_WPATH];
    WCHAR escaped_map_project[DLUMA_MAX_WPATH];
    WCHAR lexical_alias[DLUMA_MAX_WPATH];
    WCHAR project_manifest[DLUMA_MAX_WPATH];
    WCHAR outside_file[DLUMA_MAX_WPATH];
    WCHAR excessive[DLUMA_MAX_WPATH + 32];
    DlumaDirectoryIdentity subscribed_identity;
    DlumaDirectoryIdentity local_identity;
    DlumaDirectoryIdentity proof_identity;
    DlumaProjectOwnerProofSnapshot owner_proof;
    unsigned int index;
    CHECK(argc == 7);
    CHECK(ConvertArgument(
        argv[1], discovery, DLUMA_ARRAY_COUNT(discovery)));
    CHECK(ConvertArgument(
        argv[2], project, DLUMA_ARRAY_COUNT(project)));
    CHECK(ConvertArgument(
        argv[3], alias, DLUMA_ARRAY_COUNT(alias)));
    CHECK(ConvertArgument(
        argv[4], other, DLUMA_ARRAY_COUNT(other)));
    CHECK(ConvertArgument(
        argv[5], escape, DLUMA_ARRAY_COUNT(escape)));
    CHECK(ConvertArgument(
        argv[6], escaped_map_project,
        DLUMA_ARRAY_COUNT(escaped_map_project)));

    CHECK(dluma_project_roots_same_identity(project, project));
    dluma_wstr_copy(
        lexical_alias, DLUMA_ARRAY_COUNT(lexical_alias), project);
    dluma_wstr_append(
        lexical_alias, DLUMA_ARRAY_COUNT(lexical_alias), L"\\.");
    CHECK(dluma_project_roots_same_identity(
        project, lexical_alias));
    CHECK(dluma_project_roots_same_identity(project, alias));
    CHECK(!dluma_project_roots_same_identity(project, other));
    CHECK(dluma_open_directory_identity(
        project, &subscribed_identity));
    CHECK(dluma_open_directory_identity(
        other, &local_identity));
    proof_identity = subscribed_identity;
    CHECK(dluma_directory_identity_equal(
        &proof_identity, &subscribed_identity));
    CHECK(!dluma_directory_identity_equal(
        &proof_identity, &local_identity));
    proof_identity = local_identity;
    CHECK(dluma_directory_identity_equal(
        &proof_identity, &local_identity));
    CHECK(!dluma_directory_identity_equal(
        &proof_identity, &subscribed_identity));

    CHECK(dluma_project_candidate_root_is_contained(
        discovery, project));
    CHECK(dluma_project_candidate_root_is_contained(
        discovery, alias));
    CHECK(!dluma_project_candidate_root_is_contained(
        discovery, escape));

    CHECK(dluma_project_map_anchor_is_contained(
        project, "dlua_example"));
    CHECK(dluma_project_map_anchor_is_contained(
        alias, "DLUA_EXAMPLE"));
    CHECK(!dluma_project_map_anchor_is_contained(
        project, ".."));
    CHECK(!dluma_project_map_anchor_is_contained(
        project, "dlua_example\\.."));
    CHECK(!dluma_project_map_anchor_is_contained(
        project, "CON"));
    CHECK(!dluma_project_map_anchor_is_contained(
        project, "LPT1.data"));
    CHECK(!dluma_project_map_anchor_is_contained(
        project, "trailing."));
    CHECK(!dluma_project_map_anchor_is_contained(
        escaped_map_project, "dlua_example"));
    CHECK(dluma_checked_path_join(
        project_manifest,
        DLUMA_ARRAY_COUNT(project_manifest),
        project, L"desc.scr"));
    CHECK(dluma_project_regular_file_is_contained(
        project, project_manifest));
    CHECK(dluma_checked_path_join(
        outside_file, DLUMA_ARRAY_COUNT(outside_file),
        other, L"map_target\\dlua_example.scr"));
    CHECK(!dluma_project_regular_file_is_contained(
        project, outside_file));

    for (index = 0;
         index + 1 < DLUMA_ARRAY_COUNT(excessive);
         ++index)
        excessive[index] = L'a';
    excessive[index] = 0;
    CHECK(!dluma_project_roots_same_identity(
        project, excessive));

    memset(&owner_proof, 0, sizeof(owner_proof));
    owner_proof.status = DLUMA_PROJECT_OWNER_PROOF_PENDING;
    owner_proof.reason = DLUMA_PROJECT_OWNER_REASON_CONSUMED;
    CHECK(dluma_project_owner_pending_proof_retains_context(
        7, 11, &owner_proof));
    owner_proof.reason =
        DLUMA_PROJECT_OWNER_REASON_TRANSITION_MISMATCH;
    CHECK(dluma_project_owner_pending_proof_retains_context(
        7, 11, &owner_proof));
    CHECK(!dluma_project_owner_pending_proof_retains_context(
        0, 11, &owner_proof));
    CHECK(!dluma_project_owner_pending_proof_retains_context(
        7, 0, &owner_proof));
    owner_proof.status = DLUMA_PROJECT_OWNER_PROOF_REJECTED;
    CHECK(!dluma_project_owner_pending_proof_retains_context(
        7, 11, &owner_proof));
    owner_proof.status = DLUMA_PROJECT_OWNER_PROOF_READY;
    CHECK(!dluma_project_owner_pending_proof_retains_context(
        7, 11, &owner_proof));

    puts("[PASS] final paths and file identities");
    puts("[PASS] physical aliases collapse to one owner");
    puts("[PASS] reparse escape and traversal fail closed");
    puts("[PASS] map anchor remains under project root");
    puts("[PASS] exact evidence selects either physical owner");
    puts("[PASS] pending proof retains exact-confirmed context");
    return 0;
}
