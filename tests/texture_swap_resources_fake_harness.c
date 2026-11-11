#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_texture_swap_resources.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failure %d: %s\n",__LINE__,#x); return 1; } } while(0)
typedef struct Fixture { unsigned acquired, missing, releases, fail_release; uintptr_t order[16]; int set_ok; uintptr_t invalid; char last[128]; } Fixture;
static uintptr_t acquire(void *u,const char *name) { Fixture *f=u; (void)name; ++f->acquired; return f->acquired==f->missing?0:f->acquired; }
static int release(void *u,uintptr_t h) { Fixture *f=u; f->order[f->releases++]=h; if(f->fail_release){f->fail_release=0;return 0;} return 1; }
static int validate(void *u,uintptr_t h,const char *name) { Fixture *f=u;(void)name;return h!=f->invalid; }
static int set(void *u,const char *name) { Fixture *f=u;strcpy(f->last,name);return f->set_ok; }
static int poll(void *u,const char *name,uintptr_t *handle) { Fixture *f=u;(void)name;
 if(!*handle){*handle=++f->acquired;return 0;}return *handle==f->missing?-1:1; }
int main(void) {
 DlumaTextureResources r={0}; Fixture f={0}; DlumaTextureSwapPair p[]={{"a","A"},{"b","B"},{"c","C"}};
 f.missing=4; f.fail_release=1;
 CHECK(dluma_texture_resources_begin(&r,1,2,3,p,3,acquire,release,validate,&f)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(f.releases==1 && f.order[0]==3);
 CHECK(dluma_texture_resources_begin(&r,1,2,3,p,3,acquire,release,validate,&f)==DLUMA_TEXTURE_SWAP_BUSY);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_OK);
 CHECK(f.releases==4 && f.order[1]==3 && f.order[2]==2 && f.order[3]==1);
 memset(&f,0,sizeof(f));f.set_ok=1;
 CHECK(dluma_texture_resources_begin(&r,1,2,3,p,3,acquire,release,validate,&f)==DLUMA_TEXTURE_SWAP_OK);
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_REPLACED);
 f.invalid=2;
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL);
 CHECK(!strcmp(f.last,"a"));
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_OK && f.releases==6);
 memset(&f,0,sizeof(f)); f.set_ok=1;
 CHECK(dluma_texture_resources_begin(&r,1,2,3,p,3,acquire,release,validate,&f)==DLUMA_TEXTURE_SWAP_OK);
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_REPLACED);
 CHECK(dluma_texture_resources_stop(&r,1,2,3)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_RETRY && f.releases==0);
 f.set_ok=0; CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_RETRY && f.releases==0);
 f.set_ok=1; CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_OK && f.releases==6);
 CHECK(f.order[0]==6 && f.order[1]==5 && f.order[2]==4 && f.order[3]==3 && f.order[4]==2 && f.order[5]==1);
 memset(&f,0,sizeof(f));f.set_ok=1;
 CHECK(dluma_texture_resources_begin(&r,1,2,3,p,3,acquire,release,validate,&f)==DLUMA_TEXTURE_SWAP_OK);
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_REPLACED);
 f.invalid=1;
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING);
 CHECK(!strcmp(f.last,"a") && dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_RETRY);
 f.invalid=0;
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"unknown",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_OK);
 memset(&f,0,sizeof(f));f.set_ok=1;
 CHECK(dluma_texture_resources_stage(&r,1,2,3,p,3,poll,release,validate,&f)==DLUMA_TEXTURE_SWAP_OK);
 CHECK(dluma_texture_resources_cleanup(&r)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(dluma_texture_resources_pump(&r,2)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(r.count==1 && !r.ready_count && !r.swap.state);
 CHECK(dluma_texture_resources_dispatch(&r,1,3,7,"a",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL);
 CHECK(dluma_texture_resources_pump(&r,2)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(r.ready_count==1 && r.count==2 && !r.swap.state);
 f.missing=2;
 CHECK(dluma_texture_resources_pump(&r,2)==DLUMA_TEXTURE_SWAP_NOT_READY);
 CHECK(f.releases==2 && f.order[0]==2 && f.order[1]==1 && !r.count && !r.swap.state);
 memset(&f,0,sizeof(f));f.set_ok=1;
 CHECK(dluma_texture_resources_stage(&r,1,2,3,p,3,poll,release,validate,&f)==DLUMA_TEXTURE_SWAP_OK);
 while(r.preparing.state) { DlumaTextureSwapResult result=dluma_texture_resources_pump(&r,2);CHECK(result==DLUMA_TEXTURE_SWAP_OK||result==DLUMA_TEXTURE_SWAP_RETRY); }
 CHECK(r.ready_count==6 && r.count==6 && r.swap.state);
 CHECK(dluma_texture_resources_stop(&r,1,2,3)==DLUMA_TEXTURE_SWAP_OK);
 memset(&f,0,sizeof(f));f.set_ok=1;
 CHECK(dluma_texture_resources_stage(&r,1,2,3,p,3,poll,release,validate,&f)==DLUMA_TEXTURE_SWAP_OK);
 CHECK(dluma_texture_resources_pump(&r,2)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(dluma_texture_resources_cancel_stage(&r)==DLUMA_TEXTURE_SWAP_OK && f.releases==1);
 puts("texture resource transaction passed");return 0;
}
