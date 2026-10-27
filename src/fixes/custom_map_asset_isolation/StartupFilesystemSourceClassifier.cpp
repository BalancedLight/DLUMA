#define _HAS_EXCEPTIONS 0

#include "StartupFilesystemSourceClassifier.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::startupsource {
namespace {

using cold_reboot::BaselineSourceKind;

char Lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool TextEqual(const char* left, const char* right) noexcept
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (Lower(*left) != Lower(*right)) return false;
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

bool NormalizePath(const char* input, char* output,
                   std::size_t capacity) noexcept
{
    return capacity <= UINT32_MAX &&
        dluma_process_lifetime_source_normalize_path(
            input, output, static_cast<unsigned int>(capacity)) != 0;
}

bool StartsWith(const char* value, const char* prefix) noexcept
{
    if (!value || !prefix) return false;
    while (*prefix) {
        if (Lower(*value++) != Lower(*prefix++)) return false;
    }
    return true;
}

bool PathWithin(const char* path, const char* root) noexcept
{
    char normalized_path[vfs::kFilesystemSourcePathCapacity] = {};
    char normalized_root[vfs::kFilesystemSourcePathCapacity] = {};
    if (!NormalizePath(path, normalized_path, sizeof(normalized_path)) ||
        !NormalizePath(root, normalized_root, sizeof(normalized_root)))
        return false;
    std::size_t root_length = 0;
    while (normalized_root[root_length]) ++root_length;
    return StartsWith(normalized_path, normalized_root) &&
        (normalized_path[root_length] == 0 ||
         normalized_path[root_length] == '\\');
}

bool AppendChild(const char* root, const char* child,
                 char* output, std::size_t capacity) noexcept
{
    if (!root || !root[0] || !child || !child[0] ||
        !output || !capacity)
        return false;
    std::size_t length = 0;
    while (root[length]) {
        if (length + 1U >= capacity) return false;
        output[length] = Lower(root[length]);
        ++length;
    }
    if (length && output[length - 1U] != '\\') {
        if (length + 1U >= capacity) return false;
        output[length++] = '\\';
    }
    std::size_t child_index = 0;
    while (child[child_index]) {
        if (length + 1U >= capacity) return false;
        output[length++] = Lower(child[child_index++]);
    }
    output[length] = 0;
    return true;
}

bool PathWithinChild(const char* path, const char* root,
                     const char* child) noexcept
{
    char child_root[vfs::kFilesystemSourcePathCapacity] = {};
    return AppendChild(root, child, child_root, sizeof(child_root)) &&
        PathWithin(path, child_root);
}

bool DlcChild(const char* path, const char* root) noexcept
{
    if (!path || !root || !root[0] || !PathWithin(path, root))
        return false;
    char normalized_path[vfs::kFilesystemSourcePathCapacity] = {};
    char normalized_root[vfs::kFilesystemSourcePathCapacity] = {};
    if (!NormalizePath(path, normalized_path, sizeof(normalized_path)) ||
        !NormalizePath(root, normalized_root, sizeof(normalized_root)))
        return false;
    std::size_t root_length = 0;
    while (normalized_root[root_length]) ++root_length;
    const char* relative = normalized_path + root_length;
    if (*relative == '\\') ++relative;
    static const char prefix[] = "dw_dlc";
    for (std::size_t index = 0; prefix[index]; ++index)
        if (relative[index] != prefix[index]) return false;
    const char suffix = relative[sizeof(prefix) - 1U];
    return suffix == '\\' || suffix == '_' ||
        (suffix >= '0' && suffix <= '9');
}

bool Contains(const char* text, const char* needle) noexcept
{
    if (!text || !needle || !needle[0]) return false;
    for (std::size_t begin = 0; text[begin]; ++begin) {
        std::size_t index = 0;
        while (needle[index] && text[begin + index] &&
               Lower(text[begin + index]) == Lower(needle[index]))
            ++index;
        if (!needle[index]) return true;
    }
    return false;
}

bool ParentPath(const char* path, char* output,
                std::size_t capacity) noexcept
{
    if (!path || !output || capacity < 2U) return false;
    std::size_t length = 0;
    std::size_t last_separator = 0;
    bool found = false;
    while (path[length]) {
        if (length + 1U >= capacity) return false;
        output[length] = path[length];
        if (path[length] == '\\') {
            last_separator = length;
            found = true;
        }
        ++length;
    }
    if (!found || !last_separator) return false;
    output[last_separator] = 0;
    return true;
}

bool MountPathCorroborates(
    const DlumaProcessLifetimeSourceRecord& record,
    const char* observed_normalized) noexcept
{
    char requested[vfs::kFilesystemSourcePathCapacity] = {};
    if (!NormalizePath(
            record.requested_mount_path, requested,
            sizeof(requested)))
        return false;
    if (TextEqual(requested, observed_normalized))
        return true;
    if (record.runtime_source_type !=
            DLUMA_PROCESS_SOURCE_RUNTIME_PACK)
        return false;
    char parent[vfs::kFilesystemSourcePathCapacity] = {};
    return ParentPath(requested, parent, sizeof(parent)) &&
        TextEqual(parent, observed_normalized);
}

std::uint64_t FingerprintText(const char* text) noexcept
{
    if (!text || !text[0]) return 0;
    std::uint64_t hash = 1469598103934665603ULL;
    std::size_t index = 0;
    while (text[index] &&
           index < DLUMA_PROCESS_SOURCE_PATH_CAPACITY) {
        hash ^= static_cast<std::uint8_t>(Lower(text[index++]));
        hash *= 1099511628211ULL;
    }
    return text[index] == 0 && hash ? hash : 0;
}

std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) noexcept
{
    if (!hash) return 0;
    for (unsigned int shift = 0; shift < 64U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1U;
}

bool IdentitySeen(const StartupSourceClassificationReport& report,
                  std::uint64_t identity) noexcept
{
    for (std::uint32_t index = 0; index < report.row_count; ++index)
        if (report.rows[index].result ==
                StartupSourceClassificationResult::Accepted &&
            report.rows[index].identity_fingerprint == identity)
            return true;
    return false;
}

void Reject(StartupSourceClassificationReport* report,
            StartupSourceClassificationRow* row,
            StartupSourceClassificationResult result) noexcept
{
    if (!report || !row) return;
    row->result = result;
    ++report->rejected_count;
    if (report->first_rejected_row == UINT32_MAX)
        report->first_rejected_row =
            static_cast<std::uint32_t>(row - report->rows);
}

BaselineSourceKind OwnerKind(
    DlumaProcessLifetimeSourceOwnerKind owner_kind) noexcept
{
    switch (owner_kind) {
    case DLUMA_SOURCE_OWNER_LOADER_PAK:
        return BaselineSourceKind::LoaderPak;
    case DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE:
    case DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS:
    case DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU:
    case DLUMA_SOURCE_OWNER_ASSET_MOD:
    case DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK:
        return BaselineSourceKind::ApprovedGlobal;
    default:
        return BaselineSourceKind::Unknown;
    }
}

BaselineSourceKind ClassifyOrdinaryKind(
    const StartupSourceClassificationPolicy& policy,
    const vfs::FilesystemSourceInventoryRecord& observed,
    const char* normalized,
    StartupSourceClassificationResult* rejection) noexcept
{
    if (rejection)
        *rejection =
            StartupSourceClassificationResult::OutsideProcessLifetimeRoots;
    if (observed.native_fallback ||
        TextEqual(normalized, "filesystem:native-fallback"))
        return BaselineSourceKind::Base;
    for (std::uint32_t index = 0;
         index < policy.workshop_root_count; ++index)
        if (PathWithin(normalized, policy.workshop_roots[index])) {
            if (rejection)
                *rejection = StartupSourceClassificationResult::
                    UnapprovedWorkshopSource;
            return BaselineSourceKind::Unknown;
        }
    if (PathWithinChild(normalized, policy.player_root, "workshop")) {
        if (rejection)
            *rejection = StartupSourceClassificationResult::
                UnapprovedWorkshopSource;
        return BaselineSourceKind::Unknown;
    }
    const bool base_tree =
        PathWithinChild(normalized, policy.player_root, "dw") ||
        PathWithinChild(normalized, policy.player_root, "engine") ||
        DlcChild(normalized, policy.player_root) ||
        PathWithinChild(normalized, policy.game_install_root, "dw") ||
        PathWithinChild(normalized, policy.game_install_root, "engine") ||
        DlcChild(normalized, policy.game_install_root);
    if (!base_tree)
        return BaselineSourceKind::Unknown;
    if (Contains(normalized, "speech") ||
        Contains(normalized, "locale"))
        return BaselineSourceKind::LocaleSpeech;
    if (Contains(normalized, "\\dw_dlc") ||
        Contains(normalized, "\\dlc"))
        return BaselineSourceKind::Dlc;
    return BaselineSourceKind::Base;
}

bool RuntimeTypeMatches(
    FilesystemSourceRuntimeType observed,
    unsigned int registered) noexcept
{
    return
        (observed == FilesystemSourceRuntimeType::Directory &&
         registered == DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY) ||
        (observed == FilesystemSourceRuntimeType::Pack &&
         registered == DLUMA_PROCESS_SOURCE_RUNTIME_PACK);
}

}  // namespace

