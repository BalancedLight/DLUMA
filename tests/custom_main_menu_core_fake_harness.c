#include "dlw_custom_main_menu_core.h"
#include "dlw_custom_main_menu_runtime_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,     \
                    #expression);                                             \
            return 0;                                                         \
        }                                                                     \
    } while (0)

void dluma_str_append(char *destination, unsigned int capacity, const char *text)
{
    unsigned int length;
    unsigned int index = 0;
    if (!destination || !capacity || !text)
        return;
    length = (unsigned int)strlen(destination);
    while (text[index] && length + 1U < capacity)
        destination[length++] = text[index++];
    destination[length] = 0;
}

static const char g_valid_manifest[] =
    "{"
      "\"meta\":{\"id\":\"example.testmenu\","
                   "\"author\":\"Example\"},"
      "\"general\":{\"name\":\"Test Menu\","
                      "\"description\":\"Static menu fixture\","
                      "\"version\":\"1.0.0\","
                      "\"is3DMenuEnabled\":false,"
                      "\"useCustomBanks\":true},"
      "\"scene\":{\"map\":\"menu_lvl_test\"},"
      "\"cameras\":[{\"gameObject\":[\"camera_main\",60]}],"
      "\"audio\":{"
        "\"banks\":[{\"name\":\"menu_audio\","
                     "\"path\":\"data/custom.csb\"}],"
        "\"music_overrides\":["
          "{\"name\":\"mes_menu\",\"overrides\":\"dw_menu\"},"
          "{\"name\":\"mes_credits_menu\","
           "\"overrides\":\"dl_menu_end_credits\"}]},"
      "\"menus\":[{\"name\":\"dluma_menumain\","
                    "\"overrides\":"
                    "\"MenuMain_PC_workshop_with_B_Mods\"}]"
    "}";

static int parse_manifest(const char *json,
                          DlumaCustomMainMenuManifest *manifest,
                          char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY])
{
    return dluma_custom_main_menu_manifest_parse(
        json, (unsigned int)strlen(json), manifest, diagnostic,
        DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY);
}

static int test_manifest_file(const char *path)
{
    FILE *input;
    long size;
    char *json;
    DlumaCustomMainMenuManifest manifest;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    int success = 0;
    if (!path || !path[0])
        return 0;
    if (fopen_s(&input, path, "rb") != 0 || !input)
        return 0;
    if (fseek(input, 0, SEEK_END) != 0)
        goto done;
    size = ftell(input);
    if (size <= 0 || size > (long)DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES ||
        fseek(input, 0, SEEK_SET) != 0)
        goto done;
    json = (char *)malloc((size_t)size + 1U);
    if (!json)
        goto done;
    if (fread(json, 1U, (size_t)size, input) == (size_t)size)
    {
        json[size] = 0;
        success = dluma_custom_main_menu_manifest_parse(
            json, (unsigned int)size, &manifest, diagnostic,
            DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY) &&
            manifest.does_use_3d_camera_movement == 0U &&
            manifest.use_custom_textures == 1U &&
            manifest.texture_def_count == 1U &&
            strcmp(manifest.texture_defs[0].name,
                   "ui_theme.def") == 0 &&
            strcmp(manifest.texture_defs[0].source_path,
                   "data/menu/texturedefs/ui_theme.def") == 0;
        if (!success)
            fprintf(stderr, "manifest file rejected: %s\n", diagnostic);
    }
    free(json);
done:
    fclose(input);
    return success;
}

static int test_manifest_acceptance(void)
{
    static const char legacy_audio[] =
        "{"
          "\"meta\":{\"id\":\"author.legacy\",\"author\":\"A\"},"
          "\"general\":{\"name\":\"Legacy\","
                          "\"description\":\"Description\","
                          "\"version\":\"1\","
                          "\"is3DMenuEnabled\":false,"
                          "\"useCustomBanks\":true},"
          "\"scene\":{\"map\":\"menu_legacy\"},"
          "\"cameras\":[{\"gameObject\":[\"camera_main\",1]}],"
          "\"audio\":{\"banks\":[{\"name\":\"menu_audio\","
                                      "\"path\":\"data/custom.csb\"}],"
                      "\"music_override\":{\"name\":\"mes_menu\","
                          "\"overrides\":\"dw_menu\"}},"
          "\"menus\":[{\"name\":\"main\",\"overrides\":"
              "\"MenuMain_PC_workshop_with_B_Mods\"}]"
        "}";
    static const char no_audio[] =
        "{"
          "\"meta\":{\"id\":\"author.noaudio\",\"author\":\"A\"},"
          "\"general\":{\"name\":\"No Audio\","
                          "\"description\":\"Description\","
                          "\"version\":\"1\","
                          "\"is3DMenuEnabled\":false,"
                          "\"useCustomBanks\":false},"
          "\"scene\":{\"map\":\"menu_no_audio\"},"
          "\"cameras\":[{\"gameObject\":[\"camera_main\",1]}],"
          "\"menus\":[{\"name\":\"main\","
                        "\"overrides\":"
                        "\"MenuMain_PC_workshop_with_B_Mods\"}]"
        "}";
    static const char dynamic_targets[] =
        "{\"meta\":{\"id\":\"author.dynamic\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],\"menus\":["
        "{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"},"
        "{\"name\":\"dluma_menuextras\",\"overrides\":\"menuextras\"},"
        "{\"name\":\"new_screen\",\"overrides\":\"AddedByAnotherMod\"}]}";
    static const char custom_texture_options[] =
        "{\"meta\":{\"id\":\"example.texturedmenu\","
        "\"author\":\"Example\"},\"general\":{"
        "\"name\":\"Textured Custom Menu\","
        "\"description\":\"D\",\"version\":\"1\","
        "\"is3DMenuEnabled\":false,"
        "\"doesUse3DCameraMovement\":true,"
        "\"useCustomBanks\":false,\"useCustomTextures\":true},"
        "\"scene\":{\"map\":\"menu_textured\"},"
        "\"cameras\":[{\"gameObject\":[\"camera_main\",60]}],"
        "\"textures\":[{\"def\":\"ui_theme.def\"}],"
        "\"menus\":[{\"name\":\"textured_menumain\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    DlumaCustomMainMenuManifest manifest;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    CHECK(parse_manifest(g_valid_manifest, &manifest, diagnostic));
    CHECK(strcmp(manifest.id, "example.testmenu") == 0);
    CHECK(strcmp(manifest.map, "menu_lvl_test") == 0);
    CHECK(strcmp(manifest.camera_name, "camera_main") == 0);
    CHECK(manifest.camera_id == 60);
    CHECK(manifest.bank_count == 1U);
    CHECK(strcmp(manifest.banks[0].name, "menu_audio") == 0);
    CHECK(manifest.music_override_count == 2U);
    CHECK(manifest.music_overrides[0].target ==
          DLUMA_CUSTOM_MENU_MUSIC_MAIN);
    CHECK(strcmp(manifest.music_overrides[0].name, "mes_menu") == 0);
    CHECK(manifest.music_overrides[1].target ==
          DLUMA_CUSTOM_MENU_MUSIC_CREDITS);
    CHECK(strcmp(manifest.music_overrides[1].name,
                 "mes_credits_menu") == 0);
    CHECK(manifest.override_count == 1U);
    CHECK(strcmp(manifest.overrides[0].source_path,
                 "data/menu/dluma/dluma_menumain.xui") == 0);
    CHECK(strcmp(manifest.overrides[0].destination,
                 "DLUMA_Main_PC_workshop.xui") == 0);
    CHECK(parse_manifest(legacy_audio, &manifest, diagnostic));
    CHECK(manifest.music_override_count == 1U);
    CHECK(manifest.music_overrides[0].target ==
          DLUMA_CUSTOM_MENU_MUSIC_MAIN);
    CHECK(parse_manifest(no_audio, &manifest, diagnostic));
    CHECK(!manifest.use_custom_banks);
    CHECK(!manifest.does_use_3d_camera_movement);
    CHECK(!manifest.use_custom_textures);
    CHECK(manifest.texture_def_count == 0U);
    CHECK(manifest.bank_path[0] == 0);
    CHECK(parse_manifest(dynamic_targets, &manifest, diagnostic));
    CHECK(manifest.override_count == 3U);
    CHECK(strcmp(manifest.overrides[1].source_path,
                 "data/menu/dluma/dluma_menuextras.xui") == 0);
    CHECK(strcmp(manifest.overrides[1].destination,
                 "MenuExtras.xui") == 0);
    CHECK(strcmp(manifest.overrides[2].destination,
                 "AddedByAnotherMod.xui") == 0);
    CHECK(parse_manifest(custom_texture_options, &manifest, diagnostic));
    CHECK(manifest.does_use_3d_camera_movement);
    CHECK(manifest.use_custom_textures);
    CHECK(manifest.texture_def_count == 1U);
    CHECK(strcmp(manifest.texture_defs[0].name,
                 "ui_theme.def") == 0);
    CHECK(strcmp(manifest.texture_defs[0].source_path,
                 "data/menu/texturedefs/ui_theme.def") == 0);
    return 1;
}

