#ifndef DLUMA_MAIN_MENU_PRESENTATION_CORE_H
#define DLUMA_MAIN_MENU_PRESENTATION_CORE_H

/* This helper is deliberately independent of the game/UI ABI.  It makes the
 * small set of loader-owned substitutions deterministic and unit-testable
 * without allocating or depending on the CRT. */
typedef struct DlumaMainMenuPresentationPlaceholder {
    const char *token;
    const char *value;
} DlumaMainMenuPresentationPlaceholder;

typedef enum DlumaMainMenuHoliday {
    DLUMA_MAIN_MENU_HOLIDAY_NONE = 0,
    DLUMA_MAIN_MENU_HOLIDAY_NEW_YEAR,
    DLUMA_MAIN_MENU_HOLIDAY_VALENTINES,
    DLUMA_MAIN_MENU_HOLIDAY_SAINT_PATRICKS,
    DLUMA_MAIN_MENU_HOLIDAY_EASTER,
    DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY,
    DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY,
    DLUMA_MAIN_MENU_HOLIDAY_INDEPENDENCE_DAY,
    DLUMA_MAIN_MENU_HOLIDAY_TANABATA,
    DLUMA_MAIN_MENU_HOLIDAY_LEIF_ERIKSON,
    DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN,
    DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING,
    DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS
} DlumaMainMenuHoliday;

/* The build identity stores a canonical 64-character SHA-256 digest.  This
 * pure validator deliberately preserves the digest's original case: the
 * loader hashes files as lowercase hexadecimal, while older builds and tools
 * may provide uppercase.  Both forms are equally valid and should present
 * consistently with the immutable startup snapshot. */
#define DLUMA_MAIN_MENU_PRESENTATION_SHA256_HEX_LENGTH 64U
#define DLUMA_MAIN_MENU_PRESENTATION_SHA256_PREFIX_LENGTH 16U
#define DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH 40U

int dluma_main_menu_presentation_sha256_prefix(
    const char *digest,
    char out[DLUMA_MAIN_MENU_PRESENTATION_SHA256_PREFIX_LENGTH + 1U]);
/* The presentation shows a full source commit, not a mutable/derived build
 * label.  Accept the canonical forty hexadecimal characters only. */
int dluma_main_menu_presentation_git_commit(
    const char *commit,
    char out[DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH + 1U]);

int dluma_main_menu_presentation_expand(
    const char *source,
    const DlumaMainMenuPresentationPlaceholder *placeholders,
    unsigned int placeholder_count,
    char *out,
    unsigned int capacity);

/* A missing game-localization entry is not a presentation failure.  Dynamic
 * loader text must still expand its reviewed English emergency fallback. */
int dluma_main_menu_presentation_expand_localized(
    const char *localized,
    int localized_available,
    const char *english_fallback,
    const DlumaMainMenuPresentationPlaceholder *placeholders,
    unsigned int placeholder_count,
    char *out,
    unsigned int capacity);

/* Selects at most one loader-owned seasonal ticker entry from a validated
 * Gregorian local-calendar date. Invalid dates fail closed to NONE. */
DlumaMainMenuHoliday dluma_main_menu_presentation_holiday(
    unsigned int year,
    unsigned int month,
    unsigned int day);

#endif
