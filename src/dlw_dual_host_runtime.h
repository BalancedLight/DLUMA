#ifndef DLUMA_DUAL_HOST_RUNTIME_H
#define DLUMA_DUAL_HOST_RUNTIME_H

#include "dlw_common.h"
#include "core/dlw_dual_host.h"

#define DLUMA_HANDOFF_DISK_MAGIC "DLWHV1\0"
#define DLUMA_HANDOFF_MAX_APPROVALS 256U
#define DLUMA_HANDOFF_APPROVAL_ID_CAPACITY 128U
#define DLUMA_HANDOFF_SIDECAR_PATH_CAPACITY 1024U

typedef struct DlumaHandoffApprovalRecord {
    char mod_id[DLUMA_HANDOFF_APPROVAL_ID_CAPACITY];
    char language[16];
    char approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char artifact_sha256[DLUMA_SHA256_HEX_CAPACITY];
} DlumaHandoffApprovalRecord;

typedef struct DlumaHandoffSidecarRecord {
    char canonical_path_utf8[DLUMA_HANDOFF_SIDECAR_PATH_CAPACITY];
    char sidecar_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char compiled_xml_sha256[DLUMA_SHA256_HEX_CAPACITY];
} DlumaHandoffSidecarRecord;

typedef struct DlumaHandoffDiskHeader {
    char magic[8];
    unsigned int header_size;
    unsigned int payload_size;
    DlumaPlaytestHandoff handoff;
} DlumaHandoffDiskHeader;

typedef struct DlumaDualHostRuntimeSnapshot {
    DlumaHostClassification classification;
    DlumaHostEvidence evidence;
    DlumaPlaytestHandoff handoff;
    WCHAR executable_path[DLUMA_MAX_WPATH];
    WCHAR command_line[DLUMA_MAX_WPATH * 2U];
    ULONGLONG process_start_time;
    unsigned int initialized;
    unsigned int module_pinned;
} DlumaDualHostRuntimeSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

int dluma_dual_host_runtime_initialize(void);
const DlumaDualHostRuntimeSnapshot* dluma_dual_host_runtime_snapshot(void);
DlumaHostKind dluma_dual_host_runtime_kind(void);
int dluma_dual_host_runtime_forwarding_only(void);
int dluma_dual_host_runtime_is_editor(void);
int dluma_dual_host_runtime_is_playtest(void);
int dluma_dual_host_runtime_pin_editor_module(void);

int dluma_dual_host_runtime_handoff_directory(
    WCHAR* output, unsigned int capacity, int create);
int dluma_dual_host_runtime_publish_handoff_file(
    const DlumaPlaytestHandoff* handoff,
    const DlumaHandoffApprovalRecord* approvals,
    unsigned int approval_count,
    const DlumaHandoffSidecarRecord* sidecars,
    unsigned int sidecar_count,
    WCHAR* published_path,
    unsigned int published_path_capacity);

#ifdef __cplusplus
}
#endif

#endif
