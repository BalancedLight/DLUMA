#ifndef DLUMA_DISCORD_PRESENCE_API_H
#define DLUMA_DISCORD_PRESENCE_API_H

/*
 * Private, copied-data-only ABI between the no-CRT loader and the isolated
 * Discord desktop RPC helper.  This is not a public MOD or Lua interface.
 */

#include <stdint.h>

#ifdef _WIN32
#define DLUMA_DISCORD_CALL __cdecl
#if defined(DLUMA_DISCORD_PRESENCE_HELPER_EXPORTS)
#define DLUMA_DISCORD_EXPORT __declspec(dllexport)
#else
#define DLUMA_DISCORD_EXPORT
#endif
#else
#define DLUMA_DISCORD_CALL
#define DLUMA_DISCORD_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_DISCORD_PRESENCE_ABI_VERSION 0x00040000u
#define DLUMA_DISCORD_PRESENCE_HELPER_RELEASE "dluma-discord-rpc-3"
#define DLUMA_DISCORD_RPC_TRANSPORT_RELEASE "discord-desktop-rpc-v1"
#define DLUMA_DISCORD_RPC_PROTOCOL_VERSION 1u

#define DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY 513u
#define DLUMA_DISCORD_ACTIVITY_IMAGE_CAPACITY 301u
#define DLUMA_DISCORD_ACTIVITY_BUTTON_LABEL_CAPACITY 129u
#define DLUMA_DISCORD_ACTIVITY_BUTTON_URL_CAPACITY 513u
#define DLUMA_DISCORD_STATUS_TEXT_CAPACITY 256u

#define DLUMA_DISCORD_ACTIVITY_HAS_STATE 0x00000001u
#define DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE 0x00000002u
#define DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP 0x00000004u
#define DLUMA_DISCORD_ACTIVITY_HAS_BUTTON 0x00000008u

typedef struct DlumaDiscordActivityV1 {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t generation;
    uint64_t start_timestamp_seconds;
    char details[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    char state[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    char large_image[DLUMA_DISCORD_ACTIVITY_IMAGE_CAPACITY];
    char large_text[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    char small_image[DLUMA_DISCORD_ACTIVITY_IMAGE_CAPACITY];
    char small_text[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    char button_label[DLUMA_DISCORD_ACTIVITY_BUTTON_LABEL_CAPACITY];
    char button_url[DLUMA_DISCORD_ACTIVITY_BUTTON_URL_CAPACITY];
} DlumaDiscordActivityV1;

typedef struct DlumaDiscordPresenceStatusV1 {
    uint32_t struct_size;
    uint32_t initialized;
    uint32_t connected;
    uint32_t ready;
    uint32_t responses_pending;
    uint32_t last_operation_succeeded;
    uint32_t rpc_protocol_version;
    char transport_release[32];
    char last_error[DLUMA_DISCORD_STATUS_TEXT_CAPACITY];
} DlumaDiscordPresenceStatusV1;

typedef int (DLUMA_DISCORD_CALL *DlumaDiscordInitializeFn)(uint64_t application_id);
typedef int (DLUMA_DISCORD_CALL *DlumaDiscordPublishFn)(
    const DlumaDiscordActivityV1* activity);
typedef int (DLUMA_DISCORD_CALL *DlumaDiscordClearFn)(void);
typedef int (DLUMA_DISCORD_CALL *DlumaDiscordPumpFn)(void);
typedef int (DLUMA_DISCORD_CALL *DlumaDiscordStatusFn)(
    DlumaDiscordPresenceStatusV1* status);
typedef int (DLUMA_DISCORD_CALL *DlumaDiscordQuiesceFn)(uint32_t timeout_ms);

typedef struct DlumaDiscordPresenceApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* helper_release;
    const char* transport_release;
    uint32_t rpc_protocol_version;
    DlumaDiscordInitializeFn initialize;
    DlumaDiscordPublishFn publish;
    DlumaDiscordClearFn clear;
    DlumaDiscordPumpFn pump;
    DlumaDiscordStatusFn status;
    DlumaDiscordQuiesceFn quiesce;
} DlumaDiscordPresenceApiV1;

typedef int (DLUMA_DISCORD_CALL *DlumaDiscordPresenceGetApiFn)(
    uint32_t requested_abi,
    uint32_t caller_table_size,
    const DlumaDiscordPresenceApiV1** api_out);

DLUMA_DISCORD_EXPORT int DLUMA_DISCORD_CALL DLUMA_DiscordPresence_GetApi(
    uint32_t requested_abi,
    uint32_t caller_table_size,
    const DlumaDiscordPresenceApiV1** api_out);

#ifdef __cplusplus
}
#endif

#endif
