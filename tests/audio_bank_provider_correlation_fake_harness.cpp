#include "fixes/custom_map_asset_isolation/AudioBankProviderCorrelation.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace audio =
    dluma::fixes::custom_map_asset_isolation::audio;

static_assert(std::is_trivially_copyable<
                  audio::AudioBankProviderCorrelationContext>::value,
              "hook context must remain bounded POD");
static_assert(std::is_standard_layout<
                  audio::AudioBankProviderCorrelationContext>::value,
              "hook context must remain standard-layout POD");
static_assert(std::is_trivially_copyable<
                  audio::AudioBankProviderRecord>::value,
              "drained observation must remain bounded POD");

namespace {

int g_failures;
int g_passes;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::printf("check failed: %s:%d: %s\n", __FILE__, __LINE__,     \
                        #expression);                                         \
            return false;                                                     \
        }                                                                     \
    } while (0)

audio::AudioNativeOpenCompleteResult ObserveExactOpen(
    audio::AudioBankProviderCorrelationContext* context,
    const char* provider, std::uintptr_t native_file,
    std::uint64_t sequence) noexcept
{
    audio::AudioNativeOpenAttempt attempt = {};
    const auto begin = audio::BeginAudioNativeOpen(
        context, audio::kSoundBankManagerNativeOpenReturnRva,
        0, audio::kSoundBankNativeOpenMode,
        audio::kSoundBankNativeOpenFlags, provider, true,
        sequence, &attempt);
    if (begin != audio::AudioNativeOpenBeginResult::Armed)
        return audio::AudioNativeOpenCompleteResult::InvalidAttempt;
    return audio::CompleteAudioNativeOpen(
        context, &attempt, native_file, sequence + 1U);
}

audio::AudioNativeOpenCompleteResult ObserveExactSpeechOpen(
    audio::AudioBankProviderCorrelationContext* context,
    const char* provider, std::uintptr_t native_file,
    std::uint64_t sequence) noexcept
{
    audio::AudioNativeOpenAttempt attempt = {};
    const auto begin = audio::BeginAudioNativeOpen(
        context, audio::kSpeechBankNativeOpenReturnRva,
        0, audio::kSoundBankNativeOpenMode,
        audio::kSpeechBankNativeOpenFlags, provider, true,
        sequence, &attempt);
    if (begin != audio::AudioNativeOpenBeginResult::Armed)
        return audio::AudioNativeOpenCompleteResult::InvalidAttempt;
    return audio::CompleteAudioNativeOpen(
        context, &attempt, native_file, sequence + 1U);
}

bool TestExactProvider()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 0x1111, "menu_main", true, false, false, 10);
    CHECK(token.tracked && !token.suppressed);
    CHECK(ObserveExactOpen(
              &context, "X:\\base\\data\\audio\\menu_main.bank",
              0x2222, 11) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 13, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome == audio::AudioBankProviderOutcome::ExactProvider);
    CHECK(record.observation_complete);
    CHECK(record.provider_identity_available);
    CHECK(record.first_open.native_file == 0x2222);
    CHECK(std::strcmp(record.first_open.provider_path,
                      "X:\\base\\data\\audio\\menu_main.bank") == 0);
    CHECK(audio::AudioBankProviderCorrelationIdle(&context));
    return true;
}

bool TestExactFullPathSourceIsCarriedIntoNativeOpen()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    CHECK(audio::SetAudioResolvedSourceObservationRequired(
        &context, true));
    const auto token = audio::BeginAudioBankLoad(
        &context, 0x1111, "menu_main", true, false, false, 14);
    CHECK(audio::ObserveAudioResolvedSource(&context, 0xABCDEF00) ==
          audio::AudioResolvedSourceObservationResult::Correlated);
    CHECK(ObserveExactOpen(
              &context, "X:\\base\\data\\audio\\menu_main.bank",
              0x2222, 15) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 17, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome == audio::AudioBankProviderOutcome::ExactProvider);
    CHECK(record.observation_complete);
    CHECK(record.resolved_source_count == 1);
    CHECK(record.selected_source == 0xABCDEF00);
    CHECK(record.exact_source_identity_available);
    CHECK(record.first_open.selected_source == 0xABCDEF00);
    CHECK(record.first_open.exact_source);
    return true;
}

