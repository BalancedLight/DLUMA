#include "../src/core/dlw_editor_missing_asset_probe_guard.h"

#include <stdio.h>

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        return 0; \
    } \
} while (0)

static int test_drive_root_detection(void)
{
    CHECK(dluma_editor_missing_asset_probe_is_drive_root("X:\\", 'X'));
    CHECK(dluma_editor_missing_asset_probe_is_drive_root("x:/", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_is_drive_root("X:\\tools", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_is_drive_root("Y:\\", 'X'));
    return 1;
}

static int test_relative_flat_dmt_at_guarded_root(void)
{
    CHECK(dluma_editor_missing_asset_probe_should_reject(
        "asset_a.dmt", "X:\\", 'X'));
    CHECK(dluma_editor_missing_asset_probe_should_reject(
        "ASSET_B.DMT", "x:/", 'X'));
    return 1;
}

static int test_absolute_flat_dmt_on_guarded_drive(void)
{
    CHECK(dluma_editor_missing_asset_probe_should_reject(
        "X:\\asset_a.dmt", "X:\\tools", 'X'));
    CHECK(dluma_editor_missing_asset_probe_should_reject(
        "x:/asset_b.DmT", "Y:\\", 'X'));
    return 1;
}

static int test_project_and_subdirectory_paths_pass(void)
{
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "data\\asset_a.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "data/asset_a.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "asset:stream.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "X:\\project\\asset_a.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "asset_a.dmt", "X:\\tools", 'X'));
    return 1;
}

static int test_non_dmt_and_enumeration_paths_pass(void)
{
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "asset_a.msh", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "*.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "asset?.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        ".dmt", "X:\\", 'X'));
    return 1;
}

static int test_other_drive_and_traversal_pass(void)
{
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "Y:\\asset_a.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "..asset_a.dmt", "X:\\", 'X'));
    CHECK(!dluma_editor_missing_asset_probe_should_reject(
        "asset..a.dmt", "X:\\", 'X'));
    return 1;
}

int main(void)
{
    int passed = 0;
    passed += test_drive_root_detection();
    passed += test_relative_flat_dmt_at_guarded_root();
    passed += test_absolute_flat_dmt_on_guarded_drive();
    passed += test_project_and_subdirectory_paths_pass();
    passed += test_non_dmt_and_enumeration_paths_pass();
    passed += test_other_drive_and_traversal_pass();
    if (passed != 6) return 1;
    printf("Editor missing-asset probe guard fake cases passed: %d\n", passed);
    return 0;
}
