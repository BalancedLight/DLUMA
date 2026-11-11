#ifndef DLW_CHARACTER_LEVELS_H
#define DLW_CHARACTER_LEVELS_H
#include <stddef.h>
#include <stdint.h>

#define DLUMA_CHARACTER_LEVEL_COUNT 8u
#define DLUMA_CHARACTER_LEVELS_CANONICAL_BYTES 205u
#define DLUMA_CHARACTER_LEVELS_MAX_INPUT_BYTES 272u

typedef struct DlumaCharacterLevel {
    uint64_t total_xp;
    uint32_t current_xp;
    uint16_t points, level;
} DlumaCharacterLevel;
typedef struct DlumaCharacterLevels {
    DlumaCharacterLevel levels[DLUMA_CHARACTER_LEVEL_COUNT];
} DlumaCharacterLevels;
typedef enum DlumaCharacterLevelsResult {
    DLUMA_LEVELS_OK, DLUMA_LEVELS_INVALID, DLUMA_LEVELS_UNSUPPORTED,
    DLUMA_LEVELS_NO_SPACE, DLUMA_LEVELS_OVERFLOW
} DlumaCharacterLevelsResult;

/* Copied scalar data only. No game object pointer, native capture/load/apply,
 * save reset, or world/quest data is represented. No field is inferred from a
 * native struct's sizeof/padding. Callers provide valid accessible C buffers.
 *
 * DLUMA-owned LE wire v1 (not native fs::ifile encoding):
 * magic DCLV[4], version:u16=1, headerBytes:u16=16, count:u16=8,
 * currentXpBytes:u16=4 (canonical) or8 (lossless legacy import), reserved:u32=0.
 * Each record: nameBytes:u8, name (no NUL), points:u16, level:u16,
 * totalXp:u64, currentXp:u32/u64. Exactly one of each of the eight known slots
 * is required. StatusLevel aliases Status deliberately; duplicates reject.
 * Encoder uses canonical names/order and32-bit current XP. Decoder accepts
 * reordered records and the alias, but rejects unknown names, missing/duplicate
 * slots, trailing/truncated input, unsupported headers and64->32 overflow.
 * The saved legacy64-bit current XP is NOT an inline native64-bit array.
 * Decode failure leaves *out unchanged; encode failure leaves bytes unchanged
 * and sets *written=0. Buffer/written storage must not overlap.
 * Scope/save identity and durable integrity/authentication belong to the owning
 * transition journal, not this codec. Success does not establish playable state.
 */
DlumaCharacterLevelsResult dluma_character_levels_encode(
    const DlumaCharacterLevels*, void* bytes, size_t capacity, size_t* written);
DlumaCharacterLevelsResult dluma_character_levels_decode(
    const void* bytes, size_t size, DlumaCharacterLevels* out);
#endif
