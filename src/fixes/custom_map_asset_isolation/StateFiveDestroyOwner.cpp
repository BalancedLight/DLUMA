#include "StateFiveDestroyOwner.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::state_five_owner {

PleaseWaitOwnerToken PublishPleaseWaitStateTwoOwnerToken(
    void* game_di, std::uint32_t content_epoch,
    std::uint64_t destroy_generation,
    std::uint64_t frame_route_token,
    std::uint64_t frame_authority_sequence,
    std::uint64_t published_qpc,
    std::uint32_t callback_thread_id,
    bool exact_state_two,
    bool frame_authority_conflict) noexcept
{
    PleaseWaitOwnerToken token = {};
    token.game_di = game_di;
    token.content_epoch = content_epoch;
    token.destroy_generation = destroy_generation;
    token.frame_route_token = frame_route_token;
    token.frame_authority_sequence = frame_authority_sequence;
    token.published_qpc = published_qpc;
    token.c08_callback_thread_id = callback_thread_id;
    token.exact_state_two = exact_state_two;
    token.frame_authority_conflict = frame_authority_conflict;
    token.completed = exact_state_two && game_di && content_epoch &&
        destroy_generation;
    token.consumed = false;
    return token;
}

RejectReason AuthorizeStateFiveC10AcrossWorkerMigration(
    const PleaseWaitOwnerToken* token,
    const C10AttemptEvidence& attempt,
    std::uint64_t last_consumed_generation) noexcept
{
    if (!attempt.content_tracking_live || !attempt.exact_hook_profile)
        return RejectReason::C10HookProfileInvalid;
    if (attempt.caller_rva != kExactStateFiveCallerRva)
        return RejectReason::C10CallerRvaInvalid;
    if (!attempt.state_readable || attempt.state != 5U)
        return RejectReason::C10StateNotFive;
    if (!token)
        return RejectReason::C08NotObserved;
    if (!token->exact_state_two) {
        return token->game_di
            ? RejectReason::C08StateNotTwo
            : RejectReason::C08NotObserved;
    }
    if (!token->completed)
        return token->destroy_generation
            ? RejectReason::C08NotObserved
            : RejectReason::C08TokenGenerationMissing;
    if (token->game_di != attempt.game_di)
        return RejectReason::C08TokenGameDiMismatch;
    if (!attempt.content_epoch ||
        token->content_epoch != attempt.content_epoch)
        return RejectReason::C08TokenEpochMismatch;
    if (!token->destroy_generation)
        return RejectReason::C08TokenGenerationMissing;
    if (token->consumed)
        return RejectReason::C08TokenConsumed;
    if (token->destroy_generation <= last_consumed_generation)
        return RejectReason::C08TokenStale;
    if (token->frame_authority_conflict ||
        attempt.frame_authority_conflict)
        return RejectReason::C10FrameAuthorityConflict;
    if (!token->frame_route_token ||
        !attempt.frame_route_token ||
        !attempt.current_serialized_route_active ||
        !attempt.current_frame_thread_id ||
        attempt.current_frame_thread_id !=
            attempt.c10_callback_thread_id)
        return RejectReason::C10FrameRouteUnavailable;
    if (token->frame_route_token != attempt.frame_route_token)
        return RejectReason::C10FrameRouteChanged;
    if (!attempt.outgoing_session_readable)
        return RejectReason::C10SessionUnreadable;
    if (!attempt.outgoing_session)
        return RejectReason::C10SessionMissing;
    if (attempt.next_project_staging)
        return RejectReason::C10NextProjectStaging;
    if (attempt.shutdown_requested)
        return RejectReason::C10Shutdown;
    if (attempt.conflicting_scope_active)
        return RejectReason::C10ScopeAlreadyActive;
    return RejectReason::C10ScopeMatched;
}

