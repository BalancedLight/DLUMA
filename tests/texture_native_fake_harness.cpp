#include <cstdio>
#include "../src/game/dlw_texture_swap_native.cpp"
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"failure %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
alignas(8) unsigned char manager_data[1024],nodes[2][64],definitions[2][128],backings[2][128];
alignas(8) unsigned char image_data[64],object_data[1200],component_data[144],hud_data[1200];
uint32_t fake_map=7;uint64_t fake_instance=200,fake_manager_generation=99;
void* fake_level=reinterpret_cast<void*>(1);
void* fake_image=image_data;
char last_alias[128],applied_alias[128];String current_texture={};unsigned calls;bool fail_replacement,fail_original,fake_document_reload;
unsigned diagnostic_lines;bool diagnostic_oversized;
template<class T> void Put(uintptr_t object,unsigned offset,T value)
{std::memcpy(reinterpret_cast<void*>(object+offset),&value,sizeof(value));}
unsigned char __fastcall FakeSet(void* image,const String* value)
{if(image!=fake_image)return 0;++calls;std::memcpy(last_alias,value->data,value->size);last_alias[value->size]=0;
 std::memcpy(applied_alias,last_alias,value->size+1);current_texture=View(applied_alias);
 return (fail_replacement&&last_alias[0]=='d')||(fail_original&&last_alias[0]=='h')?0:1;}
int FakeRelease(void*,uintptr_t value)
{auto* asset=Handle(value);if(!asset)return 0;InterlockedDecrement(reinterpret_cast<volatile LONG*>(asset->definition+16));*asset={};return 1;}
void Setup()
{
    g_service={};std::memset(manager_data,0,sizeof(manager_data));std::memset(nodes,0,sizeof(nodes));
    std::memset(definitions,0,sizeof(definitions));std::memset(backings,0,sizeof(backings));
    std::memset(image_data,0,sizeof(image_data));std::memset(object_data,0,sizeof(object_data));
    std::memset(component_data,0,sizeof(component_data));std::memset(hud_data,0,sizeof(hud_data));
    static const char* aliases[]={"hud_bossfight_flash_0","dluma_custom_flash_0"};
    static const char* filenames[]={"hud_bossfight_flash_0.dds","custom_flash_0.dds"};
    auto manager=reinterpret_cast<uintptr_t>(manager_data);
    for(unsigned i=0;i<2;++i){auto node=reinterpret_cast<uintptr_t>(nodes[i])+24,def=reinterpret_cast<uintptr_t>(definitions[i]),backing=reinterpret_cast<uintptr_t>(backings[i]);
        Put(node-24,0,aliases[i]);Put(node-8,0,def);Put(def,8,manager);Put(def,24,backing);Put(def,32,filenames[i]);
        Put(def,48,512.0f);Put(def,52,512.0f);Put(backing,0,g_engine+0x9427F8);Put(backing,8,uintptr_t(3));
        Put(backing,34,static_cast<unsigned short>(512));Put(backing,36,static_cast<unsigned short>(512));
        Put(backing,40,static_cast<unsigned short>(512));Put(backing,42,static_cast<unsigned short>(512));Put(backing,96,filenames[i]);}
    Put(manager,952,reinterpret_cast<uintptr_t>(nodes[0])+24);Put(reinterpret_cast<uintptr_t>(nodes[0])+24,0,reinterpret_cast<uintptr_t>(nodes[1])+24);
    Put(g_engine+0xB4F0A0,72,uintptr_t(4));
    g_service.manager=manager;g_service.manager_generation=99;g_service.owner=9;g_service.owner_generation=3;g_service.map=7;
    g_service.status.token=1;g_original=FakeSet;calls=0;diagnostic_lines=0;diagnostic_oversized=false;fail_replacement=false;fail_original=false;fake_document_reload=false;fake_instance=200;fake_manager_generation=99;fake_map=7;
    Put(g_game,0x1D07FD0,reinterpret_cast<uintptr_t>(hud_data));Put(reinterpret_cast<uintptr_t>(hud_data),0,g_game+0x15BD918);
    Put(reinterpret_cast<uintptr_t>(hud_data),72+49*8,reinterpret_cast<uintptr_t>(component_data));
    Put(reinterpret_cast<uintptr_t>(component_data),0,g_game+0x15B1078);Put(reinterpret_cast<uintptr_t>(component_data),44,49U);
    Put(reinterpret_cast<uintptr_t>(component_data),112,reinterpret_cast<uintptr_t>(fake_image));
    Put(reinterpret_cast<uintptr_t>(image_data),24,reinterpret_cast<uintptr_t>(object_data));
    Put(reinterpret_cast<uintptr_t>(object_data),0,g_engine+0x914E48);Put(reinterpret_cast<uintptr_t>(object_data),16,reinterpret_cast<uintptr_t>(fake_image));
    Put(reinterpret_cast<uintptr_t>(object_data),1128,reinterpret_cast<uintptr_t>(&current_texture));
}
}
extern "C" unsigned int dluma_lua_runtime_current_map_generation(void){return fake_map;}
extern "C" void dluma_log_line(const char*,const char* message){++diagnostic_lines;if(std::strlen(message)>=1024)diagnostic_oversized=true;}
extern "C" int dluma_hud_visibility_backend_document_reload_active(void){return fake_document_reload?1:0;}
extern "C" int dluma_xui_native_frame(void** level,uintptr_t* manager){*level=fake_level;*manager=reinterpret_cast<uintptr_t>(manager_data);return 1;}
extern "C" int dluma_xui_native_borrow_context(void** level,uintptr_t* manager){return dluma_xui_native_frame(level,manager);}
extern "C" uint64_t dluma_xui_native_manager_generation(uintptr_t){return 99;}
extern "C" int dluma_xui_observe_borrowed_receiver(const void* image,const void* level,const void* manager,uint64_t generation,DlumaXuiReceiverObservation* out)
{if((image!=fake_image&&image!=component_data)||level!=fake_level||manager!=manager_data)return 0;
 const uint64_t instance=image==fake_image?fake_instance:555;if(generation&&generation!=instance)return 0;
 *out={};out->instance_generation=instance;out->manager_generation=fake_manager_generation;out->exact_input=1;out->load_serial=2;return 1;}
