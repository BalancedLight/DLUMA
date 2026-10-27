#include "../src/fixes/custom_map_asset_isolation/FrontendBaseline.h"
#include "../src/fixes/custom_map_asset_isolation/FrontendContentColdReboot.h"
#include "../src/fixes/custom_map_asset_isolation/FrontendColdRebootWindowsProfile.h"
#include "../src/fixes/custom_map_asset_isolation/FrontendSourceRollbackLifecycle.h"
#include "../src/fixes/custom_map_asset_isolation/StateFiveDestroyOwner.h"

#include <cstdio>
#include <cstring>

namespace cold =
    dluma::fixes::custom_map_asset_isolation::cold_reboot;
namespace coldwin =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::windows;
namespace lifecycle =
    dluma::fixes::custom_map_asset_isolation::lifecycle;
namespace statefive =
    dluma::fixes::custom_map_asset_isolation::state_five_owner;

namespace {

struct FakeClock {
    std::uint64_t qpc;
    std::uint32_t thread_id;
};

struct FakeDomain {
    cold::DomainResult baseline;
    cold::DomainResult preflight;
    cold::DomainResult quiesce;
    cold::DomainResult cancel;
    cold::DomainResult reset;
    cold::DomainResult reload;
    cold::DomainResult verify;
    cold::DomainResult retire_providers;
    std::uint32_t calls[8];
    std::uint32_t* order;
    std::uint32_t* order_count;
    std::uint32_t marker;
    cold::FrontendContentColdReboot* nested_reboot;
    const cold::ColdRebootBarrierEvidence* nested_evidence;
    cold::ColdRebootOutcome nested_outcome;
    cold::ContentDomainRestartRegistry* registry;
    bool starts_registry_mutation;
    bool fail_before_mutation;
    bool touch_enabled;
    std::uint32_t touch_phase;
    cold::ContentDomainId touch_domain;
    bool retaining_enabled;
    bool retaining_end_immediately;
    std::uint32_t retaining_phase;
    cold::ContentDomainId retaining_domain;
    cold::RetainingObservationToken retaining_token;
    lifecycle::FrontendSourceRollbackBarrier* source_lifecycle;
    bool claim_source_lifecycle_on_reset;
    bool verify_prior_domain_generations;
    bool prior_domain_generations_valid;
};

std::uint64_t ReadQpc(void* user) noexcept
{
    FakeClock* clock = static_cast<FakeClock*>(user);
    return ++clock->qpc;
}

std::uint32_t ReadThreadId(void* user) noexcept
{
    return static_cast<FakeClock*>(user)->thread_id;
}

void Record(FakeDomain* domain, std::uint32_t phase) noexcept
{
    ++domain->calls[phase];
    if (domain->order && domain->order_count &&
        *domain->order_count < 256U)
        domain->order[(*domain->order_count)++] =
            domain->marker * 10U + phase;
}

void TouchIfRequested(FakeDomain* domain,
                      std::uint32_t phase) noexcept
{
    if (domain && domain->touch_enabled &&
        domain->touch_phase == phase && domain->registry)
        (void)cold::MarkDomainTouched(
            domain->registry, domain->touch_domain);
}

void RetainIfRequested(FakeDomain* domain,
                       std::uint32_t phase) noexcept
{
    if (!domain || !domain->retaining_enabled ||
        domain->retaining_phase != phase || !domain->registry)
        return;
    (void)cold::BeginRetainingObservation(
        domain->registry, domain->retaining_domain,
        cold::RetainingObservationOrigin::ExternalProducer,
        &domain->retaining_token);
    if (domain->retaining_end_immediately &&
        domain->retaining_token.active)
        (void)cold::EndRetainingObservation(
            domain->registry, &domain->retaining_token);
}

cold::DomainResult Baseline(void* user,
                            const cold::RestartContext*) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 0);
    return domain->baseline;
}

cold::DomainResult Preflight(void* user,
                             const cold::RestartContext*) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 1);
    RetainIfRequested(domain, 1);
    return domain->preflight;
}

cold::DomainResult Quiesce(void* user,
                           const cold::RestartContext*) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 2);
    return domain->quiesce;
}

cold::DomainResult CancelQuiesce(void* user,
                                 const cold::RestartContext*) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 6);
    return domain->cancel;
}

cold::DomainResult Reset(void* user,
                         const cold::RestartContext* context) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 3);
    if (domain->fail_before_mutation)
        return domain->reset;
    if (domain->starts_registry_mutation &&
        !cold::MarkRegistryMutationStarted(
            domain->registry, context))
        return cold::DomainResult::Failed;
    if (domain->claim_source_lifecycle_on_reset &&
        (!domain->source_lifecycle ||
         !lifecycle::ConsumeForReset(domain->source_lifecycle) ||
         !lifecycle::BeginReset(domain->source_lifecycle)))
        return cold::DomainResult::Failed;
    TouchIfRequested(domain, 3);
    RetainIfRequested(domain, 3);
    if (domain->nested_reboot && domain->nested_evidence)
        domain->nested_outcome = cold::EnterColdRebootBarrier(
            domain->nested_reboot, domain->nested_evidence);
    return domain->reset;
}

cold::DomainResult Reload(void* user,
                          const cold::RestartContext*) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 4);
    TouchIfRequested(domain, 4);
    RetainIfRequested(domain, 4);
    return domain->reload;
}

cold::DomainResult RetireProviders(void* user,
                                  const cold::RestartContext*) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 7);
    TouchIfRequested(domain, 7);
    RetainIfRequested(domain, 7);
    return domain->retire_providers;
}

cold::DomainResult Verify(void* user,
                          const cold::RestartContext* context) noexcept
{
    FakeDomain* domain = static_cast<FakeDomain*>(user);
    Record(domain, 5);
    TouchIfRequested(domain, 5);
    RetainIfRequested(domain, 5);
    if (domain->verify_prior_domain_generations) {
        cold::RestartContext stale = *context;
        --stale.transaction_generation;
        cold::RestartContext wrong_epoch = *context;
        ++wrong_epoch.content_epoch;
        cold::RestartContext wrong_baseline = *context;
        ++wrong_baseline.baseline_generation;
        domain->prior_domain_generations_valid =
            cold::DomainRestoredInCurrentTransaction(
                domain->registry, cold::ContentDomainId::PlayerGameplayParams,
                context) &&
            cold::DomainRestoredInCurrentTransaction(
                domain->registry, cold::ContentDomainId::Audio, context) &&
            !cold::DomainRestoredInCurrentTransaction(
                domain->registry, cold::ContentDomainId::Audio, &stale) &&
            !cold::DomainRestoredInCurrentTransaction(
                domain->registry, cold::ContentDomainId::Audio, &wrong_epoch) &&
            !cold::DomainRestoredInCurrentTransaction(
                domain->registry, cold::ContentDomainId::Audio, &wrong_baseline) &&
            !cold::DomainRestoredInCurrentTransaction(
                domain->registry, cold::ContentDomainId::FrontendVerification,
                context);
        if (!domain->prior_domain_generations_valid)
            return cold::DomainResult::Failed;
    }
    return domain->verify;
}