bool TestRequiredFullPathSourceMissingFailsClosed()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    CHECK(audio::SetAudioResolvedSourceObservationRequired(
        &context, true));
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "missing-source", true, false, false, 18);
    CHECK(ObserveExactOpen(&context, "X:\\base\\missing.bank", 2, 19) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 21, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome ==
          audio::AudioBankProviderOutcome::SourceIdentityUnavailable);
    CHECK(record.source_identity_loss);
    CHECK(!record.observation_complete);
    CHECK(!record.exact_source_identity_available);
    return true;
}

bool TestMultipleFullPathSourcesFailClosed()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    CHECK(audio::SetAudioResolvedSourceObservationRequired(
        &context, true));
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "ambiguous-source", true, false, false, 22);
    CHECK(audio::ObserveAudioResolvedSource(&context, 0x1000) ==
          audio::AudioResolvedSourceObservationResult::Correlated);
    CHECK(audio::ObserveAudioResolvedSource(&context, 0x2000) ==
          audio::AudioResolvedSourceObservationResult::MultipleResolutions);
    CHECK(ObserveExactOpen(&context, "X:\\base\\ambiguous.bank", 2, 23) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 25, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.resolved_source_count == 2);
    CHECK(record.selected_source == 0x1000);
    CHECK(record.source_identity_loss);
    CHECK(!record.observation_complete);
    return true;
}

bool TestCacheHitDoesNotInventProvider()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 0x1111, "same_name", true, false, false, 20);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 1, 21, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome ==
          audio::AudioBankProviderOutcome::CacheHitNoOpen);
    CHECK(record.observation_complete);
    CHECK(!record.provider_identity_available);
    CHECK(record.native_open_count == 0);
    CHECK(record.first_open.provider_path[0] == 0);
    return true;
}

bool TestNestedLoadsKeepProvidersSeparate()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto outer = audio::BeginAudioBankLoad(
        &context, 0x1000, "outer", true, false, false, 30);
    const auto inner = audio::BeginAudioBankLoad(
        &context, 0x1000, "inner", true, true, true, 31);
    CHECK(inner.generation != outer.generation);
    CHECK(ObserveExactOpen(&context, "D:\\inner.bank", 0x3000, 32) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord inner_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, inner, 0, 34, &inner_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(std::strcmp(inner_record.bank_name, "inner") == 0);
    CHECK(std::strcmp(inner_record.first_open.provider_path,
                      "D:\\inner.bank") == 0);
    CHECK(inner_record.reload && inner_record.localised);

    CHECK(ObserveExactOpen(&context, "D:\\outer.bank", 0x4000, 35) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord outer_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, outer, 0, 37, &outer_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(std::strcmp(outer_record.bank_name, "outer") == 0);
    CHECK(std::strcmp(outer_record.first_open.provider_path,
                      "D:\\outer.bank") == 0);
    CHECK(outer_record.generation != inner_record.generation);
    return true;
}

bool TestOpenAttemptSurvivesCompletedReentry()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto outer = audio::BeginAudioBankLoad(
        &context, 1, "outer", true, false, false, 40);
    audio::AudioNativeOpenAttempt attempt = {};
    CHECK(audio::BeginAudioNativeOpen(
              &context, audio::kSoundBankManagerNativeOpenReturnRva,
              0, 0, 0x40, "D:\\outer.bank", true, 41, &attempt) ==
          audio::AudioNativeOpenBeginResult::Armed);
    const auto nested = audio::BeginAudioBankLoad(
        &context, 1, "nested-cache", true, false, false, 42);
    audio::AudioBankProviderRecord nested_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, nested, 1, 43, &nested_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(audio::CompleteAudioNativeOpen(
              &context, &attempt, 0x4141, 44) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord outer_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, outer, 0, 45, &outer_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(outer_record.provider_identity_available);
    CHECK(std::strcmp(outer_record.first_open.provider_path,
                      "D:\\outer.bank") == 0);
    return true;
}

bool TestMultipleOpenRetainsFirstAndFailsClosed()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "multi", true, false, false, 50);
    CHECK(ObserveExactOpen(&context, "A:\\first.bank", 1, 51) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    CHECK(ObserveExactOpen(&context, "B:\\second.bank", 2, 53) ==
          audio::AudioNativeOpenCompleteResult::CorrelatedAdditionalOpen);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 55, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome ==
          audio::AudioBankProviderOutcome::MultipleNativeOpens);
    CHECK(!record.observation_complete);
    CHECK(record.multiple_native_opens);
    CHECK(record.native_open_count == 2);
    CHECK(std::strcmp(record.first_open.provider_path,
                      "A:\\first.bank") == 0);
    return true;
}

