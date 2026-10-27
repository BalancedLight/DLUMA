#ifndef DLUMA_GAME_BACKEND_H
#define DLUMA_GAME_BACKEND_H

#include "../core/dlw_api_runtime.h"
#include "dlw_game_host_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the one language-neutral frontend over the existing exact-build
 * adapters. Language hosts never receive binding RVAs, engine pointers, or
 * Lua-branded gameplay records. */
void dluma_game_backend_initialize(DlumaApiBackendV1* backend);
void dluma_game_backend_initialize_for_host(
    DlumaApiBackendV1* backend,
    DlumaGameHostContext* host_context);
void dluma_game_backend_prepare_capabilities(
    DlumaCapabilityRegistry* registry);

#ifdef __cplusplus
}
#endif

#endif
