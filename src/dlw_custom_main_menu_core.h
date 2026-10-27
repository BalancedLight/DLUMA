#ifndef DLUMA_CUSTOM_MAIN_MENU_CORE_H
#define DLUMA_CUSTOM_MAIN_MENU_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_CUSTOM_MENU_MANIFEST_MAX_BYTES 65536U
#define DLUMA_CUSTOM_MENU_MAX_CANDIDATES 128U
#define DLUMA_CUSTOM_MENU_MAX_OVERRIDES 128U
#define DLUMA_CUSTOM_MENU_MAX_MUSIC_OVERRIDES 2U
#define DLUMA_CUSTOM_MENU_MAX_BANKS 16U
#define DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS 32U
#define DLUMA_CUSTOM_MENU_ID_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_AUTHOR_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_NAME_CAPACITY 256U
#define DLUMA_CUSTOM_MENU_DESCRIPTION_CAPACITY 1024U
#define DLUMA_CUSTOM_MENU_VERSION_CAPACITY 64U
#define DLUMA_CUSTOM_MENU_MAP_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_OBJECT_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_BANK_NAME_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_PATH_CAPACITY 512U
#define DLUMA_CUSTOM_MENU_CUE_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_TEXTURE_DEF_NAME_CAPACITY 128U
#define DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY 192U

typedef struct DlumaCustomMainMenuOverride
{
    char name[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    char source_path[DLUMA_CUSTOM_MENU_PATH_CAPACITY];
    char destination[DLUMA_CUSTOM_MENU_XUI_NAME_CAPACITY];
    char music_override[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
} DlumaCustomMainMenuOverride;

typedef struct DlumaCustomMainMenuBank
{
    char name[DLUMA_CUSTOM_MENU_BANK_NAME_CAPACITY];
    char path[DLUMA_CUSTOM_MENU_PATH_CAPACITY];
} DlumaCustomMainMenuBank;

typedef struct DlumaCustomMainMenuTextureDef
{
    char name[DLUMA_CUSTOM_MENU_TEXTURE_DEF_NAME_CAPACITY];
    char source_path[DLUMA_CUSTOM_MENU_PATH_CAPACITY];
} DlumaCustomMainMenuTextureDef;

typedef enum DlumaCustomMainMenuMusicTarget
{
    DLUMA_CUSTOM_MENU_MUSIC_INVALID = 0,
    DLUMA_CUSTOM_MENU_MUSIC_MAIN,
    DLUMA_CUSTOM_MENU_MUSIC_CREDITS
} DlumaCustomMainMenuMusicTarget;

typedef struct DlumaCustomMainMenuMusicOverride
{
    char name[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
    char overrides[DLUMA_CUSTOM_MENU_CUE_CAPACITY];
    unsigned int target;
} DlumaCustomMainMenuMusicOverride;

typedef struct DlumaCustomMainMenuManifest
{
    char id[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    char author[DLUMA_CUSTOM_MENU_AUTHOR_CAPACITY];
    char name[DLUMA_CUSTOM_MENU_NAME_CAPACITY];
    char description[DLUMA_CUSTOM_MENU_DESCRIPTION_CAPACITY];
    char version[DLUMA_CUSTOM_MENU_VERSION_CAPACITY];
    char map[DLUMA_CUSTOM_MENU_MAP_CAPACITY];
    char camera_name[DLUMA_CUSTOM_MENU_OBJECT_CAPACITY];
    int camera_id;
    /* Bank-zero compatibility mirrors for existing internal callers. */
    char bank_name[DLUMA_CUSTOM_MENU_BANK_NAME_CAPACITY];
    char bank_path[DLUMA_CUSTOM_MENU_PATH_CAPACITY];
    DlumaCustomMainMenuBank banks[DLUMA_CUSTOM_MENU_MAX_BANKS];
    unsigned int bank_count;
    DlumaCustomMainMenuMusicOverride
        music_overrides[DLUMA_CUSTOM_MENU_MAX_MUSIC_OVERRIDES];
    unsigned int music_override_count;
    DlumaCustomMainMenuOverride overrides[DLUMA_CUSTOM_MENU_MAX_OVERRIDES];
    unsigned int override_count;
    DlumaCustomMainMenuTextureDef
        texture_defs[DLUMA_CUSTOM_MENU_MAX_TEXTURE_DEFS];
    unsigned int texture_def_count;
    unsigned char is_3d_menu_enabled;
    unsigned char does_use_3d_camera_movement;
    unsigned char use_custom_banks;
    unsigned char use_custom_textures;
} DlumaCustomMainMenuManifest;

int dluma_custom_main_menu_manifest_parse(
    const void *bytes,
    unsigned int size,
    DlumaCustomMainMenuManifest *manifest,
    char *diagnostic,
    unsigned int diagnostic_capacity);

int dluma_custom_main_menu_id_valid(const char *id);
int dluma_custom_main_menu_asset_token_valid(const char *value);
int dluma_custom_main_menu_relative_path_valid(const char *path);
int dluma_custom_main_menu_id_equal(const char *left, const char *right);

int dluma_custom_main_menu_resolve_override_destination(
    const char *target,
    char *destination,
    unsigned int destination_capacity);

int dluma_custom_main_menu_xui_contract_valid(
    const unsigned char *bytes,
    unsigned int size);

int dluma_custom_main_menu_xui_music_override(
    const unsigned char *bytes,
    unsigned int size,
    char *cue_out,
    unsigned int cue_capacity);

int dluma_custom_main_menu_promote_root_record(
    const unsigned char *input,
    unsigned int input_size,
    unsigned char *output,
    unsigned int output_capacity,
    unsigned int *output_size,
    int *already_promoted,
    char *diagnostic,
    unsigned int diagnostic_capacity);

int dluma_custom_main_menu_boot_receipt_blocks(
    const char *selected_id,
    const char *selected_fingerprint,
    const char *receipt_id,
    const char *receipt_fingerprint,
    const char *receipt_state);

#ifdef __cplusplus
}
#endif

#endif