bool TestOverflowSuppressesInsteadOfAttributingToParent()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    audio::AudioBankLoadToken tokens[audio::kAudioProviderFrameCapacity] = {};
    for (std::size_t index = 0;
         index < audio::kAudioProviderFrameCapacity; ++index) {
        tokens[index] = audio::BeginAudioBankLoad(
            &context, 1, "nested", true, false, false,
            60 + index);
        CHECK(tokens[index].tracked && !tokens[index].suppressed);
    }
    const auto overflow = audio::BeginAudioBankLoad(
        &context, 1, "overflow", true, false, false, 70);
    CHECK(overflow.tracked && overflow.suppressed);
    audio::AudioNativeOpenAttempt ignored = {};
    CHECK(audio::BeginAudioNativeOpen(
              &context, audio::kSoundBankManagerNativeOpenReturnRva,
              0, 0, 0x40, "X:\\must_not_reach_parent.bank", true,
              71, &ignored) ==
          audio::AudioNativeOpenBeginResult::Suppressed);
    CHECK(audio::EndAudioBankLoad(
              &context, overflow, 0, 72, nullptr) ==
          audio::AudioBankProviderEndResult::Suppressed);

    audio::AudioBankProviderRecord deepest = {};
    CHECK(audio::EndAudioBankLoad(
              &context, tokens[audio::kAudioProviderFrameCapacity - 1U],
              1, 73, &deepest) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(deepest.native_open_count == 0);
    CHECK(deepest.nested_stack_overflow);
    CHECK(deepest.outcome == audio::AudioBankProviderOutcome::StackOverflow);
    CHECK(!deepest.observation_complete);
    for (std::size_t remaining =
             audio::kAudioProviderFrameCapacity - 1U;
         remaining > 0; --remaining) {
        audio::AudioBankProviderRecord record = {};
        CHECK(audio::EndAudioBankLoad(
                  &context, tokens[remaining - 1U], 1,
                  74 + remaining, &record) ==
              audio::AudioBankProviderEndResult::Completed);
    }
    CHECK(audio::AudioBankProviderCorrelationIdle(&context));
    return true;
}

bool TestStaleScopeGenerationCannotCloseReusedPointer()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto old_token = audio::BeginAudioBankLoad(
        &context, 0xABC, "old", true, false, false, 80);
    audio::AudioBankProviderRecord old_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, old_token, 1, 81, &old_record) ==
          audio::AudioBankProviderEndResult::Completed);
    const auto reused = audio::BeginAudioBankLoad(
        &context, 0xABC, "new", true, false, false, 82);
    CHECK(reused.generation != old_token.generation);
    CHECK(audio::EndAudioBankLoad(
              &context, old_token, 1, 83, nullptr) ==
          audio::AudioBankProviderEndResult::GenerationMismatch);
    CHECK(context.logical_depth == 1);
    audio::AudioBankProviderRecord reused_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, reused, 1, 84, &reused_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(reused_record.generation == reused.generation);
    CHECK(reused_record.outcome ==
          audio::AudioBankProviderOutcome::GenerationMismatch);
    CHECK(audio::AudioBankProviderCorrelationIdle(&context));
    return true;
}

