#ifndef DLUMA_UI_REDIRECT_CORE_H
#define DLUMA_UI_REDIRECT_CORE_H

#include <stddef.h>
#include <string.h>

typedef enum DlumaXuiRedirectDecision
{
    DLUMA_XUI_REDIRECT_INVALID = 0,
    DLUMA_XUI_REDIRECT_APPLY,
    DLUMA_XUI_REDIRECT_ALREADY_PRESENT,
    DLUMA_XUI_REDIRECT_MISMATCH
} DlumaXuiRedirectDecision;

static int dluma_xui_redirect_identity_matches(
    unsigned long actual_timestamp,
    unsigned long actual_image_size,
    unsigned long expected_timestamp,
    unsigned long expected_image_size)
{
    return actual_timestamp == expected_timestamp &&
           actual_image_size == expected_image_size;
}

static int dluma_xui_redirect_names_same_size(
    size_t original_size, size_t redirected_size)
{
    return original_size != 0 && original_size == redirected_size;
}

static DlumaXuiRedirectDecision dluma_xui_redirect_decide(
    const unsigned char *actual,
    size_t actual_size,
    const char *original_name,
    const char *redirected_name,
    size_t name_size)
{
    if (!actual || !original_name || !redirected_name ||
        name_size == 0 || actual_size < name_size)
        return DLUMA_XUI_REDIRECT_INVALID;
    if (memcmp(actual, redirected_name, name_size) == 0)
        return DLUMA_XUI_REDIRECT_ALREADY_PRESENT;
    if (memcmp(actual, original_name, name_size) == 0)
        return DLUMA_XUI_REDIRECT_APPLY;
    return DLUMA_XUI_REDIRECT_MISMATCH;
}

static int dluma_xui_redirect_should_restore(
    int loader_owns_write,
    const unsigned char *actual,
    size_t actual_size,
    const char *redirected_name,
    size_t name_size)
{
    return loader_owns_write && actual && redirected_name &&
           name_size != 0 && actual_size >= name_size &&
           memcmp(actual, redirected_name, name_size) == 0;
}

#endif
