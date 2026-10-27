#include "dlw_dual_host.h"

#define DLUMA_EDITOR_TIMESTAMP 0x623396E1UL
#define DLUMA_EDITOR_SIZE_OF_IMAGE 0x01E4A000UL
#define DLUMA_EDITOR_SHA256 \
    "CACD9C96BA5D8C35A23170DC8BE8029DB9AA5E6223F5E6CD5DE3E61D8E72DA28"
#define DLUMA_PLAYER_TIMESTAMP 0x623397B8UL
#define DLUMA_PLAYER_SIZE_OF_IMAGE 0x00103000UL
#define DLUMA_PLAYER_SHA256 \
    "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A"

static void dluma_dual_zero(void* memory, unsigned int size)
{
    unsigned char* cursor = (unsigned char*)memory;
    while (cursor && size--) *cursor++ = 0;
}

static unsigned int dluma_dual_length(const char* value)
{
    unsigned int size = 0;
    if (!value) return 0;
    while (value[size]) ++size;
    return size;
}

static int dluma_dual_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static int dluma_dual_fold_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    for (;;) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
        if (!a) return 1;
        ++index;
    }
}

static int dluma_dual_copy(
    char* output,
    unsigned int capacity,
    const char* value)
{
    unsigned int size = dluma_dual_length(value);
    unsigned int index;
    if (!output || !capacity || !value || size + 1U > capacity) return 0;
    for (index = 0; index < size; ++index) output[index] = value[index];
    output[size] = 0;
    return 1;
}

static int dluma_dual_sha256(const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0; index < 64U; ++index) {
        char c = value[index];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return value[64] == 0;
}

static const char* dluma_dual_basename(const char* path)
{
    const char* result = path;
    if (!path) return 0;
    while (*path) {
        if (*path == '\\' || *path == '/') result = path + 1;
        ++path;
    }
    return result;
}

static int dluma_dual_profile(
    const DlumaHostEvidence* evidence,
    const char* basename,
    uint32_t timestamp,
    uint32_t image_size,
    const char* sha256)
{
    const char* actual_basename;
    if (!evidence || !evidence->canonical_image_verified ||
        !evidence->canonical_image_utf8[0]) return 0;
    actual_basename = dluma_dual_basename(evidence->canonical_image_utf8);
    return actual_basename && dluma_dual_fold_equal(actual_basename, basename) &&
        evidence->image.timestamp == timestamp &&
        evidence->image.size_of_image == image_size &&
        dluma_dual_fold_equal(evidence->image.sha256, sha256);
}

int dluma_dual_host_is_editor_identity(
    const char* canonical_image_utf8,
    const DlumaPeIdentity* image)
{
    DlumaHostEvidence evidence;
    if (!canonical_image_utf8 || !image) return 0;
    dluma_dual_zero(&evidence, (unsigned int)sizeof(evidence));
    evidence.canonical_image_verified = 1U;
    if (!dluma_dual_copy(
            evidence.canonical_image_utf8,
            (unsigned int)sizeof(evidence.canonical_image_utf8),
            canonical_image_utf8))
        return 0;
    evidence.image = *image;
    return dluma_dual_profile(
        &evidence, "DyingLightEditor.exe", DLUMA_EDITOR_TIMESTAMP,
        DLUMA_EDITOR_SIZE_OF_IMAGE, DLUMA_EDITOR_SHA256);
}

int dluma_playtest_handoff_validate(
    const DlumaPlaytestHandoff* handoff,
    const DlumaHostEvidence* child_evidence)
{
    if (!handoff || !child_evidence ||
        handoff->struct_size < sizeof(*handoff) ||
        handoff->schema_version != 1U || !handoff->parent_pid ||
        !handoff->parent_start_time || !handoff->generation ||
        !child_evidence->parent_alive ||
        !child_evidence->parent_editor_profile_verified ||
        child_evidence->parent_pid != handoff->parent_pid ||
        child_evidence->parent_start_time != handoff->parent_start_time ||
        handoff->created_ms > child_evidence->now_ms ||
        handoff->expires_ms < child_evidence->now_ms ||
        handoff->expires_ms < handoff->created_ms ||
        handoff->expires_ms - handoff->created_ms > DLUMA_HANDOFF_EXPIRY_MS ||
        handoff->approval_count > 256U ||
        handoff->sidecar_count > DLUMA_HANDOFF_MAX_SIDECARS ||
        !dluma_dual_sha256(handoff->command_line_sha256) ||
        !dluma_dual_sha256(handoff->loader_sha256) ||
        !dluma_dual_sha256(handoff->inventory_sha256) ||
        !dluma_dual_equal(
            handoff->command_line_sha256,
            child_evidence->command_line_sha256) ||
        !dluma_dual_equal(handoff->loader_sha256,
                        child_evidence->loader_sha256) ||
        !handoff->project_root_utf8[0] || !handoff->project_name[0] ||
        !handoff->map_name[0])
        return 0;
    return 1;
}

