#include "../src/core/dlw_mod_manifest.h"
#include "../src/core/dlw_json_reader.h"
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        return 0; \
    } \
} while (0)

#define PREVIOUS_COMPATIBLE_CATALOG_SHA256 \
    "D49EDB1807E5E3B0109AF54D049D95E07072F499F480DD878F32AEE810DA220E"
#define REJECTED_PRIVATE_CATALOG_SHA256 \
    "E3AAF26AA9901C38F8FED3290623FBBDEEBA71753D341D5471392FD67C174A7F"

static const char g_valid_manifest[] =
    "{"
    "\"schema\":\"dluma.mod.v1\","
    "\"api_version\":\"1.0\","
    "\"id\":\"test.native\","
    "\"name\":\"Native test\","
    "\"version\":\"1.2.3\","
    "\"description\":\"Shared manifest contract\","
    "\"author\":\"DLUMA\","
    "\"language\":\"native\","
    "\"scope\":\"global\","
    "\"entry\":\"bin\\\\test.mod\","
    "\"load_priority\":-10,"
    "\"access\":[\"game.read\",\"storage.mod\"],"
    "\"configuration\":{\"schema\":\"config/schema.json\"},"
    "\"companion_content\":["
      "{\"id\":\"audio\",\"path\":\"content/audio.pak\",\"required\":true}"
    "]"
    "}";

static const char g_valid_lock[] =
    "{"
    "\"schema\":\"dluma.mod.lock.v1\","
    "\"api_version\":\"1.0\","
    "\"mod_id\":\"test.native\","
    "\"manifest_sha256\":"
      "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
    "\"catalog_sha256\":\"" DLUMA_API_CATALOG_SHA256 "\","
    "\"artifacts\":["
      "{"
        "\"path\":\"mod.dluma.json\","
        "\"kind\":\"manifest\","
        "\"size\":12,"
        "\"sha256\":"
          "\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\""
      "},"
      "{"
        "\"path\":\"bin/test.mod\","
        "\"kind\":\"entry\","
        "\"size\":42,"
        "\"sha256\":"
          "\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\""
      "}"
    "]"
    "}";

static int parse_manifest(
    const void* bytes,
    unsigned int size,
    DlumaModManifest* manifest,
    DlumaModManifestDiagnostic* diagnostic)
{
    memset(manifest, 0, sizeof(*manifest));
    manifest->struct_size = sizeof(*manifest);
    return dluma_mod_manifest_parse(
        bytes, size, manifest, diagnostic) == DLUMA_STATUS_OK;
}

static int json_skip_finished(const char* json)
{
    DlumaJsonReader reader;
    dluma_json_reader_initialize(
        &reader, json, (unsigned int)strlen(json));
    return dluma_json_skip_value(&reader) && dluma_json_finished(&reader);
}

static int test_json_skip_numbers(void)
{
    CHECK(json_skip_finished(
        "{\"wait\":0.0,\"positive\":2.5,\"exponent\":-1.25e+3}"));
    CHECK(json_skip_finished("[0,-0,123,4.0,6E-2]"));
    CHECK(!json_skip_finished("01"));
    CHECK(!json_skip_finished("1."));
    CHECK(!json_skip_finished("1e"));
    CHECK(!json_skip_finished("-.5"));
    return 1;
}

static int test_valid_manifest(void)
{
    DlumaModManifest manifest;
    DlumaModManifestDiagnostic diagnostic;
    CHECK(parse_manifest(
        g_valid_manifest, (unsigned int)strlen(g_valid_manifest),
        &manifest, &diagnostic));
    CHECK(manifest.language == DLUMA_MOD_LANGUAGE_NATIVE);
    CHECK(manifest.scope == DLUMA_SCOPE_GLOBAL);
    CHECK(manifest.load_priority == -10);
    CHECK(strcmp(manifest.entry, "bin/test.mod") == 0);
    CHECK(manifest.access_count == 2U);
    CHECK(manifest.companion_count == 1U);
    CHECK(manifest.companion[0].required == 1U);
    CHECK(strcmp(manifest.configuration_schema, "config/schema.json") == 0);
    CHECK(manifest.host_flags == DLUMA_MOD_HOST_PLAYER);
    CHECK(manifest.hosts_explicit == 0U);
    return 1;
}

