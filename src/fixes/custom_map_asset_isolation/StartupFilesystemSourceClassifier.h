#ifndef DLUMA_FIXES_STARTUP_FILESYSTEM_SOURCE_CLASSIFIER_H
#define DLUMA_FIXES_STARTUP_FILESYSTEM_SOURCE_CLASSIFIER_H

#include "FilesystemSourceInventory.h"
#include "FrontendBaseline.h"
#include "../../dlw_native_companion_source_owner.h"
#include "../../dlw_process_lifetime_sources.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::startupsource {

constexpr std::size_t kStartupWorkshopRootCapacity = 16;

enum class FilesystemSourceRuntimeType : std::uint8_t {
    Unknown = 0,
    Directory,
    Pack,
    NativeFallback,
};

enum class StartupSourceClassificationResult : std::uint8_t {
    Accepted = 0,
    PathNormalizationFailed,
    SourceIdentityUnavailable,
    SourceInspectionFailed,
    UnapprovedWorkshopSource,
    OutsideProcessLifetimeRoots,
    ProcessLifetimeRegistryUnavailable,
    ProcessLifetimeJoinNotUnique,
    ProcessLifetimeRecordIncomplete,
    ProcessLifetimeSourceTypeMismatch,
    ProcessLifetimeRegistryGenerationMismatch,
    ProcessLifetimeOwnerValidationFailed,
    ProcessLifetimeInventoryBindingMismatch,
    ProcessLifetimePathMismatch,
    LoaderMountRecordUnavailable,
    LoaderMountMetadataIncomplete,
    LoaderPathMismatch,
    LoaderSourceTypeMismatch,
    LoaderJoinNotUnique,
    LoaderBindFailed,
    LoaderBoundObjectMismatch,
    IdentityUnavailable,
    DuplicateIdentity,
    CapacityExceeded,
};

using InspectFilesystemSourceFn = bool (*)(
    void* user, std::uintptr_t source,
    std::uintptr_t* vtable,
    FilesystemSourceRuntimeType* type) noexcept;

using ValidateProcessLifetimeSourceOwnerFn =
    DlumaNativeCompanionOwnerValidationResult (*)(
    void* user,
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic) noexcept;

struct StartupSourceClassificationPolicy {
    char player_root[vfs::kFilesystemSourcePathCapacity];
    char game_install_root[vfs::kFilesystemSourcePathCapacity];
    char workshop_roots[kStartupWorkshopRootCapacity]
                       [vfs::kFilesystemSourcePathCapacity];
    const DlumaProcessLifetimeSourceSnapshot*
        process_lifetime_sources;
    InspectFilesystemSourceFn inspect_source;
    void* inspect_user;
    ValidateProcessLifetimeSourceOwnerFn
        validate_process_lifetime_owner;
    void* validate_process_lifetime_owner_user;
    std::uint64_t committed_registry_generation;
    std::uint32_t workshop_root_count;
};

struct StartupSourceClassificationRow {
    std::uintptr_t source;
    std::uintptr_t link;
    std::uintptr_t source_vtable;
    std::uint64_t identity_fingerprint;
    std::uint64_t provider_fingerprint;
    std::uint32_t list_ordinal;
    std::uint32_t flags;
    std::uint32_t process_lifetime_record_index;
    DlumaProcessLifetimeSourceOwnerKind owner_kind;
    cold_reboot::BaselineSourceKind proposed_kind;
    FilesystemSourceRuntimeType source_type;
    StartupSourceClassificationResult result;
    DlumaNativeCompanionOwnerValidationResult owner_validation_result;
    bool flags_known;
    bool native_fallback;
    bool exact_process_lifetime_join;
    bool path_corroborated;
    char canonical_path[vfs::kFilesystemSourcePathCapacity];
};

struct StartupSourceClassificationReport {
    StartupSourceClassificationRow
        rows[vfs::kFilesystemSourceInventoryCapacity];
    std::uint64_t loader_source_identity;
    std::uintptr_t loader_source;
    std::uintptr_t loader_link;
    std::uint32_t loader_list_ordinal;
    std::uint32_t row_count;
    std::uint32_t accepted_count;
    std::uint32_t rejected_count;
    std::uint32_t first_rejected_row;
    std::uint32_t loader_match_count;
    DlumaNativeCompanionOwnerValidationDiagnostic
        first_owner_validation_failure;
    bool first_owner_validation_failure_available;
    bool complete;
};

bool ClassifyStartupFilesystemSources(
    const vfs::FilesystemSourceInventory* inventory,
    const StartupSourceClassificationPolicy* policy,
    StartupSourceClassificationReport* output) noexcept;

const char* FilesystemSourceRuntimeTypeName(
    FilesystemSourceRuntimeType type) noexcept;
const char* StartupSourceClassificationResultName(
    StartupSourceClassificationResult result) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::startupsource

#endif