int dluma_dual_host_classify(
    const DlumaHostEvidence* evidence,
    DlumaHostClassification* output)
{
    int editor;
    int player;
    if (!output) return 0;
    dluma_dual_zero(output, (unsigned int)sizeof(*output));
    output->kind = DLUMA_HOST_UNKNOWN;
    output->forwarding_only = 1U;
    (void)dluma_dual_copy(output->reason, sizeof(output->reason),
                        "unknown executable profile");
    if (!evidence || evidence->struct_size < sizeof(*evidence)) return 1;
    if (!dluma_dual_sha256(evidence->command_line_sha256)) {
        (void)dluma_dual_copy(output->reason, sizeof(output->reason),
                            "command-line evidence is unavailable");
        return 1;
    }
    editor = dluma_dual_profile(
        evidence, "DyingLightEditor.exe", DLUMA_EDITOR_TIMESTAMP,
        DLUMA_EDITOR_SIZE_OF_IMAGE, DLUMA_EDITOR_SHA256);
    player = dluma_dual_profile(
        evidence, "DyingLightPlayer.exe", DLUMA_PLAYER_TIMESTAMP,
        DLUMA_PLAYER_SIZE_OF_IMAGE, DLUMA_PLAYER_SHA256);
    if (editor) {
        output->kind = DLUMA_HOST_CHROME_EDITOR;
        output->exact_profile = 1U;
        output->forwarding_only = 0U;
        (void)dluma_dual_copy(output->reason, sizeof(output->reason),
                            "validated Chrome Editor profile");
        return 1;
    }
    if (!player) return 1;
    output->exact_profile = 1U;
    if (evidence->has_editor_debugconf) {
        if (evidence->has_editor_debugconf && evidence->selected_loader &&
            dluma_playtest_handoff_validate(evidence->handoff, evidence)) {
            output->kind = DLUMA_HOST_PLAYER_EDITOR_PLAYTEST;
            output->forwarding_only = 0U;
            output->handoff_valid = 1U;
            (void)dluma_dual_copy(output->reason, sizeof(output->reason),
                                "validated Editor playtest handoff");
        } else {
            (void)dluma_dual_copy(output->reason, sizeof(output->reason),
                                "Editor Player launch lacks a valid handoff");
        }
        return 1;
    }
    output->kind = DLUMA_HOST_PLAYER_STANDALONE;
    output->forwarding_only = 0U;
    (void)dluma_dual_copy(output->reason, sizeof(output->reason),
                        "validated standalone Player profile");
    return 1;
}

void dluma_playtest_handoff_store_initialize(DlumaPlaytestHandoffStore* store)
{
    if (!store) return;
    dluma_dual_zero(store, (unsigned int)sizeof(*store));
}

int dluma_playtest_handoff_publish(
    DlumaPlaytestHandoffStore* store,
    const DlumaPlaytestHandoff* record)
{
    unsigned int slot;
    if (!store || !record || record->struct_size < sizeof(*record) ||
        record->schema_version != 1U || !record->generation ||
        record->expires_ms < record->created_ms ||
        record->expires_ms - record->created_ms > DLUMA_HANDOFF_EXPIRY_MS ||
        record->sidecar_count > DLUMA_HANDOFF_MAX_SIDECARS ||
        !dluma_dual_sha256(record->command_line_sha256) ||
        !dluma_dual_sha256(record->loader_sha256) ||
        !dluma_dual_sha256(record->inventory_sha256)) return 0;
    slot = (unsigned int)(store->next_slot % DLUMA_HANDOFF_MAX_RECORDS);
    ++store->revision;
    store->records[slot] = *record;
    ++store->next_slot;
    ++store->revision;
    return 1;
}

int dluma_playtest_handoff_find(
    const DlumaPlaytestHandoffStore* store,
    const DlumaHostEvidence* child_evidence,
    DlumaPlaytestHandoff* output)
{
    unsigned int index;
    const DlumaPlaytestHandoff* best = 0;
    if (!store || !child_evidence || !output || (store->revision & 1ULL))
        return 0;
    for (index = 0; index < DLUMA_HANDOFF_MAX_RECORDS; ++index) {
        const DlumaPlaytestHandoff* item = &store->records[index];
        if (!dluma_playtest_handoff_validate(item, child_evidence)) continue;
        if (!best || item->generation > best->generation) best = item;
    }
    if (!best) return 0;
    *output = *best;
    return !(store->revision & 1ULL);
}

