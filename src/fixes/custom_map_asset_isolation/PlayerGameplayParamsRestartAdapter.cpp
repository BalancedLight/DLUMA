#define _HAS_EXCEPTIONS 0

#include "PlayerGameplayParamsRestartAdapter.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart {
namespace {

constexpr std::uint32_t kRequiredPropertyIds[
    kRequiredPlayerPropertyCount] = {
        kLowHealthEffectThresholdPropertyId,
        kStaminaWarningLevelPropertyId,
        kMaxStaminaPropertyId,
        kMoveSprintSpeedPropertyId,
        kMaxHealthPropertyId,
        kBaseMaxHealthPropertyId,
        kHealthCriticalLevelPropertyId,
        kHealthRegenerationDelayPropertyId,
        kHealthRegenerationMaxLevelPropertyId,
        kHealthRegenerationTimeToMaxPropertyId,
        kCameraDefaultFovPropertyId,
        kKickStaminaUsagePropertyId,
        kCameraCarFovPropertyId,
        kFovSprintModifierPropertyId,
        kFovAmortizerModifierPropertyId,
};

class TryLock {
public:
    explicit TryLock(volatile LONG* lock) noexcept
        : lock_(lock), locked_(false)
    {
        if (lock_ && dluma_atomic_compare_exchange(lock_, 1, 0) == 0)
            locked_ = true;
    }