void CopyName(char* output, std::size_t capacity,
              const char* input) noexcept
{
    if (!output || !capacity) return;
    std::size_t index = 0;
    while (input && input[index] && index + 1U < capacity) {
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
}

cold::BaselinePlayerProviderIdentity PlayerProvider(
    std::uintptr_t source, std::uint64_t path_fingerprint,
    std::uint32_t source_record_index) noexcept
{
    cold::BaselinePlayerProviderIdentity provider = {};
    provider.source = source;
    provider.normalized_path_fingerprint = path_fingerprint;
    provider.source_registry_generation = 55U;
    provider.source_record_index = source_record_index;
    provider.source_inventory_ordinal = source_record_index + 1U;
    provider.observed = true;
    provider.exact_source = true;
    provider.base_provider = true;
    provider.source_registry_generation_observed = true;
    return provider;
}

cold::BaselineAudioProviderIdentity AudioProvider(
    std::uintptr_t source, std::uint64_t path_fingerprint,
    std::uint32_t source_record_index) noexcept
{
    cold::BaselineAudioProviderIdentity provider = {};
    provider.source = source;
    provider.normalized_path_fingerprint = path_fingerprint;
    provider.source_registry_generation = 55U;
    provider.source_record_index = source_record_index;
    provider.source_inventory_ordinal = source_record_index + 1U;
    provider.source_kind = cold::BaselineSourceKind::Base;
    provider.observed = true;
    provider.exact_source = true;
    provider.source_registry_generation_observed = true;
    return provider;
}

struct AudioSnapshotFixture {
    cold::BaselineAudioSettings settings;
    cold::BaselineAudioScript script;
    cold::BaselineAudioBank bank;
    cold::BaselineSpeechBank speech;
};

void InitializeAudioSnapshotFixture(AudioSnapshotFixture* fixture) noexcept
{
    if (!fixture) return;
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->settings.fingerprint = 99;
    fixture->settings.global_settings_selector_fingerprint = 100;
    fixture->settings.selector_available = true;
    fixture->settings.available = true;

    fixture->script.logical_path_fingerprint = 10;
    fixture->script.content_identity_fingerprint = 11;
    fixture->script.load_order = 1;
    fixture->script.provider_identity = AudioProvider(0x71000U, 12, 4);
    fixture->script.natural_load_observed = true;
    fixture->script.parse_succeeded = true;
    CopyName(fixture->script.logical_path,
             sizeof(fixture->script.logical_path),
             "data\\audio\\audiodata.scr");
    CopyName(fixture->script.provider, sizeof(fixture->script.provider),
             "base-audio-source");

    fixture->bank.logical_bank_id = 20;
    fixture->bank.provider_fingerprint = 21;
    fixture->bank.content_fingerprint = 22;
    fixture->bank.instance_generation = 23;
    fixture->bank.manager_record = 0x72000U;
    fixture->bank.retained_native_file = 0x73000U;
    fixture->bank.provider_identity = AudioProvider(0x71000U, 24, 4);
    fixture->bank.role = cold::BaselineAudioBankRole::Base;
    fixture->bank.dependency_order = 2;
    fixture->bank.membership_observed = true;
    fixture->bank.natural_load_observed = true;
    fixture->bank.dependency_order_observed = true;
    fixture->bank.reference_count = 0;
    fixture->bank.reference_count_observed = false;
    CopyName(fixture->bank.name, sizeof(fixture->bank.name), "base-bank");
    CopyName(fixture->bank.provider, sizeof(fixture->bank.provider),
             "base-provider");

    fixture->speech.logical_bank_id = 30;
    fixture->speech.provider_fingerprint = 31;
    fixture->speech.content_identity_fingerprint = 32;
    fixture->speech.instance_generation = 33;
    fixture->speech.bank_record = 0x74000U;
    fixture->speech.load_order = 3;
    fixture->speech.provider_identity = AudioProvider(0x75000U, 34, 5);
    fixture->speech.membership_observed = true;
    fixture->speech.natural_load_observed = true;
    fixture->speech.load_order_observed = true;
    fixture->speech.localized = true;
    CopyName(fixture->speech.name, sizeof(fixture->speech.name),
             "base-speech");
    CopyName(fixture->speech.provider, sizeof(fixture->speech.provider),
             "speech-provider");
}

bool AudioSnapshotStorageEmpty(const cold::FrontendBaseline& baseline) noexcept
{
    return !baseline.audio_script_count && !baseline.audio_bank_count &&
        !baseline.speech_bank_count &&
        !baseline.audio_settings_complete &&
        !baseline.audio_script_inventory_complete &&
        !baseline.audio_fmod_inventory_complete &&
        !baseline.audio_speech_inventory_complete &&
        !baseline.audio_provider_coverage_complete &&
        !baseline.audio_replay_owner_complete &&
        !baseline.audio_inventory_complete &&
        !baseline.audio_settings.fingerprint &&
        !baseline.audio_scripts[0].logical_path_fingerprint &&
        !baseline.audio_scripts[1].logical_path_fingerprint &&
        !baseline.audio_banks[0].logical_bank_id &&
        !baseline.audio_banks[1].logical_bank_id &&
        !baseline.speech_banks[0].logical_bank_id &&
        !baseline.speech_banks[1].logical_bank_id;
}

cold::ContentDomainAdapter MakeAdapter(
    cold::ContentDomainId id, const char* name,
    cold::ContentDomainMask dependencies, bool mandatory,
    FakeDomain* domain) noexcept
{
    cold::ContentDomainAdapter adapter = {};
    adapter.id = id;
    CopyName(adapter.name, sizeof(adapter.name), name);
    adapter.dependency_mask = dependencies;
    adapter.enabled = true;
    adapter.mandatory = mandatory;
    adapter.destructive = true;
    adapter.user = domain;
    adapter.capture_baseline = &Baseline;
    adapter.preflight = &Preflight;
    adapter.quiesce = &Quiesce;
    adapter.cancel_quiesce = &CancelQuiesce;
    adapter.reset = &Reset;
    adapter.reload = &Reload;
    adapter.verify = &Verify;
    return adapter;
}

void Succeed(FakeDomain* domain, std::uint32_t marker,
             std::uint32_t* order, std::uint32_t* count) noexcept
{
    std::memset(domain, 0, sizeof(*domain));
    domain->baseline = cold::DomainResult::Succeeded;
    domain->preflight = cold::DomainResult::Succeeded;
    domain->quiesce = cold::DomainResult::Succeeded;
    domain->cancel = cold::DomainResult::Succeeded;
    domain->reset = cold::DomainResult::Succeeded;
    domain->reload = cold::DomainResult::Succeeded;
    domain->verify = cold::DomainResult::Succeeded;
    domain->retire_providers = cold::DomainResult::Succeeded;
    domain->marker = marker;
    domain->order = order;
    domain->order_count = count;
}

bool Setup(cold::ContentDomainRestartRegistry* registry,
           cold::FrontendContentColdReboot* reboot,
           cold::ColdRebootCriticalQueue* queue,
           FakeClock* clock, FakeDomain* vfs, FakeDomain* player,
           FakeDomain* audio, FakeDomain* verify,
           std::uint32_t* order, std::uint32_t* order_count,
           FakeDomain* optional = nullptr,
           bool enabled = true,
           bool retire_providers = false) noexcept
{
    cold::InitializeRegistry(registry);
    Succeed(vfs, 1, order, order_count);
    Succeed(player, 2, order, order_count);
    Succeed(audio, 3, order, order_count);
    Succeed(verify, 4, order, order_count);
    if (optional) Succeed(optional, 5, order, order_count);
    vfs->registry = registry;
    player->registry = registry;
    audio->registry = registry;
    verify->registry = registry;
    if (optional) optional->registry = registry;
    vfs->starts_registry_mutation = true;
    cold::ContentDomainAdapter vfs_adapter = MakeAdapter(
        cold::ContentDomainId::VfsSources, "VfsSources", 0, true, vfs);
    if (retire_providers) vfs_adapter.retire_providers = &RetireProviders;
    cold::ContentDomainAdapter player_adapter = MakeAdapter(
        cold::ContentDomainId::PlayerGameplayParams,
        "PlayerGameplayParams",
        cold::DomainBit(cold::ContentDomainId::VfsSources), true, player);
    cold::ContentDomainAdapter audio_adapter = MakeAdapter(
        cold::ContentDomainId::Audio, "Audio",
        cold::DomainBit(cold::ContentDomainId::VfsSources), true, audio);
    cold::ContentDomainAdapter verify_adapter = MakeAdapter(
        cold::ContentDomainId::FrontendVerification,
        "FrontendVerification",
        cold::DomainBit(cold::ContentDomainId::PlayerGameplayParams) |
            cold::DomainBit(cold::ContentDomainId::Audio),
        true, verify);
    if (!cold::RegisterAdapter(registry, &vfs_adapter) ||
        !cold::RegisterAdapter(registry, &player_adapter) ||
        !cold::RegisterAdapter(registry, &audio_adapter))
        return false;
    if (optional) {
        cold::ContentDomainAdapter optional_adapter = MakeAdapter(
            cold::ContentDomainId::Curves, "Curves",
            cold::DomainBit(cold::ContentDomainId::VfsSources),
            false, optional);
        if (!cold::RegisterAdapter(registry, &optional_adapter)) return false;
    }
    if (!cold::RegisterAdapter(registry, &verify_adapter)) return false;
    cold::InitializeCriticalQueue(queue);
    cold::ColdRebootPlatform platform = {};
    platform.read_qpc = &ReadQpc;
    platform.read_thread_id = &ReadThreadId;
    platform.user = clock;
    cold::InitializeColdReboot(reboot, registry, queue, &platform, enabled);
    return cold::SealRegistry(registry);
}

bool SetupTouchedDomainPolicy(
    cold::ContentDomainRestartRegistry* registry,
    cold::FrontendContentColdReboot* reboot,
    cold::ColdRebootCriticalQueue* queue,
    FakeClock* clock, FakeDomain* vfs, FakeDomain* player,
    FakeDomain* audio, FakeDomain* global_domain,
    FakeDomain* unclassified, FakeDomain* verify,
    std::uint32_t* order, std::uint32_t* order_count,
    cold::ContentDomainMask verify_dependencies = 0) noexcept
{
    if (!registry || !reboot || !queue || !clock || !vfs || !player ||
        !audio || !global_domain || !unclassified || !verify)
        return false;
    cold::InitializeRegistry(registry);
    Succeed(vfs, 1, order, order_count);
    Succeed(player, 2, order, order_count);
    Succeed(audio, 3, order, order_count);
    Succeed(global_domain, 4, order, order_count);
    Succeed(unclassified, 5, order, order_count);
    Succeed(verify, 6, order, order_count);
    FakeDomain* domains[] = {
        vfs, player, audio, global_domain, unclassified, verify};
    for (FakeDomain* domain : domains) domain->registry = registry;
    vfs->starts_registry_mutation = true;

    const cold::ContentDomainMask vfs_dependency =
        cold::DomainBit(cold::ContentDomainId::VfsSources);
    if (!verify_dependencies) verify_dependencies = vfs_dependency;
    cold::ContentDomainAdapter vfs_adapter = MakeAdapter(
        cold::ContentDomainId::VfsSources, "VfsSources", 0, true, vfs);
    cold::ContentDomainAdapter player_adapter = MakeAdapter(
        cold::ContentDomainId::PlayerGameplayParams,
        "PlayerGameplayParams", vfs_dependency, false, player);
    cold::ContentDomainAdapter audio_adapter = MakeAdapter(
        cold::ContentDomainId::Audio, "Audio", vfs_dependency, false,
        audio);
    cold::ContentDomainAdapter global_adapter = MakeAdapter(
        cold::ContentDomainId::Curves, "Curves", vfs_dependency, false,
        global_domain);
    cold::ContentDomainAdapter unclassified_adapter = MakeAdapter(
        cold::ContentDomainId::UnclassifiedPersistentConsumers,
        "UnclassifiedPersistentConsumers", vfs_dependency, false,
        unclassified);
    cold::ContentDomainAdapter verify_adapter = MakeAdapter(
        cold::ContentDomainId::FrontendVerification,
        "FrontendVerification", verify_dependencies, true, verify);

    if (!cold::RegisterAdapter(registry, &vfs_adapter) ||
        !cold::RegisterAdapter(registry, &player_adapter) ||
        !cold::RegisterAdapter(registry, &audio_adapter) ||
        !cold::RegisterAdapter(registry, &global_adapter) ||
        !cold::RegisterAdapter(registry, &unclassified_adapter) ||
        !cold::RegisterAdapter(registry, &verify_adapter))
        return false;
    cold::InitializeCriticalQueue(queue);
    cold::ColdRebootPlatform platform = {};
    platform.read_qpc = &ReadQpc;
    platform.read_thread_id = &ReadThreadId;
    platform.user = clock;
    cold::InitializeColdReboot(reboot, registry, queue, &platform, true);
    return cold::SealRegistry(registry);
}

cold::RestartContext BaselineContext() noexcept
{
    cold::RestartContext context = {};
    context.baseline_generation = 1;
    context.owner_thread_id = 77;
    return context;
}

cold::ColdRebootBarrierEvidence ValidEvidence() noexcept
{
    cold::ColdRebootBarrierEvidence evidence = {};
    evidence.game_di = reinterpret_cast<void*>(0x1000U);
    evidence.state = 5;
    evidence.content_epoch = 9;
    evidence.current_thread_id = 77;
    evidence.game_thread_id = 77;
    evidence.caller_return_rva = 0x003D0B71U;
    evidence.exact_state_five_owner = true;
    evidence.please_wait_active = true;
    evidence.pipeline_wait_returned = true;
    evidence.session_destroyed = true;
    evidence.source_reset_ready = true;
    return evidence;
}

cold::ColdRebootPreflightEvidence ValidPreflightEvidence() noexcept
{
    cold::ColdRebootPreflightEvidence evidence = {};
    evidence.game_di = reinterpret_cast<void*>(0x1000U);
    evidence.state = 5;
    evidence.content_epoch = 9;
    evidence.current_thread_id = 77;
    evidence.game_thread_id = 77;
    evidence.caller_return_rva = 0x003D0970U;
    evidence.exact_state_five_owner = true;
    evidence.please_wait_active = true;
    evidence.pipeline_wait_returned = true;
    return evidence;
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

bool TestVerifiedTransaction() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {100, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(reboot.completed_epoch == 9);
    CHECK(!reboot.restart_required);
    CHECK(registry.touch_transaction_state == 0);
    CHECK(cold::ObserveShowMenuMain(&reboot, 9, 77) ==
          cold::ShowMenuMainDisposition::DelegateOriginal);
    CHECK(vfs.calls[3] == 1 && player.calls[3] == 1 &&
          audio.calls[3] == 1 && verify.calls[3] == 1);
    bool saw_reset = false;
    std::uint32_t last_preflight = 0;
    std::uint32_t first_quiesce = 0xFFFFFFFFU;
    for (std::uint32_t index = 0; index < order_count; ++index) {
        const std::uint32_t phase = order[index] % 10U;
        if (phase == 1U) last_preflight = index;
        if (phase == 2U && first_quiesce == 0xFFFFFFFFU)
            first_quiesce = index;
        if (phase == 3U) saw_reset = true;
        if (saw_reset) continue;
        CHECK(phase == 0U || phase == 1U || phase == 2U);
    }
    CHECK(first_quiesce != 0xFFFFFFFFU &&
          last_preflight < first_quiesce);
    cold::ColdRebootCriticalEvent events[256] = {};
    const std::size_t event_count =
        cold::DrainCriticalEvents(&queue, events, 256);
    CHECK(event_count > 10);
    for (std::size_t index = 1; index < event_count; ++index)
        CHECK(events[index].sequence > events[index - 1U].sequence);
    return true;
}

bool TestTerminalVerificationRejectsPriorEpochSuccess() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {100, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const auto baseline = BaselineContext();
    CHECK(cold::CaptureRegistryBaseline(&registry, &baseline) ==
          cold::RestartResult::Verified);
    verify.verify_prior_domain_generations = true;
    for (unsigned i = 0; i != 2; ++i) {
        cold::RestartContext context = baseline;
        context.transaction_generation = 20 + i;
        context.content_epoch = 50 + i;
        verify.prior_domain_generations_valid = false;
        CHECK(cold::RunRegistryTransaction(&registry, &context) ==
              cold::RestartResult::Verified);
        CHECK(verify.prior_domain_generations_valid);
        CHECK(!cold::DomainRestoredInCurrentTransaction(
            &registry, cold::ContentDomainId::Audio, &context));
    }
    return true;
}

bool TestProviderRetirementFollowsConsumerRelease() noexcept
{
    for (unsigned scenario = 0; scenario != 4; ++scenario) {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {100, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count, nullptr,
                    true, true));
        if (scenario == 1) player.reset = cold::DomainResult::Failed;
        if (scenario == 2)
            vfs.retire_providers = cold::DomainResult::Failed;
        if (scenario == 3) {
            vfs.retaining_enabled = true;
            vfs.retaining_end_immediately = true;
            vfs.retaining_phase = 7;
            vfs.retaining_domain = cold::ContentDomainId::Audio;
        }
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const auto preflight = ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(
                  &reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        const cold::ColdRebootOutcome outcome =
            cold::EnterColdRebootBarrier(&reboot, &evidence);
        if (scenario == 0) {
            CHECK(outcome == cold::ColdRebootOutcome::Verified);
            CHECK(vfs.calls[7] == 1);
            std::uint32_t last_reset = 0, retirement = 0;
            std::uint32_t first_reload = 0xFFFFFFFFU;
            for (std::uint32_t i = 0; i < order_count; ++i) {
                if (order[i] % 10U == 3) last_reset = i;
                if (order[i] % 10U == 7) retirement = i;
                if (order[i] % 10U == 4 && first_reload == 0xFFFFFFFFU)
                    first_reload = i;
            }
            CHECK(last_reset < retirement && retirement < first_reload);
            CHECK(registry.states[0].retire_providers_result ==
                  cold::DomainResult::Succeeded);
        } else {
            CHECK(outcome != cold::ColdRebootOutcome::Verified);
            CHECK(vfs.calls[7] == (scenario == 1 ? 0U : 1U));
            CHECK(!cold::CanAdmitCustomProject(&registry));
            FakeDomain* domains[] = {&vfs, &player, &audio, &verify};
            for (FakeDomain* domain : domains) {
                CHECK(domain->calls[4] == 0 && domain->calls[5] == 0);
            }
        }
        const auto retire_calls = vfs.calls[7];
        (void)cold::EnterColdRebootBarrier(&reboot, &evidence);
        CHECK(vfs.calls[7] == retire_calls);
    }
    FakeDomain domain = {};
    cold::ContentDomainAdapter invalid = MakeAdapter(
        cold::ContentDomainId::Audio, "Audio", 0, true, &domain);
    invalid.retire_providers = &RetireProviders;
    CHECK(!cold::ValidateAdapter(&invalid));
    return true;
}

bool TestIncompleteMandatoryBaselineCannotArmOrMutate() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    audio.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::PreflightFailed);
    CHECK(!registry.baseline_captured);
    CHECK(!reboot.baseline_captured);
    CHECK(!cold::ArmColdRebootForEpoch(&reboot, 9));
    FakeDomain* domains[] = {&vfs, &player, &audio, &verify};
    for (FakeDomain* domain : domains) {
        for (std::uint32_t phase = 1; phase < 7; ++phase)
            CHECK(domain->calls[phase] == 0);
    }
    CHECK(!reboot.mutation_started);
    CHECK(!registry.mutation_started);
    return true;
}

bool TestDependencyPhaseOrdering() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);

    const std::uint32_t expected[] = {
        10, 20, 30, 40,       /* baseline: dependencies first */
        11, 21, 31, 41,       /* preflight: dependencies first */
        42, 32, 22, 12,       /* quiesce: dependents first */
        13, 23, 33, 43,       /* reset: dependencies first */
        14, 24, 34, 44,       /* reload: dependencies first */
        15, 25, 35, 45};      /* verify: dependencies first */
    CHECK(order_count == sizeof(expected) / sizeof(expected[0]));
    for (std::uint32_t index = 0; index < order_count; ++index)
        CHECK(order[index] == expected[index]);
    return true;
}

