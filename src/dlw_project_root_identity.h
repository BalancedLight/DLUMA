#ifndef DLUMA_PROJECT_ROOT_IDENTITY_H
#define DLUMA_PROJECT_ROOT_IDENTITY_H

#define DLUMA_PROJECT_ROOT_CANONICAL_CAPACITY 2048U

#if defined(__cplusplus)
#define DLUMA_PROJECT_ROOT_INLINE inline
#else
#define DLUMA_PROJECT_ROOT_INLINE __inline
#endif

/*
 * Canonical ownership identity for a local Workshop project root.
 *
 * Accepted inputs are absolute Windows drive paths in ordinary, long-path,
 * or the exact pseudo-prefix form observed from the supported engine build:
 *
 *   X:\...
 *   \\?\X:\...
 *   \?\X:\...
 *   //?/X:/...
 *
 * The result is lowercase ASCII with backslash separators, no duplicate or
 * trailing non-root separators, and no dot segments.  The function rejects
 * truncation and never resolves a relative path.
 */
static DLUMA_PROJECT_ROOT_INLINE int dluma_project_root_canonicalize(
    const char* input, char* output, unsigned int capacity)
{
    unsigned int input_index = 0;
    unsigned int output_index = 0;
    unsigned int segment_begin;
    if (!input || !output || capacity < 4U)
        return 0;
    output[0] = 0;

    if (input[0] == '\\' && input[1] == '\\' &&
        input[2] == '?' && input[3] == '\\')
        input_index = 4U;
    else if (input[0] == '\\' && input[1] == '?' &&
             input[2] == '\\')
        input_index = 3U;
    else if (input[0] == '/' && input[1] == '/' &&
             input[2] == '?' && input[3] == '/')
        input_index = 4U;

    if (!((input[input_index] >= 'A' &&
           input[input_index] <= 'Z') ||
          (input[input_index] >= 'a' &&
           input[input_index] <= 'z')) ||
        input[input_index + 1U] != ':' ||
        (input[input_index + 2U] != '\\' &&
         input[input_index + 2U] != '/'))
        return 0;

    output[output_index++] =
        input[input_index] >= 'A' && input[input_index] <= 'Z'
            ? (char)(input[input_index] - 'A' + 'a')
            : input[input_index];
    output[output_index++] = ':';
    output[output_index++] = '\\';
    input_index += 3U;
    while (input[input_index] == '\\' || input[input_index] == '/')
        ++input_index;

    while (input[input_index]) {
        char value = input[input_index++];
        if (value == '\\' || value == '/') {
            value = '\\';
            if (output_index &&
                output[output_index - 1U] == '\\')
                continue;
        } else {
            const unsigned char byte = (unsigned char)value;
            if (byte < 0x20U || value == ':' || value == '?' ||
                value == '*' || value == '"' || value == '<' ||
                value == '>' || value == '|') {
                output[0] = 0;
                return 0;
            }
            if (value >= 'A' && value <= 'Z')
                value = (char)(value - 'A' + 'a');
        }
        if (output_index + 1U >= capacity) {
            output[0] = 0;
            return 0;
        }
        output[output_index++] = value;
    }

    while (output_index > 3U &&
           output[output_index - 1U] == '\\')
        --output_index;
    output[output_index] = 0;

    segment_begin = 3U;
    for (input_index = segment_begin;
         input_index <= output_index; ++input_index) {
        unsigned int length;
        if (input_index != output_index &&
            output[input_index] != '\\')
            continue;
        length = input_index - segment_begin;
        if ((!length &&
             !(segment_begin == 3U && output_index == 3U)) ||
            (length == 1U && output[segment_begin] == '.') ||
            (length == 2U && output[segment_begin] == '.' &&
             output[segment_begin + 1U] == '.') ||
            output[input_index - 1U] == '.' ||
            output[input_index - 1U] == ' ') {
            output[0] = 0;
            return 0;
        }
        segment_begin = input_index + 1U;
    }
    return 1;
}

static DLUMA_PROJECT_ROOT_INLINE int dluma_project_root_identity_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    char canonical_left[DLUMA_PROJECT_ROOT_CANONICAL_CAPACITY];
    char canonical_right[DLUMA_PROJECT_ROOT_CANONICAL_CAPACITY];
    if (!dluma_project_root_canonicalize(
            left, canonical_left,
            (unsigned int)sizeof(canonical_left)) ||
        !dluma_project_root_canonicalize(
            right, canonical_right,
            (unsigned int)sizeof(canonical_right)))
        return 0;
    while (canonical_left[index] && canonical_right[index]) {
        if (canonical_left[index] != canonical_right[index])
            return 0;
        ++index;
    }
    return canonical_left[index] == canonical_right[index];
}

static DLUMA_PROJECT_ROOT_INLINE int dluma_project_root_is_canonical(
    const char* value)
{
    unsigned int index = 0;
    char canonical[DLUMA_PROJECT_ROOT_CANONICAL_CAPACITY];
    if (!dluma_project_root_canonicalize(
            value, canonical, (unsigned int)sizeof(canonical)))
        return 0;
    while (canonical[index] && value[index]) {
        if (canonical[index] != value[index])
            return 0;
        ++index;
    }
    return canonical[index] == value[index];
}

#undef DLUMA_PROJECT_ROOT_INLINE

#endif