bool TestStaleOpenAttemptCannotAttachToReusedFrame()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto first = audio::BeginAudioBankLoad(
        &context, 1, "first", true, false, false, 90);
    audio::AudioNativeOpenAttempt stale = {};
    CHECK(audio::BeginAudioNativeOpen(
              &context, audio::kSoundBankManagerNativeOpenReturnRva,
              0, 0, 0x40, "A:\\first.bank", true, 91, &stale) ==
          audio::AudioNativeOpenBeginResult::Armed);
    audio::AudioBankProviderRecord first_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, first, -1, 92, &first_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(first_record.outcome ==
          audio::AudioBankProviderOutcome::PendingNativeOpen);
    const auto second = audio::BeginAudioBankLoad(
        &context, 1, "second", true, false, false, 93);
    CHECK(audio::CompleteAudioNativeOpen(
              &context, &stale, 0x9999, 94) ==
          audio::AudioNativeOpenCompleteResult::GenerationMismatch);
    audio::AudioBankProviderRecord second_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, second, 1, 95, &second_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(second_record.native_open_count == 0);
    CHECK(second_record.first_open.provider_path[0] == 0);
    return true;
}

bool TestUnrelatedNativeOpenIsIgnored()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "bank", true, false, false, 100);
    audio::AudioNativeOpenAttempt attempt = {};
    CHECK(audio::BeginAudioNativeOpen(
              &context,
              audio::kSoundBankManagerNativeOpenReturnRva + 1U,
              0, 0, 0x40, "X:\\unrelated.file", true, 101,
              &attempt) ==
          audio::AudioNativeOpenBeginResult::DifferentCallSite);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 1, 102, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome ==
          audio::AudioBankProviderOutcome::CacheHitNoOpen);
    CHECK(record.observation_complete);
    return true;
}

bool TestExactCallsiteRequiresExactArguments()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "bank", true, false, false, 110);
    audio::AudioNativeOpenAttempt attempt = {};
    CHECK(audio::BeginAudioNativeOpen(
              &context, audio::kSoundBankManagerNativeOpenReturnRva,
              0x1234, 0, 0x40, "X:\\bank.bank", true, 111,
              &attempt) ==
          audio::AudioNativeOpenBeginResult::ArgumentContractMismatch);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 112, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome ==
          audio::AudioBankProviderOutcome::ArgumentContractMismatch);
    CHECK(!record.observation_complete);
    CHECK(!record.provider_identity_available);
    return true;
}

bool TestIncompletePathFailsClosed()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "bank", true, false, false, 120);
    audio::AudioNativeOpenAttempt attempt = {};
    CHECK(audio::BeginAudioNativeOpen(
              &context, audio::kSoundBankManagerNativeOpenReturnRva,
              0, 0, 0x40, "X:\\bank.bank", false, 121,
              &attempt) ==
          audio::AudioNativeOpenBeginResult::PathUnavailable);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 122, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome == audio::AudioBankProviderOutcome::PathUnavailable);
    CHECK(!record.observation_complete);
    return true;
}

bool TestFailedOpenIsEvidenceButNotAProvider()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "missing", true, false, false, 130);
    CHECK(ObserveExactOpen(&context, "X:\\missing.bank", 0, 131) ==
          audio::AudioNativeOpenCompleteResult::CorrelatedOpenFailed);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, -2, 133, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome == audio::AudioBankProviderOutcome::LoadFailed);
    CHECK(record.observation_complete);
    CHECK(!record.provider_identity_available);
    CHECK(!record.open_succeeded);
    CHECK(std::strcmp(record.first_open.provider_path,
                      "X:\\missing.bank") == 0);
    return true;
}