bool TestTwoPointPreflightPrecedesNaturalSourceTeardown() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(registry.transaction_prepared);
    CHECK(vfs.calls[1] == 1 && player.calls[1] == 1 &&
          audio.calls[1] == 1 && verify.calls[1] == 1);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0);
    /* The game-owned DestroySession/source rollback occurs here. */
    const cold::ColdRebootBarrierEvidence barrier = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &barrier) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(!registry.transaction_prepared);
    CHECK(vfs.calls[1] == 1 && player.calls[1] == 1);
    CHECK(vfs.calls[3] == 1 && player.calls[3] == 1);
    return true;
}

bool TestPreflightFailureMutatesNothing() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    audio.preflight = cold::DomainResult::Unsupported;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    CHECK(reboot.restart_required);
    CHECK(reboot.armed_epoch == 9U);
    CHECK(reboot.show_menu_guard_armed);
    CHECK(!cold::CanAdmitCustomProject(&reboot));
    CHECK(cold::ObserveShowMenuMain(&reboot, 9, 77) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    return true;
}

bool TestQuiesceFailureMutatesNothing() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    player.quiesce = cold::DomainResult::Failed;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    CHECK(!registry.mutation_started);
    CHECK(vfs.calls[6] == 1 && player.calls[6] == 0 &&
          audio.calls[6] == 1 && verify.calls[6] == 1);
    return true;
}

bool TestOrphanedQuiesceCancelFailureLatches() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    player.quiesce = cold::DomainResult::Failed;
    verify.cancel = cold::DomainResult::Failed;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::RuntimeFailed);
    CHECK(registry.runtime_failure_latched);
    CHECK(registry.admission_blocked);
    CHECK(reboot.restart_required);
    CHECK(!registry.transaction_prepared);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    return true;
}

bool TestFirstResetInvalidationCancelsBeforeMutation() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    vfs.fail_before_mutation = true;
    vfs.reset = cold::DomainResult::Failed;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(!registry.mutation_started &&
          !reboot.mutation_started && reboot.restart_required &&
          reboot.barrier_seen && reboot.armed_epoch == 9U);
    CHECK(vfs.calls[3] == 1 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    CHECK(vfs.calls[6] == 1 && player.calls[6] == 1 &&
          audio.calls[6] == 1 && verify.calls[6] == 1);
    CHECK(!cold::CanAdmitCustomProject(&reboot));
    CHECK(cold::ObserveShowMenuMain(&reboot, 9, 77) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    return true;
}

bool TestFirstResetInvalidationCancelFailureLatches() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    vfs.fail_before_mutation = true;
    vfs.reset = cold::DomainResult::Failed;
    player.cancel = cold::DomainResult::Failed;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::RuntimeFailed);
    CHECK(!registry.mutation_started && registry.admission_blocked &&
          registry.runtime_failure_latched && reboot.restart_required);
    CHECK(reboot.last_reject_reason ==
          cold::BarrierRejectReason::QuiesceCancelFailure);
    CHECK(player.calls[3] == 0 && audio.calls[3] == 0 &&
          verify.calls[3] == 0);
    return true;
}

bool TestPostMutationFailureBlocksAdmissionAndMenu() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    player.reset = cold::DomainResult::Failed;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::RuntimeFailed);
    CHECK(reboot.restart_required);
    CHECK(!cold::CanAdmitCustomProject(&reboot));
    CHECK(cold::ObserveShowMenuMain(&reboot, 9, 77) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    return true;
}

bool TestOptionalAdapterExclusionAndFailureLatch() noexcept
{
    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {},
                   optional = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count, &optional));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        optional.preflight = cold::DomainResult::Unsupported;
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        CHECK(optional.calls[1] == 0 && optional.calls[2] == 0);
        const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::Verified);
        CHECK(optional.calls[3] == 0 && optional.calls[4] == 0 &&
              optional.calls[5] == 0);
        CHECK(!reboot.restart_required);
    }

    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {},
                   optional = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count, &optional));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        optional.reset = cold::DomainResult::Failed;
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::Verified);
        CHECK(optional.calls[3] == 0 &&
              !registry.runtime_failure_latched &&
              !registry.admission_blocked && !reboot.restart_required);
    }
    return true;
}

bool TestTouchedOptionalDomainCannotBeDropped() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {},
               optional = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count, &optional));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::Curves));
    CHECK(cold::DomainWasTouched(
        &registry, cold::ContentDomainId::Curves));
    optional.preflight = cold::DomainResult::Unsupported;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(optional.calls[1] == 1);
    CHECK(optional.calls[2] == 0 && optional.calls[3] == 0 &&
          optional.calls[4] == 0 && optional.calls[5] == 0);
    CHECK(!registry.mutation_started && !reboot.mutation_started);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    return true;
}

bool TestTouchedDomainWithoutBaselineBlocksBeforePreflight() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {},
               optional = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count, &optional));
    optional.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::Curves));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(optional.calls[1] == 0 && optional.calls[2] == 0 &&
          optional.calls[3] == 0);
    CHECK(vfs.calls[1] == 0 && vfs.calls[3] == 0);
    CHECK(!registry.mutation_started && !reboot.mutation_started);
    return true;
}

bool TestPlayerTouchedWithoutPlayerBaselineBlocksBeforeVfsMutation() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    player.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    const cold::ContentDomainMask player_bit = cold::DomainBit(
        cold::ContentDomainId::PlayerGameplayParams);
    CHECK((registry.baseline_ready_mask & player_bit) == 0);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(vfs.calls[1] == 0 && vfs.calls[3] == 0);
    CHECK(player.calls[1] == 0 && player.calls[3] == 0);
    CHECK(!registry.mutation_started && !reboot.mutation_started);
    return true;
}

bool TestLateTouchInvalidatesPreparedSubsetBeforeMutation() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {},
               optional = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count, &optional));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    optional.preflight = cold::DomainResult::Unsupported;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(optional.calls[1] == 0);
    CHECK(registry.transaction_prepared);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::Curves));
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(!registry.mutation_started && !reboot.mutation_started);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    CHECK(vfs.calls[6] == 1 && player.calls[6] == 1 &&
          audio.calls[6] == 1 && verify.calls[6] == 1);
    return true;
}

bool TestPlayerUntouchedAudioUntouchedVfsAndVerifyReachVerified() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    player.baseline = cold::DomainResult::Unsupported;
    audio.baseline = cold::DomainResult::Unsupported;
    global_domain.baseline = cold::DomainResult::Unsupported;
    unclassified.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(registry.mandatory_mask ==
          (cold::DomainBit(cold::ContentDomainId::VfsSources) |
           cold::DomainBit(cold::ContentDomainId::FrontendVerification)));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(vfs.calls[3] == 1 && verify.calls[3] == 1);
    CHECK(player.calls[1] == 0 && player.calls[3] == 0 &&
          audio.calls[1] == 0 && audio.calls[3] == 0 &&
          global_domain.calls[1] == 0 && global_domain.calls[3] == 0 &&
          unclassified.calls[1] == 0 && unclassified.calls[3] == 0);
    CHECK(reboot.last_registry_result == cold::RestartResult::Verified);
    return true;
}

bool TestOptionalPlayerBaselineAddsToReadyMaskBeforeFirstEpoch() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    player.baseline = cold::DomainResult::Unsupported;
    audio.baseline = cold::DomainResult::Unsupported;
    global_domain.baseline = cold::DomainResult::Unsupported;
    unclassified.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    const cold::ContentDomainMask player_bit = cold::DomainBit(
        cold::ContentDomainId::PlayerGameplayParams);
    CHECK((registry.baseline_ready_mask & player_bit) == 0);

    player.baseline = cold::DomainResult::Succeeded;
    CHECK(cold::ExtendRegistryBaseline(
              &registry, &baseline, player_bit) ==
          cold::RestartResult::Verified);
    CHECK((registry.baseline_ready_mask & player_bit) == player_bit);
    const std::uint32_t captures_after_fill = player.calls[0];
    CHECK(cold::ExtendRegistryBaseline(
              &registry, &baseline, player_bit) ==
          cold::RestartResult::Verified);
    CHECK(player.calls[0] == captures_after_fill);

    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(cold::ExtendRegistryBaseline(
              &registry, &baseline,
              cold::DomainBit(cold::ContentDomainId::Audio)) ==
          cold::RestartResult::PreflightFailed);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(vfs.calls[3] == 1 && player.calls[3] == 1 &&
          verify.calls[3] == 1 && audio.calls[3] == 0);
    return true;
}

bool TestPlayerTouchedTransactionOrdersVfsPlayerVerify() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    audio.baseline = cold::DomainResult::Unsupported;
    global_domain.baseline = cold::DomainResult::Unsupported;
    unclassified.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    const std::uint32_t expected_reset_order[] = {13U, 23U, 63U};
    std::uint32_t reset_index = 0;
    for (std::uint32_t index = 0; index < order_count; ++index) {
        if (order[index] % 10U != 3U) continue;
        CHECK(reset_index < 3U);
        CHECK(order[index] == expected_reset_order[reset_index++]);
    }
    CHECK(reset_index == 3U);
    CHECK(vfs.calls[3] == 1 && player.calls[3] == 1 &&
          verify.calls[3] == 1);
    CHECK(audio.calls[3] == 0 && global_domain.calls[3] == 0 &&
          unclassified.calls[3] == 0);
    CHECK(reboot.last_registry_result == cold::RestartResult::Verified);
    return true;
}

statefive::PleaseWaitOwnerToken ValidStateFiveToken(
    std::uint32_t c08_thread = 1001U) noexcept
{
    return statefive::PublishPleaseWaitStateTwoOwnerToken(
        reinterpret_cast<void*>(0x1000U), 1U, 7U,
        0xA55A5AA55AA55AA5ULL, 41U, 9001U,
        c08_thread, true, false);
}

statefive::C10AttemptEvidence ValidStateFiveAttempt(
    std::uint32_t c10_thread = 2002U) noexcept
{
    statefive::C10AttemptEvidence attempt = {};
    attempt.game_di = reinterpret_cast<void*>(0x1000U);
    attempt.outgoing_session =
        reinterpret_cast<void*>(0x2000U);
    attempt.content_epoch = 1U;
    attempt.state = 5U;
    attempt.caller_rva =
        statefive::kExactStateFiveCallerRva;
    attempt.c10_callback_thread_id = c10_thread;
    attempt.current_frame_thread_id = c10_thread;
    attempt.frame_route_token = 0xA55A5AA55AA55AA5ULL;
    attempt.frame_authority_sequence = 52U;
    attempt.content_tracking_live = true;
    attempt.exact_hook_profile = true;
    attempt.state_readable = true;
    attempt.current_serialized_route_active = true;
    attempt.outgoing_session_readable = true;
    return attempt;
}

statefive::PostC10Expectation ValidPostC10Expectation() noexcept
{
    statefive::PostC10Expectation expectation = {};
    expectation.game_di = reinterpret_cast<void*>(0x1000U);
    expectation.outgoing_session =
        reinterpret_cast<void*>(0x2000U);
    expectation.outgoing_level =
        reinterpret_cast<void*>(0x3000U);
    expectation.baseline_generation = 11U;
    expectation.destroy_generation = 7U;
    expectation.frame_route_token =
        0xA55A5AA55AA55AA5ULL;
    expectation.frame_authority_sequence = 52U;
    expectation.source_set_identity = 0x123456789ABCDEF0ULL;
    expectation.source_registry_generation = 91U;
    expectation.content_epoch = 1U;
    expectation.c10_callback_thread_id = 2002U;
    expectation.owner_callback_depth = 1U;
    return expectation;
}

statefive::PendingPostC10Token ValidPendingPostC10Token() noexcept
{
    return statefive::MakePendingPostC10Token(
        ValidPostC10Expectation(), 1001U, 2002U,
        101U, 9002U, 102U, 1U, 1U, true, true);
}

statefive::PostC10Environment ValidPostC10Environment() noexcept
{
    const statefive::PostC10Expectation expectation =
        ValidPostC10Expectation();
    statefive::PostC10Environment environment = {};
    environment.frame_route_token =
        expectation.frame_route_token;
    environment.frame_authority_sequence =
        expectation.frame_authority_sequence;
    environment.source_set_identity =
        expectation.source_set_identity;
    environment.source_registry_generation =
        expectation.source_registry_generation;
    environment.current_thread_id =
        expectation.c10_callback_thread_id;
    environment.owner_callback_depth =
        expectation.owner_callback_depth;
    environment.current_session_slot_readable = true;
    environment.frame_route_active = true;
    return environment;
}