RejectReason BeginExactStateFiveScope(
    StateFiveScope* scope,
    PleaseWaitOwnerToken* token,
    const C10AttemptEvidence& attempt,
    unsigned int owner_callback_depth,
    std::uint64_t* last_consumed_generation) noexcept
{
    if (!scope || !token || !last_consumed_generation ||
        !owner_callback_depth)
        return RejectReason::C10ScopeBeginRejected;
    if (scope->active || attempt.conflicting_scope_active)
        return RejectReason::C10ScopeAlreadyActive;
    const RejectReason authorization =
        AuthorizeStateFiveC10AcrossWorkerMigration(
            token, attempt, *last_consumed_generation);
    if (authorization != RejectReason::C10ScopeMatched)
        return authorization;

    std::memset(scope, 0, sizeof(*scope));
    scope->game_di = attempt.game_di;
    scope->outgoing_session = attempt.outgoing_session;
    scope->content_epoch = attempt.content_epoch;
    scope->destroy_generation = token->destroy_generation;
    scope->frame_route_token = token->frame_route_token;
    scope->frame_authority_sequence =
        attempt.frame_authority_sequence;
    scope->c08_callback_thread_id =
        token->c08_callback_thread_id;
    scope->c10_callback_thread_id =
        attempt.c10_callback_thread_id;
    scope->owner_callback_depth = owner_callback_depth;
    scope->active = true;
    token->consumed = true;
    *last_consumed_generation = token->destroy_generation;
    return RejectReason::C10ScopeMatched;
}

RejectReason BindSessionOnDestroyToStateFiveScope(
    StateFiveScope* scope, void* session,
    std::uint32_t current_thread_id,
    unsigned int current_callback_depth,
    bool allow_claimed) noexcept
{
    if (!scope || !scope->active || !scope->game_di ||
        !scope->content_epoch || !scope->destroy_generation ||
        !scope->frame_route_token || !scope->armed)
        return RejectReason::C10ScopeBeginRejected;
    if (!session || session != scope->outgoing_session)
        return RejectReason::SessionMismatch;
    if (!current_thread_id ||
        current_thread_id != scope->c10_callback_thread_id)
        return RejectReason::SessionWrongThread;
    if (!current_callback_depth ||
        current_callback_depth != scope->owner_callback_depth)
        return RejectReason::SessionDepthMismatch;
    if (scope->on_destroy_claimed && !allow_claimed)
        return RejectReason::SessionScopeConsumed;
    if (!allow_claimed)
        scope->on_destroy_claimed = true;
    return RejectReason::C10ScopeMatched;
}

PendingPostC10Token MakePendingPostC10Token(
    const PostC10Expectation& expectation,
    std::uint32_t c08_callback_thread_id,
    std::uint32_t session_callback_thread_id,
    std::uint64_t session_original_return_sequence,
    std::uint64_t session_original_return_qpc,
    std::uint64_t token_publication_sequence,
    std::uint32_t release_packs_enter_count,
    std::uint32_t release_packs_complete_count,
    bool unload_level_observed,
    bool unload_level_completed) noexcept
{
    PendingPostC10Token token = {};
    token.game_di = expectation.game_di;
    token.outgoing_session = expectation.outgoing_session;
    token.outgoing_level = expectation.outgoing_level;
    token.baseline_generation = expectation.baseline_generation;
    token.destroy_generation = expectation.destroy_generation;
    token.frame_route_token = expectation.frame_route_token;
    token.frame_authority_sequence =
        expectation.frame_authority_sequence;
    token.source_set_identity = expectation.source_set_identity;
    token.source_registry_generation =
        expectation.source_registry_generation;
    token.session_original_return_sequence =
        session_original_return_sequence;
    token.session_original_return_qpc =
        session_original_return_qpc;
    token.token_publication_sequence =
        token_publication_sequence;
    token.content_epoch = expectation.content_epoch;
    token.c08_callback_thread_id = c08_callback_thread_id;
    token.c10_callback_thread_id =
        expectation.c10_callback_thread_id;
    token.session_callback_thread_id =
        session_callback_thread_id;
    token.session_callback_depth =
        expectation.owner_callback_depth;
    token.release_packs_enter_count =
        release_packs_enter_count;
    token.release_packs_complete_count =
        release_packs_complete_count;
    token.unload_level_observed = unload_level_observed;
    token.unload_level_completed = unload_level_completed;
    token.active = expectation.game_di &&
        expectation.outgoing_session &&
        expectation.outgoing_level &&
        expectation.baseline_generation &&
        expectation.destroy_generation &&
        expectation.frame_route_token &&
        expectation.frame_authority_sequence &&
        expectation.source_set_identity &&
        expectation.source_registry_generation &&
        expectation.content_epoch &&
        expectation.c10_callback_thread_id &&
        expectation.owner_callback_depth &&
        c08_callback_thread_id &&
        session_callback_thread_id &&
        session_original_return_sequence &&
        token_publication_sequence &&
        unload_level_observed &&
        unload_level_completed &&
        release_packs_enter_count == 1U &&
        release_packs_complete_count == 1U;
    return token;
}

