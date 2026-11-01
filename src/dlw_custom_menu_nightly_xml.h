#ifndef DLUMA_CUSTOM_MENU_NIGHTLY_XML_H
#define DLUMA_CUSTOM_MENU_NIGHTLY_XML_H

#include <string.h>

/* Compose the popup as a direct scene child while preserving all source bytes. */
static int dluma_custom_menu_compose_nightly(
    const unsigned char *input, unsigned int size,
    const unsigned char *popup, unsigned int popup_size,
    unsigned char *output, unsigned int capacity, unsigned int *output_size,
    unsigned int *insertion_out)
{
    unsigned int starts[128], lengths[128], depth = 0, cursor = 0;
    unsigned int scene_end = size, scenes = 0, present = 0;
    static const char id[] = "G_DLUMANightlyPopup";
    if (output_size) *output_size = 0;
    if (insertion_out) *insertion_out = size;
    if (!input || !size || !popup || !popup_size || !output || !output_size)
        return 0;
    while (cursor < size) {
        unsigned int tag, end, name, name_end, i;
        int closing = 0, self_closing = 0;
        unsigned char quote = 0;
        for (tag = cursor; tag < size && input[tag] != '<'; ++tag) {}
        if (tag == size) break;
        if (tag + 4U <= size && memcmp(input + tag, "<!--", 4U) == 0) {
            for (end = tag + 4U; end + 3U <= size && memcmp(input + end, "-->", 3U); ++end) {}
            if (end + 3U > size) return 0;
            cursor = end + 3U; continue;
        }
        if (tag + 9U <= size && memcmp(input + tag, "<![CDATA[", 9U) == 0) {
            for (end = tag + 9U; end + 3U <= size && memcmp(input + end, "]]>", 3U); ++end) {}
            if (end + 3U > size) return 0;
            cursor = end + 3U; continue;
        }
        if (tag + 1U >= size) return 0;
        if (input[tag + 1U] == '?' || input[tag + 1U] == '!') {
            for (end = tag + 2U; end < size && input[end] != '>'; ++end) {}
            if (end == size) return 0;
            cursor = end + 1U; continue;
        }
        name = tag + 1U;
        if (input[name] == '/') { closing = 1; ++name; }
        name_end = name;
        while (name_end < size && input[name_end] != '>' && input[name_end] != '/' && input[name_end] != ' ' && input[name_end] != '\t' && input[name_end] != '\r' && input[name_end] != '\n') ++name_end;
        if (name_end == name) return 0;
        for (end = name_end; end < size; ++end) {
            if (quote) { if (input[end] == quote) quote = 0; }
            else if (input[end] == '\'' || input[end] == '"') quote = input[end];
            else if (input[end] == '>') break;
        }
        if (end == size || quote) return 0;
        i = end; while (i > name_end && (input[i - 1U] == ' ' || input[i - 1U] == '\t' || input[i - 1U] == '\r' || input[i - 1U] == '\n')) --i;
        self_closing = i > name_end && input[i - 1U] == '/';
        if (closing) {
            if (!depth || lengths[depth - 1U] != name_end - name || memcmp(input + starts[depth - 1U], input + name, name_end - name)) return 0;
            if (lengths[depth - 1U] == 2U && memcmp(input + starts[depth - 1U], "Id", 2U) == 0 && depth >= 3U && scenes == 1U && scene_end == size) {
                unsigned int a = cursor, b = tag;
                while (a < b && (input[a] == ' ' || input[a] == '\t' || input[a] == '\r' || input[a] == '\n')) ++a;
                while (b > a && (input[b-1U] == ' ' || input[b-1U] == '\t' || input[b-1U] == '\r' || input[b-1U] == '\n')) --b;
                if (b - a == sizeof(id) - 1U && memcmp(input + a, id, sizeof(id) - 1U) == 0) present = 1;
            }
            if (depth == 2U && lengths[1] == 8U && memcmp(input + starts[1], "XuiScene", 8U) == 0) scene_end = tag;
            --depth;
        } else if (!self_closing) {
            if (depth >= 128U) return 0;
            if (depth == 1U && lengths[0] == 9U && memcmp(input + starts[0], "XuiCanvas", 9U) == 0 && name_end - name == 8U && memcmp(input + name, "XuiScene", 8U) == 0) ++scenes;
            starts[depth] = name; lengths[depth] = name_end - name; ++depth;
        }
        cursor = end + 1U;
    }
    if (depth || scenes != 1U || scene_end == size || size > capacity || (!present && popup_size > capacity - size)) return 0;
    if (present) { memcpy(output, input, size); *output_size = size; }
    else {
        if (insertion_out) *insertion_out = scene_end;
        memcpy(output, input, scene_end);
        memcpy(output + scene_end, popup, popup_size);
        memcpy(output + scene_end + popup_size, input + scene_end, size - scene_end);
        *output_size = size + popup_size;
    }
    return 1;
}
#endif
