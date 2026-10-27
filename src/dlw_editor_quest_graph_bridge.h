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

#ifdef __cplusplus
}
#endif

#endif
