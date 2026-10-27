#ifndef DLUMA_CUSTOM_MAP_LOADING_CORE_H
#define DLUMA_CUSTOM_MAP_LOADING_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_LOADING_DIRECTIVE_TITLE       0x01U
#define DLUMA_LOADING_DIRECTIVE_OBJECTIVE   0x02U
#define DLUMA_LOADING_DIRECTIVE_DESCRIPTION 0x04U
#define DLUMA_LOADING_DIRECTIVE_VISUAL      0x08U

#define DLUMA_LOADING_TEXTURE_PATH_CAPACITY 512U
#define DLUMA_LOADING_TEXTURE_ALIAS_CAPACITY 128U

typedef struct DlumaCustomMapLoadingCrop
{
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
} DlumaCustomMapLoadingCrop;

typedef struct DlumaCustomMapLoadingTextureDefinition
{
    char texture[DLUMA_LOADING_TEXTURE_PATH_CAPACITY];
    char alias[DLUMA_LOADING_TEXTURE_ALIAS_CAPACITY];
    unsigned int texture_width;
    unsigned int texture_height;
    unsigned int left;
    unsigned int top;
    unsigned int right;
    unsigned int bottom;
    unsigned char whole;
} DlumaCustomMapLoadingTextureDefinition;

unsigned int dluma_custom_map_loading_directive_mask(const char *name);

unsigned int dluma_custom_map_loading_generated_mask(
    int workshop_presentation_enabled,
    unsigned int authored_directives,
    int preview_ready);

int dluma_custom_map_loading_description_only_needed(
    int workshop_presentation_enabled,
    int content_fix_enabled,
    unsigned int authored_directives,
    int generated_description,
    int objective_has_text,
    int description_has_text);

int dluma_custom_map_loading_plain_text(
    const char *input, unsigned int input_size,
    char *output, unsigned int output_capacity);

int dluma_custom_map_loading_cover_crop(
    unsigned int source_width, unsigned int source_height,
    unsigned int target_width, unsigned int target_height,
    DlumaCustomMapLoadingCrop *out);

int dluma_custom_map_loading_parse_texture_definition(
    const char *text, unsigned int size, const char *selected_alias,
    DlumaCustomMapLoadingTextureDefinition *out);

int dluma_custom_map_loading_resource_path_valid(
    const char *path, unsigned int capacity);

#ifdef __cplusplus
}
#endif

#endif
