#include "../src/dlw_custom_map_loading_core.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_directives(void)
{
    assert(dluma_custom_map_loading_directive_mask("LevelName") ==
           DLUMA_LOADING_DIRECTIVE_TITLE);
    assert(dluma_custom_map_loading_directive_mask(
               "levelnamelocalizationid") ==
           DLUMA_LOADING_DIRECTIVE_TITLE);
    assert(dluma_custom_map_loading_directive_mask(
               "LoadingForceObjectiveText") ==
           DLUMA_LOADING_DIRECTIVE_OBJECTIVE);
    assert(dluma_custom_map_loading_directive_mask(
               "LoadingForceObjectiveDescText") ==
           DLUMA_LOADING_DIRECTIVE_DESCRIPTION);
    assert(dluma_custom_map_loading_directive_mask("LoadingBackground") ==
           DLUMA_LOADING_DIRECTIVE_VISUAL);
    assert(dluma_custom_map_loading_directive_mask("LoadingMaterial") ==
           DLUMA_LOADING_DIRECTIVE_VISUAL);
    assert(dluma_custom_map_loading_directive_mask("LoadingMusic") == 0U);
}

static void test_setting_matrix_and_independent_overrides(void)
{
    const unsigned int all =
        DLUMA_LOADING_DIRECTIVE_TITLE |
        DLUMA_LOADING_DIRECTIVE_OBJECTIVE |
        DLUMA_LOADING_DIRECTIVE_DESCRIPTION |
        DLUMA_LOADING_DIRECTIVE_VISUAL;
    assert(dluma_custom_map_loading_generated_mask(0, 0U, 1) == 0U);
    assert(dluma_custom_map_loading_generated_mask(1, 0U, 1) == all);
    assert(dluma_custom_map_loading_generated_mask(1, 0U, 0) ==
           (all & ~DLUMA_LOADING_DIRECTIVE_VISUAL));
    assert(dluma_custom_map_loading_generated_mask(
               1, DLUMA_LOADING_DIRECTIVE_TITLE, 1) ==
           (all & ~DLUMA_LOADING_DIRECTIVE_TITLE));
    assert(dluma_custom_map_loading_generated_mask(
               1, DLUMA_LOADING_DIRECTIVE_OBJECTIVE, 1) ==
           (all & ~DLUMA_LOADING_DIRECTIVE_OBJECTIVE));
    assert(dluma_custom_map_loading_generated_mask(
               1, DLUMA_LOADING_DIRECTIVE_DESCRIPTION, 1) ==
           (all & ~DLUMA_LOADING_DIRECTIVE_DESCRIPTION));
    assert(dluma_custom_map_loading_generated_mask(
               1, DLUMA_LOADING_DIRECTIVE_VISUAL, 1) ==
           (all & ~DLUMA_LOADING_DIRECTIVE_VISUAL));

    assert(!dluma_custom_map_loading_description_only_needed(
        0, 0, 0U, 0, 0, 1));
    assert(dluma_custom_map_loading_description_only_needed(
        1, 0, 0U, 1, 0, 1));
    assert(dluma_custom_map_loading_description_only_needed(
        0, 1, DLUMA_LOADING_DIRECTIVE_DESCRIPTION, 0, 0, 1));
    assert(dluma_custom_map_loading_description_only_needed(
        1, 1, DLUMA_LOADING_DIRECTIVE_DESCRIPTION, 0, 0, 1));
    assert(!dluma_custom_map_loading_description_only_needed(
        1, 1, DLUMA_LOADING_DIRECTIVE_DESCRIPTION, 0, 1, 1));
    assert(!dluma_custom_map_loading_description_only_needed(
        1, 1, DLUMA_LOADING_DIRECTIVE_DESCRIPTION, 0, 0, 0));
}

static void test_plain_text(void)
{
    char output[256];
    char maximum[8001];
    char maximum_output[8001];
    const char formatted[] =
        "[h1]Heading[/h1]\r\n"
        "[b]Bold[/b] &amp; <i>plain</i><br>next\tline";
    const char list[] =
        "Literal [not a tag][list][*]one[*]two[/list]"
        "\xC2\x85" "line" "\xE2\x80\xA8" "break";
    const char invalid[] = "bad\xC0\xAF";
    assert(dluma_custom_map_loading_plain_text(
        formatted, (unsigned int)strlen(formatted),
        output, (unsigned int)sizeof(output)));
    assert(strcmp(output, "Heading\nBold & plain\nnext line") == 0);
    assert(dluma_custom_map_loading_plain_text(
        list, (unsigned int)strlen(list),
        output, (unsigned int)sizeof(output)));
    assert(strcmp(
        output,
        "Literal [not a tag]\n- one\n- two\nline\nbreak") == 0);
    assert(!dluma_custom_map_loading_plain_text(
        invalid, (unsigned int)sizeof(invalid) - 1U,
        output, (unsigned int)sizeof(output)));
    assert(!output[0]);
    assert(dluma_custom_map_loading_plain_text(
        "", 0U, output, (unsigned int)sizeof(output)));
    assert(!output[0]);
    memset(maximum, 'a', sizeof(maximum) - 2U);
    maximum[sizeof(maximum) - 2U] = 0;
    maximum[sizeof(maximum) - 1U] = 0;
    assert(dluma_custom_map_loading_plain_text(
        maximum, (unsigned int)strlen(maximum),
        maximum_output, (unsigned int)sizeof(maximum_output)));
    assert(strlen(maximum_output) == sizeof(maximum) - 2U);
}

