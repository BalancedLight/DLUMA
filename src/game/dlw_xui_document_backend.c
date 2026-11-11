#include "dlw_xui_document_backend.h"
#include <string.h>
int dluma_xui_document_binding_matches(const DlumaXuiDocumentBinding* binding,DlumaXuiDocumentIdentity identity,uintptr_t manager,uint64_t generation)
{
    return binding&&manager&&generation&&identity.owner&&identity.token&&identity.owner_generation&&identity.map_generation&&
        binding->manager==manager&&binding->manager_generation==generation&&binding->identity.owner==identity.owner&&
        binding->identity.token==identity.token&&binding->identity.owner_generation==identity.owner_generation&&
        binding->identity.map_generation==identity.map_generation;
}
static int Same(DlumaXuiDocumentIdentity a,DlumaXuiDocumentIdentity b)
{return a.owner==b.owner&&a.token==b.token&&a.owner_generation==b.owner_generation&&a.map_generation==b.map_generation;}
static int Path(const char* p)
{size_t i;if(!p||!*p)return 0;for(i=0;i<DLUMA_XUI_DOCUMENT_PATH_CAPACITY;++i)if(!p[i])return i>4;return 0;}
static int Source(const DlumaXuiDocumentSource* s)
{return s->handle&&s->bytes&&s->bytes<=DLUMA_XUI_DOCUMENT_MAX_BYTES&&Path(s->path);}
void dluma_xui_document_initialize(DlumaXuiDocumentBackend* b,const DlumaXuiDocumentOps* o)
{if(b){memset(b,0,sizeof(*b));if(o)b->ops=*o;b->original_valid=1;}}
DlumaXuiDocumentError dluma_xui_document_release(DlumaXuiDocumentBackend* b)
{
    int ok=1;
    if(!b)return DLUMA_XUI_DOCUMENT_INVALID;
    if(b->entered)return DLUMA_XUI_DOCUMENT_BUSY;
    if(!b->original_valid)return DLUMA_XUI_DOCUMENT_ROLLBACK_FAILED;
    b->entered=1;
    if(b->replacement.handle&&!b->ops.release(b->ops.user,&b->replacement))ok=0;
    if(b->original.handle&&!b->ops.release(b->ops.user,&b->original))ok=0;
    b->entered=0;
    if(!ok||b->replacement.handle||b->original.handle){b->state=DLUMA_XUI_DOCUMENT_RELEASE_PENDING;return b->error=DLUMA_XUI_DOCUMENT_RELEASE_FAILED;}
    if(b->restore_requested){b->state=DLUMA_XUI_DOCUMENT_RESTORED;b->error=DLUMA_XUI_DOCUMENT_OK;}
    else b->state=DLUMA_XUI_DOCUMENT_FAILED;
    return DLUMA_XUI_DOCUMENT_OK;
}
static DlumaXuiDocumentError FailPrepared(DlumaXuiDocumentBackend* b,DlumaXuiDocumentError error)
{b->entered=0;b->error=error;return dluma_xui_document_release(b)==DLUMA_XUI_DOCUMENT_OK?error:b->error;}
DlumaXuiDocumentError dluma_xui_document_prepare(DlumaXuiDocumentBackend* b,DlumaXuiDocumentIdentity i,const char* a,const char* n)
{
    DlumaXuiDocumentOps ops;
    if(!b||!i.owner||!i.token||i.token>INT64_MAX||!i.owner_generation||!i.map_generation||!Path(a)||!Path(n)||!strcmp(a,n))return DLUMA_XUI_DOCUMENT_INVALID;
    if(b->entered||b->original.handle||b->replacement.handle)return DLUMA_XUI_DOCUMENT_BUSY;
    ops=b->ops;
    if(!ops.acquire||!ops.compatible||!ops.release||!ops.current||!ops.snapshot||!ops.rebuild)return DLUMA_XUI_DOCUMENT_UNAVAILABLE;
    dluma_xui_document_initialize(b,&ops);b->identity=i;b->state=DLUMA_XUI_DOCUMENT_PREPARING;b->entered=1;
    if(!ops.acquire(ops.user,a,&b->original)||!Source(&b->original)||
       !ops.acquire(ops.user,n,&b->replacement)||!Source(&b->replacement))return FailPrepared(b,DLUMA_XUI_DOCUMENT_SOURCE_FAILED);
    if(!ops.compatible(ops.user,&b->original,&b->replacement))return FailPrepared(b,DLUMA_XUI_DOCUMENT_PREFLIGHT_FAILED);
    if(b->cancel_requested)return FailPrepared(b,DLUMA_XUI_DOCUMENT_CANCELLED);
    b->state=DLUMA_XUI_DOCUMENT_READY;b->entered=0;return b->error=DLUMA_XUI_DOCUMENT_OK;
}
static DlumaXuiDocumentError RestoreEntered(DlumaXuiDocumentBackend* b)
{
    b->state=DLUMA_XUI_DOCUMENT_RESTORING;
    if(!b->ops.current(b->ops.user,&b->identity)||
       b->ops.rebuild(b->ops.user,&b->identity,&b->original,&b->before)!=DLUMA_XUI_DOCUMENT_REBUILD_SUCCEEDED){
        b->entered=0;b->state=DLUMA_XUI_DOCUMENT_RECOVERY_REQUIRED;return b->error=DLUMA_XUI_DOCUMENT_ROLLBACK_FAILED;
    }
    b->original_valid=1;b->entered=0;
    return dluma_xui_document_release(b);
}
DlumaXuiDocumentError dluma_xui_document_apply(DlumaXuiDocumentBackend* b)
{
    DlumaXuiDocumentRebuildResult rebuilt;
    if(!b)return DLUMA_XUI_DOCUMENT_INVALID;
    if(b->entered)return DLUMA_XUI_DOCUMENT_BUSY;
    if(b->state!=DLUMA_XUI_DOCUMENT_READY)return DLUMA_XUI_DOCUMENT_INVALID;
    b->entered=1;
    if(!b->ops.current(b->ops.user,&b->identity))return FailPrepared(b,DLUMA_XUI_DOCUMENT_SCOPE_LOST);
    if(!b->ops.snapshot(b->ops.user,&b->before))return FailPrepared(b,DLUMA_XUI_DOCUMENT_UNAVAILABLE);
    b->state=DLUMA_XUI_DOCUMENT_APPLYING;
    rebuilt=b->ops.rebuild(b->ops.user,&b->identity,&b->replacement,&b->before);
    if(rebuilt==DLUMA_XUI_DOCUMENT_REBUILD_REFUSED)return FailPrepared(b,DLUMA_XUI_DOCUMENT_UNAVAILABLE);
    b->original_valid=0;
    if(rebuilt!=DLUMA_XUI_DOCUMENT_REBUILD_SUCCEEDED){
        DlumaXuiDocumentError restored;
        b->error=DLUMA_XUI_DOCUMENT_LOAD_FAILED;
        restored=RestoreEntered(b);
        return restored==DLUMA_XUI_DOCUMENT_OK?DLUMA_XUI_DOCUMENT_LOAD_FAILED:restored;
    }
    if(b->restore_requested||b->cancel_requested){b->restore_requested=1;return RestoreEntered(b);}
    b->state=DLUMA_XUI_DOCUMENT_APPLIED;b->entered=0;return b->error=DLUMA_XUI_DOCUMENT_OK;
}
DlumaXuiDocumentError dluma_xui_document_restore(DlumaXuiDocumentBackend* b,DlumaXuiDocumentIdentity i)
{
    if(!b||!Same(b->identity,i))return DLUMA_XUI_DOCUMENT_INVALID;
    if(b->state==DLUMA_XUI_DOCUMENT_RESTORED)return DLUMA_XUI_DOCUMENT_OK;
    b->restore_requested=1;
    if(b->entered)return DLUMA_XUI_DOCUMENT_OK;
    if(b->original_valid)return dluma_xui_document_release(b);
    b->entered=1;return RestoreEntered(b);
}
DlumaXuiDocumentError dluma_xui_document_cancel(DlumaXuiDocumentBackend* b,DlumaXuiDocumentIdentity i)
{
    if(!b||!Same(b->identity,i))return DLUMA_XUI_DOCUMENT_INVALID;
    b->cancel_requested=1;
    if(b->entered){b->restore_requested=1;return DLUMA_XUI_DOCUMENT_OK;}
    if(!b->original_valid)return dluma_xui_document_restore(b,i);
    b->error=DLUMA_XUI_DOCUMENT_CANCELLED;return dluma_xui_document_release(b);
}
int dluma_xui_document_map_destroyed(DlumaXuiDocumentBackend* b,uint32_t g)
{
    if(!b||b->entered||!g||b->identity.map_generation!=g)return 0;
    b->original_valid=1;b->restore_requested=1;
    if(dluma_xui_document_release(b)!=DLUMA_XUI_DOCUMENT_OK)return 0;
    b->error=DLUMA_XUI_DOCUMENT_SCOPE_LOST;return 1;
}