int main()
{
    g_engine=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0xB4F200,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(g_engine);
    g_game=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x1D07FE0,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));CHECK(g_game);
    CHECK(SetEnvironmentVariableW(L"DLUMA_TEXTURE_SWAP_DIAGNOSTIC",L"1"));
    Setup();CHECK(dluma_texture_native_document_reload_ready());
    AcquireSRWLockExclusive(&g_lock);CHECK(!dluma_texture_native_document_reload_ready());CHECK(!dluma_texture_native_begin_document_reload());ReleaseSRWLockExclusive(&g_lock);
    CHECK(Lookup(g_service.manager,"missing")==0);CHECK(Lookup(g_service.manager,"hud_bossfight_flash_0")==reinterpret_cast<uintptr_t>(definitions[0]));
    DlumaTextureSwapPair pair={"hud_bossfight_flash_0","dluma_custom_flash_0"};
    CHECK(dluma_texture_resources_stage(&g_service.resources,9,3,7,&pair,1,Poll,FakeRelease,Validate,nullptr)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(dluma_texture_resources_pump(&g_service.resources,2)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(dluma_texture_native_document_reload_ready());
    CHECK(g_service.resources.count==2);CHECK(*reinterpret_cast<LONG*>(definitions[0]+16)==1);
    CHECK(!diagnostic_lines);auto* inspected=Handle(g_service.resources.handles[1]);CHECK(inspected);
    auto snapshot=InspectBacking(inspected);CHECK(!snapshot.mask&&snapshot.reads==0x3FF);
    Put(reinterpret_cast<uintptr_t>(backings[1]),8,uintptr_t(0));snapshot=InspectBacking(inspected);CHECK(snapshot.mask==FailureGpu);
    Put(reinterpret_cast<uintptr_t>(backings[1]),8,uintptr_t(3));Put(reinterpret_cast<uintptr_t>(backings[1]),34,static_cast<unsigned short>(256));
    snapshot=InspectBacking(inspected);CHECK(snapshot.mask==FailureResidency);
    Put(reinterpret_cast<uintptr_t>(backings[1]),34,static_cast<unsigned short>(512));Put(reinterpret_cast<uintptr_t>(backings[1]),56,0x03000000U);
    snapshot=InspectBacking(inspected);CHECK(snapshot.mask==FailureFormat);
    Put(reinterpret_cast<uintptr_t>(backings[1]),56,0U);Put(reinterpret_cast<uintptr_t>(definitions[1]),48,1024.0f);
    snapshot=InspectBacking(inspected);CHECK(snapshot.mask==FailureGeometry);Put(reinterpret_cast<uintptr_t>(definitions[1]),48,512.0f);
    String requested=View(pair.original);unsigned char result=0;
    CHECK(dluma_texture_native_begin_document_reload());CHECK(!dluma_texture_native_begin_document_reload());
    CHECK(!DispatchBorrowed(fake_image,&requested,&result));CHECK(!calls&&!g_service.resources.swap.target_generation);
    dluma_texture_native_end_document_reload();
    fake_manager_generation=100;CHECK(!DispatchBorrowed(fake_image,&requested,&result));CHECK(!calls&&!g_service.resources.swap.target_generation);fake_manager_generation=99;
    fake_document_reload=true;CHECK(!DispatchBorrowed(fake_image,&requested,&result));CHECK(!calls&&!g_service.resources.swap.target_generation);fake_document_reload=false;
    CHECK(DispatchBorrowed(fake_image,&requested,&result)&&result);CHECK(!std::strcmp(last_alias,pair.replacement));CHECK(g_service.status.state==DLUMA_TEXTURE_SERVICE_ACTIVE);
    CHECK(!dluma_texture_native_document_reload_ready());
    CHECK(!dluma_texture_native_begin_document_reload());
    fake_map=8;CHECK(!DispatchBorrowed(fake_image,&requested,&result));fake_map=7;
    CHECK(dluma_texture_service_stop(9,3,7,1)==DLUMA_TEXTURE_SWAP_RETRY);fake_instance=201;
    CHECK(!dluma_texture_native_document_reload_ready());
    CHECK(!DispatchBorrowed(fake_image,&requested,&result));CHECK(g_service.resources.swap.state==2);
    fake_instance=200;CHECK(DispatchBorrowed(fake_image,&requested,&result)&&result);CHECK(!g_service.resources.swap.state);
    CHECK(dluma_texture_native_document_reload_ready());
    CHECK(dluma_texture_resources_cleanup(&g_service.resources)==DLUMA_TEXTURE_SWAP_OK);CHECK(!*reinterpret_cast<LONG*>(definitions[0]+16));
    Setup();CHECK(dluma_texture_resources_stage(&g_service.resources,9,3,7,&pair,1,Poll,FakeRelease,Validate,nullptr)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(dluma_texture_resources_pump(&g_service.resources,2)==DLUMA_TEXTURE_SWAP_OK);
    Put(reinterpret_cast<uintptr_t>(backings[1]),96,"other.dds");
    CHECK(DispatchBorrowed(fake_image,&requested,&result)&&result);CHECK(!std::strcmp(last_alias,pair.original));CHECK(!g_service.resources.swap.state);
    CHECK(dluma_texture_resources_cleanup(&g_service.resources)==DLUMA_TEXTURE_SWAP_OK);
    Setup();CHECK(dluma_texture_resources_stage(&g_service.resources,9,3,7,&pair,1,Poll,FakeRelease,Validate,nullptr)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(dluma_texture_resources_pump(&g_service.resources,2)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(DispatchBorrowed(fake_image,&requested,&result)&&result);CHECK(dluma_texture_service_stop(9,3,7,1)==DLUMA_TEXTURE_SWAP_RETRY);
    fake_instance=201;CHECK(!RestoreCurrentFrame(fake_level,g_service.manager));CHECK(g_service.resources.swap.state==2);
    fake_instance=200;Put(reinterpret_cast<uintptr_t>(object_data),0,g_engine+0x915938);CHECK(!RestoreCurrentFrame(fake_level,g_service.manager));
    Put(reinterpret_cast<uintptr_t>(object_data),0,g_engine+0x914E48);fail_original=true;
    CHECK(!RestoreCurrentFrame(fake_level,g_service.manager));CHECK(g_service.resources.swap.state==2&&g_service.resources.count==2);
    fail_original=false;CHECK(RestoreCurrentFrame(fake_level,g_service.manager));CHECK(!g_service.resources.swap.state&&!std::strcmp(last_alias,pair.original));
    CHECK(dluma_texture_resources_cleanup(&g_service.resources)==DLUMA_TEXTURE_SWAP_OK);
    Setup();CHECK(dluma_texture_resources_stage(&g_service.resources,9,3,7,&pair,1,Poll,FakeRelease,Validate,nullptr)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(dluma_texture_resources_pump(&g_service.resources,2)==DLUMA_TEXTURE_SWAP_OK);
    fail_replacement=true;CHECK(DispatchBorrowed(fake_image,&requested,&result)&&result);CHECK(calls==2&&!std::strcmp(last_alias,pair.original));CHECK(!g_service.resources.swap.target_generation);
    CHECK(dluma_texture_resources_stop(&g_service.resources,9,3,7)==DLUMA_TEXTURE_SWAP_OK);
    Setup();Put(g_engine+0xB4F0A0,72,reinterpret_cast<uintptr_t>(backings[0]));
    CHECK(dluma_texture_resources_stage(&g_service.resources,9,3,7,&pair,1,Poll,FakeRelease,Validate,nullptr)==DLUMA_TEXTURE_SWAP_OK);
    CHECK(dluma_texture_resources_pump(&g_service.resources,2)==DLUMA_TEXTURE_SWAP_RETRY);CHECK(!g_service.resources.swap.state&&g_service.resources.count==1);
    CHECK(diagnostic_lines==3&&g_service.assets[0].diagnostic_first_mask==FailureFallback);
    for(unsigned i=0;i<20;++i)CHECK(dluma_texture_resources_pump(&g_service.resources,2)==DLUMA_TEXTURE_SWAP_RETRY);
    CHECK(diagnostic_lines==3&&!diagnostic_oversized);
    CHECK(dluma_texture_resources_cancel_stage(&g_service.resources)==DLUMA_TEXTURE_SWAP_OK);
    Setup();InterlockedExchangePointer(&g_manager_identity,reinterpret_cast<void*>(g_service.manager));
    AcquireSRWLockExclusive(&g_lock);dluma_texture_native_manager_destroying(g_service.manager+8,99);
    dluma_texture_native_manager_destroyed(g_service.manager+8,99);CHECK(!g_lifetime_lost);ReleaseSRWLockExclusive(&g_lock);
    CHECK(dluma_texture_resources_stage(&g_service.resources,9,3,7,&pair,1,Poll,FakeRelease,Validate,nullptr)==DLUMA_TEXTURE_SWAP_OK);
    Put(g_game,kIat,reinterpret_cast<uintptr_t>(&Hook));Put(reinterpret_cast<uintptr_t>(backings[1]),8,uintptr_t(0));
    for(unsigned i=0;i<=kWaitFrames;++i)dluma_texture_service_pump();
    CHECK(g_service.status.state==DLUMA_TEXTURE_SERVICE_FAILED&&!g_service.resources.swap.state);
    CHECK(g_service.status.ready_count==1&&diagnostic_lines==6&&!diagnostic_oversized);
    CHECK(g_service.assets[1].diagnostic_first_mask==FailureGpu&&g_service.assets[1].diagnostic_last_mask==FailureGpu&&g_service.assets[1].diagnostic_reports==2);
    dluma_texture_service_pump();CHECK(!g_service.resources.count&&!g_service.resources.preparing.state);
    Setup();CHECK(Hook(fake_image,&requested));CHECK(!std::strcmp(last_alias,pair.original));
    CHECK(dluma_texture_service_begin(9,3,7,&pair,1,reinterpret_cast<uint64_t*>(definitions[0]))==DLUMA_TEXTURE_SWAP_INVALID);
    VirtualFree(reinterpret_cast<void*>(g_game),0,MEM_RELEASE);VirtualFree(reinterpret_cast<void*>(g_engine),0,MEM_RELEASE);std::puts("native texture adapter passed");return 0;
}