    ~TryLock()
    {
        if (locked_) dluma_atomic_exchange(lock_, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    volatile LONG* lock_;
    bool locked_;
};

void SetFailure(PlayerGameplayParamsRestartAdapter* adapter,
                PlayerRestartFailure failure) noexcept
{
    if (adapter) adapter->last_failure = failure;
}

DomainResult Fail(PlayerGameplayParamsRestartAdapter* adapter,
                  PlayerRestartFailure failure,
                  bool after_mutation) noexcept
{
    SetFailure(adapter, failure);
    if (adapter && after_mutation) {
        adapter->runtime_failure_latched = true;
        adapter->reset_postconditions_complete = false;
        adapter->verified = false;
    }
    return DomainResult::Failed;
}

bool EnvironmentOperationsComplete(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter && adapter->snapshot_operations.read_environment &&
        adapter->snapshot_operations.environment_layout_validated;
}

bool StateSnapshotOperationsComplete(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter && adapter->snapshot_operations.snapshot_state &&
        adapter->snapshot_operations.state_snapshot_layout_validated;
}

bool SnapshotOperationsComplete(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return EnvironmentOperationsComplete(adapter) &&
        StateSnapshotOperationsComplete(adapter);
}

bool InventoryRoutesValidated(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter &&
        adapter->constructor_route_validated &&
        adapter->complete_destructor_route_validated;
}

bool MutationOperationsComplete(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter && adapter->mutation_operations.reset_global_cache &&
        adapter->mutation_operations.initialize_variables;
}

bool MutationRoutesValidated(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter &&
        adapter->mutation_operations.global_cache_reset_route_validated &&
        adapter->mutation_operations.initialize_variables_route_validated;
}

bool MutationProductionEnabled(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter && adapter->mutation_operations.production_enabled;
}

bool AllowsUnobservedProgressionTraceMetadata(
    const PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    return adapter && adapter->snapshot_operations.
        allow_unobserved_progression_trace_metadata;
}

bool ProviderComplete(const PlayerContentProviderIdentity& provider,
                      bool require_base_provider) noexcept
{
    return provider.observed && provider.exact_source && provider.source &&
        provider.normalized_path_fingerprint &&
        provider.source_registry_generation_observed &&
        provider.source_record_index != UINT32_MAX &&
        provider.source_inventory_ordinal &&
        (!require_base_provider ||
         (provider.base_provider && !provider.content_epoch));
}

bool ProviderUnobservedAndZero(
    const PlayerContentProviderIdentity& provider) noexcept
{
    return !provider.source && !provider.normalized_path_fingerprint &&
        !provider.source_registry_generation && !provider.content_epoch &&
        !provider.source_record_index && !provider.source_inventory_ordinal &&
        !provider.observed && !provider.exact_source &&
        !provider.base_provider &&
        !provider.source_registry_generation_observed;
}

bool ProviderAccepted(const PlayerContentProviderIdentity& provider,
                      bool require_base_provider,
                      bool allow_unobserved_metadata) noexcept
{
    return ProviderComplete(provider, require_base_provider) ||
        (allow_unobserved_metadata &&
         ProviderUnobservedAndZero(provider));
}

bool CacheGenerationMatches(std::uint64_t actual,
                            std::uint64_t expected,
                            bool allow_unobserved_metadata) noexcept
{
    if (actual || expected || !allow_unobserved_metadata)
        return actual == expected && expected != 0;
    return true;
}

bool CacheGenerationAdvanced(std::uint64_t before,
                             std::uint64_t after,
                             bool allow_unobserved_metadata) noexcept
{
    if (before || after || !allow_unobserved_metadata)
        return after > before;
    return true;
}

bool ProviderEqual(const PlayerContentProviderIdentity& expected,
                   const PlayerContentProviderIdentity& actual) noexcept
{
    return expected.source == actual.source &&
        expected.normalized_path_fingerprint ==
            actual.normalized_path_fingerprint &&
        expected.source_registry_generation ==
            actual.source_registry_generation &&
        expected.content_epoch == actual.content_epoch &&
        expected.source_record_index == actual.source_record_index &&
        expected.source_inventory_ordinal ==
            actual.source_inventory_ordinal &&
        expected.observed == actual.observed &&
        expected.exact_source == actual.exact_source &&
        expected.base_provider == actual.base_provider &&
        expected.source_registry_generation_observed ==
            actual.source_registry_generation_observed;
}

bool SnapshotSemanticEqual(
    const PlayerStateRestartSnapshot& left,
    const PlayerStateRestartSnapshot& right) noexcept
{
    if (left.state != right.state ||
        left.owner_slot != right.owner_slot ||
        left.owner_kind != right.owner_kind ||
        left.state_generation != right.state_generation ||
        left.player_variables != right.player_variables ||
        left.progression_selector_fingerprint !=
            right.progression_selector_fingerprint ||
        left.class_selector_fingerprint !=
            right.class_selector_fingerprint ||
        left.skill_membership_fingerprint !=
            right.skill_membership_fingerprint ||
        left.regular_buff_fingerprint !=
            right.regular_buff_fingerprint ||
        left.timed_buff_fingerprint !=
            right.timed_buff_fingerprint ||
        left.appearance_fingerprint != right.appearance_fingerprint ||
        left.cache_generation != right.cache_generation)
        return false;
    for (std::size_t index = 0; index < kPlayerLogicalTypeCount;
         ++index) {
        const PlayerDefinitionRowSnapshot& a = left.rows[index];
        const PlayerDefinitionRowSnapshot& b = right.rows[index];
        if (a.definition_vector != b.definition_vector ||
            a.definition_fingerprint != b.definition_fingerprint ||
            a.earned_experience != b.earned_experience ||
            a.definition_count != b.definition_count ||
            a.unknown_100 != b.unknown_100 ||
            a.current_level != b.current_level ||
            a.maximum_level != b.maximum_level ||
            a.trailing_qword != b.trailing_qword ||
            a.row_readable != b.row_readable ||
            a.definition_readable != b.definition_readable ||
            a.preset_absent != b.preset_absent ||
            a.semantic_complete != b.semantic_complete)
            return false;
    }
    for (std::size_t index = 0;
         index < kRequiredPlayerPropertyCount; ++index) {
        const PlayerPropertySnapshot& a = left.properties[index];
        const PlayerPropertySnapshot& b = right.properties[index];
        if (a.property_id != b.property_id ||
            a.value_bits != b.value_bits ||
            a.available != b.available)
            return false;
    }
    return left.body_readable == right.body_readable &&
        left.row_table_readable == right.row_table_readable &&
        left.player_variables_readable ==
            right.player_variables_readable &&
        left.selectors_complete == right.selectors_complete &&
        left.preservation_invariants_complete ==
            right.preservation_invariants_complete &&
        left.appearance_complete == right.appearance_complete &&
        left.player_semantic_snapshot_complete ==
            right.player_semantic_snapshot_complete;
}
void ResetB0AttemptDiagnostics(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (!adapter) return;
    const std::uint64_t bootstrap_sequence =
        adapter->b0_diagnostics.bootstrap_sample_sequence;
    const std::uint32_t bootstrap_count =
        adapter->b0_diagnostics.bootstrap_sample_owner_count;
    std::memset(
        &adapter->b0_diagnostics, 0,
        sizeof(adapter->b0_diagnostics));
    adapter->b0_diagnostics.bootstrap_sample_sequence =
        bootstrap_sequence;
    adapter->b0_diagnostics.bootstrap_sample_owner_count =
        bootstrap_count;
}

void SeedB0StateDiagnostic(
    PlayerB0StateCaptureDiagnostics* output,
    const PlayerStateInventoryRecord& owner) noexcept
{
    if (!output) return;
    std::memset(output, 0, sizeof(*output));
    output->state = owner.pointer;
    output->owner_slot = owner.owner_object;
    output->generation = owner.generation;
    output->owner_kind = owner.owner_kind;
    output->failed_row_index = UINT32_MAX;
    output->pass1_first_failed_row_index = UINT32_MAX;
    output->pass2_first_failed_row_index = UINT32_MAX;
}

void UpdateB0StateDiagnostic(
    PlayerB0StateCaptureDiagnostics* output,
    const PlayerStateRestartSnapshot& snapshot,
    std::uint32_t attempt_count,
    bool complete) noexcept
{
    if (!output) return;
    output->memory_layout_reason = snapshot.memory_layout_reason;
    output->provider_reason = snapshot.provider_reason;
    output->semantic_reason_mask = snapshot.semantic_reason_mask;
    output->restart_provenance_reason_mask =
        snapshot.restart_provenance_reason_mask;
    output->cache_generation = snapshot.cache_generation;
    output->snapshot_attempt_count = attempt_count;
    output->failed_row_index = snapshot.failed_row_index;
    output->failed_property_id = snapshot.failed_property_id;
    output->failure_stage = complete
        ? PlayerStateSnapshotFailureStage::None
        : snapshot.failure_stage ==
                  PlayerStateSnapshotFailureStage::None
            ? PlayerStateSnapshotFailureStage::CallbackRejected
            : snapshot.failure_stage;
    output->restart_failure_stage = snapshot.restart_failure_stage;
    PlayerGameplayParamsWindowsRowSnapshot* rows =
        attempt_count == 1U ? output->pass1_rows
                            : output->pass2_rows;
    std::memcpy(
        rows, snapshot.raw_rows,
        sizeof(snapshot.raw_rows));
    PlayerDefinitionRowSnapshot* semantic_rows =
        attempt_count == 1U ? output->pass1_semantic_rows
                            : output->pass2_semantic_rows;
    std::memcpy(
        semantic_rows, snapshot.rows,
        sizeof(snapshot.rows));
    if (attempt_count == 1U) {
        output->pass1_first_failed_row_index =
            snapshot.failed_row_index;
        output->pass1_first_failed_row_reason =
            snapshot.failed_row_index < kPlayerLogicalTypeCount
                ? snapshot.raw_rows[
                      snapshot.failed_row_index].failure_reason
                : snapshot.memory_layout_reason;
        output->pass1_semantic_reason_mask =
            snapshot.semantic_reason_mask;
        output->pass1_restart_provenance_reason_mask =
            snapshot.restart_provenance_reason_mask;
        output->pass1_failure_stage = snapshot.failure_stage;
        output->pass1_restart_failure_stage =
            snapshot.restart_failure_stage;
        output->pass1_semantic_snapshot_complete =
            snapshot.player_semantic_snapshot_complete;
        output->pass1_restart_snapshot_complete =
            snapshot.player_restart_snapshot_complete;
    } else {
        output->pass2_first_failed_row_index =
            snapshot.failed_row_index;
        output->pass2_first_failed_row_reason =
            snapshot.failed_row_index < kPlayerLogicalTypeCount
                ? snapshot.raw_rows[
                      snapshot.failed_row_index].failure_reason
                : snapshot.memory_layout_reason;
        output->pass2_semantic_reason_mask =
            snapshot.semantic_reason_mask;
        output->pass2_restart_provenance_reason_mask =
            snapshot.restart_provenance_reason_mask;
        output->pass2_failure_stage = snapshot.failure_stage;
        output->pass2_restart_failure_stage =
            snapshot.restart_failure_stage;
        output->pass2_semantic_snapshot_complete =
            snapshot.player_semantic_snapshot_complete;
        output->pass2_restart_snapshot_complete =
            snapshot.player_restart_snapshot_complete;
    }
    output->snapshot_complete = complete;
}

void ResetPostDestroyDiagnostics(
    PlayerGameplayParamsRestartAdapter* adapter,
    const RestartContext* context,
    const PlayerThreadAuthorityEvidence* authority) noexcept
{
    if (!adapter) return;
    const std::uint64_t attempt_count =
        adapter->post_destroy_diagnostics.assessment_attempt_count + 1U;
    const std::uint64_t sequence =
        adapter->post_destroy_diagnostics.assessment_sequence + 1U;
    std::memset(
        &adapter->post_destroy_diagnostics, 0,
        sizeof(adapter->post_destroy_diagnostics));
    PlayerPostDestroyCaptureDiagnostics& diagnostics =
        adapter->post_destroy_diagnostics;
    diagnostics.assessment_attempt_count = attempt_count;
    diagnostics.assessment_sequence = sequence;
    diagnostics.baseline_generation = adapter->baseline_generation;
    diagnostics.content_epoch = context ? context->content_epoch : 0;
    diagnostics.owner_thread_id =
        context ? context->owner_thread_id : 0;
    diagnostics.historical_b0_thread_id =
        authority ? authority->historical_b0_thread_id
                  : adapter->baseline_owner_thread_id;
    diagnostics.c08_callback_thread_id =
        authority ? authority->c08_callback_thread_id : 0;
    diagnostics.current_c10_thread_id =
        authority ? authority->current_c10_thread_id : 0;
    diagnostics.current_session_scope_thread_id =
        authority ? authority->current_session_scope_thread_id : 0;
    diagnostics.post_c10_observed_thread_id =
        authority ? authority->post_c10_observed_thread_id : 0;
    diagnostics.pending_post_c10_token_generation =
        authority ? authority->pending_post_c10_token_generation : 0;
    diagnostics.thread_authority_kind =
        authority ? authority->kind
                  : PlayerThreadAuthorityKind::Unspecified;
    diagnostics.baseline_kind = adapter->baseline_kind;
    diagnostics.first_failed_owner_index = UINT32_MAX;
    diagnostics.first_failed_owner_kind = PlayerStateOwnerKind::Unknown;
    diagnostics.first_failed_row = UINT32_MAX;
    diagnostics.final_assessment =
        PlayerGameplayParamsTouchAssessment::Incomplete;
    diagnostics.final_failure = PlayerRestartFailure::None;
    diagnostics.final_exact_failure =
        PlayerPostDestroyExactFailure::None;
}

void SeedPostDestroyStateDiagnostic(
    PlayerPostDestroyStateCaptureDiagnostics* output,
    const PlayerStateInventoryRecord& owner) noexcept
{
    if (!output) return;
    std::memset(output, 0, sizeof(*output));
    output->state = owner.pointer;
    output->owner_slot = owner.owner_object;
    output->generation = owner.generation;
    output->owner_kind = owner.owner_kind;
    output->pass1_failed_row_index = UINT32_MAX;
    output->pass2_failed_row_index = UINT32_MAX;
}

void UpdatePostDestroyStateDiagnostic(
    PlayerPostDestroyStateCaptureDiagnostics* output,
    const PlayerStateRestartSnapshot& snapshot,
    std::uint32_t pass, bool callback_returned,
    bool semantic_complete) noexcept
{
    if (!output || (pass != 1U && pass != 2U)) return;
    PlayerPostDestroyRowCaptureDiagnostics* rows =
        pass == 1U ? output->pass1_rows : output->pass2_rows;
    for (std::uint32_t index = 0;
         index < kPlayerLogicalTypeCount; ++index) {
        const PlayerGameplayParamsWindowsRowSnapshot& raw =
            snapshot.raw_rows[index];
        const PlayerDefinitionRowSnapshot& semantic =
            snapshot.rows[index];
        rows[index].raw_failure_reason = raw.failure_reason;
        rows[index].semantic_definition_fingerprint =
            semantic.definition_fingerprint;
        rows[index].semantic_definition_count =
            semantic.definition_count;
        rows[index].definition_traversal_failed_property_id =
            raw.definition_traversal.failed_property_id;
        rows[index].header_read_result =
            raw.header_read_result;
        rows[index].header_validation_result =
            raw.header_validation_result;
        rows[index].definition_traversal_failure =
            raw.definition_traversal.first_failure;
        rows[index].raw_semantic_complete =
            raw.semantic_complete;
        rows[index].semantic_complete =
            semantic.semantic_complete;
    }
    if (pass == 1U) {
        output->pass1_callback_attempted = true;
        output->pass1_callback_returned = callback_returned;
        output->pass1_semantic_reason_mask =
            snapshot.semantic_reason_mask;
        output->pass1_restart_provenance_reason_mask =
            snapshot.restart_provenance_reason_mask;
        output->pass1_cache_generation = snapshot.cache_generation;
        output->pass1_failed_row_index = snapshot.failed_row_index;
        output->pass1_failed_property_id = snapshot.failed_property_id;
        output->pass1_failure_stage = semantic_complete
            ? PlayerStateSnapshotFailureStage::None
            : snapshot.failure_stage ==
                      PlayerStateSnapshotFailureStage::None
                ? PlayerStateSnapshotFailureStage::CallbackRejected
                : snapshot.failure_stage;
        output->pass1_restart_failure_stage =
            snapshot.restart_failure_stage;
        output->pass1_semantic_snapshot_complete =
            semantic_complete;
        output->pass1_restart_snapshot_complete =
            snapshot.player_restart_snapshot_complete;
    } else {
        output->pass2_callback_attempted = true;
        output->pass2_callback_returned = callback_returned;
        output->pass2_semantic_reason_mask =
            snapshot.semantic_reason_mask;
        output->pass2_restart_provenance_reason_mask =
            snapshot.restart_provenance_reason_mask;
        output->pass2_cache_generation = snapshot.cache_generation;
        output->pass2_failed_row_index = snapshot.failed_row_index;
        output->pass2_failed_property_id = snapshot.failed_property_id;
        output->pass2_failure_stage = semantic_complete
            ? PlayerStateSnapshotFailureStage::None
            : snapshot.failure_stage ==
                      PlayerStateSnapshotFailureStage::None
                ? PlayerStateSnapshotFailureStage::CallbackRejected
                : snapshot.failure_stage;
        output->pass2_restart_failure_stage =
            snapshot.restart_failure_stage;
        output->pass2_semantic_snapshot_complete =
            semantic_complete;
        output->pass2_restart_snapshot_complete =
            snapshot.player_restart_snapshot_complete;
    }
}

PlayerPostDestroyExactFailure ExactFailureForCallback(
    PlayerEnvironmentCallbackFailure failure) noexcept
{
    switch (failure) {
    case PlayerEnvironmentCallbackFailure::InvalidArgument:
        return PlayerPostDestroyExactFailure::InvalidArgument;
    case PlayerEnvironmentCallbackFailure::WrongThread:
        return PlayerPostDestroyExactFailure::ThreadMismatch;
    case PlayerEnvironmentCallbackFailure::UnsupportedImage:
        return PlayerPostDestroyExactFailure::UnsupportedImage;
    case PlayerEnvironmentCallbackFailure::ShutdownOrQuiescing:
        return PlayerPostDestroyExactFailure::ShutdownOrQuiescing;
    case PlayerEnvironmentCallbackFailure::GlobalOwnerSetIncomplete:
        return PlayerPostDestroyExactFailure::
            GlobalOwnerSlotsUnreadable;
    case PlayerEnvironmentCallbackFailure::InventorySnapshotRejected:
    case PlayerEnvironmentCallbackFailure::InventoryObservationLoss:
    case PlayerEnvironmentCallbackFailure::InventoryRevisionChanged:
        return PlayerPostDestroyExactFailure::InventoryScanIncomplete;
    case PlayerEnvironmentCallbackFailure::CacheSnapshotRejected:
    case PlayerEnvironmentCallbackFailure::CacheSnapshotChanged:
    case PlayerEnvironmentCallbackFailure::CacheOwnerChanged:
        return PlayerPostDestroyExactFailure::
            CacheFingerprintUnavailable;
    case PlayerEnvironmentCallbackFailure::None:
    case PlayerEnvironmentCallbackFailure::ContextUnavailable:
    case PlayerEnvironmentCallbackFailure::
             WindowsEnvironmentSnapshotRejected:
    case PlayerEnvironmentCallbackFailure::
             WindowsEnvironmentSnapshotIncomplete:
    case PlayerEnvironmentCallbackFailure::
             LocalRepresentationUnreadable:
    default:
        return PlayerPostDestroyExactFailure::
            EnvironmentCallbackRejected;
    }
}

void RecordFirstPostDestroyStateFailure(
    PlayerPostDestroyCaptureDiagnostics* diagnostics,
    std::uint32_t index,
    const PlayerStateInventoryRecord& owner,
    const PlayerStateRestartSnapshot& snapshot,
    PlayerPostDestroyExactFailure failure) noexcept
{
    if (!diagnostics ||
        diagnostics->first_failed_owner_index != UINT32_MAX)
        return;
    diagnostics->first_failed_owner_index = index;
    diagnostics->first_failed_owner_kind = owner.owner_kind;
    diagnostics->first_failed_state = owner.pointer;
    diagnostics->first_failed_state_generation = owner.generation;
    diagnostics->first_failed_state_stage =
        snapshot.failure_stage == PlayerStateSnapshotFailureStage::None
            ? PlayerStateSnapshotFailureStage::CallbackRejected
            : snapshot.failure_stage;
    diagnostics->first_failed_row = snapshot.failed_row_index;
    diagnostics->first_failed_property_id =
        snapshot.failed_property_id;
    diagnostics->first_failed_semantic_reason_mask =
        snapshot.semantic_reason_mask;
    diagnostics->final_exact_failure = failure;
}

PlayerGameplayParamsTouchAssessment FinishPostDestroyAssessment(
    PlayerGameplayParamsRestartAdapter* adapter,
    PlayerGameplayParamsTouchAssessment assessment,
    PlayerRestartFailure failure,
    PlayerPostDestroyExactFailure exact_failure) noexcept
{
    if (adapter) {
        adapter->last_failure = failure;
        adapter->post_destroy_diagnostics.final_assessment = assessment;
        adapter->post_destroy_diagnostics.final_failure = failure;
        adapter->post_destroy_diagnostics.final_exact_failure =
            exact_failure;
    }
    return assessment;
}

const PlayerPropertySnapshot* FindProperty(
    const PlayerStateRestartSnapshot& snapshot,
    std::uint32_t property_id, std::uint32_t* matches) noexcept
{
    const PlayerPropertySnapshot* found = nullptr;
    std::uint32_t count = 0;
    for (std::size_t index = 0;
         index < kRequiredPlayerPropertyCount; ++index) {
        if (snapshot.properties[index].property_id != property_id)
            continue;
        found = &snapshot.properties[index];
        ++count;
    }
    if (matches) *matches = count;
    return found;
}

void SetSemanticValidationFailure(
    PlayerRestartFailure* failure,
    PlayerStateSnapshotFailureStage* stage,
    std::uint64_t* reason_mask,
    PlayerRestartFailure exact_failure,
    PlayerStateSnapshotFailureStage exact_stage,
    std::uint64_t exact_reason) noexcept
{
    if (failure) *failure = exact_failure;
    if (stage) *stage = exact_stage;
    if (reason_mask) *reason_mask |= exact_reason;
}

bool SemanticSnapshotComplete(
    const PlayerStateRestartSnapshot& snapshot,
    std::uintptr_t expected_state,
    std::uint64_t expected_generation,
    std::uint64_t expected_cache_generation,
    PlayerRestartFailure* failure,
    PlayerStateSnapshotFailureStage* semantic_stage = nullptr,
    std::uint64_t* semantic_reason_mask = nullptr) noexcept
{
    const PlayerStateSnapshotFailureStage observed_failure_stage =
        snapshot.failure_stage;
    if (failure) *failure = PlayerRestartFailure::None;
    if (semantic_stage) *semantic_stage = PlayerStateSnapshotFailureStage::None;
    if (semantic_reason_mask)
        *semantic_reason_mask = snapshot.semantic_reason_mask;
    if (snapshot.state != expected_state ||
        snapshot.state_generation != expected_generation ||
        !snapshot.owner_slot ||
        snapshot.owner_kind == PlayerStateOwnerKind::Unknown ||
        snapshot.cache_generation != expected_cache_generation ||
        !snapshot.player_semantic_snapshot_complete ||
        !snapshot.body_readable || !snapshot.row_table_readable ||
        !snapshot.player_variables_readable || !snapshot.player_variables ||
        !snapshot.selectors_complete ||
        !snapshot.progression_selector_fingerprint ||
        !snapshot.class_selector_fingerprint ||
        !snapshot.preservation_invariants_complete ||
        !snapshot.skill_membership_fingerprint ||
        !snapshot.regular_buff_fingerprint ||
        !snapshot.timed_buff_fingerprint ||
        !snapshot.appearance_complete ||
        !snapshot.appearance_fingerprint ||
        snapshot.semantic_reason_mask) {
        SetSemanticValidationFailure(
            failure, semantic_stage, semantic_reason_mask,
            PlayerRestartFailure::StateSnapshotIncomplete,
            observed_failure_stage ==
                    PlayerStateSnapshotFailureStage::None
                ? PlayerStateSnapshotFailureStage::SemanticRowInvalid
                : observed_failure_stage,
            snapshot.semantic_reason_mask
                ? snapshot.semantic_reason_mask
                : kPlayerSemanticReasonRowInvalid);
        return false;
    }
    for (std::size_t index = 0; index < kPlayerLogicalTypeCount;
         ++index) {
        const PlayerDefinitionRowSnapshot& row = snapshot.rows[index];
        if (!row.row_readable || !row.definition_readable ||
            !row.semantic_complete ||
            (!row.preset_absent &&
             (!row.definition_vector || !row.definition_fingerprint)) ||
            row.current_level > row.maximum_level) {
            SetSemanticValidationFailure(
                failure, semantic_stage, semantic_reason_mask,
                PlayerRestartFailure::SemanticRowInvalid,
                PlayerStateSnapshotFailureStage::SemanticRowInvalid,
                kPlayerSemanticReasonRowInvalid);
            return false;
        }
        if (row.cache_membership_evidence ==
            PlayerCacheMembershipEvidence::Conflict) {
            SetSemanticValidationFailure(
                failure, semantic_stage, semantic_reason_mask,
                PlayerRestartFailure::CacheMembershipConflict,
                PlayerStateSnapshotFailureStage::CacheMembershipConflict,
                kPlayerSemanticReasonCacheMembershipConflict);
            return false;
        }
    }
    for (std::size_t required = 0;
         required < kRequiredPlayerPropertyCount; ++required) {
        std::uint32_t matches = 0;
        const PlayerPropertySnapshot* property = FindProperty(
            snapshot, kRequiredPropertyIds[required], &matches);
        if (!property || !matches) {
            SetSemanticValidationFailure(
                failure, semantic_stage, semantic_reason_mask,
                PlayerRestartFailure::RequiredPropertyMissing,
                PlayerStateSnapshotFailureStage::SemanticPropertyInvalid,
                kPlayerSemanticReasonPropertyInvalid);
            return false;
        }
        if (matches != 1U) {
            SetSemanticValidationFailure(
                failure, semantic_stage, semantic_reason_mask,
                PlayerRestartFailure::RequiredPropertyDuplicate,
                PlayerStateSnapshotFailureStage::SemanticPropertyInvalid,
                kPlayerSemanticReasonPropertyInvalid);
            return false;
        }
        if (!property->available) {
            SetSemanticValidationFailure(
                failure, semantic_stage, semantic_reason_mask,
                PlayerRestartFailure::SemanticPropertyInvalid,
                PlayerStateSnapshotFailureStage::SemanticPropertyInvalid,
                kPlayerSemanticReasonPropertyInvalid);
            return false;
        }
    }
    return true;
}

void AddRestartProvenanceReason(
    std::uint64_t* mask,
    PlayerStateSnapshotFailureStage* first_stage,
    std::uint64_t reason,
    PlayerStateSnapshotFailureStage stage) noexcept
{
    if (mask) *mask |= reason;
    if (first_stage &&
        *first_stage == PlayerStateSnapshotFailureStage::None)
        *first_stage = stage;
}

PlayerRestartFailure RestartFailureForStage(
    PlayerStateSnapshotFailureStage stage) noexcept
{
    switch (stage) {
    case PlayerStateSnapshotFailureStage::RestartCacheMembershipUnproven:
        return PlayerRestartFailure::RestartCacheMembershipUnproven;
    case PlayerStateSnapshotFailureStage::RestartProviderUnproven:
        return PlayerRestartFailure::RestartProviderUnproven;
    case PlayerStateSnapshotFailureStage::RestartInventoryOwnerUnproven:
        return PlayerRestartFailure::RestartInventoryOwnerUnproven;
    case PlayerStateSnapshotFailureStage::RestartEquipmentOwnerUnproven:
        return PlayerRestartFailure::RestartEquipmentOwnerUnproven;
    case PlayerStateSnapshotFailureStage::RestartQuestOwnerUnproven:
        return PlayerRestartFailure::RestartQuestOwnerUnproven;
    default:
        return PlayerRestartFailure::StateSnapshotIncomplete;
    }
}

bool EvaluateRestartProvenance(
    const PlayerStateRestartSnapshot& snapshot,
    std::uint64_t expected_cache_generation,
    bool require_base_provider,
    std::uint64_t* reason_mask,
    PlayerStateSnapshotFailureStage* first_stage) noexcept
{
    std::uint64_t mask = snapshot.restart_provenance_reason_mask;
    PlayerStateSnapshotFailureStage stage =
        PlayerStateSnapshotFailureStage::None;
    if (!snapshot.inventory_fingerprint)
        AddRestartProvenanceReason(
            &mask, &stage,
            kPlayerRestartProvenanceReasonInventoryOwnerUnproven,
            PlayerStateSnapshotFailureStage::
                RestartInventoryOwnerUnproven);
    if (!snapshot.equipment_fingerprint)
        AddRestartProvenanceReason(
            &mask, &stage,
            kPlayerRestartProvenanceReasonEquipmentOwnerUnproven,
            PlayerStateSnapshotFailureStage::
                RestartEquipmentOwnerUnproven);
    if (!snapshot.quest_fingerprint)
        AddRestartProvenanceReason(
            &mask, &stage,
            kPlayerRestartProvenanceReasonQuestOwnerUnproven,
            PlayerStateSnapshotFailureStage::RestartQuestOwnerUnproven);
    if (!ProviderComplete(
            snapshot.appearance_provider, require_base_provider))
        AddRestartProvenanceReason(
            &mask, &stage,
            kPlayerRestartProvenanceReasonProviderUnproven,
            PlayerStateSnapshotFailureStage::RestartProviderUnproven);
    for (std::size_t index = 0; index < kPlayerLogicalTypeCount;
         ++index) {
        const PlayerDefinitionRowSnapshot& row = snapshot.rows[index];
        if (row.preset_absent) continue;
        if (row.cache_membership_evidence !=
                PlayerCacheMembershipEvidence::ProvenMember ||
            !row.cache_observer_available ||
            !row.cache_inventory_complete ||
            !row.cache_generation_observed ||
            !CacheGenerationMatches(
                row.cache_generation, expected_cache_generation, false))
            AddRestartProvenanceReason(
                &mask, &stage,
                kPlayerRestartProvenanceReasonCacheMembershipUnproven,
                PlayerStateSnapshotFailureStage::
                    RestartCacheMembershipUnproven);
        if (!ProviderComplete(row.provider, require_base_provider))
            AddRestartProvenanceReason(
                &mask, &stage,
                kPlayerRestartProvenanceReasonProviderUnproven,
                PlayerStateSnapshotFailureStage::RestartProviderUnproven);
    }
    for (std::size_t required = 0;
         required < kRequiredPlayerPropertyCount; ++required) {
        std::uint32_t matches = 0;
        const PlayerPropertySnapshot* property = FindProperty(
            snapshot, kRequiredPropertyIds[required], &matches);
        if (!property || matches != 1U || !property->available ||
            !CacheGenerationMatches(
                property->cache_generation,
                expected_cache_generation, false))
            AddRestartProvenanceReason(
                &mask, &stage,
                kPlayerRestartProvenanceReasonCacheMembershipUnproven,
                PlayerStateSnapshotFailureStage::
                    RestartCacheMembershipUnproven);
        if (!property || matches != 1U ||
            !ProviderComplete(
                property->provider, require_base_provider))
            AddRestartProvenanceReason(
                &mask, &stage,
                kPlayerRestartProvenanceReasonProviderUnproven,
                PlayerStateSnapshotFailureStage::RestartProviderUnproven);
    }
    if (!snapshot.player_restart_snapshot_complete && !mask)
        AddRestartProvenanceReason(
            &mask, &stage,
            kPlayerRestartProvenanceReasonProviderUnproven,
            PlayerStateSnapshotFailureStage::RestartProviderUnproven);
    if (mask &
        kPlayerRestartProvenanceReasonCacheMembershipUnproven)
        stage = PlayerStateSnapshotFailureStage::
            RestartCacheMembershipUnproven;
    else if (mask & kPlayerRestartProvenanceReasonProviderUnproven)
        stage = PlayerStateSnapshotFailureStage::RestartProviderUnproven;
    else if (mask &
             kPlayerRestartProvenanceReasonInventoryOwnerUnproven)
        stage = PlayerStateSnapshotFailureStage::
            RestartInventoryOwnerUnproven;
    else if (mask &
             kPlayerRestartProvenanceReasonEquipmentOwnerUnproven)
        stage = PlayerStateSnapshotFailureStage::
            RestartEquipmentOwnerUnproven;
    else if (mask &
             kPlayerRestartProvenanceReasonQuestOwnerUnproven)
        stage = PlayerStateSnapshotFailureStage::
            RestartQuestOwnerUnproven;
    if (reason_mask) *reason_mask = mask;
    if (first_stage) *first_stage = stage;
    return snapshot.player_restart_snapshot_complete && !mask;
}

bool RestartSnapshotComplete(
    const PlayerStateRestartSnapshot& snapshot,
    std::uintptr_t expected_state,
    std::uint64_t expected_generation,
    std::uint64_t expected_cache_generation,
    bool require_base_provider,
    bool allow_unobserved_metadata,
    PlayerRestartFailure* failure,
    PlayerStateSnapshotFailureStage* restart_stage = nullptr,
    std::uint64_t* restart_reason_mask = nullptr) noexcept
{
    (void)allow_unobserved_metadata;
    if (!SemanticSnapshotComplete(
            snapshot, expected_state, expected_generation,
            expected_cache_generation, failure))
        return false;
    std::uint64_t mask = 0;
    PlayerStateSnapshotFailureStage stage =
        PlayerStateSnapshotFailureStage::None;
    const bool complete = EvaluateRestartProvenance(
        snapshot, expected_cache_generation, require_base_provider,
        &mask, &stage);
    if (restart_stage) *restart_stage = stage;
    if (restart_reason_mask) *restart_reason_mask = mask;
    if (!complete && failure)
        *failure = RestartFailureForStage(stage);
    return complete;
}
bool PreservationEqual(const PlayerStateRestartSnapshot& before,
                       const PlayerStateRestartSnapshot& after) noexcept
{
    if (before.state != after.state ||
        before.state_generation != after.state_generation ||
        before.player_variables != after.player_variables ||
        before.progression_selector_fingerprint !=
            after.progression_selector_fingerprint ||
        before.class_selector_fingerprint !=
            after.class_selector_fingerprint ||
        before.skill_membership_fingerprint !=
            after.skill_membership_fingerprint ||
        before.regular_buff_fingerprint !=
            after.regular_buff_fingerprint ||
        before.timed_buff_fingerprint != after.timed_buff_fingerprint ||
        before.inventory_fingerprint != after.inventory_fingerprint ||
        before.equipment_fingerprint != after.equipment_fingerprint ||
        before.quest_fingerprint != after.quest_fingerprint)
        return false;
    for (std::size_t index = 0; index < kPlayerLogicalTypeCount;
         ++index) {
        if (before.rows[index].earned_experience !=
                after.rows[index].earned_experience ||
            before.rows[index].current_level !=
                after.rows[index].current_level)
            return false;
    }
    return true;
}

bool RebuildMatchesBaseline(const PlayerStateRestartSnapshot& baseline,
                            const PlayerStateRestartSnapshot& after,
                            std::uint64_t new_cache_generation,
                            bool allow_unobserved_metadata,
                            PlayerRestartFailure* failure) noexcept
{
    if (failure) *failure = PlayerRestartFailure::None;
    (void)allow_unobserved_metadata;
    for (std::size_t index = 0; index < kPlayerLogicalTypeCount;
         ++index) {
        const PlayerDefinitionRowSnapshot& expected = baseline.rows[index];
        const PlayerDefinitionRowSnapshot& actual = after.rows[index];
        if (expected.preset_absent != actual.preset_absent) {
            if (failure)
                *failure = PlayerRestartFailure::VerificationFailed;
            return false;
        }
        if (!actual.preset_absent &&
            (actual.cache_membership_evidence !=
                 PlayerCacheMembershipEvidence::ProvenMember ||
             !actual.cache_observer_available ||
             !actual.cache_inventory_complete ||
             !actual.cache_generation_observed ||
             actual.cache_generation != new_cache_generation)) {
            if (failure)
                *failure = PlayerRestartFailure::CacheMembershipMismatch;
            return false;
        }
        if (expected.definition_fingerprint !=
                actual.definition_fingerprint ||
            expected.definition_count != actual.definition_count ||
            expected.maximum_level != actual.maximum_level) {
            if (failure)
                *failure = PlayerRestartFailure::VerificationFailed;
            return false;
        }
        if (!ProviderEqual(expected.provider, actual.provider)) {
            if (failure)
                *failure = PlayerRestartFailure::ProviderMismatch;
            return false;
        }
    }
    for (std::size_t required = 0;
         required < kRequiredPlayerPropertyCount; ++required) {
        std::uint32_t baseline_matches = 0;
        std::uint32_t after_matches = 0;
        const PlayerPropertySnapshot* expected = FindProperty(
            baseline, kRequiredPropertyIds[required], &baseline_matches);
        const PlayerPropertySnapshot* actual = FindProperty(
            after, kRequiredPropertyIds[required], &after_matches);
        if (!expected || !actual || baseline_matches != 1U ||
            after_matches != 1U || expected->value_bits != actual->value_bits ||
            !CacheGenerationMatches(
                actual->cache_generation, new_cache_generation,
                allow_unobserved_metadata)) {
            if (failure)
                *failure = PlayerRestartFailure::PropertyMismatch;
            return false;
        }
        if (!ProviderEqual(expected->provider, actual->provider)) {
            if (failure)
                *failure = PlayerRestartFailure::ProviderMismatch;
            return false;
        }
    }
    if (baseline.appearance_fingerprint != after.appearance_fingerprint ||
        !ProviderEqual(baseline.appearance_provider,
                       after.appearance_provider)) {
        if (failure) *failure = PlayerRestartFailure::ProviderMismatch;
        return false;
    }
    return true;
}

bool SnapshotTouchesPlayerDomain(
    const PlayerStateRestartSnapshot& baseline,
    const PlayerStateRestartSnapshot& current) noexcept
{
    if (baseline.progression_selector_fingerprint !=
            current.progression_selector_fingerprint ||
        baseline.class_selector_fingerprint !=
            current.class_selector_fingerprint ||
        baseline.player_variables != current.player_variables ||
        baseline.skill_membership_fingerprint !=
            current.skill_membership_fingerprint ||
        baseline.regular_buff_fingerprint !=
            current.regular_buff_fingerprint ||
        baseline.timed_buff_fingerprint !=
            current.timed_buff_fingerprint ||
        baseline.appearance_fingerprint != current.appearance_fingerprint)
        return true;
    for (std::size_t index = 0; index < kPlayerLogicalTypeCount;
         ++index) {
        const PlayerDefinitionRowSnapshot& expected = baseline.rows[index];
        const PlayerDefinitionRowSnapshot& actual = current.rows[index];
        if (expected.definition_vector != actual.definition_vector ||
            expected.definition_fingerprint !=
                actual.definition_fingerprint ||
            expected.earned_experience != actual.earned_experience ||
            expected.definition_count != actual.definition_count ||
            expected.unknown_100 != actual.unknown_100 ||
            expected.current_level != actual.current_level ||
            expected.maximum_level != actual.maximum_level ||
            expected.trailing_qword != actual.trailing_qword ||
            expected.preset_absent != actual.preset_absent)
            return true;
    }
    for (std::size_t index = 0;
         index < kRequiredPlayerPropertyCount; ++index) {
        std::uint32_t baseline_matches = 0;
        std::uint32_t current_matches = 0;
        const PlayerPropertySnapshot* expected = FindProperty(
            baseline, kRequiredPropertyIds[index], &baseline_matches);
        const PlayerPropertySnapshot* actual = FindProperty(
            current, kRequiredPropertyIds[index], &current_matches);
        if (!expected || !actual || baseline_matches != 1U ||
            current_matches != 1U ||
            expected->value_bits != actual->value_bits)
            return true;
    }
    return false;
}

bool ReadEnvironment(PlayerGameplayParamsRestartAdapter* adapter,
                     PlayerRestartEnvironment* environment,
                     PlayerEnvironmentReadPurpose purpose,
                     bool require_no_session,
                     PlayerRestartFailure* failure,
                     PlayerPostDestroyEnvironmentCaptureDiagnostics*
                         diagnostics = nullptr) noexcept
{
    if (failure) *failure = PlayerRestartFailure::None;
    if (diagnostics) {
        diagnostics->environment_callback_attempted = false;
        diagnostics->environment_callback_returned = false;
        diagnostics->compatibility_failure =
            PlayerRestartFailure::None;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::None;
    }
    if (!adapter || !environment ||
        !adapter->snapshot_operations.read_environment) {
        if (failure)
            *failure = PlayerRestartFailure::EnvironmentUnreadable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::EnvironmentUnreadable;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::
                    EnvironmentCallbackRejected;
        }
        return false;
    }
    std::memset(environment, 0, sizeof(*environment));
    if (diagnostics)
        diagnostics->environment_callback_attempted = true;
    const bool callback_returned =
        adapter->snapshot_operations.read_environment(
            adapter->operations_user, purpose, environment);
    if (diagnostics) {
        diagnostics->environment = *environment;
        diagnostics->environment_callback_returned =
            callback_returned;
    }
    if (!callback_returned) {
        PlayerPostDestroyExactFailure exact_failure =
            ExactFailureForCallback(environment->callback_failure);
        if (!environment->supported_image &&
            environment->callback_failure ==
                PlayerEnvironmentCallbackFailure::UnsupportedImage)
            exact_failure =
                PlayerPostDestroyExactFailure::UnsupportedImage;
        else if (environment->shutdown_requested ||
                 environment->quiescing)
            exact_failure =
                PlayerPostDestroyExactFailure::ShutdownOrQuiescing;
        else if (!environment->gameplay_params_container_readable &&
                 (environment->raw_environment_reason_mask ||
                  environment->gameplay_params_container))
            exact_failure = PlayerPostDestroyExactFailure::
                GameplayContainerUnreadable;
        else if (!environment->game_di_readable &&
                 (environment->raw_environment_reason_mask ||
                  environment->game_di))
            exact_failure =
                PlayerPostDestroyExactFailure::GameDiUnreadable;
        else if (!environment->global_owner_slots_readable &&
                 environment->callback_failure ==
                     PlayerEnvironmentCallbackFailure::
                         GlobalOwnerSetIncomplete)
            exact_failure = PlayerPostDestroyExactFailure::
                GlobalOwnerSlotsUnreadable;
        else if (!environment->inventory_scan_complete &&
                 (environment->callback_failure ==
                      PlayerEnvironmentCallbackFailure::
                          InventorySnapshotRejected ||
                  environment->callback_failure ==
                      PlayerEnvironmentCallbackFailure::
                          InventoryObservationLoss ||
                  environment->callback_failure ==
                      PlayerEnvironmentCallbackFailure::
                          InventoryRevisionChanged))
            exact_failure = PlayerPostDestroyExactFailure::
                InventoryScanIncomplete;
        if (failure)
            *failure = PlayerRestartFailure::EnvironmentUnreadable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::EnvironmentUnreadable;
            diagnostics->exact_failure = exact_failure;
        }
        return false;
    }
    if (!environment->supported_image) {
        if (failure)
            *failure = PlayerRestartFailure::ExactRouteUnvalidated;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::ExactRouteUnvalidated;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::UnsupportedImage;
        }
        return false;
    }
    if (environment->shutdown_requested || environment->quiescing) {
        if (failure)
            *failure = PlayerRestartFailure::ShutdownOrQuiescing;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::ShutdownOrQuiescing;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::ShutdownOrQuiescing;
        }
        return false;
    }
    if (!environment->gameplay_params_container_readable ||
        !environment->gameplay_params_container ||
        (!environment->cache_generation &&
         !AllowsUnobservedProgressionTraceMetadata(adapter))) {
        if (failure)
            *failure = PlayerRestartFailure::GameplayContainerUnavailable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::GameplayContainerUnavailable;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::
                    GameplayContainerUnreadable;
        }
        return false;
    }
    if (!environment->game_di_readable || !environment->game_di) {
        if (failure) *failure = PlayerRestartFailure::GameDiUnavailable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::GameDiUnavailable;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::GameDiUnreadable;
        }
        return false;
    }
    if (!environment->global_owner_slots_readable) {
        if (failure)
            *failure = PlayerRestartFailure::EnvironmentUnreadable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::EnvironmentUnreadable;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::
                    GlobalOwnerSlotsUnreadable;
        }
        return false;
    }
    if (!environment->inventory_scan_complete) {
        if (failure)
            *failure = PlayerRestartFailure::EnvironmentUnreadable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::EnvironmentUnreadable;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::InventoryScanIncomplete;
        }
        return false;
    }
    if (purpose ==
            PlayerEnvironmentReadPurpose::
                PostDestroySemanticAssessment &&
        !environment->gameplay_cache_fingerprint) {
        if (failure)
            *failure = PlayerRestartFailure::EnvironmentUnreadable;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::EnvironmentUnreadable;
            diagnostics->exact_failure =
                PlayerPostDestroyExactFailure::
                    CacheFingerprintUnavailable;
        }
        return false;
    }
    if (require_no_session && (environment->local_player_di ||
        environment->live_gameplay_consumer)) {
        if (failure)
            *failure = PlayerRestartFailure::LiveGameplayConsumer;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::LiveGameplayConsumer;
            diagnostics->exact_failure = environment->local_player_di
                ? PlayerPostDestroyExactFailure::
                      LocalPlayerStillPresent
                : PlayerPostDestroyExactFailure::
                      LiveGameplayConsumerStillPresent;
        }
        return false;
    }
    if (require_no_session &&
        (environment->active_session || environment->active_level)) {
        if (failure)
            *failure = PlayerRestartFailure::SessionStillActive;
        if (diagnostics) {
            diagnostics->compatibility_failure =
                PlayerRestartFailure::SessionStillActive;
            diagnostics->exact_failure = environment->active_session
                ? PlayerPostDestroyExactFailure::
                      ActiveSessionStillPresent
                : PlayerPostDestroyExactFailure::
                      ActiveLevelStillPresent;
        }
        return false;
    }
    if (diagnostics) diagnostics->environment = *environment;
    return true;
}