static int test_quest_phase_manifest(void)
{
    static const char valid[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.phases\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"native\","
        "\"scope\":\"global\",\"entry\":\"main.mod\",\"load_priority\":0,"
        "\"access\":[\"quest.phase.handle\",\"editor.quest.extend\"],"
        "\"hosts\":[\"player\",\"editor\"],"
        "\"quest_phase_handlers\":[{"
          "\"id\":\"test.fade\",\"command_alias\":\"fade-test\","
          "\"description\":\"Test fade\"}]"
        "}";
    static const char csharp[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.csharp\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"csharp\","
        "\"scope\":\"global\",\"entry\":\"main.dll\","
        "\"entry_type\":\"Test.Entry\",\"load_priority\":0,"
        "\"access\":[\"quest.phase.handle\"],"
        "\"quest_phase_handlers\":[{\"id\":\"test.bad\","
          "\"description\":\"unavailable\"}]"
        "}";
    static const char editor_only[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.editor\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"native\","
        "\"scope\":\"global\",\"entry\":\"main.mod\",\"load_priority\":0,"
        "\"access\":[\"quest.phase.handle\",\"editor.quest.extend\"],"
        "\"hosts\":[\"editor\"],"
        "\"quest_phase_handlers\":[{\"id\":\"test.bad\","
          "\"description\":\"not a Player provider\"}]"
        "}";
    DlumaModManifest manifest;
    DlumaModManifestDiagnostic diagnostic;
    CHECK(parse_manifest(valid, (unsigned int)strlen(valid),
                         &manifest, &diagnostic));
    CHECK(manifest.host_flags ==
          (DLUMA_MOD_HOST_PLAYER | DLUMA_MOD_HOST_EDITOR));
    CHECK(manifest.hosts_explicit == 1U);
    CHECK(manifest.quest_phase_handler_count == 1U);
    CHECK(strcmp(manifest.quest_phase_handlers[0].id, "test.fade") == 0);
    CHECK(strcmp(manifest.quest_phase_handlers[0].command_alias,
                 "fade-test") == 0);
    CHECK(!parse_manifest(csharp, (unsigned int)strlen(csharp),
                          &manifest, &diagnostic));
    CHECK(!parse_manifest(editor_only, (unsigned int)strlen(editor_only),
                          &manifest, &diagnostic));
    return 1;
}

static int test_manifest_rejections(void)
{
    static const char duplicate[] =
        "{\"schema\":\"dluma.mod.v1\",\"schema\":\"dluma.mod.v1\"}";
    static const char unknown_access[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.lua\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"lua\","
        "\"scope\":\"global\",\"entry\":\"main.lua\",\"load_priority\":0,"
        "\"access\":[\"process.launch\"]"
        "}";
    static const char missing_entry_type[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.csharp\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"csharp\","
        "\"scope\":\"global\",\"entry\":\"main.dll\",\"load_priority\":0,"
        "\"access\":[]"
        "}";
    static const char missing_map[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.map\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"lua\","
        "\"scope\":\"map\",\"entry\":\"main.lua\",\"load_priority\":0,"
        "\"access\":[]"
        "}";
    static const char escape_path[] =
        "{"
        "\"schema\":\"dluma.mod.v1\",\"api_version\":\"1.0\","
        "\"id\":\"test.escape\",\"name\":\"x\",\"version\":\"1.0.0\","
        "\"description\":\"\",\"author\":\"x\",\"language\":\"lua\","
        "\"scope\":\"global\",\"entry\":\"../main.lua\",\"load_priority\":0,"
        "\"access\":[]"
        "}";
    char embedded_nul[sizeof(g_valid_manifest)];
    DlumaModManifest manifest;
    DlumaModManifestDiagnostic diagnostic;
    CHECK(!parse_manifest(
        duplicate, (unsigned int)strlen(duplicate),
        &manifest, &diagnostic));
    CHECK(!parse_manifest(
        unknown_access, (unsigned int)strlen(unknown_access),
        &manifest, &diagnostic));
    CHECK(diagnostic.status == DLUMA_STATUS_ACCESS_DENIED);
    CHECK(!parse_manifest(
        missing_entry_type, (unsigned int)strlen(missing_entry_type),
        &manifest, &diagnostic));
    CHECK(!parse_manifest(
        missing_map, (unsigned int)strlen(missing_map),
        &manifest, &diagnostic));
    CHECK(!parse_manifest(
        escape_path, (unsigned int)strlen(escape_path),
        &manifest, &diagnostic));
    memcpy(embedded_nul, g_valid_manifest, sizeof(g_valid_manifest));
    embedded_nul[20] = 0;
    CHECK(!parse_manifest(
        embedded_nul, (unsigned int)strlen(g_valid_manifest),
        &manifest, &diagnostic));
    CHECK(!parse_manifest(
        g_valid_manifest, (unsigned int)strlen(g_valid_manifest) - 1U,
        &manifest, &diagnostic));
    return 1;
}

static int test_path_boundaries(void)
{
    char output[512];
    char long_path[514];
    unsigned int index;
    CHECK(dluma_mod_relative_path_validate_and_normalize(
        "a\\b/c.lua", output, sizeof(output)));
    CHECK(strcmp(output, "a/b/c.lua") == 0);
    for (index = 0; index < 511U; ++index) long_path[index] = 'a';
    long_path[511] = 0;
    CHECK(dluma_mod_relative_path_validate_and_normalize(
        long_path, output, sizeof(output)));
    long_path[511] = 'a';
    long_path[512] = 0;
    CHECK(!dluma_mod_relative_path_validate_and_normalize(
        long_path, output, sizeof(output)));
    for (index = 0; index < 513U; ++index) long_path[index] = 0;
    CHECK(!dluma_mod_relative_path_validate_and_normalize(
        ".", output, sizeof(output)));
    CHECK(!dluma_mod_relative_path_validate_and_normalize(
        "..\\x", output, sizeof(output)));
    CHECK(!dluma_mod_relative_path_validate_and_normalize(
        "CON.txt", output, sizeof(output)));
    CHECK(!dluma_mod_relative_path_validate_and_normalize(
        "folder/trailing. ", output, sizeof(output)));
    CHECK(!dluma_mod_relative_path_validate_and_normalize(
        "C:\\absolute", output, sizeof(output)));
    return 1;
}

static int test_lock_contract(void)
{
    static const char duplicate_path[] =
        "{"
        "\"schema\":\"dluma.mod.lock.v1\",\"api_version\":\"1.0\","
        "\"mod_id\":\"test.native\","
        "\"manifest_sha256\":"
          "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"catalog_sha256\":\"" DLUMA_API_CATALOG_SHA256 "\","
        "\"artifacts\":["
          "{\"path\":\"x\",\"kind\":\"entry\",\"size\":1,\"sha256\":"
            "\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\"},"
          "{\"path\":\"x\",\"kind\":\"dependency\",\"size\":1,\"sha256\":"
            "\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\"}"
        "]}";
    DlumaModLockedArtifact artifacts[4];
    DlumaModLock lock;
    DlumaModManifestDiagnostic diagnostic;
    memset(&lock, 0, sizeof(lock));
    lock.struct_size = sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = 4U;
    CHECK(dluma_mod_lock_parse(
        g_valid_lock, (unsigned int)strlen(g_valid_lock),
        &lock, &diagnostic) == DLUMA_STATUS_OK);
    CHECK(lock.artifact_count == 2U);
    CHECK(lock.artifacts[0].kind == DLUMA_MOD_ARTIFACT_MANIFEST);
    CHECK(lock.artifacts[1].kind == DLUMA_MOD_ARTIFACT_ENTRY);
    memset(&lock, 0, sizeof(lock));
    lock.struct_size = sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = 1U;
    CHECK(dluma_mod_lock_parse(
        g_valid_lock, (unsigned int)strlen(g_valid_lock),
        &lock, &diagnostic) == DLUMA_STATUS_INSUFFICIENT_BUFFER);
    memset(&lock, 0, sizeof(lock));
    lock.struct_size = sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = 4U;
    CHECK(dluma_mod_lock_parse(
        duplicate_path, (unsigned int)strlen(duplicate_path),
        &lock, &diagnostic) != DLUMA_STATUS_OK);
    return 1;
}

static int test_catalog_compatibility_boundary(void)
{
    static const char compatible_lock[] =
        "{"
        "\"schema\":\"dluma.mod.lock.v1\",\"api_version\":\"1.0\","
        "\"mod_id\":\"test.native\","
        "\"manifest_sha256\":"
          "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"catalog_sha256\":\"" PREVIOUS_COMPATIBLE_CATALOG_SHA256 "\","
        "\"artifacts\":["
          "{\"path\":\"mod.dluma.json\",\"kind\":\"manifest\",\"size\":12,"
            "\"sha256\":\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\"},"
          "{\"path\":\"bin/test.mod\",\"kind\":\"entry\",\"size\":42,"
            "\"sha256\":\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\"}"
        "]} ";
    static const char rejected_lock[] =
        "{"
        "\"schema\":\"dluma.mod.lock.v1\",\"api_version\":\"1.0\","
        "\"mod_id\":\"test.native\","
        "\"manifest_sha256\":"
          "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"catalog_sha256\":\"" REJECTED_PRIVATE_CATALOG_SHA256 "\","
        "\"artifacts\":["
          "{\"path\":\"mod.dluma.json\",\"kind\":\"manifest\",\"size\":12,"
            "\"sha256\":\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\"},"
          "{\"path\":\"bin/test.mod\",\"kind\":\"entry\",\"size\":42,"
            "\"sha256\":\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\"}"
        "]} ";
    DlumaModLockedArtifact artifacts[4];
    DlumaModManifest manifest;
    DlumaModLock lock;
    DlumaModManifestDiagnostic diagnostic;

    memset(&manifest, 0, sizeof(manifest));
    manifest.struct_size = sizeof(manifest);
    manifest.hosts_explicit = 1U;
    manifest.quest_phase_handler_count = 1U;
    manifest.access_ids[0] = 15U;
    manifest.access_count = 1U;

    memset(&lock, 0, sizeof(lock));
    lock.struct_size = sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = 4U;
    CHECK(dluma_mod_lock_parse(
        compatible_lock, (unsigned int)strlen(compatible_lock),
        &lock, &diagnostic) == DLUMA_STATUS_OK);
    CHECK(lock.uses_previous_catalog == 1U);
    CHECK(dluma_mod_lock_catalog_compatible(&lock, &manifest));

    memset(&lock, 0, sizeof(lock));
    lock.struct_size = sizeof(lock);
    lock.artifacts = artifacts;
    lock.artifact_capacity = 4U;
    CHECK(dluma_mod_lock_parse(
        rejected_lock, (unsigned int)strlen(rejected_lock),
        &lock, &diagnostic) != DLUMA_STATUS_OK);
    return 1;
}

int main(void)
{
    int passed = 0;
    if (!test_json_skip_numbers()) return 1;
    puts("[PASS] strict generic JSON number skipping");
    ++passed;
    if (!test_valid_manifest()) return 1;
    puts("[PASS] shared manifest");
    ++passed;
    if (!test_manifest_rejections()) return 1;
    puts("[PASS] manifest rejection boundaries");
    ++passed;
    if (!test_path_boundaries()) return 1;
    puts("[PASS] canonical relative paths");
    ++passed;
    if (!test_lock_contract()) return 1;
    puts("[PASS] immutable lock");
    ++passed;
    if (!test_catalog_compatibility_boundary()) return 1;
    puts("[PASS] public predecessor catalog compatibility boundary");
    ++passed;
    if (!test_quest_phase_manifest()) return 1;
    puts("[PASS] dual-host quest phase manifest");
    ++passed;
    printf("DLUMA manifest fake cases passed: %d\n", passed);
    return 0;
}