bool TestPendingPostC10OwnerMatrix() noexcept
{
    const statefive::PostC10Expectation expected =
        ValidPostC10Expectation();
    const statefive::PostC10Environment valid_environment =
        ValidPostC10Environment();
    statefive::PendingPostC10Token token =
        ValidPendingPostC10Token();
    CHECK(token.active && !token.consumed && !token.conflict);
    CHECK(statefive::ValidatePostC10PreflightOwner(
              nullptr, expected, &valid_environment) ==
          statefive::PostC10Failure::TokenMissing);
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &valid_environment) ==
          statefive::PostC10Failure::None);

    statefive::PendingPostC10Token slot = {};
    CHECK(statefive::PublishPendingPostC10Token(
              &slot, token) ==
          statefive::PostC10Failure::None);
    CHECK(slot.active &&
          slot.token_publication_sequence ==
              token.token_publication_sequence);
    CHECK(statefive::PublishPendingPostC10Token(
              &slot, token) ==
          statefive::PostC10Failure::TokenMismatch);
    CHECK(slot.conflict);

    token = ValidPendingPostC10Token();
    token.conflict = true;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &valid_environment) ==
          statefive::PostC10Failure::TokenMismatch);
    token = ValidPendingPostC10Token();
    token.consumed = true;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &valid_environment) ==
          statefive::PostC10Failure::TokenReplayed);

    statefive::PostC10Expectation changed = expected;
    changed.outgoing_session =
        reinterpret_cast<void*>(0x2001U);
    token = ValidPendingPostC10Token();
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, changed, &valid_environment) ==
          statefive::PostC10Failure::TokenMismatch);
    changed = expected;
    ++changed.content_epoch;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, changed, &valid_environment) ==
          statefive::PostC10Failure::TokenMismatch);
    changed = expected;
    ++changed.destroy_generation;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, changed, &valid_environment) ==
          statefive::PostC10Failure::TokenMismatch);
    changed = expected;
    ++changed.frame_route_token;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, changed, &valid_environment) ==
          statefive::PostC10Failure::RouteMismatch);

    statefive::PostC10Environment environment =
        valid_environment;
    ++environment.current_thread_id;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::WrongThread);
    environment = valid_environment;
    environment.frame_authority_conflict = true;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::AuthorityConflict);
    environment = valid_environment;
    environment.current_session_slot_readable = false;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::SessionSlotUnreadable);
    environment = valid_environment;
    environment.current_session = expected.outgoing_session;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::
              OutgoingSessionStillPublished);
    environment = valid_environment;
    environment.current_session =
        reinterpret_cast<void*>(0x2002U);
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::
              UnexpectedReplacementSession);
    environment = valid_environment;
    environment.active_level =
        reinterpret_cast<void*>(0x3000U);
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::ActiveLevelStillPresent);
    environment = valid_environment;
    environment.local_player_di =
        reinterpret_cast<void*>(0x4000U);
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::LocalPlayerStillPresent);
    environment = valid_environment;
    environment.live_gameplay_consumer = true;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::LiveGameplayConsumer);
    environment = valid_environment;
    ++environment.source_registry_generation;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &token, expected, &environment) ==
          statefive::PostC10Failure::SourceRegistryChanged);

    slot = {};
    token = ValidPendingPostC10Token();
    CHECK(statefive::PublishPendingPostC10Token(
              &slot, token) ==
          statefive::PostC10Failure::None);
    CHECK(statefive::ConsumePendingPostC10Token(
              &slot, expected, &valid_environment) ==
          statefive::PostC10Failure::None);
    CHECK(slot.consumed);
    CHECK(statefive::ConsumePendingPostC10Token(
              &slot, expected, &valid_environment) ==
          statefive::PostC10Failure::TokenReplayed);

    CHECK(std::strcmp(
              statefive::PostC10FailureName(
                  statefive::PostC10Failure::
                      OutgoingSessionStillPublished),
              "post_c10_outgoing_session_still_published") == 0);
    CHECK(std::strcmp(
              statefive::PostC10FailureName(
                  statefive::PostC10Failure::
                      SourceRegistryChanged),
              "post_c10_source_registry_changed") == 0);
    return true;
}

bool TestStateFiveDestroyOwnerAuthorizationMatrix() noexcept
{
    statefive::C10AttemptEvidence attempt =
        ValidStateFiveAttempt();
    statefive::PleaseWaitOwnerToken token =
        ValidStateFiveToken();
    CHECK(token.completed && !token.consumed);
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10ScopeMatched);
    CHECK(token.c08_callback_thread_id !=
          attempt.c10_callback_thread_id);

    statefive::StateFiveScope scope = {};
    std::uint64_t last_consumed_generation = 0;
    CHECK(statefive::BeginExactStateFiveScope(
              &scope, &token, attempt, 1U,
              &last_consumed_generation) ==
          statefive::RejectReason::C10ScopeMatched);
    CHECK(scope.active && token.consumed);
    CHECK(scope.c08_callback_thread_id == 1001U);
    CHECK(scope.c10_callback_thread_id == 2002U);
    CHECK(last_consumed_generation == 7U);
    scope.armed = true;
    CHECK(statefive::BindSessionOnDestroyToStateFiveScope(
              &scope, attempt.outgoing_session, 2002U, 1U) ==
          statefive::RejectReason::C10ScopeMatched);
    CHECK(scope.active && scope.on_destroy_claimed);
    CHECK(statefive::BindSessionOnDestroyToStateFiveScope(
              &scope, attempt.outgoing_session, 2002U, 1U) ==
          statefive::RejectReason::SessionScopeConsumed);
    /* Nested teardown does not clear the owner. Only the outer C10 return
     * performs the explicit finish represented here. */
    CHECK(scope.active);
    scope = {};
    CHECK(!scope.active);

    token = ValidStateFiveToken(2002U);
    attempt = ValidStateFiveAttempt(2002U);
    scope = {};
    last_consumed_generation = 0;
    CHECK(statefive::BeginExactStateFiveScope(
              &scope, &token, attempt, 1U,
              &last_consumed_generation) ==
          statefive::RejectReason::C10ScopeMatched);
    CHECK(scope.c08_callback_thread_id ==
          scope.c10_callback_thread_id);

    attempt = ValidStateFiveAttempt();
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              nullptr, attempt, 0) ==
          statefive::RejectReason::C08NotObserved);
    token = statefive::PublishPleaseWaitStateTwoOwnerToken(
        attempt.game_di, 1U, 0U, attempt.frame_route_token,
        41U, 9001U, 1001U, false, false);
    CHECK(!token.completed);
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C08StateNotTwo);
    token = statefive::PublishPleaseWaitStateTwoOwnerToken(
        attempt.game_di, 1U, 0U, attempt.frame_route_token,
        41U, 9001U, 1001U, true, false);
    CHECK(!token.completed);
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C08TokenGenerationMissing);

    token = ValidStateFiveToken();
    attempt.game_di = reinterpret_cast<void*>(0x1001U);
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C08TokenGameDiMismatch);
    attempt = ValidStateFiveAttempt();
    attempt.content_epoch = 2U;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C08TokenEpochMismatch);
    attempt = ValidStateFiveAttempt();
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, token.destroy_generation) ==
          statefive::RejectReason::C08TokenStale);
    token.consumed = true;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C08TokenConsumed);

    token = ValidStateFiveToken();
    attempt = ValidStateFiveAttempt();
    attempt.exact_hook_profile = false;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10HookProfileInvalid);
    attempt = ValidStateFiveAttempt();
    ++attempt.caller_rva;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10CallerRvaInvalid);
    attempt = ValidStateFiveAttempt();
    attempt.state = 4U;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10StateNotFive);
    attempt = ValidStateFiveAttempt();
    ++attempt.frame_route_token;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10FrameRouteChanged);
    attempt = ValidStateFiveAttempt();
    attempt.frame_authority_conflict = true;
    attempt.current_serialized_route_active = false;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10FrameAuthorityConflict);
    attempt = ValidStateFiveAttempt();
    attempt.current_serialized_route_active = false;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10FrameRouteUnavailable);
    attempt = ValidStateFiveAttempt();
    attempt.outgoing_session_readable = false;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10SessionUnreadable);
    attempt = ValidStateFiveAttempt();
    attempt.outgoing_session = nullptr;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10SessionMissing);
    attempt = ValidStateFiveAttempt();
    attempt.next_project_staging = true;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10NextProjectStaging);
    attempt = ValidStateFiveAttempt();
    attempt.shutdown_requested = true;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10Shutdown);
    attempt = ValidStateFiveAttempt();
    attempt.conflicting_scope_active = true;
    CHECK(statefive::AuthorizeStateFiveC10AcrossWorkerMigration(
              &token, attempt, 0) ==
          statefive::RejectReason::C10ScopeAlreadyActive);

    token = ValidStateFiveToken();
    attempt = ValidStateFiveAttempt();
    scope = {};
    last_consumed_generation = 0;
    CHECK(statefive::BeginExactStateFiveScope(
              &scope, &token, attempt, 1U,
              &last_consumed_generation) ==
          statefive::RejectReason::C10ScopeMatched);
    scope.armed = true;
    CHECK(statefive::BindSessionOnDestroyToStateFiveScope(
              &scope, reinterpret_cast<void*>(0x2001U),
              2002U, 1U) ==
          statefive::RejectReason::SessionMismatch);
    CHECK(statefive::BindSessionOnDestroyToStateFiveScope(
              &scope, attempt.outgoing_session, 2003U, 1U) ==
          statefive::RejectReason::SessionWrongThread);
    CHECK(statefive::BindSessionOnDestroyToStateFiveScope(
              &scope, attempt.outgoing_session, 2002U, 2U) ==
          statefive::RejectReason::SessionDepthMismatch);

    statefive::PersistedResult persisted = {};
    statefive::PersistC10Result(
        &persisted, statefive::RejectReason::C10ArmRejected);
    scope = {};
    CHECK(std::strcmp(
              statefive::RejectReasonName(
                  persisted.c10_reject_reason),
              "c10_arm_rejected") == 0);
    CHECK(persisted.c10_disposition ==
          statefive::Disposition::Rejected);
    statefive::PersistSessionOnDestroyResult(
        &persisted, persisted.c10_reject_reason);
    CHECK(persisted.session_on_destroy_disposition ==
          statefive::Disposition::Rejected);
    CHECK(persisted.session_on_destroy_reject_reason ==
          statefive::RejectReason::C10ArmRejected);
    return true;
}

