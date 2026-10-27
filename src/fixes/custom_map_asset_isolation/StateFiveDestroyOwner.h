#pragma once

#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::state_five_owner {

constexpr std::uint32_t kExactStateFiveCallerRva = 0x003D0970U;

enum class Disposition : std::uint8_t {
    NotObserved = 0,
    Rejected,
    Matched,
};

enum class RejectReason : std::uint8_t {
    None = 0,
    C08NotObserved,
    C08StateNotTwo,
    C08TokenGameDiMismatch,
    C08TokenEpochMismatch,
    C08TokenGenerationMissing,
    C08TokenStale,
    C08TokenConsumed,
    C10HookProfileInvalid,
    C10CallerRvaInvalid,
    C10StateNotFive,
    C10FrameRouteUnavailable,
    C10FrameRouteChanged,
    C10FrameAuthorityConflict,
    C10SessionUnreadable,
    C10SessionMissing,
    C10NextProjectStaging,
    C10Shutdown,
    C10ScopeAlreadyActive,
    C10ScopeBeginRejected,
    C10ArmRejected,
    C10ScopeMatched,
    SessionMismatch,
    SessionScopeConsumed,
    SessionWrongThread,
    SessionDepthMismatch,
};

enum class PostC10Failure : std::uint8_t {
    None = 0,
    TokenMissing,
    TokenMismatch,
    TokenReplayed,
    WrongThread,
    RouteMismatch,
    AuthorityConflict,
    SessionSlotUnreadable,
    OutgoingSessionStillPublished,
    UnexpectedReplacementSession,
    ActiveLevelStillPresent,
    LocalPlayerStillPresent,
    LiveGameplayConsumer,
    SourceRegistryChanged,
};

struct PleaseWaitOwnerToken {
    void* game_di;
    std::uint32_t content_epoch;
    std::uint64_t destroy_generation;
    std::uint64_t frame_route_token;
    std::uint64_t frame_authority_sequence;
    std::uint64_t published_qpc;
    std::uint32_t c08_callback_thread_id;
    bool exact_state_two;
    bool frame_authority_conflict;
    bool completed;
    bool consumed;
};

struct PendingPostC10Token {
    void* game_di;
    void* outgoing_session;
    void* outgoing_level;
    std::uint64_t baseline_generation;
    std::uint64_t destroy_generation;
    std::uint64_t frame_route_token;
    std::uint64_t frame_authority_sequence;
    std::uint64_t source_set_identity;
    std::uint64_t source_registry_generation;
    std::uint64_t session_original_return_sequence;
    std::uint64_t session_original_return_qpc;
    std::uint64_t token_publication_sequence;
    std::uint32_t content_epoch;
    std::uint32_t c08_callback_thread_id;
    std::uint32_t c10_callback_thread_id;
    std::uint32_t session_callback_thread_id;
    std::uint32_t session_callback_depth;
    std::uint32_t release_packs_enter_count;
    std::uint32_t release_packs_complete_count;
    bool unload_level_observed;
    bool unload_level_completed;
    bool active;
    bool consumed;
    bool conflict;
};

struct PostC10Expectation {
    void* game_di;
    void* outgoing_session;
    void* outgoing_level;
    std::uint64_t baseline_generation;
    std::uint64_t destroy_generation;
    std::uint64_t frame_route_token;
    std::uint64_t frame_authority_sequence;
    std::uint64_t source_set_identity;
    std::uint64_t source_registry_generation;
    std::uint32_t content_epoch;
    std::uint32_t c10_callback_thread_id;
    std::uint32_t owner_callback_depth;
};

struct PostC10Environment {
    void* current_session;
    void* active_level;
    void* local_player_di;
    std::uint64_t frame_route_token;
    std::uint64_t frame_authority_sequence;
    std::uint64_t source_set_identity;
    std::uint64_t source_registry_generation;
    std::uint32_t current_thread_id;
    std::uint32_t owner_callback_depth;
    bool live_gameplay_consumer;
    bool current_session_slot_readable;
    bool frame_route_active;
    bool frame_authority_conflict;
};

struct C10AttemptEvidence {
    void* game_di;
    void* outgoing_session;
    std::uint32_t content_epoch;
    std::uint32_t state;
    std::uint32_t caller_rva;
    std::uint32_t c10_callback_thread_id;
    std::uint32_t current_frame_thread_id;
    std::uint64_t frame_route_token;
    std::uint64_t frame_authority_sequence;
    bool content_tracking_live;
    bool exact_hook_profile;
    bool state_readable;
    bool current_serialized_route_active;
    bool frame_authority_conflict;
    bool outgoing_session_readable;
    bool next_project_staging;
    bool shutdown_requested;
    bool conflicting_scope_active;
};

struct StateFiveScope {
    void* game_di;
    void* outgoing_session;
    std::uint32_t content_epoch;
    std::uint64_t destroy_generation;
    std::uint64_t frame_route_token;
    std::uint64_t frame_authority_sequence;
    std::uint32_t c08_callback_thread_id;
    std::uint32_t c10_callback_thread_id;
    unsigned int owner_callback_depth;
    bool active;
    bool on_destroy_claimed;
    bool armed;
};

struct PersistedResult {
    Disposition c10_disposition;
    RejectReason c10_reject_reason;
    Disposition session_on_destroy_disposition;
    RejectReason session_on_destroy_reject_reason;
};

PleaseWaitOwnerToken PublishPleaseWaitStateTwoOwnerToken(
    void* game_di, std::uint32_t content_epoch,
    std::uint64_t destroy_generation,
    std::uint64_t frame_route_token,
    std::uint64_t frame_authority_sequence,
    std::uint64_t published_qpc,
    std::uint32_t callback_thread_id,
    bool exact_state_two,
    bool frame_authority_conflict) noexcept;

RejectReason AuthorizeStateFiveC10AcrossWorkerMigration(
    const PleaseWaitOwnerToken* token,
    const C10AttemptEvidence& attempt,
    std::uint64_t last_consumed_generation) noexcept;

RejectReason BeginExactStateFiveScope(
    StateFiveScope* scope,
    PleaseWaitOwnerToken* token,
    const C10AttemptEvidence& attempt,
    unsigned int owner_callback_depth,
    std::uint64_t* last_consumed_generation) noexcept;

RejectReason BindSessionOnDestroyToStateFiveScope(
    StateFiveScope* scope, void* session,
    std::uint32_t current_thread_id,
    unsigned int current_callback_depth,
    bool allow_claimed = false) noexcept;

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
    bool unload_level_completed) noexcept;

PostC10Failure PublishPendingPostC10Token(
    PendingPostC10Token* slot,
    const PendingPostC10Token& candidate) noexcept;

PostC10Failure ValidatePostC10PreflightOwner(
    const PendingPostC10Token* token,
    const PostC10Expectation& expectation,
    const PostC10Environment* environment) noexcept;

PostC10Failure ConsumePendingPostC10Token(
    PendingPostC10Token* slot,
    const PostC10Expectation& expectation,
    const PostC10Environment* environment) noexcept;

void PersistC10Result(
    PersistedResult* persisted, RejectReason reason) noexcept;
void PersistSessionOnDestroyResult(
    PersistedResult* persisted, RejectReason reason) noexcept;
Disposition DispositionForReason(RejectReason reason) noexcept;
const char* DispositionName(Disposition disposition) noexcept;
const char* RejectReasonName(RejectReason reason) noexcept;
const char* PostC10FailureName(PostC10Failure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::state_five_owner
