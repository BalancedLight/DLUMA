#ifndef DLUMA_EDITOR_THEME_PROFILE_H
#define DLUMA_EDITOR_THEME_PROFILE_H
#include <stddef.h>
#include <stdint.h>
enum {
    DLUMA_EDITOR_THEME_PROFILE_EDITOR,
    DLUMA_EDITOR_THEME_PROFILE_QUEST,
    DLUMA_EDITOR_THEME_PROFILE_FRAMEWORK,
    DLUMA_EDITOR_THEME_PROFILE_MFC
};
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_profile_admit(unsigned int profile);
void *dluma_editor_theme_profile_address(unsigned int profile,
                                        uintptr_t rva, size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
