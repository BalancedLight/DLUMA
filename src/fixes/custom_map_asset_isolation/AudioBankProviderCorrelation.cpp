#define _HAS_EXCEPTIONS 0

#include "AudioBankProviderCorrelation.h"

#include "../../dlw_win32.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio {
namespace {

void Clear(void* object, std::size_t size) noexcept
{
    if (object && size) std::memset(object, 0, size);
}

void ResetProbeToken(AudioFullPathProbeToken* token) noexcept
{
    if (!token) return;
    Clear(token, sizeof(*token));
    token->slot_index = UINT16_MAX;
}

volatile LONG* AsLong(volatile std::int32_t* value) noexcept
{
    return reinterpret_cast<volatile LONG*>(value);
}

volatile LONGLONG* AsLongLong(volatile std::int64_t* value) noexcept
{
    return reinterpret_cast<volatile LONGLONG*>(value);
}

bool ProbeTokenMatches(
    const AudioFullPathProbeSlot& slot,
    const AudioFullPathProbeToken& token) noexcept
{
    return token.armed && token.thread_id != 0U &&
        token.slot_index < kAudioFullPathProbeSlotCapacity &&
        _InterlockedCompareExchange(
            AsLong(const_cast<volatile std::int32_t*>(&slot.thread_id)),
            0, 0) == static_cast<LONG>(token.thread_id) &&
        _InterlockedCompareExchange(
            AsLong(const_cast<volatile std::int32_t*>(&slot.armed)),
            0, 0) == 1 &&
        _InterlockedCompareExchange(
            AsLong(const_cast<volatile std::int32_t*>(&slot.owner)),
            0, 0) == static_cast<LONG>(token.owner) &&
        _InterlockedCompareExchange64(
            AsLongLong(const_cast<volatile std::int64_t*>(
                &slot.scope_generation)), 0, 0) ==
            static_cast<LONGLONG>(token.scope_generation);
}

bool CopyBounded(char* output, std::size_t capacity,
                 const char* input) noexcept
{
    if (!output || !capacity) return false;
    std::size_t index = 0;
    if (input) {
        while (index + 1 < capacity && input[index]) {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = 0;
    return input && input[index] == 0;
}

bool TakeGeneration(std::uint64_t* next,
                    bool* exhausted,
                    std::uint64_t* output) noexcept
{
    if (!next || !exhausted || !output || *exhausted) return false;
    if (*next == UINT64_MAX) {
        *exhausted = true;
        return false;
    }
    ++*next;
    if (!*next) {
        *exhausted = true;
        return false;
    }
    *output = *next;
    return true;
}

void MarkTopFrameOverflow(
    AudioBankProviderCorrelationContext* context) noexcept
{
    if (!context || !context->logical_depth) return;
    const std::uint32_t index = context->logical_depth >
            kAudioProviderFrameCapacity
        ? static_cast<std::uint32_t>(kAudioProviderFrameCapacity - 1U)
        : context->logical_depth - 1U;
    if (index < kAudioProviderFrameCapacity)
        context->frames[index].nested_stack_overflow = true;
}

void RecoverIdleLatches(
    AudioBankProviderCorrelationContext* context) noexcept
{
    if (!context || context->logical_depth) return;
    context->suppression_depth = 0;
    context->hard_overflow_latched = false;
    context->integrity_loss_latched = false;
}

bool IsCurrentToken(
    const AudioBankProviderCorrelationContext* context,
    const AudioBankLoadToken& token) noexcept
{
    return context && token.tracked && token.logical_depth &&
        context->logical_depth == token.logical_depth &&
        token.logical_depth <= kAudioProviderLogicalDepthCapacity &&
        context->active_generation[token.logical_depth - 1U] ==
            token.generation;
}

bool IsCompletedGeneration(const AudioBankProviderFrame& frame,
                           std::uint64_t generation) noexcept
{
    for (std::uint16_t index = 0;
         index < frame.completed_generation_count; ++index) {
        if (frame.completed_generations[index] == generation) return true;
    }
    return false;
}

void CopyFrameToRecord(const AudioBankProviderFrame& frame,
                       std::int32_t manager_result,
                       std::uint64_t exit_sequence,
                       bool context_integrity_loss,
                       AudioBankProviderRecord* output) noexcept
{
    Clear(output, sizeof(*output));
    output->generation = frame.generation;
    output->enter_sequence = frame.enter_sequence;
    output->exit_sequence = exit_sequence;
    output->manager = frame.manager;
    output->logical_depth = frame.logical_depth;
    output->manager_result = manager_result;
    output->native_open_count = frame.native_open_count;
    output->resolved_source_count = frame.resolved_source_count;
    output->selected_source = frame.selected_source;
    output->reload = frame.reload;
    output->localised = frame.localised;
    output->bank_name_copy_complete = frame.bank_name_copy_complete;
    output->nested_stack_overflow = frame.nested_stack_overflow;
    output->multiple_native_opens = frame.multiple_open_loss;
    output->generation_mismatch =
        frame.generation_mismatch_loss || context_integrity_loss;
    output->source_identity_loss = frame.source_identity_loss;
    (void)CopyBounded(output->bank_name, sizeof(output->bank_name),
                      frame.bank_name);
    if (frame.first_open_available) {
        output->first_open = frame.first_open;
        output->callsite_matched = frame.first_open.callsite_matched;
        output->argument_contract_matched =
            frame.first_open.argument_contract_matched;
        output->path_copy_complete = frame.first_open.path_copy_complete;
        output->open_succeeded = frame.first_open.open_succeeded;
        output->exact_source_identity_available =
            frame.first_open.exact_source &&
            frame.first_open.selected_source != 0;
        output->provider_identity_available =
            frame.first_open.open_succeeded &&
            frame.first_open.path_copy_complete &&
            frame.first_open.argument_contract_matched &&
            frame.first_open.callsite_matched;
    }

    const bool required_source_missing =
        frame.resolved_source_observation_required &&
        frame.native_open_count != 0U &&
        (frame.resolved_source_count != 1U ||
         frame.selected_source == 0);
    output->source_identity_loss =
        output->source_identity_loss || required_source_missing;
    const bool structural_loss = context_integrity_loss ||
        frame.nested_stack_overflow || frame.generation_mismatch_loss ||
        frame.completion_overflow || output->source_identity_loss;
    if (structural_loss) {
        output->outcome = frame.nested_stack_overflow
            ? AudioBankProviderOutcome::StackOverflow
            : output->source_identity_loss
                ? AudioBankProviderOutcome::SourceIdentityUnavailable
                : AudioBankProviderOutcome::GenerationMismatch;
    } else if (frame.pending_open_count) {
        output->outcome = AudioBankProviderOutcome::PendingNativeOpen;
    } else if (frame.multiple_open_loss || frame.native_open_count > 1U) {
        output->outcome = AudioBankProviderOutcome::MultipleNativeOpens;
    } else if (frame.argument_contract_loss) {
        output->outcome =
            AudioBankProviderOutcome::ArgumentContractMismatch;
    } else if (frame.path_copy_loss || !frame.bank_name_copy_complete) {
        output->outcome = AudioBankProviderOutcome::PathUnavailable;
    } else if (manager_result == 1 && frame.native_open_count == 0) {
        /* SoundBankManagerFMOD's exact same-name hit returns before the
         * provider-bearing native open.  Never synthesize a provider. */
        output->outcome = AudioBankProviderOutcome::CacheHitNoOpen;
    } else if (manager_result == 0 && frame.native_open_count == 1U &&
               output->provider_identity_available) {
        output->outcome = AudioBankProviderOutcome::ExactProvider;
    } else if (manager_result < 0) {
        output->outcome = AudioBankProviderOutcome::LoadFailed;
    } else if (frame.native_open_count == 0) {
        output->outcome = AudioBankProviderOutcome::MissingNativeOpen;
    } else {
        output->outcome = AudioBankProviderOutcome::UnexpectedNativeOpen;
    }

    output->observation_complete = !structural_loss &&
        !frame.pending_open_count && !frame.multiple_open_loss &&
        !frame.argument_contract_loss && !frame.path_copy_loss &&
        frame.bank_name_copy_complete &&
        (output->outcome == AudioBankProviderOutcome::ExactProvider ||
         output->outcome == AudioBankProviderOutcome::CacheHitNoOpen ||
         output->outcome == AudioBankProviderOutcome::LoadFailed);
}

}  // namespace

void InitializeAudioBankProviderCorrelation(
    AudioBankProviderCorrelationContext* context) noexcept
{
    Clear(context, context ? sizeof(*context) : 0);
}

void InitializeAudioFullPathProbeState(
    AudioFullPathProbeState* state) noexcept
{
    Clear(state, state ? sizeof(*state) : 0U);
}

AudioFullPathProbeArmResult ArmAudioFullPathProbe(
    AudioFullPathProbeState* state, std::uint32_t thread_id,
    AudioFullPathProbeOwner owner, std::uint64_t scope_generation,
    AudioFullPathProbeToken* token) noexcept
{
    ResetProbeToken(token);
    if (!state || !token || !thread_id || !scope_generation ||
        owner == AudioFullPathProbeOwner::None)
        return AudioFullPathProbeArmResult::InvalidArgument;

    const std::uint16_t slot_index = static_cast<std::uint16_t>(
        thread_id & kAudioFullPathProbeSlotMask);
    AudioFullPathProbeSlot* const slot = &state->slots[slot_index];
    const LONG observed_thread = _InterlockedCompareExchange(
        AsLong(&slot->thread_id), static_cast<LONG>(thread_id), 0);
    if (observed_thread != 0 &&
        observed_thread != static_cast<LONG>(thread_id))
        return AudioFullPathProbeArmResult::SlotCollision;

    const LONG observed_armed = _InterlockedCompareExchange(
        AsLong(&slot->armed), -1, 0);
    if (observed_armed != 0) {
        if (observed_thread == static_cast<LONG>(thread_id)) {
            /* The single slot cannot distinguish nested owners. Poison the
             * outer observation instead of attributing the inner provider. */
            _InterlockedExchange(AsLong(&slot->observation_count), 2);
            return AudioFullPathProbeArmResult::NestedSameThread;
        }
        if (observed_thread == 0)
            _InterlockedCompareExchange(
                AsLong(&slot->thread_id), 0,
                static_cast<LONG>(thread_id));
        return AudioFullPathProbeArmResult::SlotCollision;
    }

    _InterlockedExchange(AsLong(&slot->owner),
                         static_cast<LONG>(owner));
    _InterlockedExchange(AsLong(&slot->observation_count), 0);
    _InterlockedExchange64(
        AsLongLong(&slot->scope_generation),
        static_cast<LONGLONG>(scope_generation));
    _InterlockedExchange64(AsLongLong(&slot->selected_source), 0);
    _InterlockedExchange(AsLong(&slot->armed), 1);
    _InterlockedIncrement(AsLong(&state->active_slot_count));

    token->scope_generation = scope_generation;
    token->thread_id = thread_id;
    token->slot_index = slot_index;
    token->owner = owner;
    token->armed = true;
    return AudioFullPathProbeArmResult::Armed;
}

AudioFullPathProbeDrainResult DrainAudioFullPathProbe(
    AudioFullPathProbeState* state,
    const AudioFullPathProbeToken& token,
    std::uintptr_t* selected_source) noexcept
{
    if (selected_source) *selected_source = 0;
    if (!state || !selected_source ||
        token.slot_index >= kAudioFullPathProbeSlotCapacity)
        return AudioFullPathProbeDrainResult::InvalidToken;
    AudioFullPathProbeSlot* const slot =
        &state->slots[token.slot_index];
    if (!ProbeTokenMatches(*slot, token))
        return AudioFullPathProbeDrainResult::InvalidToken;

    const LONG count = _InterlockedExchange(
        AsLong(&slot->observation_count), 0);
    const LONGLONG source = _InterlockedExchange64(
        AsLongLong(&slot->selected_source), 0);
    if (count <= 0)
        return AudioFullPathProbeDrainResult::NoObservation;
    if (count != 1)
        return AudioFullPathProbeDrainResult::MultipleSources;
    if (!source)
        return AudioFullPathProbeDrainResult::SourceUnavailable;
    *selected_source = static_cast<std::uintptr_t>(source);
    return AudioFullPathProbeDrainResult::ExactSource;
}

bool DisarmAudioFullPathProbe(
    AudioFullPathProbeState* state,
    AudioFullPathProbeToken* token) noexcept
{
    if (!state || !token ||
        token->slot_index >= kAudioFullPathProbeSlotCapacity)
        return false;
    AudioFullPathProbeSlot* const slot =
        &state->slots[token->slot_index];
    if (!ProbeTokenMatches(*slot, *token)) {
        ResetProbeToken(token);
        return false;
    }

    const LONG prior = _InterlockedExchange(AsLong(&slot->armed), 0);
    if (prior == 1)
        _InterlockedDecrement(AsLong(&state->active_slot_count));
    _InterlockedExchange(AsLong(&slot->owner), 0);
    _InterlockedExchange(AsLong(&slot->observation_count), 0);
    _InterlockedExchange64(AsLongLong(&slot->scope_generation), 0);
    _InterlockedExchange64(AsLongLong(&slot->selected_source), 0);
    _InterlockedExchange(AsLong(&slot->thread_id), 0);
    ResetProbeToken(token);
    return prior == 1;
}

bool SetAudioResolvedSourceObservationRequired(
    AudioBankProviderCorrelationContext* context,
    bool required) noexcept
{
    if (!context || context->logical_depth != 0U) return false;
    context->resolved_source_observation_required = required;
    return true;
}

AudioBankLoadToken BeginAudioBankLoad(
    AudioBankProviderCorrelationContext* context,
    std::uintptr_t manager, const char* bank_name,
    bool bank_name_copy_complete, bool reload, bool localised,
    std::uint64_t enter_sequence) noexcept
{
    AudioBankLoadToken token = {};
    token.frame_index = kAudioProviderInvalidFrame;
    if (!context) return token;

    std::uint64_t generation = 0;
    if (!TakeGeneration(&context->next_scope_generation,
                        &context->generation_exhausted, &generation)) {
        context->integrity_loss_latched = true;
        return token;
    }
    token.generation = generation;

    if (context->logical_depth >=
        kAudioProviderLogicalDepthCapacity) {
        ++context->dropped_scope_count;
        context->hard_overflow_latched = true;
        context->integrity_loss_latched = true;
        MarkTopFrameOverflow(context);
        token.suppressed = true;
        return token;
    }

    const std::uint32_t depth_index = context->logical_depth;
    token.logical_depth = depth_index + 1U;
    token.tracked = true;
    context->active_generation[depth_index] = generation;
    ++context->logical_depth;

    if (context->suppression_depth ||
        depth_index >= kAudioProviderFrameCapacity) {
        ++context->suppression_depth;
        ++context->dropped_scope_count;
        MarkTopFrameOverflow(context);
        token.suppressed = true;
        return token;
    }

    token.frame_index = static_cast<std::uint16_t>(depth_index);
    AudioBankProviderFrame* frame = &context->frames[depth_index];
    Clear(frame, sizeof(*frame));
    frame->generation = generation;
    frame->enter_sequence = enter_sequence;
    frame->manager = manager;
    frame->logical_depth = token.logical_depth;
    frame->reload = reload;
    frame->localised = localised;
    frame->resolved_source_observation_required =
        context->resolved_source_observation_required;
    frame->bank_name_copy_complete = bank_name_copy_complete &&
        CopyBounded(frame->bank_name, sizeof(frame->bank_name), bank_name);
    if (!frame->bank_name_copy_complete) frame->path_copy_loss = true;
    return token;
}

AudioResolvedSourceObservationResult ObserveAudioResolvedSource(
    AudioBankProviderCorrelationContext* context,
    std::uintptr_t selected_source) noexcept
{
    if (!context || !context->logical_depth)
        return AudioResolvedSourceObservationResult::NoActiveBankLoad;
    if (context->suppression_depth || context->hard_overflow_latched ||
        context->integrity_loss_latched)
        return AudioResolvedSourceObservationResult::Suppressed;
    const std::uint32_t frame_index = context->logical_depth - 1U;
    if (frame_index >= kAudioProviderFrameCapacity)
        return AudioResolvedSourceObservationResult::Suppressed;
    AudioBankProviderFrame* frame = &context->frames[frame_index];
    if (!frame->generation ||
        context->active_generation[frame_index] != frame->generation) {
        frame->generation_mismatch_loss = true;
        context->integrity_loss_latched = true;
        ++context->generation_mismatch_count;
        return AudioResolvedSourceObservationResult::GenerationMismatch;
    }
    if (!selected_source) {
        frame->source_identity_loss = true;
        return AudioResolvedSourceObservationResult::InvalidSource;
    }
    if (frame->resolved_source_count != 0U) {
        if (frame->resolved_source_count != UINT16_MAX)
            ++frame->resolved_source_count;
        frame->source_identity_loss = true;
        return AudioResolvedSourceObservationResult::MultipleResolutions;
    }
    frame->resolved_source_count = 1U;
    frame->selected_source = selected_source;
    return AudioResolvedSourceObservationResult::Correlated;
}

AudioNativeOpenBeginResult BeginAudioNativeOpen(
    AudioBankProviderCorrelationContext* context,
    std::uint32_t caller_return_rva, std::uintptr_t explicit_source,
    std::int32_t mode, std::uint32_t flags,
    const char* full_path, bool path_copy_complete,
    std::uint64_t enter_sequence,
    AudioNativeOpenAttempt* attempt) noexcept
{
    if (attempt) Clear(attempt, sizeof(*attempt));
    if (!context || !attempt || !context->logical_depth)
        return AudioNativeOpenBeginResult::NoActiveBankLoad;
    if (context->suppression_depth || context->hard_overflow_latched ||
        context->integrity_loss_latched)
        return AudioNativeOpenBeginResult::Suppressed;
    const bool fmod_call =
        caller_return_rva == kSoundBankManagerNativeOpenReturnRva;
    const bool speech_call =
        caller_return_rva == kSpeechBankNativeOpenReturnRva;
    if (!fmod_call && !speech_call)
        return AudioNativeOpenBeginResult::DifferentCallSite;

    const std::uint32_t frame_index = context->logical_depth - 1U;
    if (frame_index >= kAudioProviderFrameCapacity)
        return AudioNativeOpenBeginResult::Suppressed;
    AudioBankProviderFrame* frame = &context->frames[frame_index];
    if (!frame->generation ||
        context->active_generation[frame_index] != frame->generation) {
        frame->generation_mismatch_loss = true;
        context->integrity_loss_latched = true;
        ++context->generation_mismatch_count;
        return AudioNativeOpenBeginResult::Suppressed;
    }

    const std::uint32_t expected_flags = fmod_call
        ? kSoundBankNativeOpenFlags : kSpeechBankNativeOpenFlags;
    if (explicit_source != 0 || mode != kSoundBankNativeOpenMode ||
        flags != expected_flags) {
        frame->argument_contract_loss = true;
        return AudioNativeOpenBeginResult::ArgumentContractMismatch;
    }
    if (!path_copy_complete || !full_path || !full_path[0]) {
        frame->path_copy_loss = true;
        return AudioNativeOpenBeginResult::PathUnavailable;
    }

    std::uint64_t generation = 0;
    if (!TakeGeneration(&context->next_open_generation,
                        &context->generation_exhausted, &generation)) {
        context->integrity_loss_latched = true;
        return AudioNativeOpenBeginResult::GenerationExhausted;
    }
    attempt->attempt_generation = generation;
    attempt->scope_generation = frame->generation;
    attempt->enter_sequence = enter_sequence;
    attempt->caller_return_rva = caller_return_rva;
    attempt->frame_index = static_cast<std::uint16_t>(frame_index);
    attempt->selected_source = frame->selected_source;
    attempt->exact_source =
        frame->resolved_source_count == 1U &&
        frame->selected_source != 0 &&
        !frame->source_identity_loss;
    attempt->callsite_matched = true;
    attempt->argument_contract_matched = true;
    attempt->path_copy_complete =
        CopyBounded(attempt->provider_path,
                    sizeof(attempt->provider_path), full_path);
    if (!attempt->path_copy_complete) {
        frame->path_copy_loss = true;
        return AudioNativeOpenBeginResult::PathUnavailable;
    }
    attempt->armed = true;
    if (frame->pending_open_count != UINT16_MAX)
        ++frame->pending_open_count;
    else {
        frame->completion_overflow = true;
        ++context->dropped_open_count;
        attempt->armed = false;
        return AudioNativeOpenBeginResult::Suppressed;
    }
    return AudioNativeOpenBeginResult::Armed;
}

AudioNativeOpenCompleteResult CompleteAudioNativeOpen(
    AudioBankProviderCorrelationContext* context,
    AudioNativeOpenAttempt* attempt, std::uintptr_t native_file,
    std::uint64_t exit_sequence) noexcept
{
    if (!context || !attempt || !attempt->armed)
        return AudioNativeOpenCompleteResult::InvalidAttempt;
    const std::uint16_t frame_index = attempt->frame_index;
    if (frame_index >= kAudioProviderFrameCapacity)
        return AudioNativeOpenCompleteResult::InvalidAttempt;
    if (context->suppression_depth || context->hard_overflow_latched)
        return AudioNativeOpenCompleteResult::Suppressed;

    AudioBankProviderFrame* frame = &context->frames[frame_index];
    if (context->logical_depth != frame_index + 1U ||
        context->active_generation[frame_index] !=
            attempt->scope_generation ||
        frame->generation != attempt->scope_generation) {
        ++context->generation_mismatch_count;
        context->integrity_loss_latched = true;
        if (frame->generation == attempt->scope_generation)
            frame->generation_mismatch_loss = true;
        attempt->armed = false;
        return AudioNativeOpenCompleteResult::GenerationMismatch;
    }
    if (IsCompletedGeneration(*frame,
                              attempt->attempt_generation)) {
        attempt->armed = false;
        return AudioNativeOpenCompleteResult::DuplicateCompletion;
    }
    if (frame->completed_generation_count >=
        kAudioProviderCompletedOpenCapacity) {
        frame->completion_overflow = true;
        if (frame->pending_open_count) --frame->pending_open_count;
        ++context->dropped_open_count;
        attempt->armed = false;
        return AudioNativeOpenCompleteResult::CompletionCapacityExceeded;
    }

    frame->completed_generations[frame->completed_generation_count++] =
        attempt->attempt_generation;
    if (frame->pending_open_count) --frame->pending_open_count;
    if (frame->native_open_count != UINT16_MAX)
        ++frame->native_open_count;
    else {
        frame->completion_overflow = true;
        ++context->dropped_open_count;
    }

    AudioNativeOpenObservation observation = {};
    observation.attempt_generation = attempt->attempt_generation;
    observation.enter_sequence = attempt->enter_sequence;
    observation.exit_sequence = exit_sequence;
    observation.native_file = native_file;
    observation.selected_source = attempt->selected_source;
    observation.caller_return_rva = attempt->caller_return_rva;
    observation.callsite_matched = attempt->callsite_matched;
    observation.argument_contract_matched =
        attempt->argument_contract_matched;
    observation.path_copy_complete = attempt->path_copy_complete;
    observation.open_succeeded = native_file != 0;
    observation.exact_source = attempt->exact_source;
    observation.native_file_instance_generation = attempt->native_file_instance_generation;
    observation.provider_instance_generation = attempt->provider_instance_generation;
    (void)CopyBounded(observation.provider_path,
                      sizeof(observation.provider_path),
                      attempt->provider_path);

    const bool first = !frame->first_open_available;
    if (first) {
        frame->first_open = observation;
        frame->first_open_available = true;
    } else {
        /* Provider selection is ambiguous until the load semantics of every
         * native open are understood. Preserve the first proof and fail
         * closed instead of silently replacing it with the last. */
        frame->multiple_open_loss = true;
    }
    attempt->armed = false;
    if (!first)
        return AudioNativeOpenCompleteResult::CorrelatedAdditionalOpen;
    return native_file
        ? AudioNativeOpenCompleteResult::Correlated
        : AudioNativeOpenCompleteResult::CorrelatedOpenFailed;
}

AudioBankProviderEndResult EndAudioBankLoad(
    AudioBankProviderCorrelationContext* context,
    const AudioBankLoadToken& token, std::int32_t manager_result,
    std::uint64_t exit_sequence,
    AudioBankProviderRecord* output) noexcept
{
    if (output) Clear(output, sizeof(*output));
    if (!context || !token.tracked)
        return token.suppressed
            ? AudioBankProviderEndResult::Suppressed
            : AudioBankProviderEndResult::InvalidToken;
    if (!IsCurrentToken(context, token)) {
        ++context->generation_mismatch_count;
        context->integrity_loss_latched = true;
        if (token.frame_index < kAudioProviderFrameCapacity &&
            context->frames[token.frame_index].generation ==
                token.generation)
            context->frames[token.frame_index].generation_mismatch_loss =
                true;
        return AudioBankProviderEndResult::GenerationMismatch;
    }

    const std::uint32_t depth_index = token.logical_depth - 1U;
    if (token.suppressed) {
        if (context->suppression_depth) --context->suppression_depth;
        context->active_generation[depth_index] = 0;
        --context->logical_depth;
        RecoverIdleLatches(context);
        return AudioBankProviderEndResult::Suppressed;
    }
    if (token.frame_index != depth_index ||
        token.frame_index >= kAudioProviderFrameCapacity)
        return AudioBankProviderEndResult::InvalidToken;

    AudioBankProviderFrame* frame = &context->frames[token.frame_index];
    if (frame->generation != token.generation) {
        ++context->generation_mismatch_count;
        context->integrity_loss_latched = true;
        return AudioBankProviderEndResult::GenerationMismatch;
    }
    if (output)
        CopyFrameToRecord(*frame, manager_result, exit_sequence,
                          context->integrity_loss_latched, output);
    Clear(frame, sizeof(*frame));
    context->active_generation[depth_index] = 0;
    --context->logical_depth;
    RecoverIdleLatches(context);
    return AudioBankProviderEndResult::Completed;
}

bool AudioBankProviderCorrelationIdle(
    const AudioBankProviderCorrelationContext* context) noexcept
{
    return context && context->logical_depth == 0 &&
        context->suppression_depth == 0 &&
        !context->hard_overflow_latched &&
        !context->integrity_loss_latched;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio
