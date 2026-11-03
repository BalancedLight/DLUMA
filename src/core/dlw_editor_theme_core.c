#include "dlw_editor_theme_core.h"
#include "../../build/generated/editor-theme/editor_theme_palette.generated.h"
#include <string.h>

int dluma_editor_theme_core_resolve(unsigned int preference, const DlumaEditorThemeSystemState *state, DlumaEditorThemeResolved *out)
{
    unsigned int kind;
    if (!state || !out || state->size != sizeof(*state) || state->version != 1 ||
        state->reserved || state->reserved_tail || out->size != sizeof(*out) || out->version != 1) return 0;
    if (preference > DLUMA_EDITOR_THEME_DARK) preference = DLUMA_EDITOR_THEME_SYSTEM;
    kind = state->high_contrast ? DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST :
        preference == DLUMA_EDITOR_THEME_DARK ? DLUMA_EDITOR_THEME_KIND_DARK :
        preference == DLUMA_EDITOR_THEME_LIGHT ? DLUMA_EDITOR_THEME_KIND_LIGHT :
        state->apps_mode_available && state->apps_dark ? DLUMA_EDITOR_THEME_KIND_DARK : DLUMA_EDITOR_THEME_KIND_LIGHT;
    out->generation = state->generation;
    out->kind = kind;
    out->preference = preference;
    memcpy(out->argb, kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ? state->system_argb :
        kind == DLUMA_EDITOR_THEME_KIND_DARK ? dluma_editor_theme_dark_argb : dluma_editor_theme_light_argb, sizeof(out->argb));
    out->reserved = 0;
    return 1;
}
void dluma_editor_theme_schedule_initialize(DlumaEditorThemeSchedule *state)
{
    if (state) memset(state, 0, sizeof(*state));
}
int dluma_editor_theme_schedule_publish(DlumaEditorThemeSchedule *state, const DlumaEditorThemeResolved *theme)
{
    uint64_t generation;
    if (!state || !theme) return 0;
    if (state->stopped) return -1;
    if (theme->size != sizeof(*theme) || theme->version != 1 || theme->reserved) return 0;
    if (state->has_latest && state->latest.kind == theme->kind &&
        memcmp(state->latest.argb, theme->argb, sizeof(theme->argb)) == 0) return 0;
    generation = state->has_latest ? state->latest.generation + 1 : 1;
    state->latest = *theme;
    state->latest.generation = generation;
    state->has_latest = state->pending = 1;
    return 1;
}
int dluma_editor_theme_schedule_take(DlumaEditorThemeSchedule *state, DlumaEditorThemeResolved *out)
{
    if (!state || !out || state->stopped || !state->pending) return 0;
    *out = state->latest;
    state->pending = 0;
    return 1;
}
void dluma_editor_theme_schedule_stop(DlumaEditorThemeSchedule *state)
{
    if (state) { state->stopped = 1; state->pending = 0; }
}
