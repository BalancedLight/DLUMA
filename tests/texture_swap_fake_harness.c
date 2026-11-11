#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_texture_swap_core.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failure %d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct Fixture { int missing, probes, results[8], calls; char names[8][128]; } Fixture;
static int ready(void* u,const char* name) { Fixture* f=u; ++f->probes; return !((f->missing==1 && !strcmp(name,"replacement_5")) || (f->missing==2 && !strcmp(name,"original_5"))); }
static int set(void* u,const char* name) { Fixture* f=u; int i=f->calls++; strcpy(f->names[i],name); return f->results[i]; }
static void calls(Fixture* f,int a,int b) { f->calls=0; f->results[0]=a; f->results[1]=b; }
int main(void) {
 DlumaTextureSwap s={0}; DlumaTextureSwapPair pairs[11]; char from[11][32],to[11][32]; unsigned i; Fixture f={0};
 for(i=0;i<11;i++){sprintf(from[i],"original_%u",i);sprintf(to[i],"replacement_%u",i);pairs[i].original=from[i];pairs[i].replacement=to[i];}
 f.missing=1; CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_NOT_READY);
 f.missing=2; CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_NOT_READY);
 f.missing=0; f.probes=0; CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_OK); CHECK(f.probes==22);
 CHECK(dluma_texture_swap_begin(&s,2,1,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_BUSY);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,0,3,0,"original_0",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL); CHECK(!strcmp(f.names[0],"original_0"));
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,0,"original_0",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL); CHECK(!strcmp(f.names[0],"original_0"));
 calls(&f,0,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_1",set,&f)==DLUMA_TEXTURE_CALL_ROLLED_BACK); CHECK(f.calls==2 && !strcmp(f.names[0],"replacement_1") && !strcmp(f.names[1],"original_1"));
 calls(&f,0,0); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_2",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED); CHECK(f.calls==2);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,99,"original_3",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING); CHECK(!strcmp(f.names[0],"original_3")); CHECK(s.state);
 calls(&f,0,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_4",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED); CHECK(s.state);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_5",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL); CHECK(!s.state && !strcmp(f.names[0],"original_5"));
 CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_OK);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,4,42,"original_10",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL); CHECK(!strcmp(f.names[0],"original_10"));
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_10",set,&f)==DLUMA_TEXTURE_CALL_REPLACED); CHECK(!strcmp(f.names[0],"replacement_10"));
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,99,"original_7",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL); CHECK(!strcmp(f.names[0],"original_7"));
 CHECK(dluma_texture_swap_stop(&s,2,2,3)==DLUMA_TEXTURE_SWAP_FOREIGN); CHECK(dluma_texture_swap_stop(&s,1,2,4)==DLUMA_TEXTURE_SWAP_STALE); CHECK(dluma_texture_swap_stop(&s,1,2,3)==DLUMA_TEXTURE_SWAP_RETRY);
 CHECK(dluma_texture_swap_begin(&s,2,1,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_BUSY);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,4,42,"original_8",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING); CHECK(s.state);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,99,"original_8",set,&f)==DLUMA_TEXTURE_CALL_RESTORE_PENDING); CHECK(s.state);
 calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_9",set,&f)==DLUMA_TEXTURE_CALL_ORIGINAL); CHECK(!s.state && !strcmp(f.names[0],"original_9"));
 CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_OK); calls(&f,1,1); CHECK(dluma_texture_swap_dispatch(&s,1,3,42,"original_0",set,&f)==DLUMA_TEXTURE_CALL_REPLACED); dluma_texture_swap_map_destroyed(&s,4); CHECK(s.state); dluma_texture_swap_map_destroyed(&s,3); CHECK(!s.state);
 pairs[1].original=pairs[0].original; CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_INVALID);
 pairs[1].original=from[1]; pairs[1].replacement=pairs[0].replacement; CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_INVALID);
 pairs[1].replacement=from[0]; CHECK(dluma_texture_swap_begin(&s,1,2,3,pairs,11,ready,&f)==DLUMA_TEXTURE_SWAP_INVALID);
 puts("texture swap core passed"); return 0;
}