bool TestC08C10OnDestroyPostOriginalPlayerOnlyIntegratedReplay() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    audio.baseline = cold::DomainResult::Unsupported;
    global_domain.baseline = cold::DomainResult::Unsupported;
    unclassified.baseline = cold::DomainResult::Unsupported;
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));

    constexpr std::uint32_t epoch = 9;
    constexpr std::uint32_t c08_thread = 1001;
    constexpr std::uint32_t c10_thread = 2002;
    constexpr std::uint32_t game_thread = c10_thread;
    void* const game_di = reinterpret_cast<void*>(0x1000U);
    void* const session = reinterpret_cast<void*>(0x2000U);
    void* const outgoing_level = reinterpret_cast<void*>(0x3000U);

    statefive::PleaseWaitOwnerToken c08 =
        statefive::PublishPleaseWaitStateTwoOwnerToken(
            game_di, epoch, 1U, 0x12345678U, 11U,
            9001U, c08_thread, true, false);
    statefive::C10AttemptEvidence c10 = {};
    c10.game_di = game_di;
    c10.outgoing_session = session;
    c10.content_epoch = epoch;
    c10.state = 5U;
    c10.caller_rva =
        statefive::kExactStateFiveCallerRva;
    c10.c10_callback_thread_id = c10_thread;
    c10.current_frame_thread_id = c10_thread;
    c10.frame_route_token = c08.frame_route_token;
    c10.frame_authority_sequence = 12U;
    c10.content_tracking_live = true;
    c10.exact_hook_profile = true;
    c10.state_readable = true;
    c10.current_serialized_route_active = true;
    c10.outgoing_session_readable = true;
    statefive::StateFiveScope c10_scope = {};
    std::uint64_t last_consumed_generation = 0;
    CHECK(statefive::BeginExactStateFiveScope(
              &c10_scope, &c08, c10, 1U,
              &last_consumed_generation) ==
          statefive::RejectReason::C10ScopeMatched);
    CHECK(c08.consumed && c08_thread != c10_thread);
    CHECK(coldwin::kDestroyingSessionCallbackSlot == 0x0C10U);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, epoch));
    CHECK(reboot.armed_epoch == epoch);
    c10_scope.armed = true;
    CHECK(statefive::BindSessionOnDestroyToStateFiveScope(
              &c10_scope, session, c10_thread, 1U) ==
          statefive::RejectReason::C10ScopeMatched);
    CHECK(c10_scope.active && c10_scope.on_destroy_claimed);

    lifecycle::FrontendSourceRollbackBarrier source_barrier = {};
    lifecycle::Initialize(&source_barrier);
    CHECK(lifecycle::BeginOnDestroy(
        &source_barrier, epoch, session, outgoing_level, game_thread));
    CHECK(!lifecycle::ObserveOnDestroyOwnedUnload(
        &source_barrier, lifecycle::kAfterFadeUnloadReturnRva,
        session, outgoing_level, game_thread, false));
    CHECK(lifecycle::ObserveOnDestroyOwnedUnload(
        &source_barrier, lifecycle::kOnDestroyUnloadReturnRva,
        session, outgoing_level, game_thread, false));
    CHECK(lifecycle::BeginOwnedLevelRelease(
        &source_barrier, outgoing_level, game_thread, false));
    CHECK(lifecycle::CompleteOwnedLevelRelease(
        &source_barrier, outgoing_level, game_thread, false));
    CHECK(lifecycle::CompleteOnDestroyOwnedUnload(&source_barrier));
    std::uint32_t session_on_destroy_original_calls = 0;
    ++session_on_destroy_original_calls;
    CHECK(lifecycle::MarkOriginalReturned(&source_barrier));
    CHECK(lifecycle::ReadyForReset(&source_barrier));
    CHECK(session_on_destroy_original_calls == 1U);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          verify.calls[3] == 0);

    statefive::PostC10Expectation post_c10_expected = {};
    post_c10_expected.game_di = game_di;
    post_c10_expected.outgoing_session = session;
    post_c10_expected.outgoing_level = outgoing_level;
    post_c10_expected.baseline_generation =
        reboot.baseline_generation;
    post_c10_expected.destroy_generation =
        c08.destroy_generation;
    post_c10_expected.frame_route_token =
        c10.frame_route_token;
    post_c10_expected.frame_authority_sequence =
        c10.frame_authority_sequence;
    post_c10_expected.source_set_identity = 0xAA55U;
    post_c10_expected.source_registry_generation = 90U;
    post_c10_expected.content_epoch = epoch;
    post_c10_expected.c10_callback_thread_id = c10_thread;
    post_c10_expected.owner_callback_depth = 1U;
    statefive::PendingPostC10Token pending =
        statefive::MakePendingPostC10Token(
            post_c10_expected, c08_thread, c10_thread,
            100U, 101U, 102U, 1U, 1U, true, true);
    statefive::PendingPostC10Token pending_slot = {};
    CHECK(statefive::PublishPendingPostC10Token(
              &pending_slot, pending) ==
          statefive::PostC10Failure::None);

    /*
     * SessionDI::OnDestroy has returned exactly once, but the nested
     * DestroySession publication is transitional. It records the outgoing
     * session and performs no final preflight or source mutation.
     */
    statefive::PostC10Environment nested_environment = {};
    nested_environment.current_session = session;
    nested_environment.frame_route_token =
        post_c10_expected.frame_route_token;
    nested_environment.frame_authority_sequence =
        post_c10_expected.frame_authority_sequence;
    nested_environment.source_set_identity =
        post_c10_expected.source_set_identity;
    nested_environment.source_registry_generation =
        post_c10_expected.source_registry_generation;
    nested_environment.current_thread_id = c10_thread;
    nested_environment.owner_callback_depth = 1U;
    nested_environment.current_session_slot_readable = true;
    nested_environment.frame_route_active = true;
    CHECK(statefive::ValidatePostC10PreflightOwner(
              &pending_slot, post_c10_expected,
              &nested_environment) ==
          statefive::PostC10Failure::
              OutgoingSessionStillPublished);
    CHECK(!pending_slot.consumed);
    CHECK(vfs.calls[1] == 0 && player.calls[1] == 0 &&
          vfs.calls[3] == 0 && player.calls[3] == 0);

    std::uint32_t c10_original_calls = 0;
    ++c10_original_calls;
    statefive::PostC10Environment post_c10_environment =
        nested_environment;
    post_c10_environment.current_session = nullptr;
    CHECK(statefive::ConsumePendingPostC10Token(
              &pending_slot, post_c10_expected,
              &post_c10_environment) ==
          statefive::PostC10Failure::None);
    CHECK(c10_original_calls == 1U && pending_slot.consumed);

    /* The exact post-C10 return now owns preparation and immediate
     * execution. The VFS adapter consumes the reset-ready lifecycle proof. */
    cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    preflight.game_di = game_di;
    preflight.content_epoch = epoch;
    preflight.current_thread_id = c10_thread;
    preflight.game_thread_id = c10_thread;
    preflight.caller_return_rva =
        statefive::kExactStateFiveCallerRva;
    CHECK(preflight.caller_return_rva ==
          statefive::kExactStateFiveCallerRva);
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0);

    vfs.source_lifecycle = &source_barrier;
    vfs.claim_source_lifecycle_on_reset = true;
    cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    evidence.game_di = game_di;
    evidence.content_epoch = epoch;
    evidence.current_thread_id = c10_thread;
    evidence.game_thread_id = c10_thread;
    evidence.caller_return_rva =
        statefive::kExactStateFiveCallerRva;
    CHECK(evidence.caller_return_rva ==
          statefive::kExactStateFiveCallerRva);
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(source_barrier.phase ==
          lifecycle::FrontendRollbackPhase::ResetInProgress);
    lifecycle::CompleteReset(&source_barrier, true, false);
    CHECK(lifecycle::ObserveFirstFrontendLookup(&source_barrier) ==
          lifecycle::FrontendLookupOrder::AfterReset);

    const std::uint32_t expected_reset_order[] = {13U, 23U, 63U};
    std::uint32_t reset_index = 0;
    for (std::uint32_t index = 0; index < order_count; ++index) {
        if (order[index] % 10U != 3U) continue;
        CHECK(reset_index < 3U);
        CHECK(order[index] == expected_reset_order[reset_index++]);
    }
    CHECK(reset_index == 3U);
    CHECK(vfs.calls[3] == 1 && player.calls[3] == 1 &&
          verify.calls[3] == 1 && audio.calls[3] == 0);
    CHECK(reboot.completed_epoch == epoch &&
          reboot.last_registry_result == cold::RestartResult::Verified);
    cold::FrontendCompletionEvidence completion = {};
    completion.armed_epoch =
        static_cast<std::uint32_t>(reboot.armed_epoch);
    completion.prepared_epoch = reboot.prepared_epoch;
    completion.completed_epoch =
        static_cast<std::uint32_t>(reboot.completed_epoch);
    completion.completed_transaction_generation =
        reboot.completed_transaction_generation;
    completion.frontend_verified = true;
    CHECK(cold::ObserveFirstFrontendResourceLookup(
              &reboot, &completion, game_thread) ==
          cold::FrontendLookupDisposition::AllowVerifiedReset);
    CHECK(cold::ObserveShowMenuMain(
              &reboot, &completion, game_thread) ==
          cold::ShowMenuMainDisposition::DelegateOriginal);
    CHECK(c10_scope.active);
    c10_scope = {};
    CHECK(!c10_scope.active);
    return true;
}

bool TestTouchedUnsupportedDomainsBlockBeforeVfsMutation() noexcept
{
    const cold::ContentDomainId touched_domains[] = {
        cold::ContentDomainId::Audio,
        cold::ContentDomainId::Curves,
        cold::ContentDomainId::UnclassifiedPersistentConsumers,
    };
    for (cold::ContentDomainId touched : touched_domains) {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
                   unclassified = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(SetupTouchedDomainPolicy(
            &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
            &global_domain, &unclassified, &verify, order, &order_count));
        FakeDomain* unavailable = touched == cold::ContentDomainId::Audio
            ? &audio
            : touched == cold::ContentDomainId::Curves
                ? &global_domain
                : &unclassified;
        unavailable->baseline = cold::DomainResult::Unsupported;
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(cold::MarkDomainTouched(&registry, touched));
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
              cold::ColdRebootOutcome::PreflightFailed);
        CHECK(vfs.calls[1] == 0 && vfs.calls[3] == 0);
        CHECK(!registry.mutation_started && !reboot.mutation_started);
    }
    return true;
}

bool TestAudioTouchedWithReadyBaselineButUnsupportedOwnerBlocksAllMutation()
    noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    const cold::ContentDomainMask audio_bit = cold::DomainBit(
        cold::ContentDomainId::Audio);
    CHECK((registry.baseline_ready_mask & audio_bit) == audio_bit);
    audio.preflight = cold::DomainResult::Unsupported;
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::Audio));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(audio.calls[1] == 1);
    CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
          audio.calls[3] == 0 && verify.calls[3] == 0);
    CHECK(!registry.mutation_started && !reboot.mutation_started);
    cold::FrontendCompletionEvidence completion = {};
    completion.active_custom_source_count = 3U;
    completion.unconsumed_epoch_count = 1U;
    completion.armed_epoch = 9U;
    CHECK(cold::ObserveShowMenuMain(
              &reboot, &completion, 2002U) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    CHECK(reboot.no_level_failure_presentation_count == 1U);
    CHECK(cold::ClaimNoLevelFailurePresentation(&reboot));
    CHECK(!cold::ClaimNoLevelFailurePresentation(&reboot));
    cold::NoteNoLevelFailurePresentation(
        &reboot, true, 2002U);
    CHECK(cold::ObserveShowMenuMain(
              &reboot, &completion, 2002U) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    CHECK(reboot.no_level_failure_presentation_count == 1U);
    CHECK(reboot.failure_panel_delivered);
    return true;
}

bool TestPlayerTouchedButPreflightUnsupportedHasNoSourceOnlyFallback() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    player.preflight = cold::DomainResult::Unsupported;
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(player.calls[1] == 1 && vfs.calls[3] == 0);
    CHECK(!registry.mutation_started && !reboot.mutation_started);
    return true;
}

bool TestParticipationCombinationsPreserveRequiredOrder() noexcept
{
    const cold::ContentDomainMask player_bit =
        cold::DomainBit(cold::ContentDomainId::PlayerGameplayParams);
    const cold::ContentDomainMask audio_bit =
        cold::DomainBit(cold::ContentDomainId::Audio);
    const cold::ContentDomainMask touched_masks[] = {
        0, player_bit, audio_bit, player_bit | audio_bit};
    const std::uint32_t expected_markers[][4] = {
        {1U, 6U, 0U, 0U},
        {1U, 2U, 6U, 0U},
        {1U, 3U, 6U, 0U},
        {1U, 2U, 3U, 6U},
    };
    const std::uint32_t expected_counts[] = {2U, 3U, 3U, 4U};
    for (std::uint32_t iteration = 0; iteration < 4U; ++iteration) {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
                   unclassified = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(SetupTouchedDomainPolicy(
            &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
            &global_domain, &unclassified, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        if (touched_masks[iteration] & player_bit)
            CHECK(cold::MarkDomainTouched(
                &registry, cold::ContentDomainId::PlayerGameplayParams));
        if (touched_masks[iteration] & audio_bit)
            CHECK(cold::MarkDomainTouched(
                &registry, cold::ContentDomainId::Audio));
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::Verified);
        std::uint32_t reset_index = 0;
        for (std::uint32_t index = 0; index < order_count; ++index) {
            if (order[index] % 10U != 3U) continue;
            CHECK(reset_index < expected_counts[iteration]);
            CHECK(order[index] / 10U ==
                  expected_markers[iteration][reset_index++]);
        }
        CHECK(reset_index == expected_counts[iteration]);
        CHECK(global_domain.calls[3] == 0 &&
              unclassified.calls[3] == 0);
        CHECK(reboot.last_registry_result == cold::RestartResult::Verified);
    }
    return true;
}

bool TestUntouchedStaticDependenciesDoNotBlockTerminalVerify() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    const cold::ContentDomainMask dependencies =
        cold::DomainBit(cold::ContentDomainId::VfsSources) |
        cold::DomainBit(cold::ContentDomainId::PlayerGameplayParams) |
        cold::DomainBit(cold::ContentDomainId::Audio);
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count,
        dependencies));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(vfs.calls[3] == 1 && verify.calls[3] == 1);
    CHECK(player.calls[3] == 0 && audio.calls[3] == 0);
    return true;
}

bool TestSuccessfulEpochClearsTouchedMask() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, global_domain = {},
               unclassified = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(SetupTouchedDomainPolicy(
        &registry, &reboot, &queue, &clock, &vfs, &player, &audio,
        &global_domain, &unclassified, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::MarkDomainTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    cold::ColdRebootPreflightEvidence preflight = ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(!cold::DomainWasTouched(
        &registry, cold::ContentDomainId::PlayerGameplayParams));
    CHECK(registry.touched_mask == 0);
    CHECK(player.calls[3] == 1);

    CHECK(cold::ArmColdRebootForEpoch(&reboot, 10));
    preflight.content_epoch = 10;
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    evidence.content_epoch = 10;
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(vfs.calls[3] == 2 && verify.calls[3] == 2);
    CHECK(player.calls[3] == 1 && audio.calls[3] == 0);
    return true;
}

bool TestRepeatedTouchCannotRacePostMutationPhases() noexcept
{
    const std::uint32_t phases[] = {3U, 4U, 5U};
    for (std::uint32_t phase : phases) {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        /* Player is already mandatory.  Mark it before Prepare, then repeat
         * the same touch in a post-mutation phase.  The mask does not change,
         * so this specifically exercises the touch revision handshake. */
        CHECK(cold::MarkDomainTouched(
            &registry, cold::ContentDomainId::PlayerGameplayParams));
        FakeDomain* trigger = phase == 5U ? &verify : &vfs;
        trigger->touch_enabled = true;
        trigger->touch_phase = phase;
        trigger->touch_domain =
            cold::ContentDomainId::PlayerGameplayParams;
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(
                  &reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::RuntimeFailed);
        CHECK(reboot.restart_required);
        CHECK(!cold::CanAdmitCustomProject(&reboot));
        CHECK(registry.touch_transaction_state == 0);
        if (phase == 3U) {
            CHECK(vfs.calls[3] == 1);
            CHECK(player.calls[3] == 0 && audio.calls[3] == 0 &&
                  verify.calls[3] == 0);
        } else if (phase == 4U) {
            CHECK(vfs.calls[4] == 1);
            CHECK(player.calls[4] == 0 && audio.calls[4] == 0 &&
                  verify.calls[4] == 0);
        } else {
            CHECK(verify.calls[5] == 1);
        }
    }
    return true;
}

bool TestInFlightRetainingObservationBlocksPrepare() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);

    cold::RetainingObservationToken first = {};
    cold::RetainingObservationToken second = {};
    CHECK(cold::BeginRetainingObservation(
        &registry, cold::ContentDomainId::PlayerGameplayParams,
        cold::RetainingObservationOrigin::ExternalProducer, &first));
    CHECK(cold::BeginRetainingObservation(
        &registry, cold::ContentDomainId::PlayerGameplayParams,
        cold::RetainingObservationOrigin::ExternalProducer, &second));
    CHECK(first.active && second.active);
    CHECK(registry.retaining_observation_active_calls == 2);
    CHECK(!cold::CanAdmitCustomProject(&registry));
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(vfs.calls[1] == 0 && player.calls[1] == 0 &&
          audio.calls[1] == 0 && verify.calls[1] == 0);
    CHECK(registry.touch_transaction_state == 0);
    CHECK(cold::EndRetainingObservation(&registry, &first));
    CHECK(registry.retaining_observation_active_calls == 1);
    CHECK(!cold::CanAdmitCustomProject(&registry));
    CHECK(cold::EndRetainingObservation(&registry, &second));
    CHECK(registry.retaining_observation_active_calls == 0);
    CHECK(cold::CanAdmitCustomProject(&registry));
    return true;
}