static int test_manifest_rejections(void)
{
    static const char missing_audio[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":true},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char reserved_id[] =
        "{\"meta\":{\"id\":\"base_game\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char duplicate_key[] =
        "{\"meta\":{\"id\":\"a.b\",\"id\":\"a.c\","
        "\"author\":\"A\"},\"general\":{}}";
    static const char wrong_type[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":\"false\"},"
        "\"scene\":{\"map\":\"menu\"},\"cameras\":[],"
        "\"menus\":[]}";
    static const char multiple_cameras[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"a\",1]},"
                       "{\"gameObject\":[\"b\",2]}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char unsupported_3d[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":true,"
        "\"useCustomBanks\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char unknown_target[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":[{\"name\":\"main\","
        "\"overrides\":\"UnknownMenu\"}]}";
    static const char duplicate_target[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":["
          "{\"name\":\"main_a\",\"overrides\":"
           "\"MenuMain_PC_workshop_with_B_Mods\"},"
          "{\"name\":\"main_b\",\"overrides\":"
           "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char missing_main[] =
        "{\"meta\":{\"id\":\"a.b\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":[{\"name\":\"options\","
        "\"overrides\":\"MenuOptions\"}]}";
    static const char unsafe_target[] =
        "{\"meta\":{\"id\":\"a.unsafe\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],\"menus\":["
        "{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"},"
        "{\"name\":\"unsafe\",\"overrides\":\"../MenuOptions\"}]}";
    static const char reserved_selector[] =
        "{\"meta\":{\"id\":\"a.reserved\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],\"menus\":["
        "{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"},"
        "{\"name\":\"selector\",\"overrides\":"
        "\"MenuDLUMACustomMainMenu\"}]}";
    static const char alias_collision[] =
        "{\"meta\":{\"id\":\"a.collision\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],\"menus\":["
        "{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"},"
        "{\"name\":\"alias\",\"overrides\":"
        "\"dluma_main_pc_workshop\"}]}";
    static const char case_collision[] =
        "{\"meta\":{\"id\":\"a.casecollision\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],\"menus\":["
        "{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"},"
        "{\"name\":\"one\",\"overrides\":\"AddedScreen\"},"
        "{\"name\":\"two\",\"overrides\":\"addedscreen\"}]}";
    static const char unknown_music_target[] =
        "{\"meta\":{\"id\":\"a.music.unknown\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":true},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"audio\":{\"banks\":[{\"name\":\"bank\","
        "\"path\":\"data/custom.csb\"}],\"music_overrides\":["
        "{\"name\":\"mes_menu\",\"overrides\":\"unknown\"}]},"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char duplicate_music_target[] =
        "{\"meta\":{\"id\":\"a.music.duplicate\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":true},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"audio\":{\"banks\":[{\"name\":\"bank\","
        "\"path\":\"data/custom.csb\"}],\"music_overrides\":["
        "{\"name\":\"mes_menu\",\"overrides\":\"dw_menu\"},"
        "{\"name\":\"mes_menu_2\",\"overrides\":\"dw_menu\"}]},"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char missing_main_music[] =
        "{\"meta\":{\"id\":\"a.music.credits\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":true},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"audio\":{\"banks\":[{\"name\":\"bank\","
        "\"path\":\"data/custom.csb\"}],\"music_overrides\":["
        "{\"name\":\"mes_credits_menu\","
        "\"overrides\":\"dl_menu_end_credits\"}]},"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    DlumaCustomMainMenuManifest manifest;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    CHECK(!parse_manifest(missing_audio, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "custom_audio_manifest_missing") == 0);
    CHECK(!parse_manifest(reserved_id, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "meta_id_invalid_or_reserved") == 0);
    CHECK(!parse_manifest(duplicate_key, &manifest, diagnostic));
    CHECK(!parse_manifest(wrong_type, &manifest, diagnostic));
    CHECK(!parse_manifest(multiple_cameras, &manifest, diagnostic));
    CHECK(!parse_manifest(unsupported_3d, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "three_dimensional_xui_unsupported") == 0);
    CHECK(!parse_manifest(unknown_target, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "main_menu_override_missing") == 0);
    CHECK(!parse_manifest(duplicate_target, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "menu_override_destination_duplicate") == 0);
    CHECK(!parse_manifest(missing_main, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "main_menu_override_missing") == 0);
    CHECK(!parse_manifest(unsafe_target, &manifest, diagnostic));
    CHECK(strcmp(diagnostic,
                 "menu_override_target_invalid_or_unsafe") == 0);
    CHECK(!parse_manifest(reserved_selector, &manifest, diagnostic));
    CHECK(strcmp(diagnostic,
                 "menu_override_target_reserved_for_recovery") == 0);
    CHECK(!parse_manifest(alias_collision, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "menu_override_destination_duplicate") == 0);
    CHECK(!parse_manifest(case_collision, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "menu_override_destination_duplicate") == 0);
    CHECK(!parse_manifest(unknown_music_target, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "custom_audio_contract_invalid") == 0);
    CHECK(!parse_manifest(duplicate_music_target, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "custom_audio_contract_invalid") == 0);
    CHECK(parse_manifest(missing_main_music, &manifest, diagnostic));
    CHECK(manifest.music_override_count == 1U);
    CHECK(manifest.music_overrides[0].target ==
          DLUMA_CUSTOM_MENU_MUSIC_CREDITS);
    CHECK(!dluma_custom_main_menu_manifest_parse(
        g_valid_manifest, DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES + 1U,
        &manifest, diagnostic, sizeof(diagnostic)));
    CHECK(strcmp(diagnostic, "manifest_size_invalid") == 0);
    return 1;
}

static int test_public_token_and_path_contract(void)
{
    CHECK(dluma_custom_main_menu_id_valid("author.menu-1"));
    CHECK(!dluma_custom_main_menu_id_valid("base_game"));
    CHECK(!dluma_custom_main_menu_id_valid("author/menu"));
    CHECK(dluma_custom_main_menu_asset_token_valid("menu_lvl_1"));
    CHECK(!dluma_custom_main_menu_asset_token_valid("menu/lvl"));
    CHECK(dluma_custom_main_menu_relative_path_valid("data/custom.csb"));
    CHECK(dluma_custom_main_menu_relative_path_valid("data\\custom.csb"));
    CHECK(!dluma_custom_main_menu_relative_path_valid("../custom.csb"));
    CHECK(!dluma_custom_main_menu_relative_path_valid("data/../custom.csb"));
    CHECK(!dluma_custom_main_menu_relative_path_valid("C:/custom.csb"));
    CHECK(!dluma_custom_main_menu_relative_path_valid("data/con.csb"));
    CHECK(!dluma_custom_main_menu_relative_path_valid("data/name. /custom.csb"));
    CHECK(!dluma_custom_main_menu_relative_path_valid("data/name./custom.csb"));
    return 1;
}

static int build_override_boundary_manifest(
    char *output, unsigned int capacity, unsigned int override_count)
{
    static const char prefix[] =
        "{\"meta\":{\"id\":\"author.boundary\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],\"menus\":["
        "{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}";
    unsigned int length = 0;
    unsigned int index;
    int written;
    if (!output || capacity <= sizeof(prefix) || !override_count)
        return 0;
    written = snprintf(output, capacity, "%s", prefix);
    if (written < 0 || (unsigned int)written >= capacity)
        return 0;
    length = (unsigned int)written;
    for (index = 1U; index < override_count; ++index)
    {
        written = snprintf(
            output + length, capacity - length,
            ",{\"name\":\"screen%u\",\"overrides\":\"Target%u\"}",
            index, index);
        if (written < 0 || (unsigned int)written >= capacity - length)
            return 0;
        length += (unsigned int)written;
    }
    written = snprintf(output + length, capacity - length, "]}");
    return written == 2;
}

static int test_override_count_boundary(void)
{
    char json[32768];
    DlumaCustomMainMenuManifest manifest;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    CHECK(DLUMA_CUSTOM_MENU_MAX_OVERRIDES == 128U);
    CHECK(build_override_boundary_manifest(
        json, sizeof(json), DLUMA_CUSTOM_MENU_MAX_OVERRIDES));
    CHECK(parse_manifest(json, &manifest, diagnostic));
    CHECK(manifest.override_count == DLUMA_CUSTOM_MENU_MAX_OVERRIDES);
    CHECK(strcmp(manifest.overrides[127].destination,
                 "Target127.xui") == 0);
    CHECK(build_override_boundary_manifest(
        json, sizeof(json), DLUMA_CUSTOM_MENU_MAX_OVERRIDES + 1U));
    CHECK(!parse_manifest(json, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "menu_override_count_limit_exceeded") == 0);
    return 1;
}

static int build_texture_boundary_manifest(
    char *output, unsigned int capacity, unsigned int texture_count)
{
    static const char prefix[] =
        "{\"meta\":{\"id\":\"author.textures\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"doesUse3DCameraMovement\":false,"
        "\"useCustomBanks\":false,\"useCustomTextures\":true},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"textures\":[";
    static const char suffix[] =
        "],\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    unsigned int index;
    unsigned int used;
    int written = snprintf(output, capacity, "%s", prefix);
    if (written < 0 || (unsigned int)written >= capacity)
        return 0;
    used = (unsigned int)written;
    for (index = 0; index < texture_count; ++index)
    {
        written = snprintf(
            output + used, capacity - used,
            "%s{\"def\":\"texture_%u.def\"}", index ? "," : "", index);
        if (written < 0 || (unsigned int)written >= capacity - used)
            return 0;
        used += (unsigned int)written;
    }
    written = snprintf(output + used, capacity - used, "%s", suffix);
    return written >= 0 && (unsigned int)written < capacity - used;
}

static int test_texture_definition_contract(void)
{
    static const char disabled_with_definition[] =
        "{\"meta\":{\"id\":\"author.disabled\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false,\"useCustomTextures\":false},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"textures\":[{\"def\":\"texture.def\"}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char duplicate_definition[] =
        "{\"meta\":{\"id\":\"author.duplicate\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false,\"useCustomTextures\":true},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"textures\":[{\"def\":\"Texture.def\"},"
        "{\"def\":\"texture.DEF\"}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char wrong_texture_flag_type[] =
        "{\"meta\":{\"id\":\"author.type\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":false,\"useCustomTextures\":\"true\"},"
        "\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}";
    static const char *const unsafe_names[] = {
        "", "../texture.def", "folder/texture.def", "C:evil.def",
        "CON.def", "texture.txt"
    };
    char json[32768];
    DlumaCustomMainMenuManifest manifest;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    unsigned int index;

    CHECK(build_texture_boundary_manifest(json, sizeof(json), 0U));
    CHECK(!parse_manifest(json, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "custom_texture_definitions_missing") == 0);
    CHECK(build_texture_boundary_manifest(
        json, sizeof(json), DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS));
    CHECK(parse_manifest(json, &manifest, diagnostic));
    CHECK(manifest.texture_def_count == DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS);
    CHECK(build_texture_boundary_manifest(
        json, sizeof(json), DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS + 1U));
    CHECK(!parse_manifest(json, &manifest, diagnostic));
    CHECK(strcmp(diagnostic,
                 "custom_texture_definition_count_exceeded") == 0);
    CHECK(!parse_manifest(disabled_with_definition, &manifest, diagnostic));
    CHECK(strcmp(diagnostic,
                 "custom_texture_definitions_disabled") == 0);
    CHECK(!parse_manifest(duplicate_definition, &manifest, diagnostic));
    CHECK(strcmp(diagnostic,
                 "custom_texture_definition_duplicate") == 0);
    CHECK(!parse_manifest(wrong_texture_flag_type, &manifest, diagnostic));
    CHECK(strcmp(diagnostic, "manifest_schema_or_type_invalid") == 0);
    for (index = 0; index < sizeof(unsafe_names) / sizeof(unsafe_names[0]);
         ++index)
    {
        int written = snprintf(
            json, sizeof(json),
            "{\"meta\":{\"id\":\"author.unsafe\",\"author\":\"A\"},"
            "\"general\":{\"name\":\"N\",\"description\":\"D\","
            "\"version\":\"1\",\"is3DMenuEnabled\":false,"
            "\"useCustomBanks\":false,\"useCustomTextures\":true},"
            "\"scene\":{\"map\":\"menu\"},"
            "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
            "\"textures\":[{\"def\":\"%s\"}],"
            "\"menus\":[{\"name\":\"main\",\"overrides\":"
            "\"MenuMain_PC_workshop_with_B_Mods\"}]}", unsafe_names[index]);
        CHECK(written > 0 && (unsigned int)written < sizeof(json));
        CHECK(!parse_manifest(json, &manifest, diagnostic));
        CHECK(strcmp(diagnostic,
                     "custom_texture_definition_unsafe") == 0);
    }
    return 1;
}

static int make_bank_manifest(
    unsigned int bank_count, char *output, unsigned int capacity)
{
    unsigned int index;
    int written;
    unsigned int used = 0;
    written = snprintf(
        output, capacity,
        "{\"meta\":{\"id\":\"author.banks\",\"author\":\"A\"},"
        "\"general\":{\"name\":\"N\",\"description\":\"D\","
        "\"version\":\"1\",\"is3DMenuEnabled\":false,"
        "\"useCustomBanks\":true},\"scene\":{\"map\":\"menu\"},"
        "\"cameras\":[{\"gameObject\":[\"camera\",1]}],"
        "\"audio\":{\"banks\":[");
    if (written < 0 || (unsigned int)written >= capacity)
        return 0;
    used = (unsigned int)written;
    for (index = 0; index < bank_count; ++index)
    {
        written = snprintf(
            output + used, capacity - used,
            "%s{\"name\":\"bank_%u\","
            "\"path\":\"data/bank_%u.csb\"}",
            index ? "," : "", index, index);
        if (written < 0 || (unsigned int)written >= capacity - used)
            return 0;
        used += (unsigned int)written;
    }
    written = snprintf(
        output + used, capacity - used,
        "]},\"menus\":[{\"name\":\"main\",\"overrides\":"
        "\"MenuMain_PC_workshop_with_B_Mods\"}]}");
    return written >= 0 && (unsigned int)written < capacity - used;
}

static int test_bank_count_and_optional_music_contract(void)
{
    char json[DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES];
    DlumaCustomMainMenuManifest manifest;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    CHECK(make_bank_manifest(0U, json, sizeof(json)));
    CHECK(!parse_manifest(json, &manifest, diagnostic));
    CHECK(make_bank_manifest(1U, json, sizeof(json)));
    CHECK(parse_manifest(json, &manifest, diagnostic));
    CHECK(manifest.bank_count == 1U);
    CHECK(manifest.music_override_count == 0U);
    CHECK(make_bank_manifest(16U, json, sizeof(json)));
    CHECK(parse_manifest(json, &manifest, diagnostic));
    CHECK(manifest.bank_count == 16U);
    CHECK(make_bank_manifest(17U, json, sizeof(json)));
    CHECK(!parse_manifest(json, &manifest, diagnostic));
    return 1;
}

static int test_xui_override_contracts(void)
{
    static const char arbitrary_xui[] =
        "<XuiCanvas><Properties><Id>Anything</Id></Properties></XuiCanvas>";
    static const char editor_lf[] =
        "<Id>MenuOptions</Id>\n"
        "<ClassOverride>MenuOptions</ClassOverride><Id>B_Back</Id>";
    static const char bad_comment[] =
        "<!--invalid-->\r\n<Id>MenuOptions</Id>\r\n"
        "<ClassOverride>MenuOptions</ClassOverride><Id>B_Back</Id>";
    static const unsigned char bad_bom[] = {
        0xEFU, 0xBBU, 0xBFU, '<', 'I', 'd', '>', 'x', '<', '/', 'I', 'd', '>'
    };
    static const char direct_music[] =
        "<XuiCanvas><Properties><MusicOverride>mes_extras"
        "</MusicOverride></Properties></XuiCanvas>";
    static const char nested_music[] =
        "<XuiCanvas><Properties><Group><MusicOverride>nested"
        "</MusicOverride></Group></Properties></XuiCanvas>";
    static const char duplicate_music[] =
        "<XuiCanvas><Properties><MusicOverride>a</MusicOverride>"
        "<MusicOverride>b</MusicOverride></Properties></XuiCanvas>";
    static const char malformed_music[] =
        "<XuiCanvas><Properties><MusicOverride>bad cue"
        "</MusicOverride></Properties></XuiCanvas>";
    char destination[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    char cue[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
    char too_long[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    memset(too_long, 'a', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = 0;
    CHECK(dluma_custom_main_menu_resolve_override_destination(
        "MenuMain_PC_workshop_with_B_Mods", destination,
        sizeof(destination)));
    CHECK(strcmp(destination, "DLUMA_Main_PC_workshop.xui") == 0);
    CHECK(dluma_custom_main_menu_resolve_override_destination(
        "MenuSteamWorkshop", destination, sizeof(destination)));
    CHECK(strcmp(destination, "DLUMA_SteamWorkshop.xui") == 0);
    CHECK(dluma_custom_main_menu_resolve_override_destination(
        "MenuOptions", destination, sizeof(destination)));
    CHECK(strcmp(destination, "MenuOptions.xui") == 0);
    CHECK(dluma_custom_main_menu_resolve_override_destination(
        "menuextras", destination, sizeof(destination)));
    CHECK(strcmp(destination, "MenuExtras.xui") == 0);
    CHECK(dluma_custom_main_menu_resolve_override_destination(
        "AddedByAnotherMod", destination, sizeof(destination)));
    CHECK(strcmp(destination, "AddedByAnotherMod.xui") == 0);
    CHECK(!dluma_custom_main_menu_resolve_override_destination(
        "MenuDLUMACustomMainMenu", destination, sizeof(destination)));
    CHECK(!dluma_custom_main_menu_resolve_override_destination(
        "../MenuOptions", destination, sizeof(destination)));
    CHECK(!dluma_custom_main_menu_resolve_override_destination(
        "con", destination, sizeof(destination)));
    CHECK(!dluma_custom_main_menu_resolve_override_destination(
        too_long, destination, sizeof(destination)));
    CHECK(dluma_custom_main_menu_xui_contract_valid(
        (const unsigned char *)editor_lf,
        (unsigned int)strlen(editor_lf)));
    CHECK(dluma_custom_main_menu_xui_contract_valid(
        (const unsigned char *)bad_comment,
        (unsigned int)strlen(bad_comment)));
    CHECK(dluma_custom_main_menu_xui_contract_valid(
        bad_bom, sizeof(bad_bom)));
    CHECK(dluma_custom_main_menu_xui_contract_valid(
        (const unsigned char *)arbitrary_xui,
        (unsigned int)strlen(arbitrary_xui)));
    CHECK(!dluma_custom_main_menu_xui_contract_valid(
        0, 1U));
    CHECK(!dluma_custom_main_menu_xui_contract_valid(
        (const unsigned char *)arbitrary_xui, 0U));
    CHECK(dluma_custom_main_menu_xui_music_override(
        (const unsigned char *)direct_music,
        (unsigned int)strlen(direct_music), cue, sizeof(cue)));
    CHECK(strcmp(cue, "mes_extras") == 0);
    CHECK(dluma_custom_main_menu_xui_music_override(
        (const unsigned char *)nested_music,
        (unsigned int)strlen(nested_music), cue, sizeof(cue)));
    CHECK(cue[0] == 0);
    CHECK(!dluma_custom_main_menu_xui_music_override(
        (const unsigned char *)duplicate_music,
        (unsigned int)strlen(duplicate_music), cue, sizeof(cue)));
    CHECK(!dluma_custom_main_menu_xui_music_override(
        (const unsigned char *)malformed_music,
        (unsigned int)strlen(malformed_music), cue, sizeof(cue)));
    return 1;
}

static void write_u16(unsigned char *bytes, unsigned int offset,
                      unsigned int value)
{
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1U] = (unsigned char)(value >> 8U);
}

static void write_u32(unsigned char *bytes, unsigned int offset,
                      unsigned int value)
{
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1U] = (unsigned char)(value >> 8U);
    bytes[offset + 2U] = (unsigned char)(value >> 16U);
    bytes[offset + 3U] = (unsigned char)(value >> 24U);
}

static unsigned int make_root(unsigned char *bytes, const char *class_name)
{
    unsigned int class_length = (unsigned int)strlen(class_name);
    unsigned int size = 15U + class_length;
    memset(bytes, 0, 64U);
    write_u32(bytes, 2U, size - 6U);
    write_u16(bytes, 6U, 0x77U);
    write_u32(bytes, 8U, class_length + 2U);
    write_u16(bytes, 12U, class_length);
    memcpy(bytes + 14U, class_name, class_length);
    bytes[14U + class_length] = 0xA5U;
    return size;
}

static int test_binary_promotion(void)
{
    unsigned char input[64];
    unsigned char output[96];
    unsigned int size;
    unsigned int output_size = 0;
    int already = 0;
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    size = make_root(input, "LevelDI");
    CHECK(dluma_custom_main_menu_promote_root_record(
        input, size, output, sizeof(output), &output_size, &already,
        diagnostic, sizeof(diagnostic)));
    CHECK(!already);
    CHECK(output_size == size + 11U);
    CHECK(memcmp(output + 14U, "Menu3DBackgroundDI", 18U) == 0);
    CHECK(output[32U] == 0xA5U);
    CHECK(dluma_custom_main_menu_promote_root_record(
        output, output_size, input, sizeof(input), &size, &already,
        diagnostic, sizeof(diagnostic)));
    CHECK(already);
    CHECK(size == output_size);

    size = make_root(input, "OtherDI");
    CHECK(!dluma_custom_main_menu_promote_root_record(
        input, size, output, sizeof(output), &output_size, &already,
        diagnostic, sizeof(diagnostic)));
    CHECK(strcmp(diagnostic, "root_class_not_leveldi") == 0);
    size = make_root(input, "LevelDI");
    input[6] = 0x76U;
    CHECK(!dluma_custom_main_menu_promote_root_record(
        input, size, output, sizeof(output), &output_size, &already,
        diagnostic, sizeof(diagnostic)));
    input[6] = 0x77U;
    write_u32(input, 8U, 8U);
    CHECK(!dluma_custom_main_menu_promote_root_record(
        input, size, output, sizeof(output), &output_size, &already,
        diagnostic, sizeof(diagnostic)));
    CHECK(!dluma_custom_main_menu_promote_root_record(
        input, 14U, output, sizeof(output), &output_size, &already,
        diagnostic, sizeof(diagnostic)));

    size = make_root(input, "LevelDI");
    CHECK(!dluma_custom_main_menu_promote_root_record(
        input, size, output, size, &output_size, &already,
        diagnostic, sizeof(diagnostic)));
    CHECK(strcmp(diagnostic, "promotion_output_too_small") == 0);

    size = make_root(input, "LevelDI");
    write_u32(input, 2U, 0xFFFFFFF9U);
    CHECK(!dluma_custom_main_menu_promote_root_record(
        input, 0xFFFFFFFFU, output, sizeof(output), &output_size, &already,
        diagnostic, sizeof(diagnostic)));
    CHECK(strcmp(diagnostic, "root_record_length_overflow") == 0);
    return 1;
}

static int test_receipt_and_runtime_contracts(void)
{
    static const unsigned char music_call_block[16] = {
        0x20U, 0x0BU, 0x00U, 0x00U, 0x48U, 0x0FU, 0x45U, 0xD1U,
        0x48U, 0x8BU, 0xCBU, 0xE8U, 0xE0U, 0xE8U, 0xE4U, 0xFEU
    };
    static const unsigned char fx_enable_block[10] = {
        0x0FU, 0xB6U, 0xC2U, 0x89U, 0x05U,
        0xFBU, 0x47U, 0x90U, 0x00U, 0xC3U
    };
    static const unsigned char stock_premount_block[83] = {
        0x48U, 0x89U, 0x5CU, 0x24U, 0x18U, 0x55U, 0x41U, 0x54U,
        0x41U, 0x55U, 0x48U, 0x8DU, 0x6CU, 0x24U, 0xB9U, 0x48U,
        0x81U, 0xECU, 0x90U, 0x00U, 0x00U, 0x00U, 0x4CU, 0x8BU,
        0xE1U, 0x48U, 0x8BU, 0x0DU, 0x48U, 0x6DU, 0x3EU, 0x01U,
        0x48U, 0x89U, 0xB4U, 0x24U, 0xB0U, 0x00U, 0x00U, 0x00U,
        0x48U, 0x8BU, 0x01U, 0x48U, 0x8BU, 0xDAU, 0xFFU, 0x90U,
        0xC8U, 0x1AU, 0x00U, 0x00U, 0x48U, 0x8BU, 0x0DU, 0x2DU,
        0x6DU, 0x3EU, 0x01U, 0x48U, 0x8BU, 0xD3U, 0x48U, 0x8BU,
        0x01U, 0xFFU, 0x90U, 0xE8U, 0x1AU, 0x00U, 0x00U, 0x48U,
        0x8BU, 0x0DU, 0x7AU, 0xB8U, 0x3EU, 0x01U, 0xE8U, 0xEDU,
        0x10U, 0xACU, 0xFFU
    };
    unsigned char changed_call_block[16];
    unsigned char changed_fx_block[10];
    unsigned char changed_stock_premount_block[83];
    float matrix[12] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
        10.0f, 20.0f, 30.0f
    };
    union FloatBits { unsigned long bits; float value; } invalid;
    float helper_forward[3] = {1.0f, -2.0f, 3.0f};
    float camera_forward[3] = {0.0f, 0.0f, 0.0f};
    int replacement;
    int displacement = 0;
    unsigned long fx_backing_rva = 0;
    unsigned long tools_global_rva = 0;
    char engine_content_name[256];
    char level_a;
    char level_b;
    CHECK(dluma_custom_main_menu_boot_receipt_blocks(
        "author.menu", "abcdef", "AUTHOR.MENU", "ABCDEF", "pending"));
    CHECK(!dluma_custom_main_menu_boot_receipt_blocks(
        "author.menu", "abcdef", "author.menu", "abcdef", "good"));
    CHECK(!dluma_custom_main_menu_boot_receipt_blocks(
        "author.menu", "abcdef", "author.menu", "changed", "pending"));
    CHECK(!dluma_custom_main_menu_boot_receipt_blocks(
        "author.menu", "abcdef", "other.menu", "abcdef", "pending"));

    CHECK(dluma_custom_main_menu_runtime_profile_identity_valid(
        DLUMA_CMM_SUPPORTED_PLAYER_SHA256,
        DLUMA_CMM_SUPPORTED_GAMEDLL_SHA256,
        DLUMA_CMM_SUPPORTED_ENGINE_TIMESTAMP,
        DLUMA_CMM_SUPPORTED_ENGINE_IMAGE_SIZE,
        DLUMA_CMM_SUPPORTED_GAMEDLL_TIMESTAMP,
        DLUMA_CMM_SUPPORTED_GAMEDLL_IMAGE_SIZE));
    CHECK(!dluma_custom_main_menu_runtime_profile_identity_valid(
        "0", DLUMA_CMM_SUPPORTED_GAMEDLL_SHA256,
        DLUMA_CMM_SUPPORTED_ENGINE_TIMESTAMP,
        DLUMA_CMM_SUPPORTED_ENGINE_IMAGE_SIZE,
        DLUMA_CMM_SUPPORTED_GAMEDLL_TIMESTAMP,
        DLUMA_CMM_SUPPORTED_GAMEDLL_IMAGE_SIZE));
    CHECK(!dluma_custom_main_menu_runtime_profile_identity_valid(
        DLUMA_CMM_SUPPORTED_PLAYER_SHA256, "0",
        DLUMA_CMM_SUPPORTED_ENGINE_TIMESTAMP,
        DLUMA_CMM_SUPPORTED_ENGINE_IMAGE_SIZE,
        DLUMA_CMM_SUPPORTED_GAMEDLL_TIMESTAMP,
        DLUMA_CMM_SUPPORTED_GAMEDLL_IMAGE_SIZE));
    CHECK(dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 60, 1, 1, 1, matrix, 12U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        0, 60, 60, 1, 1, 1, matrix, 12U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 61, 1, 1, 1, matrix, 12U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 60, 0, 1, 1, matrix, 12U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 60, 1, 0, 1, matrix, 12U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 60, 1, 1, 0, matrix, 12U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 60, 1, 1, 1, matrix, 11U, 46.8f));
    CHECK(!dluma_custom_main_menu_runtime_camera_values_valid(
        1, 60, 60, 1, 1, 1, matrix, 12U, 0.0f));
    CHECK(dluma_custom_main_menu_runtime_editor_camera_forward(
        helper_forward, camera_forward));
    CHECK(camera_forward[0] == -1.0f &&
          camera_forward[1] == 2.0f &&
          camera_forward[2] == -3.0f);
    CHECK(!dluma_custom_main_menu_runtime_editor_camera_forward(
        0, camera_forward));
    CHECK(dluma_custom_main_menu_runtime_texture_registration_matches(
        &level_a, &level_a, "abcdef", "abcdef", 1));
    CHECK(!dluma_custom_main_menu_runtime_texture_registration_matches(
        &level_a, &level_b, "abcdef", "abcdef", 1));
    CHECK(!dluma_custom_main_menu_runtime_texture_registration_matches(
        &level_a, &level_a, "abcdef", "ABCDEF", 1));
    CHECK(!dluma_custom_main_menu_runtime_texture_registration_matches(
        &level_a, &level_a, "abcdef", "abcdef", 0));
    CHECK(dluma_custom_main_menu_runtime_static_camera_bridge_enabled(0));
    CHECK(!dluma_custom_main_menu_runtime_static_camera_bridge_enabled(1));
    invalid.bits = 0x7F800000UL;
    matrix[0] = invalid.value;
    CHECK(!dluma_custom_main_menu_runtime_matrix_valid(matrix, 12U));
    matrix[0] = 10000001.0f;
    CHECK(!dluma_custom_main_menu_runtime_matrix_valid(matrix, 12U));
    memset(matrix, 0, sizeof(matrix));
    CHECK(!dluma_custom_main_menu_runtime_matrix_valid(matrix, 12U));

    CHECK(dluma_custom_main_menu_runtime_ttl_unchanged(
        &replacement, 5U, 8U, &replacement, 5U, 8U));
    CHECK(!dluma_custom_main_menu_runtime_ttl_unchanged(
        matrix, 5U, 8U, &replacement, 5U, 8U));
    CHECK(!dluma_custom_main_menu_runtime_ttl_unchanged(
        &replacement, 4U, 8U, &replacement, 5U, 8U));
    CHECK(dluma_custom_main_menu_runtime_hook_mutation_complete(1, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_hook_mutation_complete(0, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_hook_mutation_complete(1, 0, 1));
    CHECK(!dluma_custom_main_menu_runtime_hook_mutation_complete(1, 1, 0));
    CHECK(dluma_custom_main_menu_runtime_relative_call_valid(
        music_call_block, sizeof(music_call_block), 11U,
        0x011B7200UL, 0x0005AF0UL));
    memcpy(changed_call_block, music_call_block, sizeof(changed_call_block));
    changed_call_block[12] ^= 1U;
    CHECK(!dluma_custom_main_menu_runtime_relative_call_valid(
        changed_call_block, sizeof(changed_call_block), 11U,
        0x011B7200UL, 0x0005AF0UL));
    CHECK(!dluma_custom_main_menu_runtime_relative_call_valid(
        music_call_block, 14U, 11U, 0x011B7200UL, 0x0005AF0UL));
    CHECK(dluma_custom_main_menu_runtime_relative_call_displacement(
        0x10000010ULL, 0x10010000ULL, &displacement));
    CHECK(displacement == 0x0000FFF0);
    CHECK(dluma_custom_main_menu_runtime_relative_call_displacement(
        0x10000010ULL, 0x0FFF0000ULL, &displacement));
    CHECK(displacement == -0x00010010);
    CHECK(!dluma_custom_main_menu_runtime_relative_call_displacement(
        0x1000ULL, 0x80001000ULL, &displacement));
    CHECK(dluma_custom_main_menu_runtime_music_route_should_substitute(
        1, 1, 1, 1, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_music_route_should_substitute(
        0, 1, 1, 1, 1, 1)); /* MUSIC_INGAME passes through. */
    CHECK(!dluma_custom_main_menu_runtime_music_route_should_substitute(
        1, 0, 1, 1, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_music_route_should_substitute(
        1, 1, 0, 1, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_music_route_should_substitute(
        1, 1, 1, 0, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_music_route_should_substitute(
        1, 1, 1, 1, 0, 1));
    CHECK(!dluma_custom_main_menu_runtime_music_route_should_substitute(
        1, 1, 1, 1, 1, 0));
    /* Parent -> same-cue child preserves the existing wave. */
    CHECK(dluma_custom_main_menu_runtime_music_transition(
        "mes_extras", "mes_extras", "mes_menu") ==
        DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION);
    /* A different child switches while retaining the exclusive lease. */
    CHECK(dluma_custom_main_menu_runtime_music_transition(
        "mes_extras", "mes_options", "mes_menu") ==
        DLUMA_CMM_MUSIC_TRANSITION_SWITCH);
    /* A child without an override resumes normal music. */
    CHECK(dluma_custom_main_menu_runtime_music_transition(
        "mes_extras", 0, "mes_menu") ==
        DLUMA_CMM_MUSIC_TRANSITION_RELEASE);
    /* Returning to the parent after that release starts its cue again. */
    CHECK(dluma_custom_main_menu_runtime_music_transition(
        0, "mes_extras", "mes_menu") ==
        DLUMA_CMM_MUSIC_TRANSITION_START);
    /* An override equal to the effective normal cue is left untouched. */
    CHECK(dluma_custom_main_menu_runtime_music_transition(
        0, "mes_menu", "mes_menu") ==
        DLUMA_CMM_MUSIC_TRANSITION_NO_OPERATION);
    CHECK(dluma_custom_main_menu_runtime_empty_stack_grace_active(
        1000ULL, 1099ULL, 100ULL));
    CHECK(!dluma_custom_main_menu_runtime_empty_stack_grace_active(
        1000ULL, 1100ULL, 100ULL));
    CHECK(dluma_custom_main_menu_runtime_screen_script_matches(
        "MenuExtras.xui", "MenuExtras.xui"));
    CHECK(dluma_custom_main_menu_runtime_screen_script_matches(
        "data/menu/scr/menuextras", "MenuExtras.xui"));
    CHECK(dluma_custom_main_menu_runtime_screen_script_matches(
        "DATA\\MENU\\SCR\\MENUEXTRAS.XUI", "menuextras"));
    CHECK(!dluma_custom_main_menu_runtime_screen_script_matches(
        "MenuExtrasLeaderboards.xui", "MenuExtras.xui"));
    CHECK(dluma_custom_main_menu_runtime_screen_script_is_cursor(
        "data/menu/scr/UICursor.xui"));
    CHECK(dluma_custom_main_menu_runtime_screen_script_is_cursor(
        "uicursor"));
    CHECK(!dluma_custom_main_menu_runtime_screen_script_is_cursor(
        "MenuExtras.xui"));
    CHECK(dluma_custom_main_menu_runtime_fx_enable_contract_valid(
        fx_enable_block, sizeof(fx_enable_block),
        0x0018CE20UL, 0x00E8D000UL, &fx_backing_rva));
    CHECK(fx_backing_rva == 0x00A91624UL);
    memcpy(changed_fx_block, fx_enable_block, sizeof(changed_fx_block));
    changed_fx_block[5] ^= 1U;
    CHECK(!dluma_custom_main_menu_runtime_fx_enable_contract_valid(
        changed_fx_block, sizeof(changed_fx_block),
        0x0018CE20UL, 0x00E8D000UL, &fx_backing_rva));
    CHECK(!dluma_custom_main_menu_runtime_fx_enable_contract_valid(
        fx_enable_block, 9U, 0x0018CE20UL,
        0x00E8D000UL, &fx_backing_rva));
    CHECK(dluma_custom_main_menu_runtime_stock_premount_contract_valid(
        stock_premount_block, sizeof(stock_premount_block),
        0x0091C6C0UL, 0x02082000UL, &tools_global_rva));
    CHECK(tools_global_rva == 0x01D03428UL);
    CHECK(dluma_custom_main_menu_runtime_engine_content_name(
        "\\\\?\\C:\\example\\workshop\\SyntheticMenuProject",
        engine_content_name, sizeof(engine_content_name)));
    CHECK(strcmp(
        engine_content_name,
        "C:\\example\\workshop\\SyntheticMenuProject") == 0);
    CHECK(dluma_custom_main_menu_runtime_engine_content_name(
        "C:\\example\\workshop\\SyntheticMenuProject",
        engine_content_name, sizeof(engine_content_name)));
    CHECK(strcmp(
        engine_content_name,
        "C:\\example\\workshop\\SyntheticMenuProject") == 0);
    CHECK(dluma_custom_main_menu_runtime_engine_content_name(
        "\\\\?\\UNC\\server\\share\\SyntheticMenuProject",
        engine_content_name, sizeof(engine_content_name)));
    CHECK(strcmp(
        engine_content_name,
        "\\\\server\\share\\SyntheticMenuProject") == 0);
    CHECK(!dluma_custom_main_menu_runtime_engine_content_name(
        "\\\\?\\", engine_content_name, sizeof(engine_content_name)));
    CHECK(!dluma_custom_main_menu_runtime_engine_content_name(
        "\\\\?\\UNC\\server\\share", engine_content_name, 2U));
    CHECK(!dluma_custom_main_menu_runtime_engine_content_name(
        "X:\\SyntheticMenuProject", engine_content_name, 4U));
    CHECK(engine_content_name[0] == 0);
    {
        unsigned long long content_identity = 0ULL;
        CHECK(dluma_custom_main_menu_runtime_content_identity(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            &content_identity));
        CHECK(content_identity == 0x0123456789ABCDEFULL);
        CHECK(dluma_custom_main_menu_runtime_content_identity(
            "ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
            &content_identity));
        CHECK(content_identity == 0xABCDEF0123456789ULL);
        CHECK(dluma_custom_main_menu_runtime_content_identity(
            "0000000000000000000000000000000000000000000000000000000000000000",
            &content_identity));
        CHECK(content_identity == 1ULL);
        CHECK(!dluma_custom_main_menu_runtime_content_identity(
            "0123456789abcdef", &content_identity));
        CHECK(!dluma_custom_main_menu_runtime_content_identity(
            "g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            &content_identity));
        CHECK(!dluma_custom_main_menu_runtime_content_identity(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
            &content_identity));
    }

    memcpy(changed_stock_premount_block, stock_premount_block,
           sizeof(changed_stock_premount_block));
    changed_stock_premount_block[15] ^= 1U;
    CHECK(!dluma_custom_main_menu_runtime_stock_premount_contract_valid(
        changed_stock_premount_block,
        sizeof(changed_stock_premount_block),
        0x0091C6C0UL, 0x02082000UL, &tools_global_rva));
    memcpy(changed_stock_premount_block, stock_premount_block,
           sizeof(changed_stock_premount_block));
    changed_stock_premount_block[79] ^= 1U;
    CHECK(!dluma_custom_main_menu_runtime_stock_premount_contract_valid(
        changed_stock_premount_block,
        sizeof(changed_stock_premount_block),
        0x0091C6C0UL, 0x02082000UL, &tools_global_rva));
    CHECK(!dluma_custom_main_menu_runtime_stock_premount_contract_valid(
        stock_premount_block, 82U, 0x0091C6C0UL,
        0x02082000UL, &tools_global_rva));
    CHECK(!dluma_custom_main_menu_runtime_stock_premount_contract_valid(
        stock_premount_block, sizeof(stock_premount_block),
        0x02082000UL, 0x02082000UL, &tools_global_rva));
    CHECK(dluma_custom_main_menu_runtime_fx_restore_should_apply(
        1, 1, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_fx_restore_should_apply(
        0, 1, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_fx_restore_should_apply(
        1, 0, 1, 1));
    CHECK(!dluma_custom_main_menu_runtime_fx_restore_should_apply(
        1, 1, 0, 1));
    CHECK(!dluma_custom_main_menu_runtime_stability_ready(
        1000ULL, 2999ULL, 2000ULL, 1));
    CHECK(dluma_custom_main_menu_runtime_stability_ready(
        1000ULL, 3000ULL, 2000ULL, 1));
    CHECK(!dluma_custom_main_menu_runtime_stability_ready(
        1000ULL, 3000ULL, 2000ULL, 0));
    {
        int state = -1;
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER,
            DLUMA_CMM_PUBLICATION_EVENT_ENTER_UI_INITIALIZER, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_STOCK_PREMOUNT);
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            state, DLUMA_CMM_PUBLICATION_EVENT_PREMOUNT_COMPLETE, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_AWAITING_STOCK_COMPLETION);
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            state, DLUMA_CMM_PUBLICATION_EVENT_CATALOGS_VALID, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_CATALOG_VERIFIED);
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            state, DLUMA_CMM_PUBLICATION_EVENT_OVERLAY_MOUNTED, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_READY_BEFORE_UI);
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            state, DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND);
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            state, DLUMA_CMM_PUBLICATION_EVENT_ACTIVATE_COMPLETE, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_ACTIVE);
        CHECK(!dluma_custom_main_menu_runtime_publication_transition(
            state, DLUMA_CMM_PUBLICATION_EVENT_FAIL, &state));
        CHECK(!dluma_custom_main_menu_runtime_publication_transition(
            DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER,
            DLUMA_CMM_PUBLICATION_EVENT_LOAD_STARTED, &state));
        CHECK(dluma_custom_main_menu_runtime_publication_transition(
            DLUMA_CMM_PUBLICATION_AWAITING_STOCK_COMPLETION,
            DLUMA_CMM_PUBLICATION_EVENT_FAIL, &state));
        CHECK(state == DLUMA_CMM_PUBLICATION_STOCK_FALLBACK);
    }
    {
        int state = DLUMA_CMM_PUBLICATION_AWAITING_UI_INITIALIZER;
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 0, state, 0) == DLUMA_CMM_RESTORATION_WAIT);
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 1, state, 0) == DLUMA_CMM_RESTORATION_PUBLISH);
        /* A returning initializer may finish publication first.  The
         * pending restoration must continue with load, not republish. */
        state = DLUMA_CMM_PUBLICATION_READY_BEFORE_UI;
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 1, state, 0) == DLUMA_CMM_RESTORATION_LOAD);
        state = DLUMA_CMM_PUBLICATION_LOADING_BACKGROUND;
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 1, state, 0) == DLUMA_CMM_RESTORATION_WAIT);
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 1, state, 1) == DLUMA_CMM_RESTORATION_ACTIVATE);
        state = DLUMA_CMM_PUBLICATION_ACTIVE;
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 1, state, 1) == DLUMA_CMM_RESTORATION_PRESENTATION);
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            0, 1, state, 1) == DLUMA_CMM_RESTORATION_WAIT);
        CHECK(dluma_custom_main_menu_runtime_restoration_action(
            1, 1, DLUMA_CMM_PUBLICATION_STOCK_FALLBACK, 1) ==
                DLUMA_CMM_RESTORATION_WAIT);
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2)
    {
        if (!test_manifest_file(argv[1]))
            return 1;
        puts("custom main menu manifest file: PASS");
        return 0;
    }
    if (!test_manifest_acceptance() ||
        !test_manifest_rejections() ||
        !test_public_token_and_path_contract() ||
        !test_override_count_boundary() ||
        !test_texture_definition_contract() ||
        !test_bank_count_and_optional_music_contract() ||
        !test_xui_override_contracts() ||
        !test_binary_promotion() ||
        !test_receipt_and_runtime_contracts())
        return 1;
    puts("custom main menu core fake harness: PASS");
    return 0;
}
