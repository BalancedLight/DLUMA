#ifndef DLUMA_EDITOR_QUEST_GRAPH_BRIDGE_H
#define DLUMA_EDITOR_QUEST_GRAPH_BRIDGE_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Editor-only visual PXSL bridge. The bridge is deliberately pointer-free:
 * it recognizes the stock PXSL dialog by its public child-window signature,
 * exchanges text through Scintilla messages, and routes compilation through
 * the stock MFC command path. */
int dluma_editor_quest_graph_bridge_start(void);
int dluma_editor_quest_graph_bridge_stop(void);
int dluma_editor_quest_graph_bridge_callbacks_drained(void);

/* Internal native compiler boundary. The caller must hold its tracked Editor
 * callback; no Graph panel, pipe session or active map is required. Source
 * identity is canonical; UTF-8 output has a validated source-owned parent. */
int dluma_editor_quest_resolve_compile_paths(
    const char* native_source, const char* native_output,
    int editor_callback_tracked,
    WCHAR* project_root, unsigned int project_capacity,
    char* source_absolute, unsigned int source_capacity,
    char* output_absolute, unsigned int output_capacity);
int dluma_editor_quest_native_filename_from_utf8(
    const char* path, char* output, unsigned int capacity);

#ifdef __cplusplus
}
#endif

#endif