void SortByGeneration(PlayerStateInventoryRecord* records,
                      std::uint32_t count) noexcept
{
    for (std::uint32_t index = 1; index < count; ++index) {
        const PlayerStateInventoryRecord value = records[index];
        std::uint32_t insert = index;
        while (insert &&
               records[insert - 1U].generation > value.generation) {
            records[insert] = records[insert - 1U];
            --insert;
        }
        records[insert] = value;
    }
}

bool CaptureLiveInventoryImpl(PlayerGameplayParamsRestartAdapter* adapter,
                              PlayerStateInventoryRecord* output,
                              std::uint32_t* output_count,
                              std::uint64_t* revision,
                              bool allow_empty,
                              PlayerRestartFailure* failure) noexcept
{
    if (failure) *failure = PlayerRestartFailure::None;
    if (!adapter || !output || !output_count || !revision) {
        if (failure) *failure = PlayerRestartFailure::InvalidArgument;
        return false;
    }
    if (!adapter->coverage_started) {
        if (failure)
            *failure = PlayerRestartFailure::CoverageNotStarted;
        return false;
    }
    if (!adapter->coverage_started_before_first_allocation) {
        if (failure)
            *failure = PlayerRestartFailure::CoverageStartedLate;
        return false;
    }
    if (dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0) {
        if (failure) *failure = PlayerRestartFailure::InventoryLoss;
        return false;
    }
    if (dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0) {
        if (failure) *failure = PlayerRestartFailure::InventoryOverflow;
        return false;
    }
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked()) {
        if (failure) *failure = PlayerRestartFailure::InventoryBusy;
        return false;
    }
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < adapter->inventory_count;
         ++index) {
        const PlayerStateInventoryRecord& record = adapter->inventory[index];
        if (!record.live) continue;
        if (!record.pointer || !record.generation ||
            !record.owner_object ||
            record.owner_kind == PlayerStateOwnerKind::Unknown) {
            if (failure)
                *failure = PlayerRestartFailure::StateUnclassified;
            return false;
        }
        if (count >= kPlayerStateInventoryCapacity) {
            if (failure)
                *failure = PlayerRestartFailure::InventoryOverflow;
            return false;
        }
        output[count++] = record;
    }
    if (!count && !allow_empty) {
        if (failure) *failure = PlayerRestartFailure::InventoryEmpty;
        return false;
    }
    *revision = adapter->inventory_revision;
    *output_count = count;
    SortByGeneration(output, count);
    return true;
}

bool CaptureLiveInventory(PlayerGameplayParamsRestartAdapter* adapter,
                          PlayerStateInventoryRecord* output,
                          std::uint32_t* output_count,
                          std::uint64_t* revision,
                          PlayerRestartFailure* failure) noexcept
{
    return CaptureLiveInventoryImpl(
        adapter, output, output_count, revision, false, failure);
}

bool CaptureLiveInventoryAllowEmpty(
    PlayerGameplayParamsRestartAdapter* adapter,
    PlayerStateInventoryRecord* output,
    std::uint32_t* output_count,
    std::uint64_t* revision,
    PlayerRestartFailure* failure) noexcept
{
    return CaptureLiveInventoryImpl(
        adapter, output, output_count, revision, true, failure);
}

const PlayerStateInventoryRecord* FindLiveRecord(
    const PlayerStateInventoryRecord* records, std::uint32_t count,
    std::uintptr_t pointer) noexcept
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (records[index].pointer == pointer) return &records[index];
    return nullptr;
}

bool ValidateOwners(const PlayerRestartEnvironment& environment,
                     const PlayerStateInventoryRecord* records,
                     std::uint32_t count,
                     bool allow_dynamic_player_owners,
                     PlayerRestartFailure* failure) noexcept
{
    if (!environment.local_process_global_state ||
        !environment.character_manager_global_state) {
        if (failure) *failure = PlayerRestartFailure::GlobalOwnerMissing;
        return false;
    }
    if (environment.local_process_global_state ==
        environment.character_manager_global_state) {
        if (failure)
            *failure = PlayerRestartFailure::GlobalOwnerMismatch;
        return false;
    }
    const PlayerStateInventoryRecord* local = FindLiveRecord(
        records, count, environment.local_process_global_state);
    const PlayerStateInventoryRecord* character = FindLiveRecord(
        records, count, environment.character_manager_global_state);
    if (!local || !character ||
        local->owner_kind != PlayerStateOwnerKind::LocalProcessGlobal ||
        character->owner_kind !=
            PlayerStateOwnerKind::CharacterManagerGlobal) {
        if (failure)
            *failure = PlayerRestartFailure::GlobalOwnerMismatch;
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        if (records[index].owner_kind == PlayerStateOwnerKind::PlayerDi ||
            records[index].owner_kind ==
                PlayerStateOwnerKind::TutorialPlayerDi) {
            if (allow_dynamic_player_owners) continue;
            if (failure)
                *failure = PlayerRestartFailure::LiveGameplayConsumer;
            return false;
        }
    }
    return true;
}

bool AbsentOwnersProven(
    const PlayerRestartEnvironment& environment,
    std::uint32_t live_count) noexcept
{
    return live_count == 0U &&
        environment.global_owner_slots_readable &&
        !environment.local_process_global_state &&
        !environment.character_manager_global_state &&
        !environment.local_player_di &&
        !environment.live_gameplay_consumer &&
        environment.gameplay_cache_fingerprint != 0U;
}

bool StableEnvironmentPair(
    const PlayerRestartEnvironment& before,
    const PlayerRestartEnvironment& after) noexcept
{
    return before.cache_generation == after.cache_generation &&
        before.gameplay_cache_fingerprint ==
            after.gameplay_cache_fingerprint &&
        before.gameplay_params_container ==
            after.gameplay_params_container &&
        before.game_di == after.game_di &&
        before.active_session == after.active_session &&
        before.active_level == after.active_level &&
        before.local_player_di == after.local_player_di &&
        before.local_process_global_state ==
            after.local_process_global_state &&
        before.character_manager_global_state ==
            after.character_manager_global_state &&
        before.live_gameplay_consumer ==
            after.live_gameplay_consumer;
}

