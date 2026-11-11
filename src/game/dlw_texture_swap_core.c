#include "dlw_texture_swap_core.h"
#include <string.h>

#define SWAP_ACTIVE 1U
#define SWAP_RETIRING 2U

static int valid_name(const char* value)
{
    size_t i;
    if (!value || !value[0]) return 0;
    for (i = 0; i < DLUMA_TEXTURE_SWAP_NAME_CAPACITY; ++i)
        if (!value[i]) return 1;
    return 0;
}

static int equal_name(const char* left, const char* right)
{
    size_t left_size = strlen(left);
    size_t right_size = strlen(right);
    return left_size == right_size && !memcmp(left, right, left_size);
}

DlumaTextureSwapResult dluma_texture_swap_begin(
    DlumaTextureSwap* swap, uint64_t owner, uint32_t generation, uint32_t map,
    const DlumaTextureSwapPair* pairs, unsigned count,
    DlumaTextureSwapReady ready, void* user)
{
    DlumaTextureSwap pending;
    unsigned i, j;
    if (!swap || !owner || !pairs || !count || count > DLUMA_TEXTURE_SWAP_MAX_PAIRS || !ready)
        return DLUMA_TEXTURE_SWAP_INVALID;
    if (swap->state) return DLUMA_TEXTURE_SWAP_BUSY;
    memset(&pending, 0, sizeof(pending));
    for (i = 0; i < count; ++i) {
        if (!valid_name(pairs[i].original) || !valid_name(pairs[i].replacement))
            return DLUMA_TEXTURE_SWAP_INVALID;
        for (j = 0; j <= i; ++j) {
            if (!valid_name(pairs[j].original) || !valid_name(pairs[j].replacement))
                return DLUMA_TEXTURE_SWAP_INVALID;
            if (equal_name(pairs[i].original, pairs[j].replacement) ||
                equal_name(pairs[i].replacement, pairs[j].original) ||
                (i != j && (equal_name(pairs[i].original, pairs[j].original) ||
                            equal_name(pairs[i].replacement, pairs[j].replacement))))
                return DLUMA_TEXTURE_SWAP_INVALID;
        }
        memcpy(pending.entries[i].original, pairs[i].original, strlen(pairs[i].original) + 1);
        memcpy(pending.entries[i].replacement, pairs[i].replacement, strlen(pairs[i].replacement) + 1);
    }
    for (i = 0; i < count; ++i)
        if (!ready(user, pending.entries[i].original) || !ready(user, pending.entries[i].replacement))
            return DLUMA_TEXTURE_SWAP_NOT_READY;
    pending.owner = owner;
    pending.owner_generation = generation;
    pending.map_generation = map;
    pending.count = count;
    pending.state = SWAP_ACTIVE;
    *swap = pending;
    return DLUMA_TEXTURE_SWAP_OK;
}

DlumaTextureSwapResult dluma_texture_swap_stop(DlumaTextureSwap* swap, uint64_t owner,
                                               uint32_t generation, uint32_t map)
{
    if (!swap || !owner) return DLUMA_TEXTURE_SWAP_INVALID;
    if (!swap->state) return DLUMA_TEXTURE_SWAP_OK;
    if (swap->owner != owner) return DLUMA_TEXTURE_SWAP_FOREIGN;
    if (swap->owner_generation != generation || swap->map_generation != map)
        return DLUMA_TEXTURE_SWAP_STALE;
    if (!swap->target_generation) {
        memset(swap, 0, sizeof(*swap));
        return DLUMA_TEXTURE_SWAP_OK;
    }
    swap->state = SWAP_RETIRING;
    return DLUMA_TEXTURE_SWAP_RETRY;
}

DlumaTextureCallResult dluma_texture_swap_dispatch(
    DlumaTextureSwap* swap, int scoped, uint32_t map, uint64_t generation,
    const char* original, DlumaTextureSwapSet set, void* user)
{
    unsigned i;
    int result;
    if (!swap || !valid_name(original) || !set) return DLUMA_TEXTURE_CALL_FAILED;
    if (!scoped || !swap->state) return set(user, original)
        ? DLUMA_TEXTURE_CALL_ORIGINAL : DLUMA_TEXTURE_CALL_FAILED;
    if (map != swap->map_generation) {
        result = set(user, original);
        if (swap->state == SWAP_RETIRING)
            return result ? DLUMA_TEXTURE_CALL_RESTORE_PENDING
                          : DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED;
        return result ? DLUMA_TEXTURE_CALL_ORIGINAL : DLUMA_TEXTURE_CALL_FAILED;
    }
    if (swap->state == SWAP_RETIRING) {
        result = set(user, original);
        if (result && generation && generation == swap->target_generation) {
            memset(swap, 0, sizeof(*swap));
            return DLUMA_TEXTURE_CALL_ORIGINAL;
        }
        return result ? DLUMA_TEXTURE_CALL_RESTORE_PENDING
                      : DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED;
    }
    if (!generation || (swap->target_generation && swap->target_generation != generation))
        return set(user, original) ? DLUMA_TEXTURE_CALL_ORIGINAL : DLUMA_TEXTURE_CALL_FAILED;
    for (i = 0; i < swap->count; ++i) {
        if (!equal_name(swap->entries[i].original, original)) continue;
        /* A failed setter may already mutate the image; track before calling. */
        swap->target_generation = generation;
        if (set(user, swap->entries[i].replacement)) return DLUMA_TEXTURE_CALL_REPLACED;
        if (set(user, original)) {
            swap->target_generation = 0;
            return DLUMA_TEXTURE_CALL_ROLLED_BACK;
        }
        swap->state = SWAP_RETIRING;
        return DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED;
    }
    return set(user, original) ? DLUMA_TEXTURE_CALL_ORIGINAL : DLUMA_TEXTURE_CALL_FAILED;
}

void dluma_texture_swap_map_destroyed(DlumaTextureSwap* swap, uint32_t map)
{
    if (swap && swap->state && swap->map_generation == map)
        memset(swap, 0, sizeof(*swap));
}
