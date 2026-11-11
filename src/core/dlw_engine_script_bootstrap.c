#include "dlw_engine_script_bootstrap.h"
#include <string.h>
#define EBS_CALLBACK 0xACCC58u
#define EBS_MANAGER 0xACCC68u
#define EBS_FACTORY 0xACCCD0u
#define EBS_CALL_RETURN 0x24B9ABu
static const unsigned char ebs_order[]={0x48,0x8B,0x3D,0xAF,0x12,0x88,0,0xFF,0xD7,0x48,0x8B,0xF8,0x48,0x89,5,0xB3,0x12,0x88,0,0x48,0x85,0xC0,0x75,0x39};
static const unsigned char ebs_factory_order[]={0x48,0x89,0x1D,0x8C,0x12,0x88,0};
static int ebs_fail(DlumaEngineScriptBootstrapResult* out,DlumaEngineScriptBootstrapFailure why)
{ memset(out,0,sizeof(*out));out->failure=why;return 0; }
static int ebs_read(const DlumaEngineScriptBootstrapInput* in,uintptr_t rva,void* out,size_t bytes)
{
    uintptr_t address;
    if(!bytes||in->engine_base>UINTPTR_MAX-rva)return 0;
    address=in->engine_base+rva;
    return bytes-1<=UINTPTR_MAX-address&&in->read_memory(in->user,address,out,bytes);
}
static int ebs_code(const DlumaEngineScriptBootstrapInput* in,uintptr_t rva,const unsigned char* expected,size_t count)
{
    unsigned char actual[sizeof(ebs_order)];
    return count<=sizeof(actual)&&ebs_read(in,rva,actual,count)&&!memcmp(actual,expected,count);
}
int dluma_engine_script_bootstrap_unpublished(const DlumaEngineScriptBootstrapInput* in,DlumaEngineScriptBootstrapResult* out)
{
    uintptr_t pointer=0;
    if(!out)return 0;
    ebs_fail(out,DLUMA_EBS_INVALID_ARGUMENT);
    if(!in||!in->engine_base||!in->proxy_initializer||!in->read_memory)return 0;
    if(in->engine_base>UINTPTR_MAX-EBS_FACTORY)return ebs_fail(out,DLUMA_EBS_ADDRESS_OVERFLOW);
    if(!in->exact_engine_profile||sizeof(uintptr_t)!=8)return ebs_fail(out,DLUMA_EBS_UNSUPPORTED_IMAGE);
    if(!in->before_original_initializer)return ebs_fail(out,DLUMA_EBS_NOT_BEFORE_INITIALIZER);
    if(!ebs_code(in,0x24B9A2u,ebs_order,sizeof(ebs_order)))return ebs_fail(out,DLUMA_EBS_ORDER_MISMATCH);
    if(!ebs_read(in,EBS_CALLBACK,&pointer,sizeof(pointer)))return ebs_fail(out,DLUMA_EBS_CALLBACK_UNREADABLE);
    if(pointer!=in->proxy_initializer)return ebs_fail(out,DLUMA_EBS_CALLBACK_MISMATCH);
    if(!ebs_read(in,EBS_MANAGER,&pointer,sizeof(pointer)))return ebs_fail(out,DLUMA_EBS_MANAGER_UNREADABLE);
    if(pointer)return ebs_fail(out,DLUMA_EBS_MANAGER_PUBLISHED);
    if(!ebs_read(in,EBS_FACTORY,&pointer,sizeof(pointer)))return ebs_fail(out,DLUMA_EBS_FACTORY_UNREADABLE);
    if(pointer)return ebs_fail(out,DLUMA_EBS_FACTORY_PUBLISHED);
    memset(out,0,sizeof(*out));out->callback_owned=out->manager_unpublished=out->factory_unavailable=1;
    return 1;
}
int dluma_engine_script_bootstrap_entered(const DlumaEngineScriptBootstrapInput* in,DlumaEngineScriptBootstrapResult* out)
{
    if(!dluma_engine_script_bootstrap_unpublished(in,out))return 0;
    if(in->actual_return_address!=in->engine_base+EBS_CALL_RETURN)return ebs_fail(out,DLUMA_EBS_CALLER_MISMATCH);
    if(!in->entry_thread||!in->current_thread||in->entry_thread!=in->current_thread)return ebs_fail(out,DLUMA_EBS_THREAD_MISMATCH);
    if(!ebs_code(in,0x24BA3Du,ebs_factory_order,sizeof(ebs_factory_order)))return ebs_fail(out,DLUMA_EBS_FACTORY_ORDER_MISMATCH);
    out->entry_validated=1;return 1;
}