bool InventoryRevisionUnchanged(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uint64_t expected_revision) noexcept
{
    if (!adapter || dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0)
        return false;
    TryLock lock(&adapter->inventory_lock);
    return lock.locked() &&
        adapter->inventory_revision == expected_revision;
}

bool CommitCapturedBaseline(
    PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerStateRestartSnapshot* captured,
    std::uint32_t captured_count,
    std::uint64_t baseline_generation,
    std::uint64_t expected_inventory_revision,
    PlayerStartupBaselineKind baseline_kind,
    std::uint64_t gameplay_cache_fingerprint) noexcept
{
    const bool absent =
        baseline_kind == PlayerStartupBaselineKind::Absent;
    const bool snapshot =
        baseline_kind == PlayerStartupBaselineKind::Snapshot;
    if (!adapter || !captured ||
        (!absent && !snapshot) ||
        (absent && (captured_count != 0U ||
                    !gameplay_cache_fingerprint)) ||
        (snapshot && captured_count == 0U) ||
        !baseline_generation ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0)
        return false;
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked() ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0 ||
        adapter->inventory_revision != expected_inventory_revision)
        return false;
    if (captured_count)
        std::memcpy(adapter->baseline_states, captured,
                    sizeof(PlayerStateRestartSnapshot) * captured_count);
    adapter->baseline_state_count = captured_count;
    adapter->baseline_inventory_revision =
        expected_inventory_revision;
    adapter->baseline_generation = baseline_generation;
    adapter->baseline_gameplay_cache_fingerprint =
        gameplay_cache_fingerprint;
    adapter->baseline_kind = baseline_kind;
    adapter->baseline_captured = true;
    adapter->last_failure = PlayerRestartFailure::None;
    return true;
}

const PlayerStateRestartSnapshot* FindBaseline(
    const PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t state, std::uint64_t generation) noexcept
{
    if (!adapter) return nullptr;
    for (std::uint32_t index = 0;
         index < adapter->baseline_state_count; ++index) {
        const PlayerStateRestartSnapshot& baseline =
            adapter->baseline_states[index];
        if (baseline.state == state &&
            baseline.state_generation == generation)
            return &baseline;
    }
    return nullptr;
}

bool OwnerSetMatchesSemanticBaseline(
    const PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerRestartEnvironment& environment,
    const PlayerStateInventoryRecord* records,
    std::uint32_t count) noexcept
{
    if (!adapter || !records ||
        count != adapter->baseline_state_count)
        return false;
    PlayerRestartFailure ignored = PlayerRestartFailure::None;
    if (!ValidateOwners(
            environment, records, count, false, &ignored))
        return false;
    for (std::uint32_t index = 0; index < count; ++index) {
        const PlayerStateInventoryRecord& record = records[index];
        const PlayerStateRestartSnapshot* baseline = FindBaseline(
            adapter, record.pointer, record.generation);
        if (!baseline ||
            baseline->owner_kind != record.owner_kind ||
            baseline->owner_slot != record.owner_object)
            return false;
    }
    return true;
}

bool ContextMatches(const PlayerGameplayParamsRestartAdapter* adapter,
                    const RestartContext* context) noexcept
{
    return adapter && context && adapter->transaction_active &&
        context->transaction_generation ==
            adapter->transaction_generation &&
        context->baseline_generation ==
            adapter->transaction_baseline_generation &&
        context->content_epoch == adapter->transaction_content_epoch &&
        context->owner_thread_id == adapter->owner_thread_id;
}

bool EnvironmentThreadMatches(
    const PlayerRestartEnvironment& environment,
    const RestartContext* context) noexcept
{
    return context && environment.current_thread_id &&
        environment.current_thread_id == context->owner_thread_id;
}

bool PostDestroyThreadAuthorityMatches(
    const PlayerRestartEnvironment& environment,
    const RestartContext* context,
    const PlayerThreadAuthorityEvidence* authority) noexcept
{
    if (!context || !authority || !context->owner_thread_id ||
        authority->kind == PlayerThreadAuthorityKind::Unspecified ||
        !authority->expected_thread_id ||
        authority->expected_thread_id != context->owner_thread_id ||
        environment.current_thread_id !=
            authority->expected_thread_id)
        return false;
    if (authority->kind !=
            PlayerThreadAuthorityKind::C10SessionOwner &&
        authority->kind !=
            PlayerThreadAuthorityKind::C10PostReturnOwner)
        return true;
    const bool exact_c10_scope =
        authority->exact_scope_active &&
        authority->current_c10_thread_id ==
            authority->expected_thread_id &&
        authority->current_session_scope_thread_id ==
            authority->expected_thread_id;
    if (authority->kind ==
        PlayerThreadAuthorityKind::C10SessionOwner)
        return exact_c10_scope;
    return exact_c10_scope &&
        authority->post_c10_observed_thread_id ==
            authority->expected_thread_id &&
        authority->pending_post_c10_token_generation != 0;
}

void ClearPreparedTransaction(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (!adapter) return;
    adapter->plan_count = 0;
    adapter->transaction_generation = 0;
    adapter->transaction_baseline_generation = 0;
    adapter->transaction_content_epoch = 0;
    adapter->planned_inventory_revision = 0;
    adapter->before_cache_generation = 0;
    adapter->after_cache_generation = 0;
    adapter->planned_gameplay_params_container = 0;
    adapter->planned_game_di = 0;
    adapter->planned_local_process_global_state = 0;
    adapter->planned_character_manager_global_state = 0;
    adapter->transaction_global_reset_calls = 0;
    adapter->transaction_initialize_calls = 0;
    adapter->owner_thread_id = 0;
    adapter->transaction_active = false;
    adapter->preflight_complete = false;
    adapter->quiesce_complete = false;
    adapter->mutation_started = false;
    adapter->reset_postconditions_complete = false;
    adapter->verified = false;
}

bool EnvironmentMatchesPlan(
    const PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerRestartEnvironment& environment) noexcept
{
    return adapter &&
        environment.gameplay_params_container ==
            adapter->planned_gameplay_params_container &&
        environment.game_di == adapter->planned_game_di &&
        environment.local_process_global_state ==
            adapter->planned_local_process_global_state &&
        environment.character_manager_global_state ==
            adapter->planned_character_manager_global_state;
}

bool PlanStillLive(PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (!adapter || !InventoryRevisionUnchanged(
            adapter, adapter->planned_inventory_revision))
        return false;
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked() ||
        adapter->inventory_revision !=
            adapter->planned_inventory_revision ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0)
        return false;
    for (std::uint32_t planned = 0; planned < adapter->plan_count;
         ++planned) {
        bool found = false;
        for (std::uint32_t index = 0; index < adapter->inventory_count;
             ++index) {
            const PlayerStateInventoryRecord& record =
                adapter->inventory[index];
            if (record.live &&
                record.pointer == adapter->plan[planned].pointer &&
                record.generation == adapter->plan[planned].generation) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool ValidateAfterSnapshot(
    PlayerGameplayParamsRestartAdapter* adapter,
    PlayerRestartPlannedState* planned,
    const PlayerStateRestartSnapshot& snapshot,
    PlayerRestartFailure* failure) noexcept
{
    PlayerRestartFailure local_failure = PlayerRestartFailure::None;
    const bool allow_unobserved_metadata =
        AllowsUnobservedProgressionTraceMetadata(adapter);
    if (!RestartSnapshotComplete(
            snapshot, planned->pointer,
            planned->generation,
            adapter->after_cache_generation, true,
            allow_unobserved_metadata,
            &local_failure)) {
        if (failure) *failure = local_failure;
        return false;
    }
    if (!PreservationEqual(planned->before, snapshot)) {
        if (failure)
            *failure =
                PlayerRestartFailure::PreservationInvariantMismatch;
        return false;
    }
    const PlayerStateRestartSnapshot* baseline = FindBaseline(
        adapter, planned->pointer, planned->generation);
    if (!baseline) {
        if (failure)
            *failure = PlayerRestartFailure::BaselineStateMissing;
        return false;
    }
    if (!RebuildMatchesBaseline(*baseline, snapshot,
                                adapter->after_cache_generation,
                                allow_unobserved_metadata,
                                &local_failure)) {
        if (failure) *failure = local_failure;
        return false;
    }
    return true;
}

}  // namespace

void InitializePlayerGameplayParamsRestartAdapter(
    PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerGameplayParamsSnapshotOperations* snapshot_operations,
    const PlayerGameplayParamsMutationOperations* mutation_operations,
    void* operations_user) noexcept
{
    if (!adapter) return;
    std::memset(adapter, 0, sizeof(*adapter));
    if (snapshot_operations)
        adapter->snapshot_operations = *snapshot_operations;
    if (mutation_operations)
        adapter->mutation_operations = *mutation_operations;
    adapter->operations_user = operations_user;
}

bool BeginPlayerStateInventoryCoverage(
    PlayerGameplayParamsRestartAdapter* adapter,
    bool started_before_first_allocation,
    bool constructor_route_validated,
    bool complete_destructor_route_validated) noexcept
{
    if (!adapter) return false;
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked() || adapter->coverage_started ||
        adapter->inventory_count)
        return false;
    adapter->coverage_started = true;
    adapter->coverage_started_before_first_allocation =
        started_before_first_allocation;
    adapter->constructor_route_validated = constructor_route_validated;
    adapter->complete_destructor_route_validated =
        complete_destructor_route_validated;
    if (!started_before_first_allocation ||
        !constructor_route_validated ||
        !complete_destructor_route_validated)
        dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
    return started_before_first_allocation &&
        constructor_route_validated &&
        complete_destructor_route_validated;
}

bool BeginPlayerStateInventoryCoverageFromBootstrap(
    PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerInventoryBootstrapEvidence* evidence) noexcept
{
    if (!adapter || !evidence) {
        if (adapter)
            adapter->bootstrap_failure =
                PlayerInventoryBootstrapFailure::InvalidArgument;
        return false;
    }
    PlayerInventoryBootstrapFailure failure =
        PlayerInventoryBootstrapFailure::None;
    if (!evidence->supported_image)
        failure = PlayerInventoryBootstrapFailure::UnsupportedImage;
    else if (!evidence->before_original_game_script_initializer)
        failure =
            PlayerInventoryBootstrapFailure::NotBeforeOriginalInitializer;
    else if (!evidence->closed_creator_set_validated)
        failure = PlayerInventoryBootstrapFailure::CreatorSetUnvalidated;
    else if (!evidence->constructor_route_installed)
        failure =
            PlayerInventoryBootstrapFailure::ConstructorRouteUnavailable;
    else if (!evidence->complete_destructor_route_installed)
        failure = PlayerInventoryBootstrapFailure::
            CompleteDestructorRouteUnavailable;
    else if (!evidence->before_global_slots_readable ||
             !evidence->after_global_slots_readable)
        failure = PlayerInventoryBootstrapFailure::GlobalSlotsUnreadable;
    else if (evidence->before_local_process_global_state ||
             evidence->before_character_manager_global_state ||
             evidence->after_local_process_global_state ||
             evidence->after_character_manager_global_state)
        failure = PlayerInventoryBootstrapFailure::ExistingGlobalState;
    else if (evidence->constructor_active_calls ||
             evidence->complete_destructor_active_calls)
        failure =
            PlayerInventoryBootstrapFailure::HookActiveDuringBootstrap;
    else if (dluma_atomic_compare_exchange(
                 &adapter->inventory_loss_latched, 0, 0) != 0 ||
             dluma_atomic_compare_exchange(
                 &adapter->inventory_overflow_latched, 0, 0) != 0)
        failure =
            PlayerInventoryBootstrapFailure::PrecoverageObservationLoss;

    if (failure != PlayerInventoryBootstrapFailure::None) {
        adapter->bootstrap_failure = failure;
        return false;
    }
    if (!BeginPlayerStateInventoryCoverage(
            adapter, true, true, true)) {
        adapter->bootstrap_failure =
            PlayerInventoryBootstrapFailure::CoverageStartRejected;
        return false;
    }
    /* A ctor entering between the evidence sample and coverage publication
     * latches loss before returning.  Coverage may continue diagnostically,
     * but it must never be advertised as complete. */
    if (dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0) {
        adapter->coverage_started_before_first_allocation = false;
        adapter->bootstrap_failure =
            PlayerInventoryBootstrapFailure::PrecoverageObservationLoss;
        return false;
    }
    adapter->bootstrap_failure = PlayerInventoryBootstrapFailure::None;
    return true;
}

std::uint64_t ObservePlayerStateConstructed(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state) noexcept
{
    if (!adapter || !player_state || !adapter->coverage_started ||
        dluma_atomic_compare_exchange(
            &adapter->mutation_in_progress, 0, 0) != 0) {
        MarkPlayerStateInventoryLoss(adapter);
        return 0;
    }
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked()) {
        MarkPlayerStateInventoryLoss(adapter);
        return 0;
    }
    for (std::uint32_t index = 0; index < adapter->inventory_count;
         ++index) {
        if (adapter->inventory[index].live &&
            adapter->inventory[index].pointer == player_state) {
            dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
            return 0;
        }
    }
    if (adapter->inventory_count >= kPlayerStateInventoryCapacity ||
        adapter->next_state_generation == UINT64_MAX) {
        dluma_atomic_exchange(&adapter->inventory_overflow_latched, 1);
        dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
        return 0;
    }
    PlayerStateInventoryRecord& record =
        adapter->inventory[adapter->inventory_count++];
    std::memset(&record, 0, sizeof(record));
    record.pointer = player_state;
    record.generation = ++adapter->next_state_generation;
    record.creation_sequence = ++adapter->observation_sequence;
    record.owner_kind = PlayerStateOwnerKind::Unknown;
    record.live = true;
    ++adapter->inventory_revision;
    return record.generation;
}

bool ObservePlayerStateDestroyed(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state,
    std::uint64_t* destroyed_generation) noexcept
{
    if (destroyed_generation) *destroyed_generation = 0;
    if (!adapter || !player_state || !adapter->coverage_started ||
        dluma_atomic_compare_exchange(
            &adapter->mutation_in_progress, 0, 0) != 0) {
        MarkPlayerStateInventoryLoss(adapter);
        return false;
    }
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked()) {
        MarkPlayerStateInventoryLoss(adapter);
        return false;
    }
    PlayerStateInventoryRecord* found = nullptr;
    for (std::uint32_t index = 0; index < adapter->inventory_count;
         ++index) {
        PlayerStateInventoryRecord& record = adapter->inventory[index];
        if (!record.live || record.pointer != player_state) continue;
        if (found) {
            dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
            return false;
        }
        found = &record;
    }
    if (!found) {
        dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
        return false;
    }
    found->live = false;
    found->destruction_sequence = ++adapter->observation_sequence;
    ++adapter->inventory_revision;
    if (destroyed_generation) *destroyed_generation = found->generation;
    return true;
}

bool ClassifyPlayerStateOwner(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state, std::uint64_t generation,
    PlayerStateOwnerKind owner_kind,
    std::uintptr_t owner_object) noexcept
{
    if (!adapter || !player_state || !generation ||
        owner_kind == PlayerStateOwnerKind::Unknown)
        return false;
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked()) {
        MarkPlayerStateInventoryLoss(adapter);
        return false;
    }
    for (std::uint32_t index = 0; index < adapter->inventory_count;
         ++index) {
        PlayerStateInventoryRecord& record = adapter->inventory[index];
        if (!record.live || record.pointer != player_state ||
            record.generation != generation)
            continue;
        if (record.owner_kind != PlayerStateOwnerKind::Unknown &&
            (record.owner_kind != owner_kind ||
             record.owner_object != owner_object)) {
            dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
            return false;
        }
        if (record.owner_kind == owner_kind &&
            record.owner_object == owner_object)
            return true;
        record.owner_kind = owner_kind;
        record.owner_object = owner_object;
        ++adapter->observation_sequence;
        ++adapter->inventory_revision;
        return true;
    }
    dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
    return false;
}

std::uint64_t FindLivePlayerStateGeneration(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state) noexcept
{
    /* Loss/overflow permanently blocks restart preflight, but it does not
     * make an already recorded generation unusable for bounded diagnostics
     * and later owner classification.  This lookup is read-only and returns
     * zero when live pointer identity is ambiguous. */
    if (!adapter || !player_state || !adapter->coverage_started)
        return 0;
    TryLock lock(&adapter->inventory_lock);
    if (!lock.locked()) return 0;
    std::uint64_t generation = 0;
    for (std::uint32_t index = 0; index < adapter->inventory_count;
         ++index) {
        const PlayerStateInventoryRecord& record = adapter->inventory[index];
        if (!record.live || record.pointer != player_state) continue;
        if (generation) return 0;
        generation = record.generation;
    }
    return generation;
}

void MarkPlayerStateInventoryLoss(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (adapter)
        dluma_atomic_exchange(&adapter->inventory_loss_latched, 1);
}

void RecordPlayerB0BootstrapSample(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uint64_t sequence,
    std::uint32_t owner_count) noexcept
{
    if (!adapter || !sequence) return;
    adapter->b0_diagnostics.bootstrap_sample_sequence = sequence;
    adapter->b0_diagnostics.bootstrap_sample_owner_count = owner_count;
}

bool AbandonPlayerGameplayParamsBaseline(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (!adapter) return false;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return false;
    if (adapter->runtime_failure_latched ||
        adapter->transaction_active ||
        adapter->preflight_complete ||
        adapter->quiesce_complete ||
        adapter->mutation_started ||
        dluma_atomic_compare_exchange(
            &adapter->mutation_in_progress, 0, 0) != 0)
        return false;

    std::memset(adapter->baseline_states, 0,
                sizeof(adapter->baseline_states));
    adapter->baseline_state_count = 0;
    adapter->baseline_inventory_revision = 0;
    adapter->baseline_generation = 0;
    adapter->baseline_gameplay_cache_fingerprint = 0;
    adapter->baseline_owner_thread_id = 0;
    adapter->baseline_kind = PlayerStartupBaselineKind::Unavailable;
    adapter->baseline_captured = false;
    return true;
}

