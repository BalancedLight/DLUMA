#ifndef DLW_NIGHTLY_OFFER_CORE_H
#define DLW_NIGHTLY_OFFER_CORE_H

typedef struct DlumaNightlyManifest {
    char commit[41];
    char filename[160];
    char sha256[65];
} DlumaNightlyManifest;

int dluma_nightly_manifest_parse(const void *bytes, unsigned int size, DlumaNightlyManifest *out);
/* Only an ahead comparison from the official repository permits an update. */
int dluma_nightly_compare_is_ahead(const void *bytes, unsigned int size);
/* 1 ahead, 0 identical/behind, -1 invalid, diverged or unavailable. */
int dluma_nightly_compare_status(const void *bytes, unsigned int size);
int dluma_nightly_commit_valid(const char *commit);
int dluma_nightly_text_compare(const char *left, const char *right);

#endif
