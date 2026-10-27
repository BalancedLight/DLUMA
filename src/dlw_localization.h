#ifndef DLUMA_LOCALIZATION_H
#define DLUMA_LOCALIZATION_H

#include "dlw_win32.h"

/* These are the exact 16-byte ttl::string_base layouts used by the DevTools
 * engine.  The engine allocates returned strings with its CRT; localization
 * releases them through the dynamically resolved engine CRT free function. */
typedef struct DlumaLocalizationTtlString {
    const char *data;
    unsigned int size;
    unsigned int capacity;
} DlumaLocalizationTtlString;

typedef struct DlumaLocalizationTtlWideString {
    const WCHAR *data;
    unsigned int size;
    unsigned int capacity;
} DlumaLocalizationTtlWideString;

typedef char DlumaLocalizationTtlStringSizeMustBe16[
    (sizeof(DlumaLocalizationTtlString) == 16) ? 1 : -1];
typedef char DlumaLocalizationTtlWideStringSizeMustBe16[
    (sizeof(DlumaLocalizationTtlWideString) == 16) ? 1 : -1];

/* Read the native selection without installing catalog hooks or assuming the
 * loader's localization subsystem has initialized (Player playtests skip it).
 * The caller must already own the validated game-thread interface. */
int dluma_localization_capture_native_locale(char out[8]);

#endif