static void test_crop(void)
{
    DlumaCustomMapLoadingCrop crop;
    assert(dluma_custom_map_loading_cover_crop(
        512U, 512U, 16U, 9U, &crop));
    assert(crop.x == 0U && crop.y == 112U);
    assert(crop.width == 512U && crop.height == 288U);
    assert(dluma_custom_map_loading_cover_crop(
        1920U, 1080U, 16U, 9U, &crop));
    assert(crop.x == 0U && crop.y == 0U);
    assert(crop.width == 1920U && crop.height == 1080U);
    assert(dluma_custom_map_loading_cover_crop(
        2048U, 1024U, 16U, 9U, &crop));
    assert(crop.x == 114U && crop.y == 0U);
    assert(crop.width == 1820U && crop.height == 1024U);
    assert(!dluma_custom_map_loading_cover_crop(
        0U, 512U, 16U, 9U, &crop));
}

static void test_texture_definitions(void)
{
    static const char valid[] =
        "// generated\n"
        "!Texture(s, i, i)\n"
        "!HiddenInEditor(i)\n"
        "!Rect(s, i, i, i, i)\n"
        "!RectWithCorner(s, i, i, i, i, i, i)\n"
        "!Whole(s)\n"
        "Texture(\"menu/custom_loading.dds\", 2048, 1024)\n"
        "{\n"
        "  Rect(\"custom_rect\", 128, 64, 1920, 1000)\n"
        "  Whole(\"custom_whole\")\n"
        "}\n";
    static const char duplicate[] =
        "Texture(\"a.dds\", 4, 4) { Whole(\"same\") }\n"
        "Texture(\"b.dds\", 8, 8) { Whole(\"same\") }\n";
    static const char invalid_rect[] =
        "Texture(\"a.dds\", 4, 4) { Rect(\"bad\", 0, 0, 5, 4) }\n";
    static const char malformed[] =
        "Texture(\"a.dds\", 4, 4) { Whole(\"broken\")\n";
    DlumaCustomMapLoadingTextureDefinition definition;

    assert(dluma_custom_map_loading_parse_texture_definition(
        valid, (unsigned int)strlen(valid), "custom_rect", &definition));
    assert(strcmp(definition.texture, "menu/custom_loading.dds") == 0);
    assert(strcmp(definition.alias, "custom_rect") == 0);
    assert(definition.texture_width == 2048U);
    assert(definition.texture_height == 1024U);
    assert(definition.left == 128U && definition.top == 64U);
    assert(definition.right == 1920U && definition.bottom == 1000U);
    assert(!definition.whole);

    assert(dluma_custom_map_loading_parse_texture_definition(
        valid, (unsigned int)strlen(valid), "custom_whole", &definition));
    assert(definition.whole);
    assert(definition.left == 0U && definition.top == 0U);
    assert(definition.right == 2048U && definition.bottom == 1024U);

    assert(!dluma_custom_map_loading_parse_texture_definition(
        duplicate, (unsigned int)strlen(duplicate), "same", &definition));
    assert(!dluma_custom_map_loading_parse_texture_definition(
        invalid_rect, (unsigned int)strlen(invalid_rect), "bad", &definition));
    assert(!dluma_custom_map_loading_parse_texture_definition(
        malformed, (unsigned int)strlen(malformed), "broken", &definition));
    assert(!dluma_custom_map_loading_parse_texture_definition(
        valid, (unsigned int)strlen(valid), "missing", &definition));
}

static void test_resource_paths(void)
{
    assert(dluma_custom_map_loading_resource_path_valid(
        "menu\\custom_loading.dds", 512U));
    assert(dluma_custom_map_loading_resource_path_valid(
        "textures/loading.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "..\\outside.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "C:\\outside.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "\\\\server\\outside.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "menu\\NUL.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "menu\\trailing.\\bad.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "menu\\\\bad.dds", 512U));
    assert(!dluma_custom_map_loading_resource_path_valid(
        "menu\\", 512U));
}

int main(void)
{
    test_directives();
    test_setting_matrix_and_independent_overrides();
    test_plain_text();
    test_crop();
    test_texture_definitions();
    test_resource_paths();
    puts("custom_map_loading_fake_harness: ok");
    return 0;
}
