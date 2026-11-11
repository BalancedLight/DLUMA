#include "dlw_texture_swap_resources.h"
#include <string.h>

static int same_name(const char* a,const char* b)
{
    unsigned i;
    for (i=0;i<DLUMA_TEXTURE_SWAP_NAME_CAPACITY;++i) {
        if (a[i]!=b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 0;
}

static int staged_ready(void* user,const char* name)
{
    (void)user;(void)name;return 1;
}

DlumaTextureSwapResult dluma_texture_resources_cleanup(DlumaTextureResources* resources)
{
    if (!resources) return DLUMA_TEXTURE_SWAP_INVALID;
    if (resources->swap.state || resources->preparing.state) return DLUMA_TEXTURE_SWAP_RETRY;
    while (resources->count) {
        uintptr_t handle = resources->handles[resources->count - 1];
        if (!resources->release || !resources->release(resources->user,handle))
            return DLUMA_TEXTURE_SWAP_RETRY;
        resources->handles[--resources->count] = 0;
    }
    resources->release = 0;
    resources->validate = 0;
    resources->user = 0;
    resources->poll = 0;
    resources->ready_count = 0;
    return DLUMA_TEXTURE_SWAP_OK;
}

DlumaTextureSwapResult dluma_texture_resources_stage(
    DlumaTextureResources* resources,uint64_t owner,uint32_t generation,uint32_t map,
    const DlumaTextureSwapPair* pairs,unsigned count,DlumaTexturePoll poll,
    DlumaTextureRelease release,DlumaTextureValidate validate,void* user)
{
    DlumaTextureSwapResult result;
    if (!resources || !poll || !release || !validate) return DLUMA_TEXTURE_SWAP_INVALID;
    if (resources->swap.state || resources->preparing.state || resources->count) return DLUMA_TEXTURE_SWAP_BUSY;
    result=dluma_texture_swap_begin(&resources->preparing,owner,generation,map,pairs,count,staged_ready,0);
    if (result!=DLUMA_TEXTURE_SWAP_OK) return result;
    resources->release=release;resources->validate=validate;resources->user=user;
    resources->poll=poll;resources->ready_count=0;
    return DLUMA_TEXTURE_SWAP_OK;
}

DlumaTextureSwapResult dluma_texture_resources_cancel_stage(DlumaTextureResources* resources)
{
    if (!resources) return DLUMA_TEXTURE_SWAP_INVALID;
    memset(&resources->preparing,0,sizeof(resources->preparing));
    return dluma_texture_resources_cleanup(resources);
}

DlumaTextureSwapResult dluma_texture_resources_pump(DlumaTextureResources* resources,unsigned budget)
{
    unsigned total;
    if (!resources || !resources->preparing.state || !resources->poll || !budget)
        return DLUMA_TEXTURE_SWAP_INVALID;
    total=resources->preparing.count*2U;
    while (budget-- && resources->ready_count<total) {
        unsigned index=resources->ready_count;
        const DlumaTextureSwapEntry* entry=&resources->preparing.entries[index/2U];
        const char* alias=index%2U?entry->replacement:entry->original;
        uintptr_t old=resources->handles[index];
        int result=resources->poll(resources->user,alias,&resources->handles[index]);
        if (resources->handles[index] && index==resources->count) ++resources->count;
        if ((old && old!=resources->handles[index]) || result<0 ||
            (result>0 && (!resources->handles[index] ||
             !resources->validate(resources->user,resources->handles[index],alias)))) {
            DlumaTextureSwapResult cleanup=dluma_texture_resources_cancel_stage(resources);
            return cleanup==DLUMA_TEXTURE_SWAP_OK?DLUMA_TEXTURE_SWAP_NOT_READY:cleanup;
        }
        if (!result) return DLUMA_TEXTURE_SWAP_RETRY;
        ++resources->ready_count;
    }
    if (resources->ready_count!=total) return DLUMA_TEXTURE_SWAP_RETRY;
    /* Recheck every earlier asset before publishing the complete mapping. */
    {
        unsigned i;
        for (i=0;i<total;++i) {
            const DlumaTextureSwapEntry* entry=&resources->preparing.entries[i/2U];
            if (!resources->validate(resources->user,resources->handles[i],i%2U?entry->replacement:entry->original)) {
                DlumaTextureSwapResult cleanup=dluma_texture_resources_cancel_stage(resources);
                return cleanup==DLUMA_TEXTURE_SWAP_OK?DLUMA_TEXTURE_SWAP_NOT_READY:cleanup;
            }
        }
    }
    resources->swap=resources->preparing;
    memset(&resources->preparing,0,sizeof(resources->preparing));
    resources->poll=0;
    return DLUMA_TEXTURE_SWAP_OK;
}

DlumaTextureSwapResult dluma_texture_resources_begin(
    DlumaTextureResources* resources,uint64_t owner,uint32_t generation,uint32_t map,
    const DlumaTextureSwapPair* pairs,unsigned count,DlumaTextureAcquire acquire,
    DlumaTextureRelease release,DlumaTextureValidate validate,void* user)
{
    DlumaTextureSwap candidate;
    DlumaTextureSwapResult result;
    unsigned i;
    if (!resources || !acquire || !release || !validate) return DLUMA_TEXTURE_SWAP_INVALID;
    if (resources->swap.state || resources->preparing.state || resources->count) return DLUMA_TEXTURE_SWAP_BUSY;
    memset(&candidate,0,sizeof(candidate));
    result = dluma_texture_swap_begin(&candidate,owner,generation,map,pairs,count,staged_ready,0);
    if (result != DLUMA_TEXTURE_SWAP_OK) return result;
    resources->release = release;
    resources->validate = validate;
    resources->user = user;
    for (i=0;i<count;++i) {
        uintptr_t handle = acquire(user,candidate.entries[i].original);
        if (!handle) goto failed;
        resources->handles[resources->count++] = handle;
        handle = acquire(user,candidate.entries[i].replacement);
        if (!handle) goto failed;
        resources->handles[resources->count++] = handle;
    }
    resources->swap = candidate;
    return DLUMA_TEXTURE_SWAP_OK;
failed:
    result = dluma_texture_resources_cleanup(resources);
    return result == DLUMA_TEXTURE_SWAP_OK ? DLUMA_TEXTURE_SWAP_NOT_READY : result;
}

DlumaTextureSwapResult dluma_texture_resources_stop(
    DlumaTextureResources* resources,uint64_t owner,uint32_t generation,uint32_t map)
{
    DlumaTextureSwapResult result;
    if (!resources) return DLUMA_TEXTURE_SWAP_INVALID;
    result = dluma_texture_swap_stop(&resources->swap,owner,generation,map);
    if (result != DLUMA_TEXTURE_SWAP_OK) return result;
    return dluma_texture_resources_cleanup(resources);
}

DlumaTextureCallResult dluma_texture_resources_dispatch(
    DlumaTextureResources* resources,int scoped,uint32_t map,uint64_t generation,
    const char* original,DlumaTextureSwapSet set,void* user)
{
    unsigned i;
    if (!resources) return DLUMA_TEXTURE_CALL_FAILED;
    if (scoped && resources->swap.state && map == resources->swap.map_generation &&
        generation && (!resources->swap.target_generation ||
                       generation == resources->swap.target_generation) && original) {
        for (i=0;i<resources->swap.count;++i) {
            if (same_name(original,resources->swap.entries[i].original)) {
                int original_ok = resources->validate &&
                    resources->validate(resources->user,resources->handles[2*i],
                                        resources->swap.entries[i].original);
                int replacement_ok = resources->validate &&
                    resources->validate(resources->user,resources->handles[2*i+1],
                                        resources->swap.entries[i].replacement);
                if (!original_ok || !replacement_ok) {
                    (void)dluma_texture_swap_stop(&resources->swap,resources->swap.owner,
                        resources->swap.owner_generation,resources->swap.map_generation);
                    if (!original_ok) generation = 0;
                }
                break;
            }
        }
        /* An unknown original is still forwarded, but cannot prove restoration. */
        if (i==resources->swap.count && resources->swap.target_generation) generation=0;
    }
    return dluma_texture_swap_dispatch(&resources->swap,scoped,map,generation,original,set,user);
}

void dluma_texture_resources_map_destroyed(DlumaTextureResources* resources,uint32_t map)
{
    if (resources) dluma_texture_swap_map_destroyed(&resources->swap,map);
}