bool TestRepeatedSameDomainRetainingActivityInvalidatesPreflight() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    cold::RetainingObservationToken prior = {};
    CHECK(cold::BeginRetainingObservation(
        &registry, cold::ContentDomainId::PlayerGameplayParams,
        cold::RetainingObservationOrigin::ExternalProducer, &prior));
    CHECK(cold::EndRetainingObservation(&registry, &prior));
    vfs.retaining_enabled = true;
    vfs.retaining_end_immediately = true;
    vfs.retaining_phase = 1;
    vfs.retaining_domain =
        cold::ContentDomainId::PlayerGameplayParams;
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(vfs.calls[1] == 1 && player.calls[1] == 0);
    CHECK(vfs.calls[2] == 0 && vfs.calls[3] == 0);
    CHECK(registry.retaining_observation_active_calls == 0);
    CHECK(registry.touch_transaction_state == 0);
    return true;
}

bool TestProducerStraddlingFinalCommitFailsClosed() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    const cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    verify.retaining_enabled = true;
    verify.retaining_end_immediately = false;
    verify.retaining_phase = 5;
    verify.retaining_domain = cold::ContentDomainId::Audio;
    const cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::RuntimeFailed);
    CHECK(verify.retaining_token.active);
    CHECK(registry.retaining_observation_active_calls == 1);
    CHECK(reboot.restart_required);
    CHECK(!cold::CanAdmitCustomProject(&registry));
    CHECK(cold::EndRetainingObservation(
        &registry, &verify.retaining_token));
    CHECK(registry.retaining_observation_active_calls == 0);
    CHECK(!cold::CanAdmitCustomProject(&registry));
    return true;
}

bool TestProducerClaimsCommittingWindow() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));

    /* The numeric states are part of the public POD handshake contract:
     * 3 is committing and a producer must atomically claim state 4. */
    registry.touch_transaction_state = 3;
    cold::RetainingObservationToken producer = {};
    CHECK(!cold::BeginRetainingObservation(
        &registry, cold::ContentDomainId::Audio,
        cold::RetainingObservationOrigin::ExternalProducer, &producer));
    CHECK(producer.active);
    CHECK(!producer.admitted);
    CHECK(registry.touch_transaction_state == 4);
    CHECK(registry.runtime_failure_latched != 0);
    CHECK(cold::EndRetainingObservation(&registry, &producer));
    CHECK(registry.retaining_observation_active_calls == 0);
    CHECK(!cold::CanAdmitCustomProject(&registry));
    return true;
}

bool TestResetOwnedReplayOriginFailsClosedUntilImplemented() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    cold::RetainingObservationToken replay = {};
    CHECK(!cold::BeginRetainingObservation(
        &registry, cold::ContentDomainId::Audio,
        cold::RetainingObservationOrigin::ResetOwnedReplay, &replay));
    CHECK(replay.active);
    CHECK(cold::EndRetainingObservation(&registry, &replay));
    CHECK(registry.runtime_failure_latched != 0);
    CHECK(!cold::CanAdmitCustomProject(&registry));
    return true;
}

bool TestSecondEpochPreflightFailureIsStillPreMutation() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    const cold::ColdRebootBarrierEvidence first = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &first) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(reboot.mutation_started);

    CHECK(cold::ArmColdRebootForEpoch(&reboot, 10));
    CHECK(!reboot.mutation_started);
    preflight.content_epoch = 10;
    audio.preflight = cold::DomainResult::Unsupported;
    CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
          cold::ColdRebootOutcome::PreflightFailed);
    CHECK(reboot.restart_required);
    CHECK(reboot.armed_epoch == 10U);
    CHECK(reboot.show_menu_guard_armed);
    CHECK(!cold::CanAdmitCustomProject(&reboot));
    /* A prior completed epoch cannot authorize the frontend once the current
     * armed epoch's mandatory preflight has failed. */
    CHECK(cold::ObserveShowMenuMain(&reboot, 9, 77) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    return true;
}

bool TestShowMenuRejectsIncompleteArmedEpoch() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
    CHECK(cold::ObserveShowMenuMain(
              &reboot, static_cast<std::uint32_t>(0),
              static_cast<std::uint32_t>(77)) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    CHECK(reboot.restart_required &&
          !cold::CanAdmitCustomProject(&reboot));
    CHECK(reboot.last_reject_reason ==
          cold::BarrierRejectReason::PreflightNotPrepared);
    return true;
}

bool TestFrontendCompletionEvidenceFailsClosedWithoutGuard() noexcept
{
    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(!reboot.show_menu_guard_armed);
        cold::FrontendCompletionEvidence evidence = {};
        evidence.active_custom_source_count = 1;
        CHECK(cold::ObserveShowMenuMain(&reboot, &evidence, 77) ==
              cold::ShowMenuMainDisposition::BlockRestartRequired);
        CHECK(reboot.restart_required && reboot.show_menu_guard_armed);
        CHECK(reboot.last_reject_reason ==
              cold::BarrierRejectReason::FrontendContaminated);
    }
    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(!reboot.show_menu_guard_armed);
        cold::FrontendCompletionEvidence evidence = {};
        evidence.unconsumed_epoch_count = 1;
        CHECK(cold::ObserveShowMenuMain(&reboot, &evidence, 77) ==
              cold::ShowMenuMainDisposition::BlockRestartRequired);
        CHECK(reboot.restart_required && reboot.show_menu_guard_armed);
    }
    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        cold::FrontendCompletionEvidence evidence = {};
        evidence.active_custom_source_count = 1;
        evidence.unconsumed_epoch_count = 1;
        evidence.armed_epoch = 9;
        CHECK(cold::ObserveFirstFrontendResourceLookup(
                  &reboot, &evidence, 77) ==
              cold::FrontendLookupDisposition::LatchRestartRequired);
        CHECK(reboot.restart_required && reboot.show_menu_guard_armed);
        CHECK(reboot.last_reject_reason ==
              cold::BarrierRejectReason::FrontendLookupBeforeReset);
        CHECK(cold::CurrentColdRebootState(&reboot) ==
              cold::ColdRebootState::RuntimeFailed);
        CHECK(!cold::CanAdmitCustomProject(&reboot));
    }
    return true;
}

bool TestTransitionQuarantineRemainsCleanupCapable() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::QuarantineActiveEpochForCleanup(
        &reboot, 9U, "change_after_fade_context_concurrent"));
    CHECK(reboot.active_epoch_quarantined);
    CHECK(reboot.transition_attestation_failure);
    CHECK(reboot.new_project_admission_blocked);
    CHECK(reboot.cleanup_still_authorized);
    CHECK(reboot.quarantined_epoch == 9U);
    CHECK(!reboot.restart_required);
    CHECK(!cold::CanAdmitCustomProject(&reboot));

    CHECK(cold::ArmColdRebootForEpoch(&reboot, 9U));
    cold::ColdRebootPreflightEvidence preflight =
        ValidPreflightEvidence();
    CHECK(cold::PrepareColdRebootAtStateFive(
              &reboot, &preflight) ==
          cold::ColdRebootOutcome::Verified);
    cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
    CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
          cold::ColdRebootOutcome::Verified);
    CHECK(!reboot.active_epoch_quarantined);
    CHECK(!reboot.transition_attestation_failure);
    CHECK(!reboot.new_project_admission_blocked);
    CHECK(!reboot.cleanup_still_authorized);
    CHECK(reboot.quarantined_epoch == 0U);
    CHECK(!reboot.restart_required);
    CHECK(cold::ObserveShowMenuMain(&reboot, 9U, 77U) ==
          cold::ShowMenuMainDisposition::DelegateOriginal);
    return true;
}

bool TestSourceContradictionRequiresOneFailurePresentation() noexcept
{
    cold::ContentDomainRestartRegistry registry = {};
    cold::FrontendContentColdReboot reboot = {};
    cold::ColdRebootCriticalQueue queue = {};
    FakeClock clock = {0, 77};
    FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
    std::uint32_t order[256] = {};
    std::uint32_t order_count = 0;
    CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(cold::QuarantineActiveEpochForCleanup(
        &reboot, 9U, "change_after_fade_context_generation_invalid"));
    cold::LatchRestartRequired(
        &reboot, cold::BarrierRejectReason::DuplicateSourceOwnership,
        9U);
    CHECK(reboot.restart_required);
    CHECK(!reboot.cleanup_still_authorized);
    CHECK(reboot.active_epoch_quarantined);
    CHECK(!cold::ArmColdRebootForEpoch(&reboot, 9U));

    cold::FrontendCompletionEvidence evidence = {};
    evidence.active_custom_source_count = 3U;
    evidence.unconsumed_epoch_count = 1U;
    evidence.armed_epoch = 9U;
    CHECK(cold::ObserveShowMenuMain(&reboot, &evidence, 42112U) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    CHECK(reboot.no_level_failure_presentation_scheduled);
    CHECK(reboot.no_level_failure_presentation_count == 1U);
    CHECK(reboot.no_level_failure_presentation_show_menu_attempt == 1U);
    CHECK(reboot.no_level_failure_presentation_reason ==
          cold::BarrierRejectReason::DuplicateSourceOwnership);
    CHECK(reboot.no_level_failure_presentation_thread == 42112U);
    CHECK(cold::ClaimNoLevelFailurePresentation(&reboot));
    CHECK(!cold::ClaimNoLevelFailurePresentation(&reboot));
    cold::NoteNoLevelFailurePresentation(
        &reboot, true, 42112U);
    CHECK(reboot.failure_panel_delivered);

    CHECK(cold::ObserveShowMenuMain(&reboot, &evidence, 42112U) ==
          cold::ShowMenuMainDisposition::BlockRestartRequired);
    CHECK(reboot.no_level_failure_presentation_count == 1U);
    CHECK(reboot.no_level_failure_presentation_show_menu_attempt == 2U);
    CHECK(!cold::ClaimNoLevelFailurePresentation(&reboot));
    CHECK(reboot.failure_panel_delivered);
    return true;
}

bool TestPreparedCancellationUnwindsReverseQuiesceAndFailureLatches() noexcept
{
    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        const std::uint32_t cancel_start = order_count;
        cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        evidence.source_reset_ready = false;
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::Rejected);
        CHECK(order_count == cancel_start + 4U);
        CHECK(order[cancel_start + 0U] == 16U);
        CHECK(order[cancel_start + 1U] == 26U);
        CHECK(order[cancel_start + 2U] == 36U);
        CHECK(order[cancel_start + 3U] == 46U);
        CHECK(!registry.transaction_prepared &&
              !reboot.preflight_prepared && !registry.mutation_started);
        CHECK(vfs.calls[3] == 0 && player.calls[3] == 0 &&
              audio.calls[3] == 0 && verify.calls[3] == 0);
    }

    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(&reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        player.cancel = cold::DomainResult::Failed;
        cold::ColdRebootBarrierEvidence evidence = ValidEvidence();
        evidence.source_reset_ready = false;
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::RuntimeFailed);
        CHECK(registry.runtime_failure_latched &&
              registry.admission_blocked && reboot.restart_required);
        CHECK(reboot.last_reject_reason ==
              cold::BarrierRejectReason::QuiesceCancelFailure);
    }
    return true;
}

bool TestBarrierNegativesAndNested() noexcept
{
    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootBarrierEvidence evidence =
            ValidEvidence();
        /* Natural source teardown cannot be accepted unless the pre-source
         * generation/epoch token was prepared first. */
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::Rejected);
        CHECK(reboot.last_reject_reason ==
              cold::BarrierRejectReason::PreflightNotPrepared);
        CHECK(vfs.calls[3] == 0);
        CHECK(reboot.restart_required && reboot.armed_epoch == 9U);
        CHECK(!cold::ArmColdRebootForEpoch(&reboot, 9));
    }

    const auto rejected_preflight =
        [](bool same_project, std::uint32_t thread_id,
           bool shutdown_requested) noexcept -> bool {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        if (!Setup(
                &registry, &reboot, &queue, &clock, &vfs, &player,
                &audio, &verify, order, &order_count))
            return false;
        const cold::RestartContext baseline = BaselineContext();
        if (cold::CaptureColdRebootBaseline(&reboot, &baseline) !=
                cold::RestartResult::Verified ||
            !cold::ArmColdRebootForEpoch(&reboot, 9))
            return false;
        cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        preflight.same_project_transition = same_project;
        preflight.current_thread_id = thread_id;
        preflight.shutdown_requested = shutdown_requested;
        return cold::PrepareColdRebootAtStateFive(
                   &reboot, &preflight) ==
                   cold::ColdRebootOutcome::Rejected &&
            reboot.restart_required && reboot.armed_epoch == 9U &&
            !cold::CanAdmitCustomProject(&reboot);
    };
    CHECK(rejected_preflight(true, 77U, false));
    CHECK(rejected_preflight(false, 12U, false));
    CHECK(rejected_preflight(false, 77U, true));

    {
        cold::ContentDomainRestartRegistry registry = {};
        cold::FrontendContentColdReboot reboot = {};
        cold::ColdRebootCriticalQueue queue = {};
        FakeClock clock = {0, 77};
        FakeDomain vfs = {}, player = {}, audio = {}, verify = {};
        std::uint32_t order[256] = {};
        std::uint32_t order_count = 0;
        CHECK(Setup(&registry, &reboot, &queue, &clock, &vfs, &player,
                    &audio, &verify, order, &order_count));
        const cold::RestartContext baseline = BaselineContext();
        CHECK(cold::CaptureColdRebootBaseline(&reboot, &baseline) ==
              cold::RestartResult::Verified);
        CHECK(cold::ArmColdRebootForEpoch(&reboot, 9));
        const cold::ColdRebootPreflightEvidence preflight =
            ValidPreflightEvidence();
        CHECK(cold::PrepareColdRebootAtStateFive(
                  &reboot, &preflight) ==
              cold::ColdRebootOutcome::Verified);
        const cold::ColdRebootBarrierEvidence evidence =
            ValidEvidence();
        player.nested_reboot = &reboot;
        player.nested_evidence = &evidence;
        CHECK(cold::EnterColdRebootBarrier(&reboot, &evidence) ==
              cold::ColdRebootOutcome::Verified);
        CHECK(player.nested_outcome ==
              cold::ColdRebootOutcome::Rejected);
        CHECK(player.calls[3] == 1);
    }
    return true;
}

