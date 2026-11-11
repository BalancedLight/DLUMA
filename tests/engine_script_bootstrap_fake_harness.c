#include "../src/core/dlw_engine_script_bootstrap.c"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct Fixture {
    unsigned char order[24],factory_order[7];
    uintptr_t callback,manager,factory;
    unsigned unreadable;
} Fixture;
static int read_memory(void* user,uintptr_t address,void* out,size_t size)
{
    Fixture* f=user;uintptr_t rva=address-(uintptr_t)0x10000000;
    if(rva==0x24B9A2 && size==sizeof(f->order) && f->unreadable!=1) {memcpy(out,f->order,size);return 1;}
    if(rva==0x24BA3D && size==sizeof(f->factory_order) && f->unreadable!=2) {memcpy(out,f->factory_order,size);return 1;}
    if(rva==0xACCC58 && size==sizeof(uintptr_t) && f->unreadable!=3) {memcpy(out,&f->callback,size);return 1;}
    if(rva==0xACCC68 && size==sizeof(uintptr_t) && f->unreadable!=4) {memcpy(out,&f->manager,size);return 1;}
    if(rva==0xACCCD0 && size==sizeof(uintptr_t) && f->unreadable!=5) {memcpy(out,&f->factory,size);return 1;}
    return 0;
}
int main(void)
{
    static const unsigned char order[]={0x48,0x8B,0x3D,0xAF,0x12,0x88,0,0xFF,0xD7,0x48,0x8B,0xF8,0x48,0x89,5,0xB3,0x12,0x88,0,0x48,0x85,0xC0,0x75,0x39};
    static const unsigned char factory[]={0x48,0x89,0x1D,0x8C,0x12,0x88,0};
    Fixture f={0};DlumaEngineScriptBootstrapInput input={0};DlumaEngineScriptBootstrapResult result={0};unsigned i;
    memcpy(f.order,order,sizeof(order));memcpy(f.factory_order,factory,sizeof(factory));f.callback=0x20001000;
    input.engine_base=0x10000000;input.proxy_initializer=f.callback;input.actual_return_address=input.engine_base+0x24B9AB;
    input.entry_thread=11;input.current_thread=11;input.exact_engine_profile=1;input.before_original_initializer=1;
    input.read_memory=read_memory;input.user=&f;
    CHECK(dluma_engine_script_bootstrap_entered(&input,&result));
    CHECK(result.entry_validated&&result.callback_owned&&result.manager_unpublished&&result.factory_unavailable);
    input.actual_return_address=0;input.entry_thread=input.current_thread=0;
    CHECK(dluma_engine_script_bootstrap_unpublished(&input,&result)&&!result.entry_validated);
    CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_CALLER_MISMATCH);
    CHECK(!result.callback_owned&&!result.manager_unpublished&&!result.factory_unavailable&&!result.entry_validated);
    input.actual_return_address=input.engine_base+0x24B9AC;input.entry_thread=input.current_thread=11;
    CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_CALLER_MISMATCH);
    input.actual_return_address=input.engine_base+0x24B9AB;input.current_thread=12;
    CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_THREAD_MISMATCH);
    input.current_thread=input.entry_thread=0;
    CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_THREAD_MISMATCH);
    input.current_thread=input.entry_thread=11;
    for(i=1;i<=5;++i){f.unreadable=i;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result));CHECK(!result.entry_validated);}
    f.unreadable=0;f.order[0]^=1;
    CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_ORDER_MISMATCH);f.order[0]^=1;
    f.factory_order[0]^=1;
    CHECK(dluma_engine_script_bootstrap_unpublished(&input,&result)); /* legacy behavior deliberately unchanged */
    CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_FACTORY_ORDER_MISMATCH);f.factory_order[0]^=1;
    ++f.callback;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_CALLBACK_MISMATCH);--f.callback;
    f.manager=1;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_MANAGER_PUBLISHED);f.manager=0;
    f.factory=1;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_FACTORY_PUBLISHED);f.factory=0;
    input.exact_engine_profile=0;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_UNSUPPORTED_IMAGE);input.exact_engine_profile=1;
    input.before_original_initializer=0;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_NOT_BEFORE_INITIALIZER);input.before_original_initializer=1;
    input.engine_base=UINTPTR_MAX;CHECK(!dluma_engine_script_bootstrap_entered(&input,&result)&&result.failure==DLUMA_EBS_ADDRESS_OVERFLOW);
    CHECK(!dluma_engine_script_bootstrap_entered(0,&result));CHECK(!dluma_engine_script_bootstrap_entered(&input,0));
    puts("PASS: exact engine cold publication and entry proof");return 0;
}