PostC10Failure PublishPendingPostC10Token(
    PendingPostC10Token* slot,
    const PendingPostC10Token& candidate) noexcept
{
    if (!slot || !candidate.active)
        return PostC10Failure::TokenMismatch;
    if (slot->active && !slot->consumed) {
        slot->conflict = true;
        return PostC10Failure::TokenMismatch;
    }
    *slot = candidate;
    return PostC10Failure::None;
}

PostC10Failure ValidatePostC10PreflightOwner(
    const PendingPostC10Token* token,
    const PostC10Expectation& expectation,
    const PostC10Environment* environment) noexcept
{
    if (!token || !token->active)
        return PostC10Failure::TokenMissing;
    if (token->consumed)
        return PostC10Failure::TokenReplayed;
    if (token->conflict)
        return PostC10Failure::TokenMismatch;
    if (token->game_di != expectation.game_di ||
        token->outgoing_session != expectation.outgoing_session ||
        token->outgoing_level != expectation.outgoing_level ||
        token->baseline_generation !=
            expectation.baseline_generation ||
        token->destroy_generation != expectation.destroy_generation ||
        token->content_epoch != expectation.content_epoch ||
        token->session_callback_depth !=
            expectation.owner_callback_depth ||
        !token->session_original_return_sequence ||
        !token->token_publication_sequence ||
        !token->c08_callback_thread_id)
        return PostC10Failure::TokenMismatch;
    if (token->frame_route_token !=
            expectation.frame_route_token ||
        token->frame_authority_sequence !=
            expectation.frame_authority_sequence)
        return PostC10Failure::RouteMismatch;
    if (!environment)
        return PostC10Failure::TokenMismatch;
    if (environment->frame_authority_conflict)
        return PostC10Failure::AuthorityConflict;
    if (!environment->frame_route_active ||
        environment->frame_route_token !=
            expectation.frame_route_token ||
        environment->frame_authority_sequence !=
            expectation.frame_authority_sequence)
        return PostC10Failure::RouteMismatch;
    if (!expectation.c10_callback_thread_id ||
        environment->current_thread_id !=
            expectation.c10_callback_thread_id ||
        token->c10_callback_thread_id !=
            expectation.c10_callback_thread_id ||
        token->session_callback_thread_id !=
            expectation.c10_callback_thread_id)
        return PostC10Failure::WrongThread;
    if (environment->owner_callback_depth !=
            expectation.owner_callback_depth)
        return PostC10Failure::TokenMismatch;
    if (!token->unload_level_observed ||
        !token->unload_level_completed ||
        token->release_packs_enter_count != 1U ||
        token->release_packs_complete_count != 1U)
        return PostC10Failure::TokenMismatch;
    if (!environment->current_session_slot_readable)
        return PostC10Failure::SessionSlotUnreadable;
    if (environment->current_session ==
        expectation.outgoing_session)
        return PostC10Failure::OutgoingSessionStillPublished;
    if (environment->current_session)
        return PostC10Failure::UnexpectedReplacementSession;
    if (environment->active_level)
        return PostC10Failure::ActiveLevelStillPresent;
    if (environment->local_player_di)
        return PostC10Failure::LocalPlayerStillPresent;
    if (environment->live_gameplay_consumer)
        return PostC10Failure::LiveGameplayConsumer;
    if (token->source_set_identity !=
            expectation.source_set_identity ||
        token->source_registry_generation !=
            expectation.source_registry_generation ||
        environment->source_set_identity !=
            expectation.source_set_identity ||
        environment->source_registry_generation !=
            expectation.source_registry_generation)
        return PostC10Failure::SourceRegistryChanged;
    return PostC10Failure::None;
}

PostC10Failure ConsumePendingPostC10Token(
    PendingPostC10Token* slot,
    const PostC10Expectation& expectation,
    const PostC10Environment* environment) noexcept
{
    const PostC10Failure failure =
        ValidatePostC10PreflightOwner(
            slot, expectation, environment);
    if (!slot || !slot->active ||
        failure == PostC10Failure::TokenMissing ||
        failure == PostC10Failure::TokenReplayed)
        return failure;
    slot->consumed = true;
    return failure;
}

void PersistC10Result(
    PersistedResult* persisted, RejectReason reason) noexcept
{
    if (!persisted) return;
    persisted->c10_disposition = DispositionForReason(reason);
    persisted->c10_reject_reason = reason;
}

void PersistSessionOnDestroyResult(
    PersistedResult* persisted, RejectReason reason) noexcept
{
    if (!persisted) return;
    persisted->session_on_destroy_disposition =
        DispositionForReason(reason);
    persisted->session_on_destroy_reject_reason = reason;
}