bool TestCriticalOverflowIsBounded() noexcept
{
    cold::ColdRebootCriticalQueue queue = {};
    cold::InitializeCriticalQueue(&queue);
    FakeClock clock = {0, 77};
    cold::ColdRebootPlatform platform = {&ReadQpc, &ReadThreadId, &clock};
    cold::ColdRebootCriticalEvent event = {};
    event.type = cold::ColdRebootEventType::ColdRebootBarrierEnter;
    std::size_t accepted = 0;
    for (std::size_t index = 0;
         index < cold::kColdRebootCriticalEventCapacity + 20U; ++index)
        if (cold::PublishCriticalEvent(&queue, &platform, &event))
            ++accepted;
    CHECK(accepted == cold::kColdRebootCriticalEventCapacity);
    CHECK(queue.count == cold::kColdRebootCriticalEventCapacity);
    CHECK(queue.dropped == 20);
    cold::ColdRebootCriticalEvent output[4] = {};
    const std::size_t count = cold::DrainCriticalEvents(&queue, output, 4);
    CHECK(count == 4);
    CHECK(output[0].type == cold::ColdRebootEventType::ColdRebootBarrierEnter);
    CHECK(queue.count == cold::kColdRebootCriticalEventCapacity - 4U);
    std::uint64_t last_sequence = output[count - 1U].sequence;
    bool saw_overflow = false;
    std::uint64_t overflow_count = 0;
    while (queue.count ||
           dluma_atomic_compare_exchange(
               &queue.overflow_latched, 0, 0) != 0) {
        const std::size_t drained =
            cold::DrainCriticalEvents(&queue, output, 4);
        CHECK(drained != 0);
        for (std::size_t index = 0; index < drained; ++index) {
            CHECK(output[index].sequence > last_sequence);
            last_sequence = output[index].sequence;
            if (output[index].type ==
                cold::ColdRebootEventType::ColdRebootCriticalOverflow) {
                CHECK(!saw_overflow);
                saw_overflow = true;
                overflow_count = output[index].value0;
            }
        }
    }
    CHECK(saw_overflow && overflow_count == 20);

    /* A producer that loses the non-blocking lock must latch the same
     * bounded overflow diagnostic even when the ring itself is empty. */
    cold::InitializeCriticalQueue(&queue);
    queue.lock = 1;
    CHECK(!cold::PublishCriticalEvent(&queue, &platform, &event));
    queue.lock = 0;
    CHECK(cold::DrainCriticalEvents(&queue, output, 4) == 1);
    CHECK(output[0].type ==
          cold::ColdRebootEventType::ColdRebootCriticalOverflow);
    CHECK(output[0].value0 == 1);
    return true;
}

bool TestBaselineAdmissionAndFovProperties() noexcept
{
    cold::FrontendBaseline baseline = {};
    cold::InitializeBaseline(&baseline);
    const std::uint32_t ids[
        cold::kBaselineRequiredPlayerPropertyCount] = {
            103U,
            209U,
            496U,
            cold::kMoveSprintSpeedProperty,
            702U,
            706U,
            707U,
            713U,
            714U,
            715U,
            cold::kCameraDefaultFovProperty,
            cold::kCameraCarFovProperty,
            cold::kFovSprintModifierProperty,
            cold::kFovAmortizerModifierProperty,
            823U,
        };
    for (std::uint32_t index = 0;
         index < cold::kBaselineRequiredPlayerPropertyCount; ++index)
        CHECK(cold::ConfigureRequiredProperty(&baseline, ids[index]));
    cold::StartupBaselineAdmission admission = {};
    admission.stable_frontend = true;
    admission.first_startup_frontend = true;
    admission.game_thread_owner = true;
    CHECK(cold::BeginStartupBaselineCapture(
        &baseline, &admission, 1, 2, 3, 77));
    baseline.frontend.available = true;
    baseline.frontend.menu_pack_fingerprint = 700;
    baseline.frontend.material_provider_fingerprint = 701;
    baseline.loader.loader_pak_state_known = true;
    baseline.loader.loader_pak_present = true;
    baseline.loader.loader_xui_state_known = true;
    baseline.loader.loader_xui_operational = true;
    baseline.loader.loader_pak_identity = 800;
    baseline.source_inventory_complete = true;
    baseline.global_domain_inventory_complete = true;
    cold::BaselineSourceIdentity source = {};
    source.identity_fingerprint = 800;
    source.provider_fingerprint = 11;
    source.kind = cold::BaselineSourceKind::LoaderPak;
    source.active = true;
    CopyName(source.name, sizeof(source.name), "loader-pak");
    CHECK(cold::AddBaselineSource(&baseline, &source));
    cold::BaselinePlayerState incomplete_state = {};
    incomplete_state.state = 0x1000;
    incomplete_state.state_generation = 1;
    CHECK(!cold::AddBaselinePlayerState(&baseline, &incomplete_state));
    cold::BaselinePlayerState states[2] = {};
    cold::BaselinePlayerProperty properties[
        2U * cold::kBaselineRequiredPlayerPropertyCount] = {};
    std::uint32_t property_count = 0;
    for (std::uint32_t owner = 1; owner <= 2; ++owner) {
        cold::BaselinePlayerState state = {};
        state.state = 0x10000U + owner * 0x1000U;
        state.owner_object = 0x20000U + owner * 0x1000U;
        state.player_variables = 0x30000U + owner * 0x1000U;
        state.state_generation = owner;
        state.progression_selector_fingerprint = 1000U + owner;
        state.class_selector_fingerprint = 1100U + owner;
        state.skill_membership_fingerprint = 1200U + owner;
        state.regular_buff_fingerprint = 1300U + owner;
        state.timed_buff_fingerprint = 1400U + owner;
        state.inventory_fingerprint = 1500U + owner;
        state.equipment_fingerprint = 1600U + owner;
        state.quest_fingerprint = 1700U + owner;
        state.appearance_fingerprint = 1800U + owner;
        state.appearance_provider = PlayerProvider(
            0x40000U + owner * 0x1000U, 1900U + owner, owner);
        state.owner_kind = owner == 1
            ? cold::BaselinePlayerOwnerKind::LocalProcessGlobal
            : cold::BaselinePlayerOwnerKind::CharacterManagerGlobal;
        for (std::uint32_t row = 0;
             row < cold::kPlayerLogicalTypeCount; ++row) {
            state.rows[row].definition_vector =
                0x50000U + owner * 0x10000U + row * 0x100U;
            state.rows[row].definition_fingerprint =
                3000U + owner * 100U + row;
            state.rows[row].earned_experience =
                4000U + owner * 100U + row;
            state.rows[row].cache_generation = 500;
            state.rows[row].definition_count = 379U + row;
            state.rows[row].current_level =
                static_cast<std::uint16_t>(10U + row);
            state.rows[row].maximum_level =
                static_cast<std::uint16_t>(25U + row);
            state.rows[row].provider = PlayerProvider(
                0x60000U, 5000U, 3U);
            state.rows[row].member_of_live_cache = true;
        }
        states[owner - 1U] = state;
        for (std::uint32_t index = 0;
             index < cold::kBaselineRequiredPlayerPropertyCount;
             ++index) {
            cold::BaselinePlayerProperty property = {};
            property.state_generation = owner;
            property.cache_generation = 500;
            property.property_id = ids[index];
            property.value_bits = owner * 100U + index;
            property.provider = PlayerProvider(
                0x60000U, 5000U, 3U);
            property.available = true;
            properties[property_count++] = property;
        }
    }
    baseline.player_states[0].state = 0xDEADU;
    baseline.player_properties[0].property_id = 0xBEEFU;
    properties[0].provider.exact_source = false;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          !baseline.player_state_count &&
          !baseline.player_property_count &&
          !baseline.player_inventory_complete &&
          baseline.player_states[0].state == 0xDEADU &&
          baseline.player_properties[0].property_id == 0xBEEFU);
    properties[0].provider.exact_source = true;
    properties[0].provider.content_epoch = 7;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          !baseline.player_inventory_complete &&
          baseline.player_properties[0].property_id == 0xBEEFU);
    properties[0].provider.content_epoch = 0;
    properties[0].provider.source_inventory_ordinal = 0;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          !baseline.player_inventory_complete &&
          baseline.player_properties[0].property_id == 0xBEEFU);
    properties[0].provider.source_inventory_ordinal = 4;
    states[0].owner_object = 0;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          !baseline.player_inventory_complete &&
          baseline.player_states[0].state == 0xDEADU);
    states[0].owner_object = 0x21000U;
    states[0].progression_selector_fingerprint = 0;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          !baseline.player_inventory_complete &&
          baseline.player_states[0].state == 0xDEADU);
    states[0].progression_selector_fingerprint = 1001U;
    states[0].rows[0].cache_generation = 499;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          baseline.player_states[0].state == 0xDEADU);
    states[0].rows[0].cache_generation = 500;
    properties[0].state_generation = 99;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(!baseline.gameplay_cache_generation &&
          baseline.player_properties[0].property_id == 0xBEEFU);
    properties[0].state_generation = 1;
    CHECK(cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(baseline.gameplay_cache_generation == 500 &&
          baseline.player_inventory_complete);
    CHECK(baseline.required_property_count ==
          cold::kBaselineRequiredPlayerPropertyCount);
    CHECK(baseline.player_property_count ==
          2U * cold::kBaselineRequiredPlayerPropertyCount);
    CHECK(cold::AbandonBaselinePlayerSnapshot(&baseline));
    CHECK(!baseline.gameplay_cache_generation &&
          !baseline.player_state_count &&
          !baseline.player_property_count &&
          !baseline.player_inventory_complete &&
          baseline.capturing && !baseline.captured &&
          baseline.required_property_count ==
              cold::kBaselineRequiredPlayerPropertyCount);
    cold::BaselinePlayerState empty_states[
        cold::kBaselinePlayerStateCapacity] = {};
    cold::BaselinePlayerProperty empty_properties[
        cold::kBaselinePlayerPropertyCapacity] = {};
    CHECK(std::memcmp(baseline.player_states, empty_states,
                      sizeof(empty_states)) == 0);
    CHECK(std::memcmp(baseline.player_properties, empty_properties,
                      sizeof(empty_properties)) == 0);
    CHECK(!cold::AbandonBaselinePlayerSnapshot(&baseline));
    CHECK(cold::SetBaselinePlayerSnapshot(
        &baseline, 500, states, 2, properties, property_count));
    CHECK(baseline.gameplay_cache_generation == 500 &&
          baseline.player_state_count == 2U &&
          baseline.player_property_count == property_count &&
          baseline.player_inventory_complete);
    states[0].state ^= 0x10U;
    CHECK(!cold::SetBaselinePlayerSnapshot(
        &baseline, 501, states, 2, properties, property_count));
    CHECK(baseline.player_states[0].state != states[0].state &&
          baseline.gameplay_cache_generation == 500);
    cold::BaselineAudioSettings audio_settings = {};
    audio_settings.fingerprint = 99;
    audio_settings.global_settings_selector_fingerprint = 100;
    audio_settings.selector_available = true;
    audio_settings.available = true;
    cold::BaselineAudioScript script = {};
    script.logical_path_fingerprint = 10;
    script.content_identity_fingerprint = 11;
    script.load_order = 1;
    script.provider_identity = AudioProvider(0x71000U, 12, 4);
    script.natural_load_observed = true;
    script.parse_succeeded = true;
    CopyName(script.logical_path, sizeof(script.logical_path),
             "data\\audio\\audiodata.scr");
    CopyName(script.provider, sizeof(script.provider), "base-audio-source");
    cold::BaselineAudioBank bank = {};
    bank.logical_bank_id = 20;
    bank.provider_fingerprint = 21;
    bank.content_fingerprint = 22;
    bank.instance_generation = 23;
    bank.manager_record = 0x72000U;
    bank.retained_native_file = 0x73000U;
    bank.provider_identity = AudioProvider(0x71000U, 24, 4);
    bank.role = cold::BaselineAudioBankRole::Base;
    bank.dependency_order = 2;
    bank.membership_observed = true;
    bank.natural_load_observed = true;
    bank.dependency_order_observed = true;
    bank.reference_count = 0;
    bank.reference_count_observed = false;
    CopyName(bank.name, sizeof(bank.name), "base-bank");
    CopyName(bank.provider, sizeof(bank.provider), "base-provider");
    cold::BaselineSpeechBank speech = {};
    speech.logical_bank_id = 30;
    speech.provider_fingerprint = 31;
    speech.content_identity_fingerprint = 32;
    speech.instance_generation = 33;
    speech.bank_record = 0x74000U;
    speech.load_order = 3;
    speech.provider_identity = AudioProvider(0x75000U, 34, 5);
    speech.membership_observed = true;
    speech.natural_load_observed = true;
    speech.load_order_observed = true;
    speech.localized = true;
    CopyName(speech.name, sizeof(speech.name), "base-speech");
    CopyName(speech.provider, sizeof(speech.provider), "speech-provider");
    CHECK(cold::SetBaselineAudioSnapshot(
        &baseline, &audio_settings, &script, 1, &bank, 1, &speech, 1,
        600, 601));
    CHECK(baseline.audio_inventory_complete &&
          !baseline.audio_replay_owner_complete);
    CHECK(cold::CompleteStartupBaselineCapture(&baseline));
    CHECK(!cold::AbandonBaselinePlayerSnapshot(&baseline));

    cold::FrontendBaseline returned = {};
    CHECK(cold::SnapshotBaseline(&baseline, &returned));
    returned.gameplay_cache_generation = 501;
    for (std::uint32_t state = 0;
         state < returned.player_state_count; ++state) {
        for (std::uint32_t row = 0;
             row < cold::kPlayerLogicalTypeCount; ++row) {
            returned.player_states[state].rows[row].cache_generation = 501;
            returned.player_states[state].rows[row].definition_vector +=
                0x100000U;
        }
    }
    for (std::uint32_t property = 0;
         property < returned.player_property_count; ++property)
        returned.player_properties[property].cache_generation = 501;
    returned.audio_banks[0].instance_generation = 24;
    returned.speech_banks[0].instance_generation = 34;
    cold::FrontendBaselineComparison comparison =
        cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(comparison.comparable && comparison.match);

    returned.player_properties[6].value_bits ^= 1U;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerProperties)) != 0);
    returned.player_properties[6].value_bits ^= 1U;
    returned.player_properties[0].provider.source ^= 0x10U;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerProperties)) != 0);
    returned.player_properties[0].provider.source ^= 0x10U;
    returned.player_properties[0].cache_generation = 500;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerProperties)) != 0);
    returned.player_properties[0].cache_generation = 501;
    returned.player_states[1].player_variables ^= 0x10U;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerRows)) != 0);
    returned.player_states[1].player_variables ^= 0x10U;
    returned.player_states[1].inventory_fingerprint ^= 1U;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match && comparison.stale_player_rows ==
              cold::kPlayerLogicalTypeCount &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerRows)) != 0);
    returned.player_states[1].inventory_fingerprint ^= 1U;
    returned.player_states[1].owner_object ^= 0x10U;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerOwners)) != 0);
    returned.player_states[1].owner_object ^= 0x10U;
    returned.player_states[0].rows[0].provider.source ^= 0x10U;
    comparison = cold::CompareFrontendBaseline(&baseline, &returned);
    CHECK(!comparison.match && comparison.stale_player_rows == 1U &&
          (comparison.mismatch_mask & static_cast<std::uint32_t>(
               cold::BaselineMismatch::PlayerRows)) != 0);

    cold::InitializeBaseline(&returned);
    admission.first_startup_frontend = false;
    admission.returned_frontend = true;
    CHECK(!cold::BeginStartupBaselineCapture(
        &returned, &admission, 2, 3, 4, 77));
    CHECK(cold::BeginReturnedFrontendCapture(
        &returned, &admission, 2, 3, 4, 77));
    return true;
}