bool PlayerGameplayParamsBaselineEvidenceStillCurrent(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (!adapter) return false;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked() || !adapter->baseline_captured ||
        adapter->baseline_kind == PlayerStartupBaselineKind::Unavailable ||
        !EnvironmentOperationsComplete(adapter) ||
        (adapter->baseline_kind == PlayerStartupBaselineKind::Snapshot &&
         (!adapter->baseline_state_count ||
          !StateSnapshotOperationsComplete(adapter))) ||
        (adapter->baseline_kind == PlayerStartupBaselineKind::Absent &&
         (adapter->baseline_state_count != 0U ||
          !adapter->baseline_gameplay_cache_fingerprint)) ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) != 0)
        return false;
    TryLock inventory(&adapter->inventory_lock);
    return inventory.locked() &&
        dluma_atomic_compare_exchange(
            &adapter->inventory_loss_latched, 0, 0) == 0 &&
        dluma_atomic_compare_exchange(
            &adapter->inventory_overflow_latched, 0, 0) == 0 &&
        adapter->inventory_revision ==
            adapter->baseline_inventory_revision;
}

bool CapturePlayerGameplayParamsAbsentEvidence(
    PlayerGameplayParamsRestartAdapter* adapter,
    PlayerGameplayParamsAbsentEvidence* output) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!adapter || !output) return false;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked() ||
        !EnvironmentOperationsComplete(adapter) ||
        !InventoryRoutesValidated(adapter)) {
        SetFailure(
            adapter, PlayerRestartFailure::OperationsIncomplete);
        return false;
    }

    PlayerStateInventoryRecord before[kPlayerStateInventoryCapacity] = {};
    PlayerStateInventoryRecord after[kPlayerStateInventoryCapacity] = {};
    std::uint32_t before_count = 0;
    std::uint32_t after_count = 0;
    std::uint64_t before_revision = 0;
    std::uint64_t after_revision = 0;
    PlayerRestartFailure failure = PlayerRestartFailure::None;
    if (!CaptureLiveInventoryAllowEmpty(
            adapter, before, &before_count, &before_revision, &failure)) {
        SetFailure(adapter, failure);
        return false;
    }
    PlayerRestartEnvironment environment_before = {};
    PlayerRestartEnvironment environment_after = {};
    if (!ReadEnvironment(
            adapter, &environment_before,
            PlayerEnvironmentReadPurpose::StartupSemanticBaseline,
            false, &failure) ||
        !ReadEnvironment(
            adapter, &environment_after,
            PlayerEnvironmentReadPurpose::StartupSemanticBaseline,
            false, &failure) ||
        !CaptureLiveInventoryAllowEmpty(
            adapter, after, &after_count, &after_revision, &failure) ||
        before_revision != after_revision ||
        before_count != after_count ||
        !StableEnvironmentPair(environment_before, environment_after)) {
        SetFailure(
            adapter,
            failure == PlayerRestartFailure::None
                ? PlayerRestartFailure::InventoryChanged : failure);
        return false;
    }
    for (std::uint32_t index = 0; index < before_count; ++index) {
        if (before[index].pointer != after[index].pointer ||
            before[index].owner_object != after[index].owner_object ||
            before[index].generation != after[index].generation ||
            before[index].owner_kind != after[index].owner_kind ||
            !before[index].live || !after[index].live) {
            SetFailure(adapter, PlayerRestartFailure::InventoryChanged);
            return false;
        }
    }
    output->environment = environment_after;
    std::memcpy(
        output->live_states, after,
        sizeof(PlayerStateInventoryRecord) * after_count);
    output->live_state_count = after_count;
    output->inventory_revision = after_revision;
    output->complete = true;
    return true;
}

bool CancelPreparedPlayerGameplayParamsRestart(
    PlayerGameplayParamsRestartAdapter* adapter,
    const RestartContext* context) noexcept
{
    if (!adapter) return false;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return false;
    if (!ContextMatches(adapter, context) || adapter->mutation_started)
        return false;
    /* Pair this with CancelPreparedRegistryTransaction. Cancellation only
     * abandons an unmutated POD plan; it must remain available after session,
     * shutdown, or environment evidence invalidates the prepared barrier. */
    ClearPreparedTransaction(adapter);
    adapter->last_failure = PlayerRestartFailure::None;
    return true;
}

DomainResult CancelQuiescedPlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    return CancelPreparedPlayerGameplayParamsRestart(adapter, context)
        ? DomainResult::Succeeded
        : DomainResult::Failed;
}

DomainResult CapturePlayerGameplayParamsBaseline(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    if (!adapter) return DomainResult::Failed;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return DomainResult::Failed;
    if (!context || !context->baseline_generation ||
        !context->owner_thread_id)
        return Fail(adapter, PlayerRestartFailure::InvalidArgument, false);
    if (adapter->runtime_failure_latched)
        return Fail(adapter, PlayerRestartFailure::BaselineMissing, false);
    if (adapter->baseline_captured) {
        if (adapter->baseline_generation == context->baseline_generation)
            return DomainResult::Succeeded;
        return Fail(adapter, PlayerRestartFailure::BaselineMissing, false);
    }
    if (!EnvironmentOperationsComplete(adapter))
        return Fail(adapter, PlayerRestartFailure::OperationsIncomplete,
                    false);
    if (!InventoryRoutesValidated(adapter))
        return Fail(adapter, PlayerRestartFailure::ExactRouteUnvalidated,
                    false);

    ResetB0AttemptDiagnostics(adapter);
    PlayerStateInventoryRecord live_before[
        kPlayerStateInventoryCapacity] = {};
    PlayerStateInventoryRecord live_after[
        kPlayerStateInventoryCapacity] = {};
    PlayerStateRestartSnapshot captured_first[
        kPlayerStateInventoryCapacity] = {};
    PlayerStateRestartSnapshot captured_second[
        kPlayerStateInventoryCapacity] = {};
    std::uint32_t live_before_count = 0;
    std::uint32_t live_after_count = 0;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
    PlayerRestartFailure failure = PlayerRestartFailure::None;
    if (!CaptureLiveInventoryAllowEmpty(
            adapter, live_before, &live_before_count,
            &revision_before, &failure))
        return Fail(adapter, failure, false);

    PlayerRestartEnvironment environment_before = {};
    if (!ReadEnvironment(
            adapter, &environment_before,
            PlayerEnvironmentReadPurpose::StartupSemanticBaseline,
            false, &failure))
        return Fail(adapter, failure, false);
    if (!EnvironmentThreadMatches(environment_before, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, false);
    adapter->b0_diagnostics.pass1_sequence = context->sequence;
    adapter->b0_diagnostics.pass1_owner_count = live_before_count;
    adapter->b0_diagnostics.pass1_inventory_revision = revision_before;
    adapter->b0_diagnostics.pass1_cache_fingerprint =
        environment_before.gameplay_cache_fingerprint;
    const PlayerStartupBaselineKind baseline_kind =
        live_before_count == 0U
            ? PlayerStartupBaselineKind::Absent
            : PlayerStartupBaselineKind::Snapshot;
    adapter->b0_diagnostics.selected_kind = baseline_kind;
    if (!environment_before.gameplay_cache_fingerprint)
        return Fail(
            adapter, PlayerRestartFailure::GameplayContainerUnavailable,
            false);
    if (baseline_kind == PlayerStartupBaselineKind::Absent) {
        if (!AbsentOwnersProven(environment_before, live_before_count))
            return Fail(
                adapter, PlayerRestartFailure::AbsentBaselineUnproven,
                false);
    } else {
        if (!StateSnapshotOperationsComplete(adapter))
            return Fail(
                adapter, PlayerRestartFailure::OperationsIncomplete,
                false);
        if (!ValidateOwners(
                environment_before, live_before,
                live_before_count, true, &failure))
            return Fail(adapter, failure, false);
    }

    for (std::uint32_t index = 0; index < live_before_count; ++index) {
        SeedB0StateDiagnostic(
            &adapter->b0_diagnostics.states[index],
            live_before[index]);
        if (!adapter->snapshot_operations.snapshot_state(
                adapter->operations_user, live_before[index].pointer,
                live_before[index].generation,
                &captured_first[index])) {
            UpdateB0StateDiagnostic(
                &adapter->b0_diagnostics.states[index],
                captured_first[index], 1U, false);
            return Fail(adapter,
                        PlayerRestartFailure::StateSnapshotFailed, false);
        }
        captured_first[index].owner_slot =
            live_before[index].owner_object;
        captured_first[index].owner_kind =
            live_before[index].owner_kind;
        captured_first[index].cache_generation =
            environment_before.cache_generation;
        captured_first[index].snapshot_attempt_count = 1U;
        if (!SemanticSnapshotComplete(
                captured_first[index], live_before[index].pointer,
                live_before[index].generation,
                environment_before.cache_generation,
                &failure, &captured_first[index].failure_stage,
                &captured_first[index].semantic_reason_mask)) {
            if (captured_first[index].failure_stage ==
                PlayerStateSnapshotFailureStage::None)
                captured_first[index].failure_stage =
                    PlayerStateSnapshotFailureStage::CallbackRejected;
            UpdateB0StateDiagnostic(
                &adapter->b0_diagnostics.states[index],
                captured_first[index], 1U, false);
            return Fail(adapter, failure, false);
        }
        captured_first[index].player_semantic_snapshot_complete = true;
        PlayerRestartFailure restart_failure =
            PlayerRestartFailure::None;
        captured_first[index].player_restart_snapshot_complete =
            RestartSnapshotComplete(
                captured_first[index], live_before[index].pointer,
                live_before[index].generation,
                environment_before.cache_generation, true,
                AllowsUnobservedProgressionTraceMetadata(adapter),
                &restart_failure,
                &captured_first[index].restart_failure_stage,
                &captured_first[index].restart_provenance_reason_mask);
        captured_first[index].snapshot_complete = true;
        UpdateB0StateDiagnostic(
            &adapter->b0_diagnostics.states[index],
            captured_first[index], 1U, true);
    }

    PlayerRestartEnvironment environment_after = {};
    if (!ReadEnvironment(
            adapter, &environment_after,
            PlayerEnvironmentReadPurpose::StartupSemanticBaseline,
            false, &failure))
        return Fail(adapter, failure, false);
    if (!EnvironmentThreadMatches(environment_after, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, false);
    if (!CaptureLiveInventoryAllowEmpty(
            adapter, live_after, &live_after_count,
            &revision_after, &failure))
        return Fail(adapter, failure, false);
    adapter->b0_diagnostics.pass2_sequence = context->sequence;
    adapter->b0_diagnostics.pass2_owner_count = live_after_count;
    adapter->b0_diagnostics.pass2_inventory_revision = revision_after;
    adapter->b0_diagnostics.pass2_cache_fingerprint =
        environment_after.gameplay_cache_fingerprint;
    if (revision_before != revision_after ||
        live_before_count != live_after_count ||
        !StableEnvironmentPair(environment_before, environment_after))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, false);
    if (baseline_kind == PlayerStartupBaselineKind::Absent &&
        !AbsentOwnersProven(environment_after, live_after_count))
        return Fail(
            adapter, PlayerRestartFailure::AbsentBaselineUnproven,
            false);
    for (std::uint32_t index = 0; index < live_before_count; ++index) {
        const PlayerStateInventoryRecord& before = live_before[index];
        const PlayerStateInventoryRecord& after = live_after[index];
        if (before.pointer != after.pointer ||
            before.generation != after.generation ||
            before.owner_kind != after.owner_kind ||
            before.owner_object != after.owner_object)
            return Fail(adapter, PlayerRestartFailure::InventoryChanged,
                        false);
    }
    adapter->b0_diagnostics.owner_set_stable_across_passes = true;
    for (std::uint32_t index = 0; index < live_after_count; ++index) {
        if (!adapter->snapshot_operations.snapshot_state(
                adapter->operations_user, live_after[index].pointer,
                live_after[index].generation,
                &captured_second[index])) {
            UpdateB0StateDiagnostic(
                &adapter->b0_diagnostics.states[index],
                captured_second[index], 2U, false);
            return Fail(
                adapter, PlayerRestartFailure::StateSnapshotFailed,
                false);
        }
        captured_second[index].owner_slot =
            live_after[index].owner_object;
        captured_second[index].owner_kind =
            live_after[index].owner_kind;
        captured_second[index].cache_generation =
            environment_after.cache_generation;
        captured_second[index].snapshot_attempt_count = 2U;
        if (!SemanticSnapshotComplete(
                captured_second[index], live_after[index].pointer,
                live_after[index].generation,
                environment_after.cache_generation,
                &failure, &captured_second[index].failure_stage,
                &captured_second[index].semantic_reason_mask)) {
            if (captured_second[index].failure_stage ==
                PlayerStateSnapshotFailureStage::None)
                captured_second[index].failure_stage =
                    PlayerStateSnapshotFailureStage::CallbackRejected;
            UpdateB0StateDiagnostic(
                &adapter->b0_diagnostics.states[index],
                captured_second[index], 2U, false);
            return Fail(adapter, failure, false);
        }
        captured_second[index].player_semantic_snapshot_complete = true;
        PlayerRestartFailure restart_failure =
            PlayerRestartFailure::None;
        captured_second[index].player_restart_snapshot_complete =
            RestartSnapshotComplete(
                captured_second[index], live_after[index].pointer,
                live_after[index].generation,
                environment_after.cache_generation, true,
                AllowsUnobservedProgressionTraceMetadata(adapter),
                &restart_failure,
                &captured_second[index].restart_failure_stage,
                &captured_second[index].restart_provenance_reason_mask);
        if (!SnapshotSemanticEqual(
                captured_first[index], captured_second[index])) {
            captured_second[index].failure_stage =
                PlayerStateSnapshotFailureStage::
                    StateChangedBetweenPasses;
            captured_second[index].snapshot_complete = false;
            UpdateB0StateDiagnostic(
                &adapter->b0_diagnostics.states[index],
                captured_second[index], 2U, false);
            return Fail(
                adapter, PlayerRestartFailure::InventoryChanged,
                false);
        }
        captured_second[index].snapshot_complete = true;
        UpdateB0StateDiagnostic(
            &adapter->b0_diagnostics.states[index],
            captured_second[index], 2U, true);
    }
    if (!InventoryRevisionUnchanged(adapter, revision_after))
        return Fail(
            adapter, PlayerRestartFailure::InventoryChanged, false);
    if (!CommitCapturedBaseline(
            adapter, captured_second, live_before_count,
            context->baseline_generation, revision_before,
            baseline_kind,
            environment_before.gameplay_cache_fingerprint))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, false);
    adapter->baseline_owner_thread_id = context->owner_thread_id;
    adapter->b0_diagnostics.state_snapshot_count = live_before_count;
    adapter->b0_diagnostics.player_semantic_snapshot_complete = true;
    adapter->b0_diagnostics.player_restart_snapshot_complete = true;
    for (std::uint32_t index = 0; index < live_before_count; ++index)
        if (!captured_second[index].player_restart_snapshot_complete)
            adapter->b0_diagnostics.
                player_restart_snapshot_complete = false;
    return DomainResult::Succeeded;
}

bool SameInventoryOwnerSet(
    const PlayerStateInventoryRecord* first,
    std::uint32_t first_count,
    const PlayerStateInventoryRecord* second,
    std::uint32_t second_count) noexcept
{
    if (!first || !second || first_count != second_count)
        return false;
    for (std::uint32_t index = 0; index < first_count; ++index) {
        if (first[index].pointer != second[index].pointer ||
            first[index].owner_object != second[index].owner_object ||
            first[index].generation != second[index].generation ||
            first[index].owner_kind != second[index].owner_kind ||
            first[index].live != second[index].live)
            return false;
    }
    return true;
}

void PopulateSemanticFailureDetail(
    PlayerStateRestartSnapshot* snapshot) noexcept
{
    if (!snapshot) return;
    if (snapshot->failed_row_index >= kPlayerLogicalTypeCount) {
        for (std::uint32_t index = 0;
             index < kPlayerLogicalTypeCount; ++index) {
            const PlayerDefinitionRowSnapshot& row =
                snapshot->rows[index];
            if (!row.row_readable || !row.definition_readable ||
                !row.semantic_complete ||
                (!row.preset_absent &&
                 (!row.definition_vector ||
                  !row.definition_fingerprint)) ||
                row.current_level > row.maximum_level ||
                row.cache_membership_evidence ==
                    PlayerCacheMembershipEvidence::Conflict) {
                snapshot->failed_row_index = index;
                break;
            }
        }
    }
    if (!snapshot->failed_property_id) {
        for (std::size_t required = 0;
             required < kRequiredPlayerPropertyCount; ++required) {
            std::uint32_t matches = 0;
            const PlayerPropertySnapshot* property = FindProperty(
                *snapshot, kRequiredPropertyIds[required], &matches);
            if (!property || matches != 1U || !property->available) {
                snapshot->failed_property_id =
                    kRequiredPropertyIds[required];
                break;
            }
        }
    }
}