bool TestOutOfOrderExitCannotCorruptStack()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto outer = audio::BeginAudioBankLoad(
        &context, 1, "outer", true, false, false, 140);
    const auto inner = audio::BeginAudioBankLoad(
        &context, 1, "inner", true, false, false, 141);
    CHECK(audio::EndAudioBankLoad(
              &context, outer, 1, 142, nullptr) ==
          audio::AudioBankProviderEndResult::GenerationMismatch);
    CHECK(context.logical_depth == 2);
    audio::AudioBankProviderRecord inner_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, inner, 1, 143, &inner_record) ==
          audio::AudioBankProviderEndResult::Completed);
    audio::AudioBankProviderRecord outer_record = {};
    CHECK(audio::EndAudioBankLoad(
              &context, outer, 1, 144, &outer_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(inner_record.generation_mismatch);
    CHECK(outer_record.generation_mismatch);
    CHECK(audio::AudioBankProviderCorrelationIdle(&context));
    return true;
}

bool TestGenerationNeverWrapsToIdentityReuse()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    context.next_scope_generation = UINT64_MAX;
    const auto token = audio::BeginAudioBankLoad(
        &context, 1, "never", true, false, false, 150);
    CHECK(!token.tracked);
    CHECK(token.generation == 0);
    CHECK(context.generation_exhausted);
    CHECK(context.integrity_loss_latched);
    return true;
}

bool TestSpeechCallsiteAcceptsOnlySpeechContract()
{
    audio::AudioBankProviderCorrelationContext context = {};
    audio::InitializeAudioBankProviderCorrelation(&context);
    const auto token = audio::BeginAudioBankLoad(
        &context, 0x5151, "speech_en", true, false, true, 160);
    CHECK(ObserveExactSpeechOpen(
              &context,
              "X:\\base\\data\\speech_en\\menu_dialogue.bank",
              0x6161, 161) ==
          audio::AudioNativeOpenCompleteResult::Correlated);
    audio::AudioBankProviderRecord record = {};
    CHECK(audio::EndAudioBankLoad(&context, token, 0, 163, &record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(record.outcome == audio::AudioBankProviderOutcome::ExactProvider);
    CHECK(record.observation_complete);
    CHECK(record.first_open.caller_return_rva ==
          audio::kSpeechBankNativeOpenReturnRva);
    CHECK(std::strcmp(record.first_open.provider_path,
                      "X:\\base\\data\\speech_en\\menu_dialogue.bank") ==
          0);
    return true;
}

bool TestNativeOpenFlagsAreCallsiteSpecific()
{
    audio::AudioBankProviderCorrelationContext fmod_context = {};
    audio::InitializeAudioBankProviderCorrelation(&fmod_context);
    const auto fmod = audio::BeginAudioBankLoad(
        &fmod_context, 1, "fmod", true, false, false, 170);
    audio::AudioNativeOpenAttempt fmod_attempt = {};
    CHECK(audio::BeginAudioNativeOpen(
              &fmod_context,
              audio::kSoundBankManagerNativeOpenReturnRva,
              0, audio::kSoundBankNativeOpenMode,
              audio::kSpeechBankNativeOpenFlags,
              "X:\\base\\data\\audio\\fmod.bank", true, 171,
              &fmod_attempt) ==
          audio::AudioNativeOpenBeginResult::ArgumentContractMismatch);
    audio::AudioBankProviderRecord fmod_record = {};
    CHECK(audio::EndAudioBankLoad(
              &fmod_context, fmod, 0, 172, &fmod_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(fmod_record.outcome ==
          audio::AudioBankProviderOutcome::ArgumentContractMismatch);

    audio::AudioBankProviderCorrelationContext speech_context = {};
    audio::InitializeAudioBankProviderCorrelation(&speech_context);
    const auto speech = audio::BeginAudioBankLoad(
        &speech_context, 2, "speech", true, false, true, 173);
    audio::AudioNativeOpenAttempt speech_attempt = {};
    CHECK(audio::BeginAudioNativeOpen(
              &speech_context, audio::kSpeechBankNativeOpenReturnRva,
              0, audio::kSoundBankNativeOpenMode,
              audio::kSoundBankNativeOpenFlags,
              "X:\\base\\data\\speech_en\\speech.bank", true, 174,
              &speech_attempt) ==
          audio::AudioNativeOpenBeginResult::ArgumentContractMismatch);
    audio::AudioBankProviderRecord speech_record = {};
    CHECK(audio::EndAudioBankLoad(
              &speech_context, speech, 0, 175, &speech_record) ==
          audio::AudioBankProviderEndResult::Completed);
    CHECK(speech_record.outcome ==
          audio::AudioBankProviderOutcome::ArgumentContractMismatch);
    return true;
}

bool TestFullPathProbeSlotPublishesExactSource()
{
    audio::AudioFullPathProbeState state = {};
    audio::InitializeAudioFullPathProbeState(&state);
    audio::AudioFullPathProbeToken token = {};
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x1234,
              audio::AudioFullPathProbeOwner::Fmod, 7, &token) ==
          audio::AudioFullPathProbeArmResult::Armed);
    CHECK(state.active_slot_count == 1);
    auto& slot = state.slots[token.slot_index];
    slot.selected_source = 0xABCDEF00;
    slot.observation_count = 1;
    std::uintptr_t source = 0;
    CHECK(audio::DrainAudioFullPathProbe(&state, token, &source) ==
          audio::AudioFullPathProbeDrainResult::ExactSource);
    CHECK(source == 0xABCDEF00);
    CHECK(audio::DrainAudioFullPathProbe(&state, token, &source) ==
          audio::AudioFullPathProbeDrainResult::NoObservation);
    CHECK(audio::DisarmAudioFullPathProbe(&state, &token));
    CHECK(state.active_slot_count == 0);
    CHECK(!token.armed);
    return true;
}

bool TestFullPathProbeSlotCollisionFailsClosed()
{
    audio::AudioFullPathProbeState state = {};
    audio::InitializeAudioFullPathProbeState(&state);
    audio::AudioFullPathProbeToken first = {};
    audio::AudioFullPathProbeToken collision = {};
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x0101,
              audio::AudioFullPathProbeOwner::Fmod, 1, &first) ==
          audio::AudioFullPathProbeArmResult::Armed);
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x0201,
              audio::AudioFullPathProbeOwner::Speech, 2,
              &collision) ==
          audio::AudioFullPathProbeArmResult::SlotCollision);
    CHECK(!collision.armed);
    CHECK(state.active_slot_count == 1);
    CHECK(audio::DisarmAudioFullPathProbe(&state, &first));
    return true;
}