bool TestAudioSnapshotRejectsInvalidInputsAtomically() noexcept
{
    cold::FrontendBaseline baseline = {};
    cold::InitializeBaseline(&baseline);
    cold::StartupBaselineAdmission admission = {};
    admission.stable_frontend = true;
    admission.first_startup_frontend = true;
    admission.game_thread_owner = true;
    CHECK(cold::BeginStartupBaselineCapture(
        &baseline, &admission, 20, 21, 22, 77));

    AudioSnapshotFixture fixture = {};
    InitializeAudioSnapshotFixture(&fixture);
    CHECK(AudioSnapshotStorageEmpty(baseline));

    cold::BaselineAudioSettings incomplete_settings = fixture.settings;
    incomplete_settings.selector_available = false;
    CHECK(!cold::SetBaselineAudioSnapshot(
        &baseline, &incomplete_settings, &fixture.script, 1,
        &fixture.bank, 1, &fixture.speech, 1, 600, 601));
    CHECK(AudioSnapshotStorageEmpty(baseline));

    cold::BaselineAudioScript scripts[2] = {
        fixture.script, fixture.script};
    scripts[1].logical_path_fingerprint = 12;
    scripts[1].content_identity_fingerprint = 13;
    scripts[1].load_order = 2;
    scripts[1].provider_identity.exact_source = false;
    CopyName(scripts[1].logical_path, sizeof(scripts[1].logical_path),
             "data\\audio\\second.scr");
    CHECK(!cold::SetBaselineAudioSnapshot(
        &baseline, &fixture.settings, scripts, 2, &fixture.bank, 1,
        &fixture.speech, 1, 600, 601));
    CHECK(AudioSnapshotStorageEmpty(baseline));

    cold::BaselineAudioBank banks[2] = {fixture.bank, fixture.bank};
    banks[1].logical_bank_id = 25;
    banks[1].provider_fingerprint = 26;
    banks[1].content_fingerprint = 27;
    banks[1].instance_generation = 28;
    banks[1].dependency_order = 3;
    /* A duplicate native manager record is rejected only after the first
     * complete record has been validated.  No prefix may be published. */
    CHECK(!cold::SetBaselineAudioSnapshot(
        &baseline, &fixture.settings, &fixture.script, 1, banks, 2,
        &fixture.speech, 1, 600, 601));
    CHECK(AudioSnapshotStorageEmpty(baseline));

    cold::BaselineSpeechBank speech[2] = {
        fixture.speech, fixture.speech};
    speech[1].logical_bank_id = 35;
    speech[1].provider_fingerprint = 36;
    speech[1].content_identity_fingerprint = 37;
    speech[1].instance_generation = 38;
    speech[1].bank_record = 0x74100U;
    speech[1].load_order = 4;
    speech[1].provider_identity.exact_source = false;
    CopyName(speech[1].name, sizeof(speech[1].name), "second-speech");
    CHECK(!cold::SetBaselineAudioSnapshot(
        &baseline, &fixture.settings, &fixture.script, 1,
        &fixture.bank, 1, speech, 2, 600, 601));
    CHECK(AudioSnapshotStorageEmpty(baseline));

    CHECK(cold::SetBaselineAudioSnapshot(
        &baseline, &fixture.settings, &fixture.script, 1,
        &fixture.bank, 1, &fixture.speech, 1, 600, 601));
    CHECK(baseline.audio_inventory_complete &&
          !baseline.audio_replay_owner_complete &&
          baseline.audio_script_count == 1U &&
          baseline.audio_bank_count == 1U &&
          baseline.speech_bank_count == 1U);
    const std::uint64_t published_script =
        baseline.audio_scripts[0].logical_path_fingerprint;
    fixture.script.logical_path_fingerprint ^= 0x80U;
    CHECK(!cold::SetBaselineAudioSnapshot(
        &baseline, &fixture.settings, &fixture.script, 1,
        &fixture.bank, 1, &fixture.speech, 1, 700, 701));
    CHECK(baseline.audio_scripts[0].logical_path_fingerprint ==
          published_script);
    return true;
}

bool TestAudioSnapshotDuplicateLogicalPathRequiresDistinctOrder() noexcept
{
    AudioSnapshotFixture fixture = {};
    InitializeAudioSnapshotFixture(&fixture);
    cold::BaselineAudioScript scripts[2] = {
        fixture.script, fixture.script};
    scripts[1].load_order = 2U;

    cold::FrontendBaseline accepted = {};
    cold::InitializeBaseline(&accepted);
    cold::StartupBaselineAdmission admission = {};
    admission.stable_frontend = true;
    admission.first_startup_frontend = true;
    admission.game_thread_owner = true;
    CHECK(cold::BeginStartupBaselineCapture(
        &accepted, &admission, 30, 31, 32, 77));
    CHECK(cold::SetBaselineAudioSnapshot(
        &accepted, &fixture.settings, scripts, 2,
        &fixture.bank, 1, &fixture.speech, 1, 600, 601));
    CHECK(accepted.audio_script_count == 2U);
    CHECK(std::strcmp(accepted.audio_scripts[0].logical_path,
                      accepted.audio_scripts[1].logical_path) == 0);
    CHECK(accepted.audio_scripts[0].load_order == 1U);
    CHECK(accepted.audio_scripts[1].load_order == 2U);

    cold::FrontendBaseline rejected = {};
    cold::InitializeBaseline(&rejected);
    CHECK(cold::BeginStartupBaselineCapture(
        &rejected, &admission, 40, 41, 42, 77));
    scripts[1].load_order = scripts[0].load_order;
    CHECK(!cold::SetBaselineAudioSnapshot(
        &rejected, &fixture.settings, scripts, 2,
        &fixture.bank, 1, &fixture.speech, 1, 600, 601));
    CHECK(AudioSnapshotStorageEmpty(rejected));
    return true;
}

bool TestPartialBaselineEvidenceRemainsNonComparable() noexcept
{
    cold::FrontendBaseline partial = {};
    cold::InitializeBaseline(&partial);
    cold::StartupBaselineAdmission admission = {};
    admission.stable_frontend = true;
    admission.first_startup_frontend = true;
    admission.game_thread_owner = true;
    CHECK(cold::BeginStartupBaselineCapture(
        &partial, &admission, 10, 11, 12, 77));
    partial.custom_source_count = 0;
    partial.loader.global_lua_fingerprint = 1234;
    partial.loader.approved_global_lua_count = 2;
    partial.loader.local_project_lua_count = 0;
    cold::FailStartupBaselineCapture(&partial);
    CHECK(partial.generation == 10);
    CHECK(partial.capture_failed);
    CHECK(!partial.captured);

    cold::FrontendBaseline snapshot = {};
    CHECK(!cold::SnapshotBaseline(&partial, &snapshot));
    const cold::FrontendBaselineComparison comparison =
        cold::CompareFrontendBaseline(&partial, &snapshot);
    CHECK(!comparison.comparable);
    CHECK(!comparison.match);
    return true;
}

}  // namespace

int main()
{
    if (!TestVerifiedTransaction() ||
        !TestTerminalVerificationRejectsPriorEpochSuccess() ||
        !TestProviderRetirementFollowsConsumerRelease() ||
        !TestIncompleteMandatoryBaselineCannotArmOrMutate() ||
        !TestDependencyPhaseOrdering() ||
        !TestTwoPointPreflightPrecedesNaturalSourceTeardown() ||
        !TestPreflightFailureMutatesNothing() ||
        !TestQuiesceFailureMutatesNothing() ||
        !TestOrphanedQuiesceCancelFailureLatches() ||
        !TestFirstResetInvalidationCancelsBeforeMutation() ||
        !TestFirstResetInvalidationCancelFailureLatches() ||
        !TestPostMutationFailureBlocksAdmissionAndMenu() ||
        !TestOptionalAdapterExclusionAndFailureLatch() ||
        !TestTouchedOptionalDomainCannotBeDropped() ||
        !TestTouchedDomainWithoutBaselineBlocksBeforePreflight() ||
        !TestPlayerTouchedWithoutPlayerBaselineBlocksBeforeVfsMutation() ||
        !TestLateTouchInvalidatesPreparedSubsetBeforeMutation() ||
        !TestPlayerUntouchedAudioUntouchedVfsAndVerifyReachVerified() ||
        !TestOptionalPlayerBaselineAddsToReadyMaskBeforeFirstEpoch() ||
        !TestPlayerTouchedTransactionOrdersVfsPlayerVerify() ||
        !TestAudioTouchedWithReadyBaselineButUnsupportedOwnerBlocksAllMutation() ||
        !TestPendingPostC10OwnerMatrix() ||
        !TestStateFiveDestroyOwnerAuthorizationMatrix() ||
        !TestC08C10OnDestroyPostOriginalPlayerOnlyIntegratedReplay() ||
        !TestTouchedUnsupportedDomainsBlockBeforeVfsMutation() ||
        !TestPlayerTouchedButPreflightUnsupportedHasNoSourceOnlyFallback() ||
        !TestParticipationCombinationsPreserveRequiredOrder() ||
        !TestUntouchedStaticDependenciesDoNotBlockTerminalVerify() ||
        !TestSuccessfulEpochClearsTouchedMask() ||
        !TestRepeatedTouchCannotRacePostMutationPhases() ||
        !TestInFlightRetainingObservationBlocksPrepare() ||
        !TestRepeatedSameDomainRetainingActivityInvalidatesPreflight() ||
        !TestProducerStraddlingFinalCommitFailsClosed() ||
        !TestProducerClaimsCommittingWindow() ||
        !TestResetOwnedReplayOriginFailsClosedUntilImplemented() ||
        !TestSecondEpochPreflightFailureIsStillPreMutation() ||
        !TestShowMenuRejectsIncompleteArmedEpoch() ||
        !TestFrontendCompletionEvidenceFailsClosedWithoutGuard() ||
        !TestTransitionQuarantineRemainsCleanupCapable() ||
        !TestSourceContradictionRequiresOneFailurePresentation() ||
        !TestPreparedCancellationUnwindsReverseQuiesceAndFailureLatches() ||
        !TestBarrierNegativesAndNested() ||
        !TestCriticalOverflowIsBounded() ||
        !TestBaselineAdmissionAndFovProperties() ||
        !TestAudioSnapshotRejectsInvalidInputsAtomically() ||
        !TestAudioSnapshotDuplicateLogicalPathRequiresDistinctOrder() ||
        !TestPartialBaselineEvidenceRemainsNonComparable())
        return 1;
    std::puts("frontend content cold reboot fake harness: PASS");
    return 0;
}