bool CapturePostDestroyEnvironmentPass(
    PlayerGameplayParamsRestartAdapter* adapter,
    const RestartContext* context,
    const PlayerThreadAuthorityEvidence* authority,
    PlayerStateInventoryRecord* owners,
    std::uint32_t* owner_count,
    std::uint64_t* inventory_revision,
    PlayerPostDestroyEnvironmentCaptureDiagnostics* diagnostics,
    PlayerRestartFailure* failure,
    PlayerPostDestroyExactFailure* exact_failure) noexcept
{
    if (failure) *failure = PlayerRestartFailure::None;
    if (exact_failure)
        *exact_failure = PlayerPostDestroyExactFailure::None;
    if (!adapter || !context || !owners || !owner_count ||
        !inventory_revision || !diagnostics) {
        if (failure) *failure = PlayerRestartFailure::InvalidArgument;
        if (exact_failure)
            *exact_failure =
                PlayerPostDestroyExactFailure::InvalidArgument;
        return false;
    }
    if (!CaptureLiveInventoryAllowEmpty(
            adapter, owners, owner_count, inventory_revision, failure)) {
        diagnostics->compatibility_failure =
            failure ? *failure : PlayerRestartFailure::EnvironmentUnreadable;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::InventoryCaptureFailed;
        if (exact_failure)
            *exact_failure = diagnostics->exact_failure;
        return false;
    }
    diagnostics->inventory_capture_complete = true;
    diagnostics->owner_count = *owner_count;
    diagnostics->inventory_revision = *inventory_revision;
    PlayerRestartEnvironment environment = {};
    const bool environment_returned = ReadEnvironment(
            adapter, &environment,
            PlayerEnvironmentReadPurpose::
                PostDestroySemanticAssessment,
            false, failure, diagnostics);
    diagnostics->environment_observed_thread =
        diagnostics->environment.current_thread_id;
    diagnostics->environment_expected_thread =
        authority ? authority->expected_thread_id
                  : context->owner_thread_id;
    diagnostics->thread_match =
        PostDestroyThreadAuthorityMatches(
            diagnostics->environment, context, authority);
    if (!diagnostics->thread_match) {
        diagnostics->compatibility_failure =
            PlayerRestartFailure::ThreadMismatch;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::ThreadMismatch;
        if (failure) *failure = diagnostics->compatibility_failure;
        if (exact_failure)
            *exact_failure = diagnostics->exact_failure;
        return false;
    }
    if (!environment_returned) {
        if (exact_failure)
            *exact_failure = diagnostics->exact_failure;
        return false;
    }
    diagnostics->environment = environment;
    if (environment.active_session) {
        diagnostics->compatibility_failure =
            PlayerRestartFailure::SessionStillActive;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::ActiveSessionStillPresent;
    } else if (environment.active_level) {
        diagnostics->compatibility_failure =
            PlayerRestartFailure::SessionStillActive;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::ActiveLevelStillPresent;
    } else if (environment.local_player_di) {
        diagnostics->compatibility_failure =
            PlayerRestartFailure::LiveGameplayConsumer;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::LocalPlayerStillPresent;
    } else if (environment.live_gameplay_consumer) {
        diagnostics->compatibility_failure =
            PlayerRestartFailure::LiveGameplayConsumer;
        diagnostics->exact_failure =
            PlayerPostDestroyExactFailure::
                LiveGameplayConsumerStillPresent;
    }
    if (diagnostics->exact_failure !=
        PlayerPostDestroyExactFailure::None) {
        if (failure) *failure = diagnostics->compatibility_failure;
        if (exact_failure)
            *exact_failure = diagnostics->exact_failure;
        return false;
    }
    return true;
}

bool CapturePostDestroyStatePass(
    PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerRestartEnvironment& environment,
    const PlayerStateInventoryRecord* owners,
    std::uint32_t owner_count,
    std::uint32_t pass,
    PlayerStateRestartSnapshot* snapshots,
    PlayerRestartFailure* failure,
    PlayerPostDestroyExactFailure* exact_failure) noexcept
{
    if (failure) *failure = PlayerRestartFailure::None;
    if (exact_failure)
        *exact_failure = PlayerPostDestroyExactFailure::None;
    if (!adapter || !owners || !snapshots ||
        (pass != 1U && pass != 2U))
        return false;
    PlayerPostDestroyCaptureDiagnostics& diagnostics =
        adapter->post_destroy_diagnostics;
    for (std::uint32_t index = 0; index < owner_count; ++index) {
        const PlayerStateInventoryRecord& owner = owners[index];
        PlayerPostDestroyStateCaptureDiagnostics& state_diagnostics =
            diagnostics.states[index];
        if (pass == 1U)
            SeedPostDestroyStateDiagnostic(
                &state_diagnostics, owner);
        else if (state_diagnostics.state != owner.pointer ||
                 state_diagnostics.owner_slot != owner.owner_object ||
                 state_diagnostics.generation != owner.generation ||
                 state_diagnostics.owner_kind != owner.owner_kind) {
            if (failure)
                *failure = PlayerRestartFailure::InventoryChanged;
            if (exact_failure)
                *exact_failure = PlayerPostDestroyExactFailure::
                    OwnerSetChangedBetweenPasses;
            return false;
        }
        PlayerStateRestartSnapshot& snapshot = snapshots[index];
        std::memset(&snapshot, 0, sizeof(snapshot));
        snapshot.failed_row_index = UINT32_MAX;
        const bool callback_returned =
            adapter->snapshot_operations.snapshot_state(
                adapter->operations_user, owner.pointer,
                owner.generation, &snapshot);
        snapshot.owner_slot = owner.owner_object;
        snapshot.owner_kind = owner.owner_kind;
        snapshot.cache_generation = environment.cache_generation;
        snapshot.snapshot_attempt_count = pass;
        if (!callback_returned) {
            if (snapshot.failure_stage ==
                PlayerStateSnapshotFailureStage::None)
                snapshot.failure_stage =
                    PlayerStateSnapshotFailureStage::CallbackRejected;
            PopulateSemanticFailureDetail(&snapshot);
            UpdatePostDestroyStateDiagnostic(
                &state_diagnostics, snapshot, pass, false, false);
            RecordFirstPostDestroyStateFailure(
                &diagnostics, index, owner, snapshot,
                PlayerPostDestroyExactFailure::
                    StateSnapshotCallbackRejected);
            if (failure)
                *failure = PlayerRestartFailure::StateSnapshotFailed;
            if (exact_failure)
                *exact_failure = PlayerPostDestroyExactFailure::
                    StateSnapshotCallbackRejected;
            return false;
        }
        if (!SemanticSnapshotComplete(
                snapshot, owner.pointer, owner.generation,
                environment.cache_generation, failure,
                &snapshot.failure_stage,
                &snapshot.semantic_reason_mask)) {
            PopulateSemanticFailureDetail(&snapshot);
            UpdatePostDestroyStateDiagnostic(
                &state_diagnostics, snapshot, pass, true, false);
            RecordFirstPostDestroyStateFailure(
                &diagnostics, index, owner, snapshot,
                PlayerPostDestroyExactFailure::
                    StateSnapshotSemanticIncomplete);
            if (exact_failure)
                *exact_failure = PlayerPostDestroyExactFailure::
                    StateSnapshotSemanticIncomplete;
            return false;
        }
        snapshot.player_semantic_snapshot_complete = true;
        PlayerRestartFailure restart_failure =
            PlayerRestartFailure::None;
        snapshot.player_restart_snapshot_complete =
            RestartSnapshotComplete(
                snapshot, owner.pointer, owner.generation,
                environment.cache_generation, true,
                AllowsUnobservedProgressionTraceMetadata(adapter),
                &restart_failure, &snapshot.restart_failure_stage,
                &snapshot.restart_provenance_reason_mask);
        snapshot.snapshot_complete = true;
        UpdatePostDestroyStateDiagnostic(
            &state_diagnostics, snapshot, pass, true, true);
    }
    return true;
}

PlayerGameplayParamsTouchAssessment AssessPlayerGameplayParamsTouch(
    PlayerGameplayParamsRestartAdapter* adapter,
    const RestartContext* context,
    const PlayerThreadAuthorityEvidence* authority) noexcept
{
    if (!adapter) return PlayerGameplayParamsTouchAssessment::Incomplete;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked())
        return PlayerGameplayParamsTouchAssessment::Incomplete;
    PlayerThreadAuthorityEvidence implicit_authority = {};
    if (!authority && context) {
        implicit_authority.kind =
            PlayerThreadAuthorityKind::RestartTransactionOwner;
        implicit_authority.expected_thread_id =
            context->owner_thread_id;
        implicit_authority.historical_b0_thread_id =
            adapter->baseline_owner_thread_id;
        authority = &implicit_authority;
    }
    ResetPostDestroyDiagnostics(adapter, context, authority);
    if (!context || !context->content_epoch ||
        !context->owner_thread_id || adapter->transaction_active) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            PlayerRestartFailure::InvalidArgument,
            PlayerPostDestroyExactFailure::InvalidArgument);
    }
    if (!adapter->baseline_captured ||
        context->baseline_generation != adapter->baseline_generation ||
        adapter->baseline_kind ==
            PlayerStartupBaselineKind::Unavailable) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            PlayerRestartFailure::BaselineMissing,
            PlayerPostDestroyExactFailure::BaselineMissing);
    }
    if (!EnvironmentOperationsComplete(adapter) ||
        !InventoryRoutesValidated(adapter) ||
        (adapter->baseline_kind ==
             PlayerStartupBaselineKind::Snapshot &&
         !StateSnapshotOperationsComplete(adapter))) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            PlayerRestartFailure::OperationsIncomplete,
            PlayerPostDestroyExactFailure::OperationsIncomplete);
    }

    PlayerStateInventoryRecord live_before[
        kPlayerStateInventoryCapacity] = {};
    PlayerStateInventoryRecord live_after[
        kPlayerStateInventoryCapacity] = {};
    PlayerStateRestartSnapshot pass1_states[
        kPlayerStateInventoryCapacity] = {};
    PlayerStateRestartSnapshot pass2_states[
        kPlayerStateInventoryCapacity] = {};
    std::uint32_t live_before_count = 0;
    std::uint32_t live_after_count = 0;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
    PlayerRestartFailure failure = PlayerRestartFailure::None;
    PlayerPostDestroyExactFailure exact_failure =
        PlayerPostDestroyExactFailure::None;
    PlayerRestartEnvironment environment_before = {};
    if (!CapturePostDestroyEnvironmentPass(
            adapter, context, authority, live_before, &live_before_count,
            &revision_before,
            &adapter->post_destroy_diagnostics.pass1,
            &failure, &exact_failure)) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            failure == PlayerRestartFailure::None
                ? PlayerRestartFailure::EnvironmentUnreadable
                : failure,
            exact_failure == PlayerPostDestroyExactFailure::None
                ? PlayerPostDestroyExactFailure::
                      EnvironmentCallbackRejected
                : exact_failure);
    }
    environment_before =
        adapter->post_destroy_diagnostics.pass1.environment;
    adapter->post_destroy_diagnostics.state_count =
        live_before_count;

    if (adapter->baseline_kind ==
        PlayerStartupBaselineKind::Absent) {
        if (!CapturePostDestroyEnvironmentPass(
                adapter, context, authority, live_after, &live_after_count,
                &revision_after,
                &adapter->post_destroy_diagnostics.pass2,
                &failure, &exact_failure)) {
            return FinishPostDestroyAssessment(
                adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
                failure == PlayerRestartFailure::None
                    ? PlayerRestartFailure::EnvironmentUnreadable
                    : failure,
                exact_failure == PlayerPostDestroyExactFailure::None
                    ? PlayerPostDestroyExactFailure::
                          EnvironmentCallbackRejected
                    : exact_failure);
        }
        const PlayerRestartEnvironment& environment_after =
            adapter->post_destroy_diagnostics.pass2.environment;
        adapter->post_destroy_diagnostics.environment_pair_stable =
            StableEnvironmentPair(
                environment_before, environment_after);
        if (!adapter->post_destroy_diagnostics.environment_pair_stable) {
            return FinishPostDestroyAssessment(
                adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
                PlayerRestartFailure::InventoryChanged,
                PlayerPostDestroyExactFailure::
                    EnvironmentChangedBetweenPasses);
        }
        if (revision_before != revision_after) {
            return FinishPostDestroyAssessment(
                adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
                PlayerRestartFailure::InventoryChanged,
                PlayerPostDestroyExactFailure::
                    InventoryChangedBetweenPasses);
        }
        adapter->post_destroy_diagnostics.inventory_pair_stable =
            SameInventoryOwnerSet(
                live_before, live_before_count,
                live_after, live_after_count);
        if (!adapter->post_destroy_diagnostics.inventory_pair_stable) {
            return FinishPostDestroyAssessment(
                adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
                PlayerRestartFailure::InventoryChanged,
                PlayerPostDestroyExactFailure::
                    OwnerSetChangedBetweenPasses);
        }
        const bool baseline_absent_pass1 =
            AbsentOwnersProven(environment_before, live_before_count);
        const bool baseline_absent_pass2 =
            AbsentOwnersProven(environment_after, live_after_count);
        adapter->post_destroy_diagnostics.
            owner_set_matches_baseline_pass1 =
                baseline_absent_pass1;
        adapter->post_destroy_diagnostics.
            owner_set_matches_baseline_pass2 =
                baseline_absent_pass2;
        adapter->post_destroy_diagnostics.
            semantic_state_pair_stable = true;
        if (live_before_count != 0U ||
            environment_before.local_process_global_state ||
            environment_before.character_manager_global_state) {
            return FinishPostDestroyAssessment(
                adapter,
                PlayerGameplayParamsTouchAssessment::
                    TouchedPersistentOwner,
                PlayerRestartFailure::TouchedResetUnavailable,
                PlayerPostDestroyExactFailure::None);
        }
        if (environment_before.gameplay_cache_fingerprint !=
            adapter->baseline_gameplay_cache_fingerprint) {
            return FinishPostDestroyAssessment(
                adapter,
                PlayerGameplayParamsTouchAssessment::
                    TouchedDefinitionDelta,
                PlayerRestartFailure::TouchedResetUnavailable,
                PlayerPostDestroyExactFailure::None);
        }
        return FinishPostDestroyAssessment(
            adapter,
            PlayerGameplayParamsTouchAssessment::UntouchedAbsent,
            PlayerRestartFailure::None,
            PlayerPostDestroyExactFailure::None);
    }

    const bool owner_set_matches_before =
        OwnerSetMatchesSemanticBaseline(
            adapter, environment_before, live_before,
            live_before_count);
    adapter->post_destroy_diagnostics.
        owner_set_matches_baseline_pass1 =
            owner_set_matches_before;
    for (std::uint32_t index = 0;
         index < live_before_count; ++index)
        SeedPostDestroyStateDiagnostic(
            &adapter->post_destroy_diagnostics.states[index],
            live_before[index]);
    if (owner_set_matches_before &&
        !CapturePostDestroyStatePass(
            adapter, environment_before, live_before,
            live_before_count, 1U, pass1_states,
            &failure, &exact_failure)) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            failure == PlayerRestartFailure::None
                ? PlayerRestartFailure::StateSnapshotFailed
                : failure,
            exact_failure == PlayerPostDestroyExactFailure::None
                ? PlayerPostDestroyExactFailure::
                      StateSnapshotCallbackRejected
                : exact_failure);
    }
    if (owner_set_matches_before &&
        adapter->snapshot_operations.
            observe_post_destroy_semantic_pass)
        adapter->snapshot_operations.
            observe_post_destroy_semantic_pass(
                adapter->operations_user, 1U,
                &environment_before, live_before, pass1_states,
                live_before_count, revision_before);

    if (!CapturePostDestroyEnvironmentPass(
            adapter, context, authority, live_after, &live_after_count,
            &revision_after,
            &adapter->post_destroy_diagnostics.pass2,
            &failure, &exact_failure)) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            failure == PlayerRestartFailure::None
                ? PlayerRestartFailure::EnvironmentUnreadable
                : failure,
            exact_failure == PlayerPostDestroyExactFailure::None
                ? PlayerPostDestroyExactFailure::
                      EnvironmentCallbackRejected
                : exact_failure);
    }
    const PlayerRestartEnvironment& environment_after =
        adapter->post_destroy_diagnostics.pass2.environment;
    adapter->post_destroy_diagnostics.environment_pair_stable =
        StableEnvironmentPair(environment_before, environment_after);
    if (!adapter->post_destroy_diagnostics.environment_pair_stable) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            PlayerRestartFailure::InventoryChanged,
            PlayerPostDestroyExactFailure::
                EnvironmentChangedBetweenPasses);
    }
    if (revision_before != revision_after) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            PlayerRestartFailure::InventoryChanged,
            PlayerPostDestroyExactFailure::
                InventoryChangedBetweenPasses);
    }
    adapter->post_destroy_diagnostics.inventory_pair_stable =
        SameInventoryOwnerSet(
            live_before, live_before_count,
            live_after, live_after_count);
    if (!adapter->post_destroy_diagnostics.inventory_pair_stable) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            PlayerRestartFailure::InventoryChanged,
            PlayerPostDestroyExactFailure::
                OwnerSetChangedBetweenPasses);
    }
    const bool owner_set_matches_after =
        OwnerSetMatchesSemanticBaseline(
            adapter, environment_after, live_after,
            live_after_count);
    adapter->post_destroy_diagnostics.
        owner_set_matches_baseline_pass2 =
            owner_set_matches_after;
    if (!owner_set_matches_before || !owner_set_matches_after) {
        return FinishPostDestroyAssessment(
            adapter,
            PlayerGameplayParamsTouchAssessment::
                TouchedPersistentOwner,
            PlayerRestartFailure::TouchedResetUnavailable,
            PlayerPostDestroyExactFailure::None);
    }
    if (!CapturePostDestroyStatePass(
            adapter, environment_after, live_after,
            live_after_count, 2U, pass2_states,
            &failure, &exact_failure)) {
        return FinishPostDestroyAssessment(
            adapter, PlayerGameplayParamsTouchAssessment::Incomplete,
            failure == PlayerRestartFailure::None
                ? PlayerRestartFailure::StateSnapshotFailed
                : failure,
            exact_failure == PlayerPostDestroyExactFailure::None
                ? PlayerPostDestroyExactFailure::
                      StateSnapshotCallbackRejected
                : exact_failure);
    }
    if (adapter->snapshot_operations.
            observe_post_destroy_semantic_pass)
        adapter->snapshot_operations.
            observe_post_destroy_semantic_pass(
                adapter->operations_user, 2U,
                &environment_after, live_after, pass2_states,
                live_after_count, revision_after);

    bool touched = environment_after.gameplay_cache_fingerprint !=
        adapter->baseline_gameplay_cache_fingerprint;
    for (std::uint32_t index = 0;
         index < live_after_count; ++index) {
        if (!SnapshotSemanticEqual(
                pass1_states[index], pass2_states[index])) {
            PlayerStateRestartSnapshot mismatch =
                pass2_states[index];
            mismatch.failure_stage =
                PlayerStateSnapshotFailureStage::
                    StateChangedBetweenPasses;
            mismatch.snapshot_complete = false;
            UpdatePostDestroyStateDiagnostic(
                &adapter->post_destroy_diagnostics.states[index],
                mismatch, 2U, true, false);
            RecordFirstPostDestroyStateFailure(
                &adapter->post_destroy_diagnostics, index,
                live_after[index], mismatch,
                PlayerPostDestroyExactFailure::
                    StateChangedBetweenPasses);
            return FinishPostDestroyAssessment(
                adapter,
                PlayerGameplayParamsTouchAssessment::Incomplete,
                PlayerRestartFailure::InventoryChanged,
                PlayerPostDestroyExactFailure::
                    StateChangedBetweenPasses);
        }
        const PlayerStateRestartSnapshot* baseline = FindBaseline(
            adapter, live_after[index].pointer,
            live_after[index].generation);
        if (!baseline) {
            adapter->post_destroy_diagnostics.
                first_failed_owner_index = index;
            adapter->post_destroy_diagnostics.
                first_failed_owner_kind =
                    live_after[index].owner_kind;
            adapter->post_destroy_diagnostics.first_failed_state =
                live_after[index].pointer;
            adapter->post_destroy_diagnostics.
                first_failed_state_generation =
                    live_after[index].generation;
            return FinishPostDestroyAssessment(
                adapter,
                PlayerGameplayParamsTouchAssessment::Incomplete,
                PlayerRestartFailure::BaselineStateMissing,
                PlayerPostDestroyExactFailure::
                    BaselineStateMissing);
        }
        if (SnapshotTouchesPlayerDomain(
                *baseline, pass2_states[index]))
            touched = true;
    }
    adapter->post_destroy_diagnostics.semantic_state_pair_stable = true;
    return FinishPostDestroyAssessment(
        adapter,
        touched
            ? PlayerGameplayParamsTouchAssessment::
                  TouchedDefinitionDelta
            : PlayerGameplayParamsTouchAssessment::
                  UntouchedSnapshot,
        touched ? PlayerRestartFailure::TouchedResetUnavailable
                : PlayerRestartFailure::None,
        PlayerPostDestroyExactFailure::None);
}

