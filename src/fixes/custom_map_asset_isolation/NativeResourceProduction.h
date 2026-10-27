#ifndef DLUMA_NATIVE_RESOURCE_PRODUCTION_H
#define DLUMA_NATIVE_RESOURCE_PRODUCTION_H
#include "MaterialBaselineOwner.h"
#include "NativeResourceInputCapture.h"
#include "RendererStreamingOwner.h"

namespace dluma::fixes::custom_map_asset_isolation::resource_production {
struct State {
    resource_gateway::State gateway;
    resource_input_capture::State capture;
    renderer_streaming::State renderer;
    renderer_streaming::Record* renderer_records=nullptr;
    renderer_streaming::NativeDescriptor* renderer_scratch=nullptr;
    retained::Ledger textures={};packed::Ledger packs={};
    retained::windows::Binding texture_native={};packed::windows::Binding pack_native={};
    material_baseline::State material;
    retained::Inventory texture_snapshot={};retained::Plan texture_plan={};
    packed::Snapshot pack_snapshot={};packed::Plan pack_plan={};material_baseline::Snapshot material_snapshot={};
    resource_gateway::Callbacks upstream={};
    resource_gateway::Lease gate={};std::uint64_t baseline_generation=0,completed_transaction=0;
    std::uint32_t completed_epoch=0;bool initialized=false,available=false,file_admission_owned=false;
    std::uintptr_t material_retirement_pack=0;std::uint64_t material_retirement_generation=0;
    std::uint32_t material_retirement_thread=0;
    void* storage=nullptr;
};
// Must run before any resource routing write. Storage and worker lifetimes are
// owned by the already process-pinned loader once any native route is installed.
bool Initialize(State*,std::uintptr_t engine,std::uintptr_t filesystem,const char* engine_sha,const char* filesystem_sha,
    const resource_gateway::Callbacks&,file_lifetime::bridge::State*,audio::AudioFullPathProbeState*,std::uintptr_t provider_probe,
    std::uintptr_t renderer,const char* renderer_sha)noexcept;
bool Seal(State*)noexcept;
bool InitializeRenderer(State*,std::uintptr_t renderer,const char* renderer_sha)noexcept;
bool SealEngine(State*)noexcept;
bool StartInputCapture(State*)noexcept;
bool CaptureBaseline(State*,std::uint64_t generation)noexcept;
bool CaptureMaterialBaseline(State*,std::uintptr_t manager,std::uint64_t generation)noexcept;
bool PreflightResources(State*,std::uint64_t transaction,std::uint32_t epoch)noexcept;
bool ResetResources(State*,std::uint64_t transaction,std::uint32_t epoch)noexcept;
bool VerifyResources(State*,std::uint64_t transaction,std::uint32_t epoch)noexcept;
bool PreflightMaterials(State*,std::uint64_t transaction,std::uint32_t epoch)noexcept;
bool ResetMaterials(State*,std::uint64_t transaction,std::uint32_t epoch)noexcept;
bool VerifyMaterials(State*,std::uint64_t transaction,std::uint32_t epoch)noexcept;
bool CanRetireSource(State*,file_lifetime::SourceKey,std::uint64_t transaction)noexcept;
bool ReclaimRetiredSource(State*,file_lifetime::SourceKey,std::uint64_t transaction)noexcept;
// Stop returns false until every reader/pin has closed. The caller must retain
// the module/state when false; it must not force-unload worker code.
bool StopInputCapture(State*,std::uint32_t timeout_ms)noexcept;
} // namespace
#endif
