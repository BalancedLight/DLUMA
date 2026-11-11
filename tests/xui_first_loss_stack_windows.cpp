#include <cstring>
#include "fixes/custom_map_asset_isolation/NativeXuiInstanceOwner.h"
extern "C" __declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
namespace instance=dluma::fixes::custom_map_asset_isolation::xui_instance;
static instance::State state;
__declspec(noinline) static bool Trigger(){return instance::MarkLost(&state,instance::Loss::CallbackFlags,{1,2},3,0,1);}
int main(){
    if(!instance::SealStartup(&state)||Trigger())return 1;
    instance::ReceiverDiagnostics first={};
    if(!instance::CopyReceiverDiagnostics(&state,0,&first)||!first.lost||first.first_loss.reason!=instance::Loss::CallbackFlags)return 2;
    if(first.first_loss.thread_id!=GetCurrentThreadId()||!first.first_loss.stack_count||first.first_loss.stack_count>16)return 3;
    for(unsigned i=0;i<first.first_loss.stack_count;++i)if(!first.first_loss.stack[i])return 4;
    (void)instance::MarkLost(&state,instance::Loss::Generic,{4,5},6,7,8);
    instance::ReceiverDiagnostics next={};
    if(!instance::CopyReceiverDiagnostics(&state,0,&next)||std::memcmp(&first.first_loss,&next.first_loss,sizeof(first.first_loss)))return 5;
    return 0;
}
