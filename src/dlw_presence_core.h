#ifndef DLUMA_PRESENCE_CORE_H
#define DLUMA_PRESENCE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_PRESENCE_TEXT_CAPACITY 513U
#define DLUMA_STEAM_STATUS_MAX_UTF8_BYTES 255U
#define DLUMA_STEAM_STATUS_CAPACITY \
    (DLUMA_STEAM_STATUS_MAX_UTF8_BYTES + 1U)

typedef enum DlumaPresenceContextKind {
    DLUMA_PRESENCE_CONTEXT_NONE = 0,
    DLUMA_PRESENCE_CONTEXT_FRONTEND = 1,
    DLUMA_PRESENCE_CONTEXT_GAMEPLAY = 2,
    DLUMA_PRESENCE_CONTEXT_EDITOR = 3
} DlumaPresenceContextKind;

#define DLUMA_PRESENCE_HAS_QUEST 0x01U
#define DLUMA_PRESENCE_PAUSE_KNOWN 0x02U
#define DLUMA_PRESENCE_PAUSED 0x04U

/*
 * Service-neutral, publisher-owned text snapshot. Discord-specific images,
 * timestamps, and buttons deliberately remain outside this contract.
 */
typedef struct DlumaPresenceSnapshot {
    unsigned int struct_size;
    unsigned int context;
    unsigned int flags;
    unsigned int reserved;
    char primary_text[DLUMA_PRESENCE_TEXT_CAPACITY];
    char secondary_text[DLUMA_PRESENCE_TEXT_CAPACITY];
    char pause_text[DLUMA_PRESENCE_TEXT_CAPACITY];
} DlumaPresenceSnapshot;

int dluma_presence_snapshot_equal(
    const DlumaPresenceSnapshot* left,
    const DlumaPresenceSnapshot* right);

/* Copies only safe display text. Invalid UTF-8, controls, paths, URLs/domains,
 * and standalone long numeric identifiers are omitted. */
int dluma_presence_sanitize_text(
    const char* input,
    char* output,
    unsigned int output_capacity);

/* Builds the loader-owned Steam `status` fallback. The result is valid UTF-8,
 * contains no controls, paths, URLs, or standalone long numeric identifiers,
 * and never exceeds 255 bytes or splits a code point. */
int dluma_presence_format_steam_status(
    const DlumaPresenceSnapshot* snapshot,
    char* output,
    unsigned int output_capacity);

#ifdef __cplusplus
}
#endif

#endif