DomainResult PreflightPlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    if (!adapter) return DomainResult::Failed;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return DomainResult::Failed;
    if (!context || !context->transaction_generation ||
        !context->content_epoch || !context->owner_thread_id)
        return Fail(adapter, PlayerRestartFailure::InvalidArgument, false);
    if (adapter->runtime_failure_latched)
        return Fail(adapter, PlayerRestartFailure::VerificationFailed,
                    false);
    if (!adapter->baseline_captured)
        return Fail(adapter, PlayerRestartFailure::BaselineMissing, false);
    if (context->baseline_generation != adapter->baseline_generation)
        return Fail(adapter, PlayerRestartFailure::TransactionMismatch,
                    false);
    if (adapter->baseline_kind == PlayerStartupBaselineKind::Absent)
        return Fail(
            adapter, PlayerRestartFailure::TouchedResetUnavailable,
            false);
    if (context->transaction_generation <=
        adapter->completed_transaction_generation)
        return Fail(adapter,
                    PlayerRestartFailure::TransactionAlreadyCompleted,
                    false);
    if (adapter->transaction_active) {
        if (adapter->mutation_started ||
            context->transaction_generation <=
                adapter->transaction_generation)
            return Fail(adapter,
                        PlayerRestartFailure::TransactionAlreadyActive,
                        false);
        ClearPreparedTransaction(adapter);
    }
    if (!SnapshotOperationsComplete(adapter) ||
        !MutationOperationsComplete(adapter))
        return Fail(adapter, PlayerRestartFailure::OperationsIncomplete,
                    false);
    if (!InventoryRoutesValidated(adapter) ||
        !MutationRoutesValidated(adapter))
        return Fail(adapter, PlayerRestartFailure::ExactRouteUnvalidated,
                    false);
    if (!MutationProductionEnabled(adapter))
        return Fail(adapter,
                    PlayerRestartFailure::MutationProductionDisabled,
                    false);

    PlayerStateInventoryRecord live[kPlayerStateInventoryCapacity] = {};
    std::uint32_t live_count = 0;
    std::uint64_t revision = 0;
    PlayerRestartFailure failure = PlayerRestartFailure::None;
    if (!CaptureLiveInventory(adapter, live, &live_count, &revision,
                              &failure))
        return Fail(adapter, failure, false);

    PlayerRestartEnvironment environment = {};
    if (!ReadEnvironment(
            adapter, &environment,
            PlayerEnvironmentReadPurpose::RestartMutationPreflight,
            true, &failure))
        return Fail(adapter, failure, false);
    if (!EnvironmentThreadMatches(environment, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, false);
    if (!ValidateOwners(environment, live, live_count, false, &failure))
        return Fail(adapter, failure, false);
    if (live_count != adapter->baseline_state_count)
        return Fail(adapter, PlayerRestartFailure::BaselineStateMissing,
                    false);

    adapter->plan_count = 0;
    for (std::uint32_t index = 0; index < live_count; ++index) {
        const PlayerStateRestartSnapshot* baseline = FindBaseline(
            adapter, live[index].pointer, live[index].generation);
        if (!baseline)
            return Fail(adapter,
                        PlayerRestartFailure::BaselineStateMissing, false);
        PlayerRestartPlannedState planned = {};
        planned.pointer = live[index].pointer;
        planned.generation = live[index].generation;
        planned.owner_kind = live[index].owner_kind;
        if (!adapter->snapshot_operations.snapshot_state(
                adapter->operations_user, planned.pointer,
                planned.generation, &planned.before))
            return Fail(adapter,
                        PlayerRestartFailure::StateSnapshotFailed, false);
        planned.before.owner_slot = live[index].owner_object;
        planned.before.owner_kind = live[index].owner_kind;
        if (!RestartSnapshotComplete(
                planned.before, planned.pointer,
                planned.generation,
                environment.cache_generation, false,
                AllowsUnobservedProgressionTraceMetadata(
                    adapter),
                &failure))
            return Fail(adapter, failure, false);
        if (planned.before.progression_selector_fingerprint !=
                baseline->progression_selector_fingerprint ||
            planned.before.class_selector_fingerprint !=
                baseline->class_selector_fingerprint)
            return Fail(adapter, PlayerRestartFailure::SelectorMismatch,
                        false);
        adapter->plan[adapter->plan_count++] = planned;
    }
    if (!InventoryRevisionUnchanged(adapter, revision))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, false);

    adapter->transaction_generation = context->transaction_generation;
    adapter->transaction_baseline_generation =
        context->baseline_generation;
    adapter->transaction_content_epoch = context->content_epoch;
    adapter->owner_thread_id = context->owner_thread_id;
    adapter->planned_inventory_revision = revision;
    adapter->before_cache_generation = environment.cache_generation;
    adapter->after_cache_generation = 0;
    adapter->planned_gameplay_params_container =
        environment.gameplay_params_container;
    adapter->planned_game_di = environment.game_di;
    adapter->planned_local_process_global_state =
        environment.local_process_global_state;
    adapter->planned_character_manager_global_state =
        environment.character_manager_global_state;
    adapter->transaction_global_reset_calls = 0;
    adapter->transaction_initialize_calls = 0;
    adapter->transaction_active = true;
    adapter->preflight_complete = true;
    adapter->quiesce_complete = false;
    adapter->mutation_started = false;
    adapter->reset_postconditions_complete = false;
    adapter->verified = false;
    adapter->last_failure = PlayerRestartFailure::None;
    return DomainResult::Succeeded;
}

DomainResult QuiescePlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    if (!adapter) return DomainResult::Failed;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return DomainResult::Failed;
    if (!ContextMatches(adapter, context))
        return Fail(adapter, PlayerRestartFailure::TransactionMismatch,
                    false);
    if (!adapter->preflight_complete || adapter->mutation_started)
        return Fail(adapter, PlayerRestartFailure::MutationAlreadyStarted,
                    false);
    PlayerRestartFailure failure = PlayerRestartFailure::None;
    PlayerRestartEnvironment environment = {};
    if (!ReadEnvironment(
            adapter, &environment,
            PlayerEnvironmentReadPurpose::RestartMutationPreflight,
            true, &failure)) {
        ClearPreparedTransaction(adapter);
        return Fail(adapter, failure, false);
    }
    if (!EnvironmentThreadMatches(environment, context)) {
        ClearPreparedTransaction(adapter);
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, false);
    }
    if (!EnvironmentMatchesPlan(adapter, environment) ||
        environment.cache_generation != adapter->before_cache_generation ||
        !PlanStillLive(adapter)) {
        ClearPreparedTransaction(adapter);
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, false);
    }
    adapter->quiesce_complete = true;
    adapter->last_failure = PlayerRestartFailure::None;
    return DomainResult::Succeeded;
}

DomainResult ResetPlayerGameplayParams(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    if (!adapter) return DomainResult::Failed;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return DomainResult::Failed;
    if (!ContextMatches(adapter, context))
        return Fail(adapter, PlayerRestartFailure::TransactionMismatch,
                    adapter->mutation_started);
    if (!adapter->preflight_complete || !adapter->quiesce_complete)
        return Fail(adapter, PlayerRestartFailure::TransactionMismatch,
                    false);
    if (adapter->mutation_started)
        return Fail(adapter, PlayerRestartFailure::MutationAlreadyStarted,
                    true);
    /* Defense in depth: a prepared fake/test transaction cannot become a
     * production mutation merely because callbacks are non-null. */
    if (!MutationProductionEnabled(adapter))
        return Fail(adapter,
                    PlayerRestartFailure::MutationProductionDisabled,
                    false);

    PlayerRestartFailure failure = PlayerRestartFailure::None;
    PlayerRestartEnvironment before_environment = {};
    if (!ReadEnvironment(
            adapter, &before_environment,
            PlayerEnvironmentReadPurpose::RestartMutationPreflight,
            true, &failure))
        return Fail(adapter, failure, false);
    if (!EnvironmentThreadMatches(before_environment, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, false);
    if (!EnvironmentMatchesPlan(adapter, before_environment) ||
        before_environment.cache_generation !=
            adapter->before_cache_generation)
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, false);

    dluma_atomic_exchange(&adapter->mutation_in_progress, 1);
    if (!PlanStillLive(adapter)) {
        dluma_atomic_exchange(&adapter->mutation_in_progress, 0);
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, false);
    }
    adapter->mutation_started = true;
    ++adapter->transaction_global_reset_calls;
    ++adapter->total_global_reset_calls;
    adapter->mutation_operations.reset_global_cache(
        adapter->operations_user,
        before_environment.gameplay_params_container);

    for (std::uint32_t index = 0; index < adapter->plan_count; ++index) {
        if (!InventoryRevisionUnchanged(
                adapter, adapter->planned_inventory_revision)) {
            dluma_atomic_exchange(&adapter->mutation_in_progress, 0);
            return Fail(adapter,
                        PlayerRestartFailure::InventoryChanged, true);
        }
        adapter->mutation_operations.initialize_variables(
            adapter->operations_user, adapter->plan[index].pointer);
        ++adapter->plan[index].initialize_calls;
        ++adapter->transaction_initialize_calls;
        ++adapter->total_initialize_calls;
    }
    dluma_atomic_exchange(&adapter->mutation_in_progress, 0);

    if (adapter->transaction_global_reset_calls != 1U ||
        adapter->transaction_initialize_calls != adapter->plan_count) {
        return Fail(adapter,
                    PlayerRestartFailure::InitializeCountMismatch, true);
    }
    if (!InventoryRevisionUnchanged(
            adapter, adapter->planned_inventory_revision))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, true);

    PlayerRestartEnvironment after_environment = {};
    if (!ReadEnvironment(
            adapter, &after_environment,
            PlayerEnvironmentReadPurpose::RestartMutationVerify,
            true, &failure))
        return Fail(adapter, failure, true);
    if (!EnvironmentThreadMatches(after_environment, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, true);
    const bool allow_unobserved_metadata =
        AllowsUnobservedProgressionTraceMetadata(adapter);
    if (!EnvironmentMatchesPlan(adapter, after_environment) ||
        (!allow_unobserved_metadata &&
         after_environment.cache_generation <=
             adapter->before_cache_generation) ||
        (allow_unobserved_metadata &&
         !CacheGenerationAdvanced(
             adapter->before_cache_generation,
             after_environment.cache_generation, true))) {
        return Fail(adapter,
                    PlayerRestartFailure::CacheGenerationDidNotAdvance,
                    true);
    }
    adapter->after_cache_generation = after_environment.cache_generation;

    for (std::uint32_t index = 0; index < adapter->plan_count; ++index) {
        PlayerRestartPlannedState& planned = adapter->plan[index];
        PlayerStateRestartSnapshot snapshot = {};
        if (!adapter->snapshot_operations.snapshot_state(
                adapter->operations_user, planned.pointer,
                planned.generation, &snapshot))
            return Fail(adapter,
                        PlayerRestartFailure::StateSnapshotFailed, true);
        snapshot.owner_slot = planned.before.owner_slot;
        snapshot.owner_kind = planned.owner_kind;
        if (!ValidateAfterSnapshot(adapter, &planned, snapshot, &failure))
            return Fail(adapter, failure, true);
        planned.after_reset = snapshot;
        planned.after_reset_captured = true;
    }
    if (!InventoryRevisionUnchanged(
            adapter, adapter->planned_inventory_revision))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, true);
    adapter->reset_postconditions_complete = true;
    adapter->last_failure = PlayerRestartFailure::None;
    return DomainResult::Succeeded;
}

DomainResult ReloadPlayerGameplayParams(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    if (!adapter) return DomainResult::Failed;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return DomainResult::Failed;
    if (!ContextMatches(adapter, context))
        return Fail(adapter, PlayerRestartFailure::TransactionMismatch,
                    adapter->mutation_started);
    if (!adapter->reset_postconditions_complete ||
        adapter->runtime_failure_latched)
        return Fail(adapter, PlayerRestartFailure::VerificationFailed,
                    true);
    PlayerRestartEnvironment environment = {};
    PlayerRestartFailure failure = PlayerRestartFailure::None;
    if (!ReadEnvironment(
            adapter, &environment,
            PlayerEnvironmentReadPurpose::RestartMutationVerify,
            true, &failure))
        return Fail(adapter, failure, true);
    if (!EnvironmentThreadMatches(environment, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, true);
    if (!EnvironmentMatchesPlan(adapter, environment) ||
        environment.cache_generation != adapter->after_cache_generation)
        return Fail(adapter,
                    PlayerRestartFailure::CacheMembershipMismatch, true);
    return DomainResult::Succeeded;
}

DomainResult VerifyPlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept
{
    PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<PlayerGameplayParamsRestartAdapter*>(user);
    if (!adapter) return DomainResult::Failed;
    TryLock phase(&adapter->phase_lock);
    if (!phase.locked()) return DomainResult::Failed;
    if (!ContextMatches(adapter, context))
        return Fail(adapter, PlayerRestartFailure::TransactionMismatch,
                    adapter->mutation_started);
    if (!adapter->reset_postconditions_complete ||
        adapter->runtime_failure_latched)
        return Fail(adapter, PlayerRestartFailure::VerificationFailed,
                    true);
    if (!PlanStillLive(adapter))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, true);

    PlayerRestartFailure failure = PlayerRestartFailure::None;
    PlayerRestartEnvironment environment = {};
    if (!ReadEnvironment(
            adapter, &environment,
            PlayerEnvironmentReadPurpose::RestartMutationVerify,
            true, &failure))
        return Fail(adapter, failure, true);
    if (!EnvironmentThreadMatches(environment, context))
        return Fail(adapter, PlayerRestartFailure::ThreadMismatch, true);
    if (!EnvironmentMatchesPlan(adapter, environment) ||
        environment.cache_generation != adapter->after_cache_generation)
        return Fail(adapter,
                    PlayerRestartFailure::CacheMembershipMismatch, true);

    for (std::uint32_t index = 0; index < adapter->plan_count; ++index) {
        PlayerStateRestartSnapshot snapshot = {};
        if (!adapter->snapshot_operations.snapshot_state(
                adapter->operations_user, adapter->plan[index].pointer,
                adapter->plan[index].generation, &snapshot))
            return Fail(adapter,
                        PlayerRestartFailure::StateSnapshotFailed, true);
        snapshot.owner_slot =
            adapter->plan[index].before.owner_slot;
        snapshot.owner_kind =
            adapter->plan[index].owner_kind;
        if (!ValidateAfterSnapshot(adapter, &adapter->plan[index],
                                   snapshot, &failure))
            return Fail(adapter, failure, true);
    }
    if (!InventoryRevisionUnchanged(
            adapter, adapter->planned_inventory_revision))
        return Fail(adapter, PlayerRestartFailure::InventoryChanged, true);
    adapter->completed_transaction_generation =
        adapter->transaction_generation;
    adapter->verified = true;
    adapter->transaction_active = false;
    adapter->last_failure = PlayerRestartFailure::None;
    return DomainResult::Succeeded;
}

ContentDomainAdapter MakePlayerGameplayParamsContentDomainAdapter(
    PlayerGameplayParamsRestartAdapter* adapter,
    ContentDomainMask dependency_mask, bool mandatory) noexcept
{
    ContentDomainAdapter definition = {};
    definition.id = ContentDomainId::PlayerGameplayParams;
    const char name[] = "PlayerGameplayParams";
    std::memcpy(definition.name, name, sizeof(name));
    definition.dependency_mask = dependency_mask;
    definition.enabled = true;
    definition.mandatory = mandatory;
    definition.destructive = true;
    definition.user = adapter;
    definition.capture_baseline = &CapturePlayerGameplayParamsBaseline;
    definition.preflight = &PreflightPlayerGameplayParamsRestart;
    definition.quiesce = &QuiescePlayerGameplayParamsRestart;
    definition.cancel_quiesce =
        &CancelQuiescedPlayerGameplayParamsRestart;
    definition.reset = &ResetPlayerGameplayParams;
    definition.reload = &ReloadPlayerGameplayParams;
    definition.verify = &VerifyPlayerGameplayParamsRestart;
    return definition;
}