bool ClassifyStartupFilesystemSources(
    const vfs::FilesystemSourceInventory* inventory,
    const StartupSourceClassificationPolicy* policy,
    StartupSourceClassificationReport* output) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    output->first_rejected_row = UINT32_MAX;
    if (!inventory || !policy || !inventory->complete ||
        !inventory->source_count ||
        inventory->source_count >
            vfs::kFilesystemSourceInventoryCapacity ||
        policy->workshop_root_count >
            kStartupWorkshopRootCapacity ||
        !policy->process_lifetime_sources ||
        !policy->process_lifetime_sources->complete ||
        policy->process_lifetime_sources->count >
            DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY ||
        !policy->committed_registry_generation ||
        inventory->registry_generation !=
            policy->committed_registry_generation) {
        output->rejected_count = 1;
        output->first_rejected_row = 0;
        return false;
    }

    const DlumaProcessLifetimeSourceSnapshot& mounts =
        *policy->process_lifetime_sources;
    bool joined[DLUMA_PROCESS_LIFETIME_SOURCE_CAPACITY] = {};
    std::uint32_t loader_record_count = 0;
    for (std::uint32_t index = 0; index < mounts.count; ++index)
        if (mounts.records[index].lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_RETIRED &&
            mounts.records[index].owner_kind ==
                DLUMA_SOURCE_OWNER_LOADER_PAK)
            ++loader_record_count;

    output->row_count = inventory->source_count;
    for (std::uint32_t index = 0;
         index < inventory->source_count; ++index) {
        const vfs::FilesystemSourceInventoryRecord& observed =
            inventory->sources[index];
        StartupSourceClassificationRow* row = &output->rows[index];
        row->source = observed.source;
        row->link = observed.link;
        row->list_ordinal = observed.list_ordinal;
        row->native_fallback = observed.native_fallback;
        row->result = StartupSourceClassificationResult::Accepted;
        row->owner_validation_result =
            DLUMA_COMPANION_ARCHIVE_OWNER_VALID;
        row->process_lifetime_record_index = UINT32_MAX;
        row->source_type = observed.native_fallback
            ? FilesystemSourceRuntimeType::NativeFallback
            : FilesystemSourceRuntimeType::Unknown;
        if (!observed.source || !observed.link ||
            !observed.list_ordinal) {
            Reject(output, row, StartupSourceClassificationResult::
                SourceIdentityUnavailable);
            continue;
        }
        if (policy->inspect_source && !observed.native_fallback &&
            (!policy->inspect_source(
                 policy->inspect_user, observed.source,
                 &row->source_vtable, &row->source_type) ||
             !row->source_vtable)) {
            Reject(output, row, StartupSourceClassificationResult::
                SourceInspectionFailed);
            continue;
        }
        if (observed.native_fallback &&
            TextEqual(observed.path, "filesystem:native-fallback")) {
            std::memcpy(
                row->canonical_path, observed.path,
                sizeof("filesystem:native-fallback"));
        } else if (!NormalizePath(
                       observed.path, row->canonical_path,
                       sizeof(row->canonical_path))) {
            Reject(output, row, StartupSourceClassificationResult::
                PathNormalizationFailed);
            continue;
        }

        const DlumaProcessLifetimeSourceRecord* mount = nullptr;
        std::uint32_t mount_index = UINT32_MAX;
        std::uint32_t mount_matches = 0;
        for (std::uint32_t mount_cursor = 0;
             mount_cursor < mounts.count; ++mount_cursor) {
            if (mounts.records[mount_cursor].lifecycle_state ==
                    DLUMA_PROCESS_SOURCE_STATE_RETIRED ||
                mounts.records[mount_cursor].source_object !=
                    reinterpret_cast<void*>(observed.source))
                continue;
            mount = &mounts.records[mount_cursor];
            mount_index = mount_cursor;
            ++mount_matches;
        }
        if (mount_matches > 1U) {
            for (std::uint32_t mount_cursor = 0;
                 mount_cursor < mounts.count; ++mount_cursor)
                if (mounts.records[mount_cursor].lifecycle_state !=
                        DLUMA_PROCESS_SOURCE_STATE_RETIRED &&
                    mounts.records[mount_cursor].source_object ==
                        reinterpret_cast<void*>(observed.source) &&
                    mounts.records[mount_cursor].owner_kind ==
                        DLUMA_SOURCE_OWNER_LOADER_PAK)
                    ++output->loader_match_count;
            Reject(output, row, StartupSourceClassificationResult::
                ProcessLifetimeJoinNotUnique);
            continue;
        }

        if (mount) {
            row->process_lifetime_record_index = mount_index;
            row->owner_kind = mount->owner_kind;
            row->proposed_kind = OwnerKind(mount->owner_kind);
            row->flags = mount->source_flags;
            row->flags_known = true;
            if (!mount->metadata_complete ||
                !mount->mount_generation ||
                !mount->owner_id[0] ||
                row->proposed_kind == BaselineSourceKind::Unknown) {
                Reject(output, row, StartupSourceClassificationResult::
                    ProcessLifetimeRecordIncomplete);
                continue;
            }
            if (!RuntimeTypeMatches(
                    row->source_type, mount->runtime_source_type)) {
                Reject(output, row, mount->owner_kind ==
                        DLUMA_SOURCE_OWNER_LOADER_PAK
                    ? StartupSourceClassificationResult::
                        LoaderSourceTypeMismatch
                    : StartupSourceClassificationResult::
                        ProcessLifetimeSourceTypeMismatch);
                continue;
            }
            if (!mount->exact_source_joined ||
                mount->intrusive_link !=
                    reinterpret_cast<void*>(observed.link) ||
                mount->list_ordinal != observed.list_ordinal) {
                Reject(output, row, StartupSourceClassificationResult::
                    ProcessLifetimeInventoryBindingMismatch);
                continue;
            }
            if (mount->registry_generation !=
                    policy->committed_registry_generation) {
                Reject(output, row, StartupSourceClassificationResult::
                    ProcessLifetimeRegistryGenerationMismatch);
                continue;
            }
            if (policy->validate_process_lifetime_owner) {
                DlumaNativeCompanionOwnerValidationDiagnostic
                    owner_diagnostic = {};
                row->owner_validation_result =
                    policy->validate_process_lifetime_owner(
                        policy->validate_process_lifetime_owner_user,
                        mount, &owner_diagnostic);
                owner_diagnostic.result =
                    row->owner_validation_result;
                if (row->owner_validation_result !=
                        DLUMA_COMPANION_ARCHIVE_OWNER_VALID) {
                    if (!output->
                            first_owner_validation_failure_available) {
                        output->first_owner_validation_failure =
                            owner_diagnostic;
                        output->
                            first_owner_validation_failure_available =
                                true;
                    }
                    Reject(
                        output, row,
                        StartupSourceClassificationResult::
                            ProcessLifetimeOwnerValidationFailed);
                    continue;
                }
            }
            row->path_corroborated =
                MountPathCorroborates(*mount, row->canonical_path);
            if (!row->path_corroborated) {
                Reject(output, row, mount->owner_kind ==
                        DLUMA_SOURCE_OWNER_LOADER_PAK
                    ? StartupSourceClassificationResult::
                        LoaderPathMismatch
                    : StartupSourceClassificationResult::
                        ProcessLifetimePathMismatch);
                continue;
            }
            row->exact_process_lifetime_join = true;
            joined[mount_index] = true;
            if (mount->owner_kind ==
                    DLUMA_SOURCE_OWNER_LOADER_PAK)
                ++output->loader_match_count;
        } else {
            StartupSourceClassificationResult rejection =
                StartupSourceClassificationResult::
                    OutsideProcessLifetimeRoots;
            row->proposed_kind = ClassifyOrdinaryKind(
                *policy, observed, row->canonical_path, &rejection);
            if (row->proposed_kind == BaselineSourceKind::Unknown) {
                Reject(output, row, rejection);
                continue;
            }
        }

        char provider_path[
            vfs::kFilesystemSourcePathCapacity] = {};
        if (mount) {
            if (!NormalizePath(
                    mount->requested_mount_path, provider_path,
                    sizeof(provider_path))) {
                Reject(output, row, StartupSourceClassificationResult::
                    IdentityUnavailable);
                continue;
            }
        } else {
            std::memcpy(
                provider_path, row->canonical_path,
                sizeof(provider_path));
        }
        row->provider_fingerprint =
            FingerprintText(provider_path);
        std::uint64_t identity = Mix(
            row->provider_fingerprint,
            static_cast<std::uint64_t>(row->proposed_kind));
        identity = Mix(identity, row->source);
        identity = Mix(identity, row->link);
        identity = Mix(identity, row->list_ordinal);
        identity = Mix(identity, row->source_vtable);
        identity = Mix(
            identity, static_cast<std::uint64_t>(row->source_type));
        if (row->flags_known)
            identity = Mix(identity, row->flags);
        if (mount) {
            identity = Mix(identity, mount->owner_kind);
            identity = Mix(
                identity, FingerprintText(mount->owner_id));
            identity = Mix(identity, mount->mount_generation);
            identity = Mix(identity, mount->registry_generation);
            identity = Mix(identity, mount->file_size);
            identity = Mix(identity, mount->volume_serial);
            identity = Mix(identity, mount->file_index_high);
            identity = Mix(identity, mount->file_index_low);
            identity = Mix(
                identity,
                FingerprintText(mount->content_sha256));
            identity = Mix(
                identity,
                FingerprintText(mount->approval_fingerprint));
            identity = Mix(
                identity,
                FingerprintText(mount->manifest_fingerprint));
            if (mount->owner_kind ==
                    DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK) {
                identity = Mix(
                    identity,
                    mount->native_admission.admission_generation);
                identity = Mix(
                    identity,
                    mount->native_admission.
                        native_registry_publication_generation);
                identity = Mix(
                    identity,
                    FingerprintText(
                        mount->native_admission.stable_candidate_id));
                identity = Mix(
                    identity,
                    FingerprintText(
                        mount->native_admission.module_sha256));
            }
        }
        if (!row->provider_fingerprint || !identity) {
            Reject(output, row, StartupSourceClassificationResult::
                IdentityUnavailable);
            continue;
        }
        if (IdentitySeen(*output, identity)) {
            Reject(output, row, StartupSourceClassificationResult::
                DuplicateIdentity);
            continue;
        }
        row->identity_fingerprint = identity;
        ++output->accepted_count;
        if (row->owner_kind ==
                DLUMA_SOURCE_OWNER_LOADER_PAK) {
            output->loader_source_identity = identity;
            output->loader_source = row->source;
            output->loader_link = row->link;
            output->loader_list_ordinal = row->list_ordinal;
        }
    }

    bool every_mount_joined = true;
    for (std::uint32_t index = 0; index < mounts.count; ++index)
        if (mounts.records[index].lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_RETIRED &&
            !joined[index])
            every_mount_joined = false;
    if (loader_record_count != 1U ||
        output->loader_match_count != 1U) {
        for (std::uint32_t index = 0;
             index < output->row_count; ++index) {
            StartupSourceClassificationRow* row =
                &output->rows[index];
            if (row->owner_kind !=
                    DLUMA_SOURCE_OWNER_LOADER_PAK ||
                row->result !=
                    StartupSourceClassificationResult::Accepted)
                continue;
            if (output->accepted_count) --output->accepted_count;
            Reject(output, row, StartupSourceClassificationResult::
                LoaderJoinNotUnique);
            row->identity_fingerprint = 0;
        }
        output->loader_source_identity = 0;
        output->loader_source = 0;
        output->loader_link = 0;
        output->loader_list_ordinal = 0;
    }
    if (!every_mount_joined && output->rejected_count == 0) {
        output->rejected_count = 1;
        output->first_rejected_row = 0;
    }

    output->complete =
        output->row_count == inventory->source_count &&
        output->accepted_count == output->row_count &&
        output->rejected_count == 0 &&
        every_mount_joined &&
        loader_record_count == 1U &&
        output->loader_match_count == 1U &&
        output->loader_source_identity != 0;
    return output->complete;
}