bool TestNestedFullPathProbePoisonsOuterAttribution()
{
    audio::AudioFullPathProbeState state = {};
    audio::InitializeAudioFullPathProbeState(&state);
    audio::AudioFullPathProbeToken outer = {};
    audio::AudioFullPathProbeToken inner = {};
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x33,
              audio::AudioFullPathProbeOwner::Fmod, 1, &outer) ==
          audio::AudioFullPathProbeArmResult::Armed);
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x33,
              audio::AudioFullPathProbeOwner::Speech, 2, &inner) ==
          audio::AudioFullPathProbeArmResult::NestedSameThread);
    std::uintptr_t source = 0;
    CHECK(audio::DrainAudioFullPathProbe(&state, outer, &source) ==
          audio::AudioFullPathProbeDrainResult::MultipleSources);
    CHECK(!inner.armed);
    CHECK(audio::DisarmAudioFullPathProbe(&state, &outer));
    return true;
}

bool TestStaleFullPathProbeTokenCannotDrainReusedSlot()
{
    audio::AudioFullPathProbeState state = {};
    audio::InitializeAudioFullPathProbeState(&state);
    audio::AudioFullPathProbeToken first = {};
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x44,
              audio::AudioFullPathProbeOwner::Fmod, 1, &first) ==
          audio::AudioFullPathProbeArmResult::Armed);
    const audio::AudioFullPathProbeToken stale = first;
    CHECK(audio::DisarmAudioFullPathProbe(&state, &first));
    audio::AudioFullPathProbeToken reused = {};
    CHECK(audio::ArmAudioFullPathProbe(
              &state, 0x44,
              audio::AudioFullPathProbeOwner::Fmod, 2, &reused) ==
          audio::AudioFullPathProbeArmResult::Armed);
    std::uintptr_t source = 0;
    CHECK(audio::DrainAudioFullPathProbe(&state, stale, &source) ==
          audio::AudioFullPathProbeDrainResult::InvalidToken);
    CHECK(audio::DisarmAudioFullPathProbe(&state, &reused));
    return true;
}

