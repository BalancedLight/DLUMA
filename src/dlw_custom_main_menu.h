#ifndef DLUMA_CUSTOM_MAIN_MENU_H
#define DLUMA_CUSTOM_MAIN_MENU_H

#include "dlw_common.h"
#include "dlw_companion_archive.h"
#include "dlw_custom_main_menu_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The stock frontend level.  A selected custom menu is published under this
 * name so the engine loads it as its own menu level from loose files, instead
 * of the loader renaming the level the frontend asked for.  The level records
 * carry no internal name, so the identity is entirely the directory and file
 * names, and the loader's pak already ships the matching level-name-keyed
 * camera and 3D placement scripts.
 */
#define DLUMA_CUSTOM_MENU_STOCK_LEVEL "menu_lvl_new"
#define DLUMA_CUSTOM_MENU_STOCK_LEVEL_W L"menu_lvl_new"

typedef struct DlumaCustomMainMenuCandidateSnapshot
{
    char id[DLUMA_CUSTOM_MENU_ID_CAPACITY];
    char name[DLUMA_CUSTOM_MENU_NAME_CAPACITY];
    char author[DLUMA_CUSTOM_MENU_AUTHOR_CAPACITY];
    char description[DLUMA_CUSTOM_MENU_DESCRIPTION_CAPACITY];
    char version[DLUMA_CUSTOM_MENU_VERSION_CAPACITY];
    char map[DLUMA_CUSTOM_MENU_MAP_CAPACITY];
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char diagnostic[DLUMA_CUSTOM_MENU_DIAGNOSTIC_CAPACITY];
    char source_name[128];
    char workshop_id[64];
    char source_identity[512];
    WCHAR project_root[DLUMA_MAX_WPATH];
    unsigned char valid;
    unsigned char quarantined;
    unsigned char duplicate_id;
    unsigned char source_is_subscribed;
    unsigned char source_selected;
    unsigned char source_selection_missing;
} DlumaCustomMainMenuCandidateSnapshot;

void dluma_custom_main_menu_discover_and_prepare(void);
unsigned int dluma_custom_main_menu_snapshot(
    DlumaCustomMainMenuCandidateSnapshot *output,
    unsigned int capacity,
    unsigned int *total_out);

int dluma_custom_main_menu_select_next_launch(
    const char *id,
    const char *source_identity,
    char *diagnostic,
    unsigned int diagnostic_capacity);

int dluma_custom_main_menu_project_marked(const WCHAR *project_root);
int dluma_custom_main_menu_project_identity_marked(
    const char *project_root_utf8);

int dluma_custom_main_menu_selected_ready(void);
int dluma_custom_main_menu_selected_main_xui_alias(
    char *output, unsigned int capacity);
const DlumaCustomMainMenuManifest *dluma_custom_main_menu_selected_manifest(void);
const char *dluma_custom_main_menu_selected_id(void);
const char *dluma_custom_main_menu_requested_id(void);
const char *dluma_custom_main_menu_requested_source(void);
const char *dluma_custom_main_menu_selected_map(void);
const char *dluma_custom_main_menu_selected_fingerprint(void);
const WCHAR *dluma_custom_main_menu_selected_project_root(void);
const WCHAR *dluma_custom_main_menu_selected_overlay(void);
int dluma_custom_main_menu_selected_bank_alias(
    char *output, unsigned int capacity);
unsigned int dluma_custom_main_menu_selected_bank_count(void);
int dluma_custom_main_menu_selected_bank_alias_at(
    unsigned int index, char *output, unsigned int capacity);
int dluma_custom_main_menu_selected_bank_source_snapshot(
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity);
int dluma_custom_main_menu_selected_bank_source_snapshot_at(
    unsigned int index,
    char *path_out, unsigned int path_capacity,
    char *fingerprint_out, unsigned int fingerprint_capacity);
const DlumaCompanionArchiveAudioManifest *
dluma_custom_main_menu_selected_bank_catalog_at(unsigned int index);
int dluma_custom_main_menu_selected_source_pinned(void);

/* Call after the menu background and outgoing project consumers retire. */
int dluma_custom_main_menu_refresh_asset_sources(void);
int dluma_custom_main_menu_prepare_selected_sources(void);
int dluma_custom_main_menu_mount_selected_sources(void);
void dluma_custom_main_menu_retire_generation(void);
void dluma_custom_main_menu_disable_for_process(const char *reason);
void dluma_custom_main_menu_mark_activation_pending(void);
void dluma_custom_main_menu_mark_activation_good(void);

#ifdef __cplusplus
}
#endif

#endif
