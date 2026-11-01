#ifndef DLW_CONFIG_LEGACY_DEFAULT_H
#define DLW_CONFIG_LEGACY_DEFAULT_H

/* Called only after the saved value has parsed and failed current validation. */
static int dluma_config_legacy_integer_default(
    int enabled, int saved, int legacy_default, int current_default,
    int *out)
{
    if (!enabled || saved != legacy_default || !out) return 0;
    *out = current_default;
    return 1;
}
#endif