Disposition DispositionForReason(RejectReason reason) noexcept
{
    if (reason == RejectReason::C10ScopeMatched)
        return Disposition::Matched;
    if (reason == RejectReason::C08NotObserved ||
        reason == RejectReason::None)
        return Disposition::NotObserved;
    return Disposition::Rejected;
}

const char* DispositionName(Disposition disposition) noexcept
{
    switch (disposition) {
    case Disposition::NotObserved: return "not_observed";
    case Disposition::Rejected: return "rejected";
    case Disposition::Matched: return "matched";
    default: return "not_observed";
    }
}

const char* RejectReasonName(RejectReason reason) noexcept
{
    switch (reason) {
    case RejectReason::None: return "none";
    case RejectReason::C08NotObserved: return "c08_not_observed";
    case RejectReason::C08StateNotTwo:
        return "c08_state_not_two";
    case RejectReason::C08TokenGameDiMismatch:
        return "c08_token_game_di_mismatch";
    case RejectReason::C08TokenEpochMismatch:
        return "c08_token_epoch_mismatch";
    case RejectReason::C08TokenGenerationMissing:
        return "c08_token_generation_missing";
    case RejectReason::C08TokenStale:
        return "c08_token_stale";
    case RejectReason::C08TokenConsumed:
        return "c08_token_consumed";
    case RejectReason::C10HookProfileInvalid:
        return "c10_hook_profile_invalid";
    case RejectReason::C10CallerRvaInvalid:
        return "c10_caller_rva_invalid";
    case RejectReason::C10StateNotFive:
        return "c10_state_not_five";
    case RejectReason::C10FrameRouteUnavailable:
        return "c10_frame_route_unavailable";
    case RejectReason::C10FrameRouteChanged:
        return "c10_frame_route_changed";
    case RejectReason::C10FrameAuthorityConflict:
        return "c10_frame_authority_conflict";
    case RejectReason::C10SessionUnreadable:
        return "c10_session_unreadable";
    case RejectReason::C10SessionMissing:
        return "c10_session_missing";
    case RejectReason::C10NextProjectStaging:
        return "c10_next_project_staging";
    case RejectReason::C10Shutdown: return "c10_shutdown";
    case RejectReason::C10ScopeAlreadyActive:
        return "c10_scope_already_active";
    case RejectReason::C10ScopeBeginRejected:
        return "c10_scope_begin_rejected";
    case RejectReason::C10ArmRejected:
        return "c10_arm_rejected";
    case RejectReason::C10ScopeMatched:
        return "c10_scope_matched";
    case RejectReason::SessionMismatch:
        return "session_on_destroy_session_mismatch";
    case RejectReason::SessionScopeConsumed:
        return "session_on_destroy_scope_consumed";
    case RejectReason::SessionWrongThread:
        return "session_on_destroy_wrong_c10_thread";
    case RejectReason::SessionDepthMismatch:
        return "session_on_destroy_callback_depth_mismatch";
    default: return "c08_not_observed";
    }
}

const char* PostC10FailureName(PostC10Failure failure) noexcept
{
    switch (failure) {
    case PostC10Failure::None:
        return "none";
    case PostC10Failure::TokenMissing:
        return "post_c10_token_missing";
    case PostC10Failure::TokenMismatch:
        return "post_c10_token_mismatch";
    case PostC10Failure::TokenReplayed:
        return "post_c10_token_replayed";
    case PostC10Failure::WrongThread:
        return "post_c10_wrong_thread";
    case PostC10Failure::RouteMismatch:
        return "post_c10_route_mismatch";
    case PostC10Failure::AuthorityConflict:
        return "post_c10_authority_conflict";
    case PostC10Failure::SessionSlotUnreadable:
        return "post_c10_session_slot_unreadable";
    case PostC10Failure::OutgoingSessionStillPublished:
        return "post_c10_outgoing_session_still_published";
    case PostC10Failure::UnexpectedReplacementSession:
        return "post_c10_unexpected_replacement_session";
    case PostC10Failure::ActiveLevelStillPresent:
        return "post_c10_active_level_still_present";
    case PostC10Failure::LocalPlayerStillPresent:
        return "post_c10_local_player_still_present";
    case PostC10Failure::LiveGameplayConsumer:
        return "post_c10_live_gameplay_consumer";
    case PostC10Failure::SourceRegistryChanged:
        return "post_c10_source_registry_changed";
    default:
        return "post_c10_token_mismatch";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::state_five_owner