const char* FilesystemSourceRuntimeTypeName(
    FilesystemSourceRuntimeType type) noexcept
{
    switch (type) {
    case FilesystemSourceRuntimeType::Unknown: return "unknown";
    case FilesystemSourceRuntimeType::Directory: return "directory";
    case FilesystemSourceRuntimeType::Pack: return "pack";
    case FilesystemSourceRuntimeType::NativeFallback:
        return "native_fallback";
    default: return "invalid";
    }
}

const char* StartupSourceClassificationResultName(
    StartupSourceClassificationResult result) noexcept
{
    switch (result) {
    case StartupSourceClassificationResult::Accepted:
        return "accepted";
    case StartupSourceClassificationResult::PathNormalizationFailed:
        return "path_normalization_failed";
    case StartupSourceClassificationResult::SourceIdentityUnavailable:
        return "source_identity_unavailable";
    case StartupSourceClassificationResult::SourceInspectionFailed:
        return "source_inspection_failed";
    case StartupSourceClassificationResult::UnapprovedWorkshopSource:
        return "unapproved_workshop_source";
    case StartupSourceClassificationResult::OutsideProcessLifetimeRoots:
        return "outside_process_lifetime_roots";
    case StartupSourceClassificationResult::
             ProcessLifetimeRegistryUnavailable:
        return "process_lifetime_registry_unavailable";
    case StartupSourceClassificationResult::
             ProcessLifetimeJoinNotUnique:
        return "process_lifetime_join_not_unique";
    case StartupSourceClassificationResult::
             ProcessLifetimeRecordIncomplete:
        return "process_lifetime_record_incomplete";
    case StartupSourceClassificationResult::
             ProcessLifetimeSourceTypeMismatch:
        return "process_lifetime_source_type_mismatch";
    case StartupSourceClassificationResult::
             ProcessLifetimeRegistryGenerationMismatch:
        return "process_lifetime_registry_generation_mismatch";
    case StartupSourceClassificationResult::
             ProcessLifetimeOwnerValidationFailed:
        return "process_lifetime_owner_validation_failed";
    case StartupSourceClassificationResult::
             ProcessLifetimeInventoryBindingMismatch:
        return "process_lifetime_inventory_binding_mismatch";
    case StartupSourceClassificationResult::
             ProcessLifetimePathMismatch:
        return "process_lifetime_path_mismatch";
    case StartupSourceClassificationResult::LoaderMountRecordUnavailable:
        return "loader_mount_record_unavailable";
    case StartupSourceClassificationResult::
             LoaderMountMetadataIncomplete:
        return "loader_mount_metadata_incomplete";
    case StartupSourceClassificationResult::LoaderPathMismatch:
        return "loader_path_mismatch";
    case StartupSourceClassificationResult::LoaderSourceTypeMismatch:
        return "loader_source_type_mismatch";
    case StartupSourceClassificationResult::LoaderJoinNotUnique:
        return "loader_join_not_unique";
    case StartupSourceClassificationResult::LoaderBindFailed:
        return "loader_bind_failed";
    case StartupSourceClassificationResult::LoaderBoundObjectMismatch:
        return "loader_bound_object_mismatch";
    case StartupSourceClassificationResult::IdentityUnavailable:
        return "identity_unavailable";
    case StartupSourceClassificationResult::DuplicateIdentity:
        return "duplicate_identity";
    case StartupSourceClassificationResult::CapacityExceeded:
        return "capacity_exceeded";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::startupsource
