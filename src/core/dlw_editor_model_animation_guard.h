#ifndef DLUMA_EDITOR_MODEL_ANIMATION_GUARD_H
#define DLUMA_EDITOR_MODEL_ANIMATION_GUARD_H

#include <stdint.h>

#define DLUMA_EDITOR_MODEL_LOOPED_ANIMATION_SIZE_OFFSET 0xE8U
#define DLUMA_EDITOR_ANIM_BIND_LOOKUP_SLOT_OFFSET 0x148U
#define DLUMA_EDITOR_ANIM_BIND_PLAY_SLOT_OFFSET 0x0D8U
#define DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE 14U

typedef enum DlumaEditorModelAnimationDecision {
    DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_EMPTY = 1,
    DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_VALID_BIND = 2,
    DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_MODEL_UNREADABLE = 3,
    DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_BIND_MISSING = 4,
    DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_BIND_UNREADABLE = 5,
    DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_VTABLE_UNREADABLE = 6,
    DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_LOOKUP_NOT_EXECUTABLE = 7,
    DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_PLAY_NOT_EXECUTABLE = 8
} DlumaEditorModelAnimationDecision;

typedef struct DlumaEditorModelAnimationAccess {
    void* context;
    int (*read_u32)(void* context, uintptr_t address, uint32_t* output);
    uintptr_t (*get_anim_bind)(void* context, uintptr_t model);
    int (*read_pointer)(
        void* context, uintptr_t address, uintptr_t* output);
    int (*is_executable)(void* context, uintptr_t address);
} DlumaEditorModelAnimationAccess;

typedef struct DlumaEditorModelAnimationIdentity {
    uint32_t timestamp;
    uint32_t size_of_image;
    const char* sha256;
} DlumaEditorModelAnimationIdentity;

typedef struct DlumaEditorModelAnimationLifecycle {
    uint64_t current_generation;
    uint64_t last_generation;
    uint32_t installed;
    uint32_t retained;
} DlumaEditorModelAnimationLifecycle;

DlumaEditorModelAnimationDecision dluma_editor_model_animation_guard_probe(
    uintptr_t model,
    const DlumaEditorModelAnimationAccess* access);
int dluma_editor_model_animation_guard_calls_original(
    DlumaEditorModelAnimationDecision decision);
int dluma_editor_model_animation_guard_first_warning(uint64_t suppression_count);

int dluma_editor_model_animation_identity_matches(
    const DlumaEditorModelAnimationIdentity* actual,
    const DlumaEditorModelAnimationIdentity* expected);
void dluma_editor_model_animation_build_absolute_jump(
    uint8_t output[DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE],
    uintptr_t destination);

int dluma_editor_model_animation_lifecycle_begin(
    DlumaEditorModelAnimationLifecycle* lifecycle,
    uint64_t generation);
void dluma_editor_model_animation_lifecycle_mark_installed(
    DlumaEditorModelAnimationLifecycle* lifecycle);
void dluma_editor_model_animation_lifecycle_abort(
    DlumaEditorModelAnimationLifecycle* lifecycle);
void dluma_editor_model_animation_lifecycle_mark_restored(
    DlumaEditorModelAnimationLifecycle* lifecycle);
void dluma_editor_model_animation_lifecycle_mark_retained(
    DlumaEditorModelAnimationLifecycle* lifecycle);

#endif