using TestFunction = bool (*)();

struct TestCase {
    const char* name;
    TestFunction function;
};

}  // namespace

int main()
{
    const TestCase cases[] = {
        {"exact_provider", &TestExactProvider},
        {"exact_full_path_source_is_carried_into_native_open",
         &TestExactFullPathSourceIsCarriedIntoNativeOpen},
        {"required_full_path_source_missing_fails_closed",
         &TestRequiredFullPathSourceMissingFailsClosed},
        {"multiple_full_path_sources_fail_closed",
         &TestMultipleFullPathSourcesFailClosed},
        {"cache_hit_does_not_invent_provider",
         &TestCacheHitDoesNotInventProvider},
        {"nested_loads_keep_providers_separate",
         &TestNestedLoadsKeepProvidersSeparate},
        {"open_attempt_survives_completed_reentry",
         &TestOpenAttemptSurvivesCompletedReentry},
        {"multiple_open_retains_first_and_fails_closed",
         &TestMultipleOpenRetainsFirstAndFailsClosed},
        {"overflow_suppresses_instead_of_attributing_to_parent",
         &TestOverflowSuppressesInsteadOfAttributingToParent},
        {"stale_scope_generation_cannot_close_reused_pointer",
         &TestStaleScopeGenerationCannotCloseReusedPointer},
        {"stale_open_attempt_cannot_attach_to_reused_frame",
         &TestStaleOpenAttemptCannotAttachToReusedFrame},
        {"unrelated_native_open_is_ignored",
         &TestUnrelatedNativeOpenIsIgnored},
        {"exact_callsite_requires_exact_arguments",
         &TestExactCallsiteRequiresExactArguments},
        {"incomplete_path_fails_closed", &TestIncompletePathFailsClosed},
        {"failed_open_is_evidence_but_not_a_provider",
         &TestFailedOpenIsEvidenceButNotAProvider},
        {"out_of_order_exit_cannot_corrupt_stack",
         &TestOutOfOrderExitCannotCorruptStack},
        {"generation_never_wraps_to_identity_reuse",
         &TestGenerationNeverWrapsToIdentityReuse},
        {"speech_callsite_accepts_only_speech_contract",
         &TestSpeechCallsiteAcceptsOnlySpeechContract},
        {"native_open_flags_are_callsite_specific",
         &TestNativeOpenFlagsAreCallsiteSpecific},
        {"full_path_probe_slot_publishes_exact_source",
         &TestFullPathProbeSlotPublishesExactSource},
        {"full_path_probe_slot_collision_fails_closed",
         &TestFullPathProbeSlotCollisionFailsClosed},
        {"nested_full_path_probe_poisons_outer_attribution",
         &TestNestedFullPathProbePoisonsOuterAttribution},
        {"stale_full_path_probe_token_cannot_drain_reused_slot",
         &TestStaleFullPathProbeTokenCannotDrainReusedSlot},
    };
    for (const TestCase& test : cases) {
        if (test.function()) {
            ++g_passes;
            std::printf("[PASS] %s\n", test.name);
        } else {
            ++g_failures;
            std::printf("[FAIL] %s\n", test.name);
        }
    }
    std::printf("audio bank provider correlation cases passed: %d\n",
                g_passes);
    return g_failures ? 1 : 0;
}
