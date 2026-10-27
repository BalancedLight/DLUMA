#ifndef DLUMA_MOD_MANIFEST_H
#define DLUMA_MOD_MANIFEST_H

#include "dlw_mod_owner.h"

#define DLUMA_MOD_MANIFEST_VERSION 1U
#define DLUMA_MOD_MANIFEST_MAX_BYTES (64U * 1024U)
#define DLUMA_MOD_LOCK_MAX_BYTES (1024U * 1024U)
#define DLUMA_MOD_MANIFEST_MAX_ACCESS 64U
#define DLUMA_MOD_MANIFEST_MAX_COMPANION 16U
#define DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS 32U
#define DLUMA_MOD_LOCK_MAX_ARTIFACTS 1024U
#define DLUMA_MOD_RELATIVE_PATH_CAPACITY 512U
#define DLUMA_MOD_SHA256_CAPACITY 65U

typedef enum DlumaModArtifactKind {
    DLUMA_MOD_ARTIFACT_MANIFEST = 1,
    DLUMA_MOD_ARTIFACT_ENTRY = 2,
    DLUMA_MOD_ARTIFACT_SCRIPT_MODULE = 3,
    DLUMA_MOD_ARTIFACT_ASSEMBLY = 4,
    DLUMA_MOD_ARTIFACT_DEPENDENCY = 5,
    DLUMA_MOD_ARTIFACT_CONFIGURATION = 6,
    DLUMA_MOD_ARTIFACT_COMPANION = 7,
    DLUMA_MOD_ARTIFACT_LICENSE = 8
} DlumaModArtifactKind;

typedef struct DlumaModCompanionDeclaration {
    char id[64];
    char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char audio_manifest[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    unsigned int required;
} DlumaModCompanionDeclaration;

typedef enum DlumaModHostFlags {
    DLUMA_MOD_HOST_PLAYER = 1U,
    DLUMA_MOD_HOST_EDITOR = 2U
} DlumaModHostFlags;

typedef struct DlumaModQuestPhaseHandlerDeclaration {
    char id[128];
    char command_alias[64];
    char description[256];
} DlumaModQuestPhaseHandlerDeclaration;

typedef struct DlumaModManifest {
    unsigned int struct_size;
    unsigned int struct_version;
    unsigned int language;
    unsigned int scope;
    int load_priority;
    char id[128];
    char name[128];
    char version[64];
    char description[1025];
    char author[128];
    char map[128];
    char entry[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char entry_type[256];
    unsigned int access_ids[DLUMA_MOD_MANIFEST_MAX_ACCESS];
    unsigned int access_count;
    char configuration_schema[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char configuration_defaults[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    DlumaModCompanionDeclaration
        companion[DLUMA_MOD_MANIFEST_MAX_COMPANION];
    unsigned int companion_count;
    unsigned int host_flags;
    unsigned int hosts_explicit;
    DlumaModQuestPhaseHandlerDeclaration quest_phase_handlers[
        DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS];
    unsigned int quest_phase_handler_count;
    char legacy_storage_id[128];
    char legacy_storage_fingerprint[DLUMA_MOD_SHA256_CAPACITY];
} DlumaModManifest;

typedef struct DlumaModLockedArtifact {
    char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    unsigned int kind;
    uint64_t size;
    char sha256[DLUMA_MOD_SHA256_CAPACITY];
} DlumaModLockedArtifact;

typedef struct DlumaModLock {
    unsigned int struct_size;
    unsigned int struct_version;
    char mod_id[128];
    char manifest_sha256[DLUMA_MOD_SHA256_CAPACITY];
    char catalog_sha256[DLUMA_MOD_SHA256_CAPACITY];
    DlumaModLockedArtifact* artifacts;
    unsigned int artifact_capacity;
    unsigned int artifact_count;
    unsigned int uses_previous_catalog;
} DlumaModLock;

typedef struct DlumaModManifestDiagnostic {
    DlumaStatus status;
    unsigned int byte_offset;
    char message[192];
} DlumaModManifestDiagnostic;

#ifdef __cplusplus
extern "C" {
#endif

DlumaStatus dluma_mod_manifest_parse(
    const void* bytes,
    unsigned int size,
    DlumaModManifest* output,
    DlumaModManifestDiagnostic* diagnostic);

DlumaStatus dluma_mod_lock_parse(
    const void* bytes,
    unsigned int size,
    DlumaModLock* output,
    DlumaModManifestDiagnostic* diagnostic);

int dluma_mod_relative_path_validate_and_normalize(
    const char* value,
    char* output,
    unsigned int output_capacity);

int dluma_mod_manifest_expected_suffix(
    const DlumaModManifest* manifest,
    const char* suffix);

int dluma_mod_manifest_has_access(
    const DlumaModManifest* manifest,
    unsigned int access_id);

int dluma_mod_manifest_declares_quest_phase_handler(
    const DlumaModManifest* manifest,
    const char* handler_id);

int dluma_mod_lock_catalog_compatible(
    const DlumaModLock* lock,
    const DlumaModManifest* manifest);

#ifdef __cplusplus
}
#endif

#endif
