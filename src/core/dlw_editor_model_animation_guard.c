#include "dlw_editor_model_animation_guard.h"

static int dluma_editor_model_animation_add(
    uintptr_t base,
    uintptr_t offset,
    uintptr_t* result)
{
    if (!result || base > UINTPTR_MAX - offset) return 0;
    *result = base + offset;
    return 1;
}

static char dluma_editor_model_animation_fold(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

DlumaEditorModelAnimationDecision dluma_editor_model_animation_guard_probe(
    uintptr_t model,
    const DlumaEditorModelAnimationAccess* access)
{
    uintptr_t field_address;
    uintptr_t bind;
    uintptr_t vtable;
    uintptr_t lookup_slot;
    uintptr_t play_slot;
    uintptr_t lookup;
    uintptr_t play;
    uint32_t looped_animation_size = 0;
    if (!model || !access || !access->read_u32 ||
        !access->get_anim_bind || !access->read_pointer ||
        !access->is_executable ||
        !dluma_editor_model_animation_add(
            model, DLUMA_EDITOR_MODEL_LOOPED_ANIMATION_SIZE_OFFSET,
            &field_address) ||
        !access->read_u32(
            access->context, field_address, &looped_animation_size))
        return DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_MODEL_UNREADABLE;
    if (!looped_animation_size)
        return DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_EMPTY;

    bind = access->get_anim_bind(access->context, model);
    if (!bind) return DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_BIND_MISSING;
    if (!access->read_pointer(access->context, bind, &vtable))
        return DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_BIND_UNREADABLE;
    if (!vtable ||
        !dluma_editor_model_animation_add(
            vtable, DLUMA_EDITOR_ANIM_BIND_LOOKUP_SLOT_OFFSET,
            &lookup_slot) ||
        !dluma_editor_model_animation_add(
            vtable, DLUMA_EDITOR_ANIM_BIND_PLAY_SLOT_OFFSET,
            &play_slot) ||
        !access->read_pointer(access->context, lookup_slot, &lookup) ||
        !access->read_pointer(access->context, play_slot, &play))
        return DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_VTABLE_UNREADABLE;
    if (!lookup || !access->is_executable(access->context, lookup))
        return DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_LOOKUP_NOT_EXECUTABLE;
    if (!play || !access->is_executable(access->context, play))
        return DLUMA_EDITOR_MODEL_ANIMATION_SUPPRESS_PLAY_NOT_EXECUTABLE;
    return DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_VALID_BIND;
}

int dluma_editor_model_animation_guard_calls_original(
    DlumaEditorModelAnimationDecision decision)
{
    return decision == DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_EMPTY ||
           decision == DLUMA_EDITOR_MODEL_ANIMATION_CALL_ORIGINAL_VALID_BIND;
}

int dluma_editor_model_animation_guard_first_warning(uint64_t suppression_count)
{
    return suppression_count == 1U;
}

int dluma_editor_model_animation_identity_matches(
    const DlumaEditorModelAnimationIdentity* actual,
    const DlumaEditorModelAnimationIdentity* expected)
{
    unsigned int index;
    if (!actual || !expected || !actual->sha256 || !expected->sha256 ||
        actual->timestamp != expected->timestamp ||
        actual->size_of_image != expected->size_of_image)
        return 0;
    for (index = 0; index < 64U; ++index) {
        char left = actual->sha256[index];
        char right = expected->sha256[index];
        if (!left || !right ||
            dluma_editor_model_animation_fold(left) !=
                dluma_editor_model_animation_fold(right))
            return 0;
    }
    return actual->sha256[64] == 0 && expected->sha256[64] == 0;
}

void dluma_editor_model_animation_build_absolute_jump(
    uint8_t output[DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE],
    uintptr_t destination)
{
    unsigned int index;
    if (!output) return;
    output[0] = 0xFFU;
    output[1] = 0x25U;
    output[2] = 0U;
    output[3] = 0U;
    output[4] = 0U;
    output[5] = 0U;
    for (index = 0; index < 8U; ++index)
        output[6U + index] =
            (uint8_t)(((uint64_t)destination >> (index * 8U)) & 0xFFU);
}

int dluma_editor_model_animation_lifecycle_begin(
    DlumaEditorModelAnimationLifecycle* lifecycle,
    uint64_t generation)
{
    if (!lifecycle || !generation || lifecycle->installed ||
        lifecycle->retained || lifecycle->current_generation ||
        generation <= lifecycle->last_generation)
        return 0;
    lifecycle->current_generation = generation;
    return 1;
}

void dluma_editor_model_animation_lifecycle_mark_installed(
    DlumaEditorModelAnimationLifecycle* lifecycle)
{
    if (lifecycle && lifecycle->current_generation && !lifecycle->retained)
        lifecycle->installed = 1U;
}

void dluma_editor_model_animation_lifecycle_abort(
    DlumaEditorModelAnimationLifecycle* lifecycle)
{
    if (lifecycle && !lifecycle->installed && !lifecycle->retained)
        lifecycle->current_generation = 0U;
}

void dluma_editor_model_animation_lifecycle_mark_restored(
    DlumaEditorModelAnimationLifecycle* lifecycle)
{
    if (!lifecycle || lifecycle->retained) return;
    lifecycle->installed = 0U;
    lifecycle->last_generation = lifecycle->current_generation;
    lifecycle->current_generation = 0U;
}

void dluma_editor_model_animation_lifecycle_mark_retained(
    DlumaEditorModelAnimationLifecycle* lifecycle)
{
    if (lifecycle) lifecycle->retained = 1U;
}