const char* PlayerRestartFailureName(PlayerRestartFailure failure) noexcept
{
    switch (failure) {
    case PlayerRestartFailure::None: return "none";
    case PlayerRestartFailure::InvalidArgument: return "invalid_argument";
    case PlayerRestartFailure::OperationsIncomplete:
        return "operations_incomplete";
    case PlayerRestartFailure::ExactRouteUnvalidated:
        return "exact_route_unvalidated";
    case PlayerRestartFailure::MutationProductionDisabled:
        return "mutation_production_disabled";
    case PlayerRestartFailure::CoverageNotStarted:
        return "coverage_not_started";
    case PlayerRestartFailure::CoverageStartedLate:
        return "coverage_started_late";
    case PlayerRestartFailure::InventoryLoss: return "inventory_loss";
    case PlayerRestartFailure::InventoryOverflow:
        return "inventory_overflow";
    case PlayerRestartFailure::InventoryBusy: return "inventory_busy";
    case PlayerRestartFailure::InventoryEmpty: return "inventory_empty";
    case PlayerRestartFailure::InventoryChanged:
        return "inventory_changed";
    case PlayerRestartFailure::StateUnclassified:
        return "state_unclassified";
    case PlayerRestartFailure::GlobalOwnerMissing:
        return "global_owner_missing";
    case PlayerRestartFailure::GlobalOwnerMismatch:
        return "global_owner_mismatch";
    case PlayerRestartFailure::LiveGameplayConsumer:
        return "live_gameplay_consumer";
    case PlayerRestartFailure::SessionStillActive:
        return "session_still_active";
    case PlayerRestartFailure::ShutdownOrQuiescing:
        return "shutdown_or_quiescing";
    case PlayerRestartFailure::EnvironmentUnreadable:
        return "environment_unreadable";
    case PlayerRestartFailure::GameplayContainerUnavailable:
        return "gameplay_container_unavailable";
    case PlayerRestartFailure::GameDiUnavailable:
        return "game_di_unavailable";
    case PlayerRestartFailure::BaselineMissing: return "baseline_missing";
    case PlayerRestartFailure::AbsentBaselineUnproven:
        return "absent_baseline_unproven";
    case PlayerRestartFailure::TouchedResetUnavailable:
        return "touched_reset_unavailable";
    case PlayerRestartFailure::BaselineStateMissing:
        return "baseline_state_missing";
    case PlayerRestartFailure::StateSnapshotFailed:
        return "state_snapshot_failed";
    case PlayerRestartFailure::StateSnapshotIncomplete:
        return "state_snapshot_incomplete";
    case PlayerRestartFailure::RequiredPropertyMissing:
        return "required_property_missing";
    case PlayerRestartFailure::RequiredPropertyDuplicate:
        return "required_property_duplicate";
    case PlayerRestartFailure::ProviderIncomplete:
        return "provider_incomplete";
    case PlayerRestartFailure::SelectorMismatch:
        return "selector_mismatch";
    case PlayerRestartFailure::TransactionAlreadyActive:
        return "transaction_already_active";
    case PlayerRestartFailure::TransactionAlreadyCompleted:
        return "transaction_already_completed";
    case PlayerRestartFailure::TransactionMismatch:
        return "transaction_mismatch";
    case PlayerRestartFailure::ThreadMismatch: return "thread_mismatch";
    case PlayerRestartFailure::MutationAlreadyStarted:
        return "mutation_already_started";
    case PlayerRestartFailure::CacheGenerationDidNotAdvance:
        return "cache_generation_did_not_advance";
    case PlayerRestartFailure::InitializeCountMismatch:
        return "initialize_count_mismatch";
    case PlayerRestartFailure::CacheMembershipMismatch:
        return "cache_membership_mismatch";
    case PlayerRestartFailure::ProviderMismatch:
        return "provider_mismatch";
    case PlayerRestartFailure::PreservationInvariantMismatch:
        return "preservation_invariant_mismatch";
    case PlayerRestartFailure::PropertyMismatch:
        return "property_mismatch";
    case PlayerRestartFailure::VerificationFailed:
        return "verification_failed";
    case PlayerRestartFailure::SemanticRowInvalid:
        return "semantic_row_invalid";
    case PlayerRestartFailure::SemanticPropertyInvalid:
        return "semantic_property_invalid";
    case PlayerRestartFailure::CacheMembershipConflict:
        return "cache_membership_conflict";
    case PlayerRestartFailure::RestartCacheMembershipUnproven:
        return "restart_cache_membership_unproven";
    case PlayerRestartFailure::RestartProviderUnproven:
        return "restart_provider_unproven";
    case PlayerRestartFailure::RestartInventoryOwnerUnproven:
        return "restart_inventory_owner_unproven";
    case PlayerRestartFailure::RestartEquipmentOwnerUnproven:
        return "restart_equipment_owner_unproven";
    case PlayerRestartFailure::RestartQuestOwnerUnproven:
        return "restart_quest_owner_unproven";
    default: return "unknown";
    }
}

const char* PlayerStateSnapshotFailureStageName(
    PlayerStateSnapshotFailureStage stage) noexcept
{
    switch (stage) {
    case PlayerStateSnapshotFailureStage::None:
        return "none";
    case PlayerStateSnapshotFailureStage::CallbackRejected:
        return "callback_rejected";
    case PlayerStateSnapshotFailureStage::BodyUnreadable:
        return "body_unreadable";
    case PlayerStateSnapshotFailureStage::PlayerVariablesUnavailable:
        return "player_variables_unavailable";
    case PlayerStateSnapshotFailureStage::RowTableUnreadable:
        return "row_table_unreadable";
    case PlayerStateSnapshotFailureStage::SelectorSnapshotIncomplete:
        return "selector_snapshot_incomplete";
    case PlayerStateSnapshotFailureStage::
            PreservationFingerprintIncomplete:
        return "preservation_fingerprint_incomplete";
    case PlayerStateSnapshotFailureStage::AppearanceSnapshotIncomplete:
        return "appearance_snapshot_incomplete";
    case PlayerStateSnapshotFailureStage::RowUnreadable:
        return "row_unreadable";
    case PlayerStateSnapshotFailureStage::RowDefinitionUnreadable:
        return "row_definition_unreadable";
    case PlayerStateSnapshotFailureStage::
            RowDefinitionTraversalIncomplete:
        return "row_definition_traversal_incomplete";
    case PlayerStateSnapshotFailureStage::RowCacheMembershipMismatch:
        return "row_cache_membership_mismatch";
    case PlayerStateSnapshotFailureStage::RowProviderIncomplete:
        return "row_provider_incomplete";
    case PlayerStateSnapshotFailureStage::RequiredPropertyMissing:
        return "required_property_missing";
    case PlayerStateSnapshotFailureStage::RequiredPropertyDuplicate:
        return "required_property_duplicate";
    case PlayerStateSnapshotFailureStage::PropertyProviderIncomplete:
        return "property_provider_incomplete";
    case PlayerStateSnapshotFailureStage::CacheGenerationMismatch:
        return "cache_generation_mismatch";
    case PlayerStateSnapshotFailureStage::StateChangedBetweenPasses:
        return "state_changed_between_passes";
    case PlayerStateSnapshotFailureStage::SemanticRowInvalid:
        return "semantic_row_invalid";
    case PlayerStateSnapshotFailureStage::SemanticPropertyInvalid:
        return "semantic_property_invalid";
    case PlayerStateSnapshotFailureStage::CacheMembershipConflict:
        return "cache_membership_conflict";
    case PlayerStateSnapshotFailureStage::RestartCacheMembershipUnproven:
        return "restart_cache_membership_unproven";
    case PlayerStateSnapshotFailureStage::RestartProviderUnproven:
        return "restart_provider_unproven";
    case PlayerStateSnapshotFailureStage::RestartInventoryOwnerUnproven:
        return "restart_inventory_owner_unproven";
    case PlayerStateSnapshotFailureStage::RestartEquipmentOwnerUnproven:
        return "restart_equipment_owner_unproven";
    case PlayerStateSnapshotFailureStage::RestartQuestOwnerUnproven:
        return "restart_quest_owner_unproven";
    default:
        return "unknown";
    }
}

const char* PlayerCacheMembershipEvidenceName(
    PlayerCacheMembershipEvidence evidence) noexcept
{
    switch (evidence) {
    case PlayerCacheMembershipEvidence::Unobserved:
        return "Unobserved";
    case PlayerCacheMembershipEvidence::ProvenMember:
        return "ProvenMember";
    case PlayerCacheMembershipEvidence::ProvenNonMember:
        return "ProvenNonMember";
    case PlayerCacheMembershipEvidence::Conflict:
        return "Conflict";
    default:
        return "unknown";
    }
}

const char* PlayerStateOwnerKindName(
    PlayerStateOwnerKind kind) noexcept
{
    switch (kind) {
    case PlayerStateOwnerKind::Unknown:
        return "unknown";
    case PlayerStateOwnerKind::LocalProcessGlobal:
        return "local_process_global";
    case PlayerStateOwnerKind::CharacterManagerGlobal:
        return "character_manager_global";
    case PlayerStateOwnerKind::PlayerDi:
        return "player_di";
    case PlayerStateOwnerKind::TutorialPlayerDi:
        return "tutorial_player_di";
    case PlayerStateOwnerKind::OtherRttiObserved:
        return "other_rtti_observed";
    default:
        return "unknown";
    }
}

const char* PlayerStartupBaselineKindName(
    PlayerStartupBaselineKind kind) noexcept
{
    switch (kind) {
    case PlayerStartupBaselineKind::Unavailable:
        return "Unavailable";
    case PlayerStartupBaselineKind::Absent:
        return "Absent";
    case PlayerStartupBaselineKind::Snapshot:
        return "Snapshot";
    default:
        return "Unknown";
    }
}

const char* PlayerGameplayParamsTouchAssessmentName(
    PlayerGameplayParamsTouchAssessment assessment) noexcept
{
    switch (assessment) {
    case PlayerGameplayParamsTouchAssessment::UntouchedAbsent:
        return "UntouchedAbsent";
    case PlayerGameplayParamsTouchAssessment::UntouchedSnapshot:
        return "UntouchedSnapshot";
    case PlayerGameplayParamsTouchAssessment::TouchedPersistentOwner:
        return "TouchedPersistentOwner";
    case PlayerGameplayParamsTouchAssessment::TouchedDefinitionDelta:
        return "TouchedDefinitionDelta";
    case PlayerGameplayParamsTouchAssessment::Incomplete:
        return "Incomplete";
    default:
        return "Unknown";
    }
}

const char* PlayerEnvironmentReadPurposeName(
    PlayerEnvironmentReadPurpose purpose) noexcept
{
    switch (purpose) {
    case PlayerEnvironmentReadPurpose::StartupSemanticBaseline:
        return "startup_semantic_baseline";
    case PlayerEnvironmentReadPurpose::PostDestroySemanticAssessment:
        return "post_destroy_semantic_assessment";
    case PlayerEnvironmentReadPurpose::RestartMutationPreflight:
        return "restart_mutation_preflight";
    case PlayerEnvironmentReadPurpose::RestartMutationVerify:
        return "restart_mutation_verify";
    default:
        return "unknown";
    }
}

const char* PlayerThreadAuthorityKindName(
    PlayerThreadAuthorityKind kind) noexcept
{
    switch (kind) {
    case PlayerThreadAuthorityKind::StartupB0CaptureOwner:
        return "startup_b0_capture_owner";
    case PlayerThreadAuthorityKind::C10SessionOwner:
        return "c10_session_owner";
    case PlayerThreadAuthorityKind::C10PostReturnOwner:
        return "c10_post_return_owner";
    case PlayerThreadAuthorityKind::RestartTransactionOwner:
        return "restart_transaction_owner";
    case PlayerThreadAuthorityKind::Unspecified:
    default:
        return "unspecified";
    }
}

const char* PlayerEnvironmentCallbackFailureName(
    PlayerEnvironmentCallbackFailure failure) noexcept
{
    switch (failure) {
    case PlayerEnvironmentCallbackFailure::None: return "none";
    case PlayerEnvironmentCallbackFailure::InvalidArgument:
        return "invalid_argument";
    case PlayerEnvironmentCallbackFailure::ContextUnavailable:
        return "context_unavailable";
    case PlayerEnvironmentCallbackFailure::WrongThread:
        return "wrong_thread";
    case PlayerEnvironmentCallbackFailure::UnsupportedImage:
        return "unsupported_image";
    case PlayerEnvironmentCallbackFailure::ShutdownOrQuiescing:
        return "shutdown_or_quiescing";
    case PlayerEnvironmentCallbackFailure::
            WindowsEnvironmentSnapshotRejected:
        return "windows_environment_snapshot_rejected";
    case PlayerEnvironmentCallbackFailure::
            WindowsEnvironmentSnapshotIncomplete:
        return "windows_environment_snapshot_incomplete";
    case PlayerEnvironmentCallbackFailure::InventorySnapshotRejected:
        return "inventory_snapshot_rejected";
    case PlayerEnvironmentCallbackFailure::InventoryObservationLoss:
        return "inventory_observation_loss";
    case PlayerEnvironmentCallbackFailure::LocalRepresentationUnreadable:
        return "local_representation_unreadable";
    case PlayerEnvironmentCallbackFailure::GlobalOwnerSetIncomplete:
        return "global_owner_set_incomplete";
    case PlayerEnvironmentCallbackFailure::CacheSnapshotRejected:
        return "cache_snapshot_rejected";
    case PlayerEnvironmentCallbackFailure::CacheSnapshotChanged:
        return "cache_snapshot_changed";
    case PlayerEnvironmentCallbackFailure::CacheOwnerChanged:
        return "cache_owner_changed";
    case PlayerEnvironmentCallbackFailure::InventoryRevisionChanged:
        return "inventory_revision_changed";
    default:
        return "unknown";
    }
}

const char* PlayerPostDestroyExactFailureName(
    PlayerPostDestroyExactFailure failure) noexcept
{
    switch (failure) {
    case PlayerPostDestroyExactFailure::None: return "none";
    case PlayerPostDestroyExactFailure::InvalidArgument:
        return "invalid_argument";
    case PlayerPostDestroyExactFailure::BaselineMissing:
        return "baseline_missing";
    case PlayerPostDestroyExactFailure::OperationsIncomplete:
        return "operations_incomplete";
    case PlayerPostDestroyExactFailure::InventoryCaptureFailed:
        return "inventory_capture_failed";
    case PlayerPostDestroyExactFailure::EnvironmentCallbackRejected:
        return "environment_callback_rejected";
    case PlayerPostDestroyExactFailure::UnsupportedImage:
        return "unsupported_image";
    case PlayerPostDestroyExactFailure::ShutdownOrQuiescing:
        return "shutdown_or_quiescing";
    case PlayerPostDestroyExactFailure::GameplayContainerUnreadable:
        return "gameplay_container_unreadable";
    case PlayerPostDestroyExactFailure::GameDiUnreadable:
        return "game_di_unreadable";
    case PlayerPostDestroyExactFailure::GlobalOwnerSlotsUnreadable:
        return "global_owner_slots_unreadable";
    case PlayerPostDestroyExactFailure::InventoryScanIncomplete:
        return "inventory_scan_incomplete";
    case PlayerPostDestroyExactFailure::CacheFingerprintUnavailable:
        return "cache_fingerprint_unavailable";
    case PlayerPostDestroyExactFailure::ThreadMismatch:
        return "thread_mismatch";
    case PlayerPostDestroyExactFailure::ActiveSessionStillPresent:
        return "active_session_still_present";
    case PlayerPostDestroyExactFailure::ActiveLevelStillPresent:
        return "active_level_still_present";
    case PlayerPostDestroyExactFailure::LocalPlayerStillPresent:
        return "local_player_still_present";
    case PlayerPostDestroyExactFailure::
            LiveGameplayConsumerStillPresent:
        return "live_gameplay_consumer_still_present";
    case PlayerPostDestroyExactFailure::
            EnvironmentChangedBetweenPasses:
        return "environment_changed_between_passes";
    case PlayerPostDestroyExactFailure::
            InventoryChangedBetweenPasses:
        return "inventory_changed_between_passes";
    case PlayerPostDestroyExactFailure::OwnerSetChangedBetweenPasses:
        return "owner_set_changed";
    case PlayerPostDestroyExactFailure::BaselineStateMissing:
        return "baseline_state_missing";
    case PlayerPostDestroyExactFailure::
            StateSnapshotCallbackRejected:
        return "state_snapshot_callback_rejected";
    case PlayerPostDestroyExactFailure::
            StateSnapshotSemanticIncomplete:
        return "state_snapshot_semantic_incomplete";
    case PlayerPostDestroyExactFailure::StateChangedBetweenPasses:
        return "state_changed_between_passes";
    default:
        return "unknown";
    }
}

const char* PlayerInventoryBootstrapFailureName(
    PlayerInventoryBootstrapFailure failure) noexcept
{
    switch (failure) {
    case PlayerInventoryBootstrapFailure::None: return "none";
    case PlayerInventoryBootstrapFailure::InvalidArgument:
        return "invalid_argument";
    case PlayerInventoryBootstrapFailure::UnsupportedImage:
        return "unsupported_image";
    case PlayerInventoryBootstrapFailure::NotBeforeOriginalInitializer:
        return "not_before_original_initializer";
    case PlayerInventoryBootstrapFailure::CreatorSetUnvalidated:
        return "creator_set_unvalidated";
    case PlayerInventoryBootstrapFailure::ConstructorRouteUnavailable:
        return "constructor_route_unavailable";
    case PlayerInventoryBootstrapFailure::
            CompleteDestructorRouteUnavailable:
        return "complete_destructor_route_unavailable";
    case PlayerInventoryBootstrapFailure::GlobalSlotsUnreadable:
        return "global_slots_unreadable";
    case PlayerInventoryBootstrapFailure::ExistingGlobalState:
        return "existing_global_state";
    case PlayerInventoryBootstrapFailure::HookActiveDuringBootstrap:
        return "hook_active_during_bootstrap";
    case PlayerInventoryBootstrapFailure::PrecoverageObservationLoss:
        return "precoverage_observation_loss";
    case PlayerInventoryBootstrapFailure::CoverageStartRejected:
        return "coverage_start_rejected";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart
