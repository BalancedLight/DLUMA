#include "NativeXuiReceiverBridge.h"
extern "C" int dluma_xui_observe_borrowed_receiver(const void* receiver,const void* level_interface,
    const void* expected_manager,std::uint64_t expected_generation,
    DlumaXuiReceiverObservation* output)
{
    using namespace dluma::fixes::custom_map_asset_isolation;
    if(output)*output={};
    if(!receiver||!level_interface||!output||!g_engine_full_profile_valid||
       !g_xui_manager_observation_ready||!g_xui_instance_observation_ready||
       !ProductionXuiHooksCurrent())return 0;
    // Exact Player ILevel::FindUiScreen reads the native level through +8.
    // Constructor observation records that native identity, not ILevel itself.
    const auto level_address=reinterpret_cast<std::uintptr_t>(level_interface);
    std::uintptr_t native_level=0;
    if(level_address>UINTPTR_MAX-8||!ReadBytes(level_address+8,&native_level,sizeof(native_level))||!native_level)return 0;
    xui_instance::Instance instance={};
    if(!xui_instance::TryCopyActiveReceiver(&g_xui_instances,
        reinterpret_cast<std::uintptr_t>(receiver),expected_generation,&instance))return 0;
    if(expected_manager&&instance.manager.manager!=reinterpret_cast<std::uintptr_t>(expected_manager))return 0;
    xui::ManagerRecord manager={};
    if(!xui::LookupManager(&g_xui_manager_ledger,instance.manager.manager,&manager)||
       manager.life!=xui::ManagerLife::Live||manager.generation!=instance.manager.generation||
       manager.level!=native_level)return 0;
    output->manager_generation=manager.generation;
    output->instance_generation=instance.generation;
    output->load_serial=instance.load_serial;
    output->epoch=instance.epoch;
    output->origin=static_cast<std::uint32_t>(instance.origin);
    output->exact_input=instance.exact_input?1:0;
    std::memcpy(output->sha256,instance.sha256,sizeof(output->sha256));
    return 1;
}
