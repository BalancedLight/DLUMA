#ifndef DLW_CHARACTER_STATE_STORE_H
#define DLW_CHARACTER_STATE_STORE_H
#include <stddef.h>
#include "../core/dlw_character_transition.h"

/* Internal Windows journal storage; the owning backend supplies a trusted,
 * serialized, per-scope path and stable save identity. SHA-256 detects damage,
 * not malicious tampering. No engine object or pointer is persisted. */
#define DLUMA_CHARACTER_STATE_SECTION_MAX 65536u
typedef struct DlumaCharacterStateRecord {
    DlumaCharacterJournal journal;
    unsigned char save_identity[32];
    unsigned int boundary_open, source_size, target_size, world_size;
    unsigned char source[DLUMA_CHARACTER_STATE_SECTION_MAX];
    unsigned char target[DLUMA_CHARACTER_STATE_SECTION_MAX];
    unsigned char world[DLUMA_CHARACTER_STATE_SECTION_MAX];
} DlumaCharacterStateRecord;
int dluma_character_state_store_create(const wchar_t*, const DlumaCharacterStateRecord*);
int dluma_character_state_store_update(const wchar_t*, const DlumaCharacterStateRecord*);
int dluma_character_state_store_load(const wchar_t*, const unsigned char save_identity[32], DlumaCharacterStateRecord*);
#endif