void dluma_dual_host_lifecycle_initialize(
    DlumaDualHostLifecycle* lifecycle,
    DlumaHostKind host_kind)
{
    if (!lifecycle) return;
    dluma_dual_zero(lifecycle, (unsigned int)sizeof(*lifecycle));
    lifecycle->host_kind = host_kind;
    lifecycle->state = DLUMA_HOST_LIFECYCLE_IDLE;
}

DlumaHostLifecycleAction dluma_dual_host_lifecycle_begin_initialize(
    DlumaDualHostLifecycle* lifecycle)
{
    if (!lifecycle) return DLUMA_HOST_ACTION_REJECT;
    ++lifecycle->initialize_exports;
    if (lifecycle->state == DLUMA_HOST_LIFECYCLE_SESSION_ACTIVE ||
        lifecycle->state == DLUMA_HOST_LIFECYCLE_INITIALIZING)
        return DLUMA_HOST_ACTION_RETURN_ACTIVE_MANAGER;
    if (lifecycle->state != DLUMA_HOST_LIFECYCLE_IDLE)
        return DLUMA_HOST_ACTION_REJECT;
    if (lifecycle->host_kind != DLUMA_HOST_CHROME_EDITOR &&
        lifecycle->generation != 0U)
        return DLUMA_HOST_ACTION_REJECT;
    ++lifecycle->generation;
    lifecycle->state = DLUMA_HOST_LIFECYCLE_INITIALIZING;
    return DLUMA_HOST_ACTION_START_GENERATION;
}

int dluma_dual_host_lifecycle_original_loaded(
    DlumaDualHostLifecycle* lifecycle)
{
    if (!lifecycle || lifecycle->state != DLUMA_HOST_LIFECYCLE_INITIALIZING)
        return 0;
    ++lifecycle->original_loads;
    return 1;
}

int dluma_dual_host_lifecycle_original_initialized(
    DlumaDualHostLifecycle* lifecycle,
    int succeeded)
{
    if (!lifecycle || lifecycle->state != DLUMA_HOST_LIFECYCLE_INITIALIZING)
        return 0;
    ++lifecycle->original_initializes;
    if (succeeded) {
        lifecycle->state = DLUMA_HOST_LIFECYCLE_SESSION_ACTIVE;
        return 1;
    }
    lifecycle->state = lifecycle->host_kind == DLUMA_HOST_CHROME_EDITOR
        ? DLUMA_HOST_LIFECYCLE_IDLE : DLUMA_HOST_LIFECYCLE_TERMINAL;
    return 0;
}

DlumaHostLifecycleAction dluma_dual_host_lifecycle_begin_shutdown(
    DlumaDualHostLifecycle* lifecycle)
{
    if (!lifecycle) return DLUMA_HOST_ACTION_REJECT;
    ++lifecycle->shutdown_exports;
    if (lifecycle->state == DLUMA_HOST_LIFECYCLE_TERMINAL ||
        lifecycle->state == DLUMA_HOST_LIFECYCLE_IDLE)
        return DLUMA_HOST_ACTION_ALREADY_STOPPED;
    if (lifecycle->state != DLUMA_HOST_LIFECYCLE_SESSION_ACTIVE)
        return DLUMA_HOST_ACTION_REJECT;
    lifecycle->state = DLUMA_HOST_LIFECYCLE_QUIESCING;
    return DLUMA_HOST_ACTION_SHUTDOWN_GENERATION;
}

int dluma_dual_host_lifecycle_finish_shutdown(
    DlumaDualHostLifecycle* lifecycle,
    int callbacks_drained,
    int original_shutdown_called,
    int original_unloaded)
{
    if (!lifecycle || lifecycle->state != DLUMA_HOST_LIFECYCLE_QUIESCING)
        return 0;
    if (original_shutdown_called) ++lifecycle->original_shutdowns;
    if (original_unloaded) ++lifecycle->original_unloads;
    if (!callbacks_drained || !original_shutdown_called ||
        (lifecycle->host_kind == DLUMA_HOST_CHROME_EDITOR && !original_unloaded)) {
        lifecycle->dependency_graph_retained = 1U;
        lifecycle->module_pinned = 1U;
        lifecycle->state = DLUMA_HOST_LIFECYCLE_RETAINED_REJECT_REINIT;
        return 0;
    }
    lifecycle->state = lifecycle->host_kind == DLUMA_HOST_CHROME_EDITOR
        ? DLUMA_HOST_LIFECYCLE_IDLE : DLUMA_HOST_LIFECYCLE_TERMINAL;
    return 1;
}
