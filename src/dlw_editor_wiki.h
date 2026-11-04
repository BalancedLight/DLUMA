#ifndef DLUMA_EDITOR_WIKI_H
#define DLUMA_EDITOR_WIKI_H
#include "dlw_editor_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_wiki_start(const WCHAR *theme_config);
int dluma_editor_wiki_stop(void);
int dluma_editor_wiki_callbacks_drained(void);
#ifdef __cplusplus
}
#endif
#endif
