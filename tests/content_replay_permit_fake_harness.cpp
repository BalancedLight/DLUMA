#include "../src/fixes/custom_map_asset_isolation/ContentDomainRestartRegistry.h"
#include "../src/fixes/custom_map_asset_isolation/NativeFileLifetimeLedger.h"
#include "../src/fixes/custom_map_asset_isolation/NativeXuiContentOwner.h"
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
namespace cold = dluma::fixes::custom_map_asset_isolation::cold_reboot;
namespace files = dluma::fixes::custom_map_asset_isolation::file_lifetime;
namespace xui_content = dluma::fixes::custom_map_asset_isolation::xui_content;
namespace {
enum class TestCase {
    Nested, DigestChanged, ProviderChanged, OwnerChanged, PathChanged,
    WrongParent, UnsupportedOrigin, AbandonedPermit, OutOfOrderReturn,
    DuplicateReturn, CopiedManifest, StalePermit, ForeignProducer, ForeignReplay,
    MissingChild, NativeFailure, WrongContext, XuiClosedAdmission, FileAuthority, CallbackContext,
    FinalizerReject,FinalizerLateProducer,Count
};
struct TestState {
    cold::ContentDomainRestartRegistry registry;
    TestCase test;
    bool checks = true;
    unsigned finalizer_calls = 0;
};
TestState state;
files::Ledger native_files;
auto& g_cold_reboot_registry = state.registry;
bool g_fix_enabled = true;
volatile LONG g_content_live = 1;
volatile LONG wrapper_rejections = 0;
void dluma_str_copy(char* output, unsigned capacity, const char* value) {
    if (capacity) std::snprintf(output, capacity, "%s", value ? value : "");
}
void dluma_str_append(char* output, unsigned capacity, const char* value) {
    const auto used = std::strlen(output);
    if (used < capacity) std::snprintf(output + used, capacity - used, "%s", value ? value : "");
}
int dluma_frontend_error_present(const char*, unsigned long long) {
    dluma_atomic_increment(&wrapper_rejections); return 1;
}
#include "retaining_observer.generated.h"
#include "factory_replay_validator.generated.h"
#include "domain_callback_context.generated.h"

struct ActiveHookCall { explicit ActiveHookCall(void*) noexcept {} };
void* g_xui_xml_load_status = nullptr;
void* g_xui_binary_pack_load_status = nullptr;
void* g_xui_binary_file_load_status = nullptr;
unsigned xui_original_calls = 0, xui_touches = 0;
bool xui_args_valid = true;
// This suite exercises the actual outer native wrappers and admission order.
// Source classification and manager closure have their own owner harness.
struct XuiContentLoadScope {
    XuiContentLoadScope(void* manager,xui_content::Route route) noexcept {
        ++xui_touches;xui_args_valid &= manager==reinterpret_cast<void*>(0x10) &&
            (route==xui_content::Route::Xml || route==xui_content::Route::BinaryPack || route==xui_content::Route::BinaryFile);
    }
    void Finish(bool = false) noexcept {}
};
unsigned char __fastcall XuiXmlOriginal(void* manager,const void* path,void* context) noexcept {
    ++xui_original_calls;
    xui_args_valid &= manager == reinterpret_cast<void*>(0x10) && path == reinterpret_cast<void*>(0x20) &&
        context == reinterpret_cast<void*>(0x30) && state.registry.retaining_observation_active_calls == 1;
    return 0xA5;
}
std::uintptr_t __fastcall XuiPackOriginal(void* manager,std::int32_t pack,void* output,unsigned char option) noexcept {
    ++xui_original_calls;
    xui_args_valid &= manager == reinterpret_cast<void*>(0x10) && pack == -7 &&
        output == reinterpret_cast<void*>(0x30) && option == 0x81 && state.registry.retaining_observation_active_calls == 1;
    return 0x123456789ULL;
}
unsigned char __fastcall XuiFileOriginal(void* manager,const void* path,unsigned char a,unsigned char b,void* c,void* d) noexcept {
    ++xui_original_calls;
    xui_args_valid &= manager == reinterpret_cast<void*>(0x10) && path == reinterpret_cast<void*>(0x20) &&
        a == 0x80 && b == 0x81 && c == reinterpret_cast<void*>(0x30) && d == reinterpret_cast<void*>(0x40) &&
        state.registry.retaining_observation_active_calls == 1;
    return 0xB6;
}
auto g_original_xui_xml_load = &XuiXmlOriginal;
auto g_original_xui_binary_pack_load = &XuiPackOriginal;
auto g_original_xui_binary_file_load = &XuiFileOriginal;
#include "xui_load_observers.generated.h"
bool ExerciseXui(bool admitted) {
    const auto calls = xui_original_calls, touches = xui_touches;
    bool good = HookXuiXmlLoad(reinterpret_cast<void*>(0x10),reinterpret_cast<void*>(0x20),reinterpret_cast<void*>(0x30)) == (admitted ? 0xA5 : 0);
    good &= HookXuiBinaryPackLoad(reinterpret_cast<void*>(0x10),-7,reinterpret_cast<void*>(0x30),0x81) == (admitted ? 0x123456789ULL : 0);
    good &= HookXuiBinaryFileLoad(reinterpret_cast<void*>(0x10),reinterpret_cast<void*>(0x20),0x80,0x81,
        reinterpret_cast<void*>(0x30),reinterpret_cast<void*>(0x40)) == (admitted ? 0xB6 : 0);
    return good && xui_original_calls == calls + (admitted ? 3 : 0) &&
        xui_touches == touches + (admitted ? 3 : 0) && xui_args_valid && !state.registry.retaining_observation_active_calls;
}

cold::ReplayOperationIdentity Identity(unsigned ordinal, unsigned parent) {
    cold::ReplayOperationIdentity result = {};
    result.domain = cold::ContentDomainId::Audio;
    result.identity_kind = cold::ReplayIdentityKind::FileContent;
    result.operation_id = parent ? 2 : 1;
    result.ordinal = ordinal;
    result.parent_ordinal = parent;
    result.owner = 0x100;
    result.provider = 0x200;
    result.provider_generation = 10;
    result.content_sha256[0] = 0x42;
    std::memcpy(result.logical_path, "data/banks/example.bin", sizeof("data/banks/example.bin"));
    return result;
}
cold::DomainResult Ok(void*, const cold::RestartContext*) noexcept { return cold::DomainResult::Succeeded; }
bool Finalize(void* user,const cold::RestartContext* context,const cold::RegistryFinalizationToken* token) noexcept {
    auto& s=*static_cast<TestState*>(user);++s.finalizer_calls;
    auto copied_context=*context;auto copied_token=*token;
    s.checks &= cold::RegistryFinalizationIsCurrent(&s.registry,context,token) &&
        !cold::RegistryFinalizationIsCurrent(&s.registry,&copied_context,token) &&
        !cold::RegistryFinalizationIsCurrent(&s.registry,context,&copied_token) &&
        s.registry.transaction_prepared && s.registry.touch_transaction_state==3 &&
        (token->verified_mask&token->required_mask)==token->required_mask;
    bool foreign_rejected=false;
    std::thread other([&]{foreign_rejected=!cold::RegistryFinalizationIsCurrent(&s.registry,context,token);});other.join();
    s.checks &= foreign_rejected;
    if(s.test==TestCase::FinalizerLateProducer) {
        bool rejected=false;
        std::thread producer([&]{
            cold::RetainingObservationToken call={};
            rejected=!cold::BeginRetainingObservation(&s.registry,cold::ContentDomainId::Audio,
                cold::RetainingObservationOrigin::ExternalProducer,&call);
            if(call.active)(void)cold::EndRetainingObservation(&s.registry,&call);
        });producer.join();
        s.checks &= rejected && !cold::RegistryFinalizationIsCurrent(&s.registry,context,token);
    }
    return s.test!=TestCase::FinalizerReject;
}
cold::DomainResult InspectCallback(void* user, const cold::RestartContext* context) noexcept {
    auto& s = *static_cast<TestState*>(user);
    if (s.test != TestCase::CallbackContext) return cold::DomainResult::Succeeded;
    const cold::RestartContext* actual = nullptr;
    cold::ContentDomainId domain = cold::ContentDomainId::Count;
    cold::RestartPhase phase = cold::RestartPhase::Idle;
    auto copied = *context;
    s.checks &= cold::SnapshotRegistryCallbackContext(&s.registry, &actual, &domain, &phase) &&
        actual == context && domain == cold::ContentDomainId::Audio &&
        (phase == cold::RestartPhase::Preflighting || phase == cold::RestartPhase::Quiescing) &&
        !s.registry.transaction_prepared && !s.registry.prepared_transaction_generation &&
        !s.registry.prepared_content_epoch &&
        ProductionDomainTransactionMatches(context, cold::ContentDomainId::Audio) &&
        !ProductionDomainTransactionMatches(&copied, cold::ContentDomainId::Audio) &&
        !ProductionDomainTransactionMatches(context, cold::ContentDomainId::Curves);
    bool foreign_rejected = false;
    std::thread foreign([&] {
        const cold::RestartContext* foreign_context = context;
        cold::ContentDomainId foreign_domain = cold::ContentDomainId::Audio;
        cold::RestartPhase foreign_phase = phase;
        foreign_rejected = !cold::SnapshotRegistryCallbackContext(&s.registry,
            &foreign_context, &foreign_domain, &foreign_phase) && !foreign_context &&
            foreign_domain == cold::ContentDomainId::Count && foreign_phase == cold::RestartPhase::Idle;
    });
    foreign.join();
    s.checks &= foreign_rejected;
    return s.checks ? cold::DomainResult::Succeeded : cold::DomainResult::Failed;
}
cold::DomainResult Mutate(void* user, const cold::RestartContext* context) noexcept {
    return cold::MarkRegistryMutationStarted(&static_cast<TestState*>(user)->registry, context)
        ? cold::DomainResult::Succeeded : cold::DomainResult::Failed;
}
cold::DomainResult Replay(void* user, const cold::RestartContext* context) noexcept {
    auto& s = *static_cast<TestState*>(user);
    auto& registry = s.registry;
    if (s.test == TestCase::XuiClosedAdmission) {
        s.checks &= ExerciseXui(false);
        return cold::DomainResult::Failed;
    }
    cold::ReplayOperationIdentity expected[] = {Identity(1, 0), Identity(2, 1), Identity(3, 1)};
    cold::RegistryReplayPermitToken permit = {};
    const auto revision = registry.retaining_observation_revision;
    auto altered_context = *context;
    if (s.test == TestCase::WrongContext) {
        s.checks &= !cold::BeginRegistryReplayPermit(&registry, &altered_context, expected, 3, &permit);
        return cold::DomainResult::Failed;
    }
    if (!cold::BeginRegistryReplayPermit(&registry, context, expected, 3, &permit)) {
        s.checks = false; return cold::DomainResult::Failed;
    }
    cold::RegistryReplayPermitToken current = {};
    s.checks &= cold::CurrentRegistryReplayPermit(&registry, cold::ContentDomainId::Audio, &current) &&
        current.generation == permit.generation;
    s.checks &= !cold::CurrentRegistryReplayPermit(&registry, cold::ContentDomainId::LocalizedTexts, &current);
    if (s.test == TestCase::AbandonedPermit) return cold::DomainResult::Succeeded;
    if (s.test == TestCase::UnsupportedOrigin) {
        { RetainingObservationCall missing(cold::ContentDomainId::Audio,
              cold::RetainingObservationOrigin::ExternalProducer);
          s.checks &= !missing.admitted(); }
        cold::RetainingObservationToken denied = {};
        s.checks &= !cold::BeginRetainingObservation(&registry, cold::ContentDomainId::Audio,
            cold::RetainingObservationOrigin::ResetOwnedReplay, &denied);
        if (denied.active) s.checks &= cold::EndRetainingObservation(&registry, &denied);
        (void)cold::EndRegistryReplayPermit(&registry, &permit, false);
        return cold::DomainResult::Failed;
    }
    if (s.test == TestCase::ForeignReplay) {
        bool rejected = false;
        std::thread other([&] {
            cold::RetainingObservationToken denied = {};
            cold::RegistryReplayPermitToken foreign = {};
            rejected = !cold::CurrentRegistryReplayPermit(&registry, cold::ContentDomainId::Audio, &foreign) &&
                !cold::BeginRetainingReplayObservation(&registry, &permit, &expected[0], &denied);
            RetainingObservationCall wrapper(cold::ContentDomainId::Audio,
                cold::RetainingObservationOrigin::ExternalProducer, &expected[0]);
            rejected &= !wrapper.admitted();
        });
        other.join();
        s.checks &= rejected;
    }
    if (s.test == TestCase::ForeignProducer) {
        bool rejected = false;
        std::thread other([&] {
            cold::RetainingObservationToken denied = {};
            rejected = !cold::BeginRetainingObservation(&registry, cold::ContentDomainId::Audio,
                cold::RetainingObservationOrigin::ExternalProducer, &denied);
            if (denied.active) (void)cold::EndRetainingObservation(&registry, &denied);
            cold::RetainingObservationToken disabled = {};
            rejected &= !cold::BeginRetainingObservation(&registry, cold::ContentDomainId::LocalizedTexts,
                cold::RetainingObservationOrigin::ExternalProducer, &disabled) && disabled.active && !disabled.admitted;
            if (disabled.active) (void)cold::EndRetainingObservation(&registry, &disabled);
        });
        other.join();
        s.checks &= rejected;
    }
    cold::RetainingObservationToken parent = {}, child = {};
    auto actual = expected[0];
    switch (s.test) {
    case TestCase::DigestChanged: actual.content_sha256[0] ^= 1; break;
    case TestCase::ProviderChanged: ++actual.provider_generation; break;
    case TestCase::OwnerChanged: ++actual.owner; break;
    case TestCase::PathChanged: actual.logical_path[0] = 'x'; break;
    case TestCase::CopiedManifest: expected[0].content_sha256[0] ^= 1; break;
    default: break;
    }
    const bool altered = s.test >= TestCase::DigestChanged && s.test <= TestCase::PathChanged;
    if (altered) {
        { RetainingObservationCall wrapper(cold::ContentDomainId::Audio,
              cold::RetainingObservationOrigin::ExternalProducer, &actual);
          s.checks &= !wrapper.admitted(); }
        s.checks &= !cold::BeginRetainingReplayObservation(&registry, &permit, &actual, &parent);
        s.checks &= !cold::EndRegistryReplayPermit(&registry, &permit, true);
        return cold::DomainResult::Failed;
    }
    if (s.test == TestCase::Nested || s.test == TestCase::FileAuthority) {
        cold::ReplayFileAuthority parent_authority = {};
        {
            RetainingObservationCall parent_wrapper(cold::ContentDomainId::Audio,
                cold::RetainingObservationOrigin::ExternalProducer, &actual);
            s.checks &= parent_wrapper.admitted();
            files::ThreadScope factory_scope = {};
            files::FactoryToken factory = {};
            if (s.test == TestCase::FileAuthority) {
                s.checks &= cold::CaptureReplayFileAuthority(&registry, &actual, &parent_authority);
                s.checks &= cold::ValidateReplayFileAuthority(&registry, &parent_authority);
                for (unsigned field = 0; field < 4; ++field) {
                    auto changed = actual;
                    if (field == 0) ++changed.provider_generation;
                    if (field == 1) changed.content_sha256[0] ^= 1;
                    if (field == 2) changed.logical_path[0] = 'x';
                    if (field == 3) ++changed.owner;
                    cold::ReplayFileAuthority rejected = {};
                    s.checks &= !cold::CaptureReplayFileAuthority(&registry, &changed, &rejected) && !rejected.permit_generation;
                }
                bool foreign_rejected = false;
                std::thread foreign([&] { foreign_rejected = !cold::ValidateReplayFileAuthority(&registry, &parent_authority); });
                foreign.join(); s.checks &= foreign_rejected;
                files::Initialize(&native_files);
                s.checks &= files::ConfigureFactoryReplayValidator(&native_files, &ValidateProductionFactoryReplayAuthorization, nullptr);
                s.checks &= files::CompleteHookInstallation(&native_files, 1, 1) && files::CloseAdmission(&native_files, context->transaction_generation);
                const files::FactoryReplayAuthorization factory_authority = {parent_authority.registry_identity,
                    parent_authority.permit_generation, parent_authority.observation, parent_authority.transaction_generation,
                    parent_authority.content_epoch, parent_authority.owner_thread_id};
                s.checks &= !files::BeginFactory(&native_files, &factory_scope, &factory);
                s.checks &= files::BeginReplayFactory(&native_files, &factory_scope, &factory, factory_authority);
                files::FactoryToken denied_child = {};
                s.checks &= !files::BeginFactory(&native_files, &factory_scope, &denied_child);
                auto wrong_observation = factory_authority; ++wrong_observation.observation;
                s.checks &= !files::BeginReplayFactory(&native_files, &factory_scope, &denied_child, wrong_observation);
            }
            { RetainingObservationCall child_wrapper(cold::ContentDomainId::Audio,
                  cold::RetainingObservationOrigin::ExternalProducer, &expected[1]);
              s.checks &= child_wrapper.admitted();
              if (s.test == TestCase::FileAuthority) {
                  s.checks &= !cold::ValidateReplayFileAuthority(&registry, &parent_authority);
                  cold::ReplayFileAuthority child_authority = {};
                  s.checks &= cold::CaptureReplayFileAuthority(&registry, &expected[1], &child_authority);
                  const files::FactoryReplayAuthorization child_factory_authority = {child_authority.registry_identity, child_authority.permit_generation,
                      child_authority.observation, child_authority.transaction_generation, child_authority.content_epoch, child_authority.owner_thread_id};
                  files::FactoryToken child_factory = {};
                  s.checks &= files::BeginReplayFactory(&native_files, &factory_scope, &child_factory, child_factory_authority);
                  s.checks &= files::EndFactory(&native_files, &factory_scope, &child_factory);
              }
            }
            if (s.test == TestCase::FileAuthority) {
                s.checks &= cold::ValidateReplayFileAuthority(&registry, &parent_authority);
                s.checks &= files::EndFactory(&native_files, &factory_scope, &factory);
            }
            { RetainingObservationCall child_wrapper(cold::ContentDomainId::Audio,
                  cold::RetainingObservationOrigin::ExternalProducer, &expected[2]);
              s.checks &= child_wrapper.admitted(); }
        }
        if (s.test == TestCase::FileAuthority) {
            s.checks &= !cold::ValidateReplayFileAuthority(&registry, &parent_authority);
            s.checks &= native_files.active_factories == 0;
        }
        s.checks &= cold::EndRegistryReplayPermit(&registry, &permit, true);
        s.checks &= revision == registry.retaining_observation_revision;
        return cold::DomainResult::Succeeded;
    }
    if (!cold::BeginRetainingReplayObservation(&registry, &permit, &actual, &parent)) {
        s.checks = false; return cold::DomainResult::Failed;
    }
    if (s.test == TestCase::MissingChild) {
        s.checks &= cold::EndRetainingObservation(&registry, &parent);
        s.checks &= !cold::EndRegistryReplayPermit(&registry, &permit, true);
        return cold::DomainResult::Failed;
    }
    auto second = expected[1];
    if (s.test == TestCase::WrongParent) {
        second.parent_ordinal = 0;
        s.checks &= !cold::BeginRetainingReplayObservation(&registry, &permit, &second, &child);
        s.checks &= cold::EndRetainingObservation(&registry, &parent);
        s.checks &= !cold::EndRegistryReplayPermit(&registry, &permit, true);
        return cold::DomainResult::Failed;
    }
    s.checks &= cold::BeginRetainingReplayObservation(&registry, &permit, &second, &child);
    if (s.test == TestCase::OutOfOrderReturn)
        s.checks &= !cold::EndRetainingObservation(&registry, &parent);
    s.checks &= cold::EndRetainingObservation(&registry, &child);
    if (s.test == TestCase::DuplicateReturn)
        s.checks &= !cold::EndRetainingObservation(&registry, &child);
    if (s.test == TestCase::OutOfOrderReturn || s.test == TestCase::DuplicateReturn) {
        s.checks &= cold::EndRetainingObservation(&registry, &parent);
        s.checks &= !cold::EndRegistryReplayPermit(&registry, &permit, true);
        return cold::DomainResult::Failed;
    }
    s.checks &= cold::BeginRetainingReplayObservation(&registry, &permit, &expected[2], &child);
    s.checks &= cold::EndRetainingObservation(&registry, &child);
    s.checks &= cold::EndRetainingObservation(&registry, &parent);
    const auto stale = permit;
    const bool native_succeeded = s.test != TestCase::NativeFailure;
    const bool ended = cold::EndRegistryReplayPermit(&registry, &permit, native_succeeded);
    s.checks &= ended == native_succeeded;
    if (s.test != TestCase::ForeignProducer)
        s.checks &= revision == registry.retaining_observation_revision;
    if (s.test == TestCase::StalePermit) {
        auto single = Identity(1, 0);
        s.checks &= cold::BeginRegistryReplayPermit(&registry, context, &single, 1, &permit);
        s.checks &= !cold::BeginRetainingReplayObservation(&registry, &stale, &single, &parent);
        s.checks &= !cold::EndRegistryReplayPermit(&registry, &permit, true);
        return cold::DomainResult::Failed;
    }
    return native_succeeded ? cold::DomainResult::Succeeded : cold::DomainResult::Failed;
}
cold::ContentDomainAdapter Adapter(cold::ContentDomainId id, cold::ContentDomainPhaseFn reset) {
    cold::ContentDomainAdapter a = {};
    a.id = id;
    std::memcpy(a.name, "GenericOwner", sizeof("GenericOwner"));
    a.enabled = a.mandatory = true;
    a.destructive = id != cold::ContentDomainId::FrontendVerification;
    if (id != cold::ContentDomainId::VfsSources) a.dependency_mask = cold::DomainBit(cold::ContentDomainId::VfsSources);
    if (id == cold::ContentDomainId::FrontendVerification) a.dependency_mask |= cold::DomainBit(cold::ContentDomainId::Audio);
    a.user = &state;
    a.capture_baseline = a.preflight = a.quiesce = a.cancel_quiesce = a.reload = a.verify = &Ok;
    a.reset = reset;
    return a;
}
}
int main() {
    cold::InitializeRegistry(&state.registry);
    { RetainingObservationCall startup(cold::ContentDomainId::Audio,
          cold::RetainingObservationOrigin::ExternalProducer);
      if (!startup.admitted() || state.registry.retaining_observation_active_calls) return 1; }
    for (unsigned i = 0; i < static_cast<unsigned>(TestCase::Count); ++i) {
        cold::InitializeRegistry(&state.registry);
        state.test = static_cast<TestCase>(i); state.checks = true;state.finalizer_calls=0;
        auto source = Adapter(cold::ContentDomainId::VfsSources, &Mutate);
        auto audio = Adapter(cold::ContentDomainId::Audio, &Replay);
        audio.preflight = audio.quiesce = &InspectCallback;
        auto frontend = Adapter(cold::ContentDomainId::FrontendVerification, &Ok);
        state.checks &= cold::RegisterAdapter(&state.registry, &source) &&
            cold::RegisterAdapter(&state.registry, &audio) &&
            cold::RegisterAdapter(&state.registry, &frontend) &&
            cold::SetRegistryFinalizer(&state.registry,&Finalize,&state) && cold::SealRegistry(&state.registry);
        state.checks &= !cold::SetRegistryFinalizer(&state.registry,&Finalize,&state);
        cold::RestartContext context = {};
        context.baseline_generation = 10; context.owner_thread_id = GetCurrentThreadId();
        state.checks &= cold::CaptureRegistryBaseline(&state.registry, &context) == cold::RestartResult::Verified;
        state.checks &= ExerciseXui(true);
        { RetainingObservationCall disabled_wrapper(cold::ContentDomainId::LocalizedTexts,
              cold::RetainingObservationOrigin::ExternalProducer);
          state.checks &= disabled_wrapper.admitted() && state.registry.retaining_observation_active_calls == 1; }
        cold::RetainingObservationToken disabled_idle = {};
        state.checks &= (state.registry.enabled_mask & cold::DomainBit(cold::ContentDomainId::LocalizedTexts)) == 0 &&
            cold::BeginRetainingObservation(&state.registry, cold::ContentDomainId::LocalizedTexts,
                cold::RetainingObservationOrigin::ExternalProducer, &disabled_idle) &&
            disabled_idle.active && disabled_idle.admitted;
        if (disabled_idle.active) state.checks &= cold::EndRetainingObservation(&state.registry, &disabled_idle);
        state.checks &= !cold::DomainWasTouched(&state.registry, cold::ContentDomainId::LocalizedTexts);
        cold::RegistryReplayPermitToken outside = {};
        auto expected = Identity(1, 0);
        state.checks &= !cold::BeginRegistryReplayPermit(&state.registry, &context, &expected, 1, &outside);
        context.content_epoch = 1; context.transaction_generation = 1;
        const auto result = cold::RunRegistryTransaction(&state.registry, &context);
        const bool success = state.test == TestCase::Nested || state.test == TestCase::CopiedManifest ||
            state.test == TestCase::ForeignReplay || state.test == TestCase::FileAuthority ||
            state.test == TestCase::CallbackContext;
        state.checks &= success ? result == cold::RestartResult::Verified : result == cold::RestartResult::RuntimeFailed;
        const bool finalized=success || state.test==TestCase::FinalizerReject || state.test==TestCase::FinalizerLateProducer;
        state.checks &= state.finalizer_calls==(finalized?1U:0U);
        if(state.test==TestCase::FinalizerReject || state.test==TestCase::FinalizerLateProducer)
            state.checks &= state.registry.admission_blocked && !state.registry.completed_transaction_generation;
        state.checks &= state.registry.retaining_observation_active_calls == 0;
        const cold::RestartContext* expired_context = &context;
        cold::ContentDomainId expired_domain = cold::ContentDomainId::Audio;
        cold::RestartPhase expired_phase = cold::RestartPhase::Verifying;
        state.checks &= !cold::SnapshotRegistryCallbackContext(&state.registry,
            &expired_context, &expired_domain, &expired_phase) && !expired_context &&
            !ProductionDomainTransactionMatches(&context, cold::ContentDomainId::Audio);
        if (!state.checks) { std::printf("FAIL replay scenario %u result %s\n", i, cold::RestartResultName(result)); return 1; }
    }
    std::puts("PASS: 22 authenticated replay scenarios including actual XUI and native factory authority");
    return 0;
}
