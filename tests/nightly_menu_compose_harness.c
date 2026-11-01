#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dlw_custom_menu_nightly_xml.h"
#include "dlw_nightly_popup_template.h"

int main(void) {
    const char *input = "<XuiCanvas><XuiScene><Properties><Id>MenuMain</Id></Properties><!-- preserved --></XuiScene></XuiCanvas>";
    unsigned int size = (unsigned int)strlen(input), out_size = 0, again_size = 0;
    unsigned char *out = malloc(size + sizeof(dluma_nightly_popup_template) + 1);
    unsigned char *again = malloc(size + sizeof(dluma_nightly_popup_template) + 1);
    if (!out || !again) return 1;
    if (!dluma_custom_menu_compose_nightly((const unsigned char *)input, size, dluma_nightly_popup_template, sizeof(dluma_nightly_popup_template), out, size + sizeof(dluma_nightly_popup_template), &out_size, NULL)) return 2;
    out[out_size] = 0;
    if (!strstr((const char *)out, "<!-- preserved -->") || !strstr((const char *)out, "B_DLUMANightlyYes") || !strstr((const char *)out, "B_DLUMANightlyNo")) return 3;
    if (!dluma_custom_menu_compose_nightly(out, out_size, dluma_nightly_popup_template, sizeof(dluma_nightly_popup_template), again, size + sizeof(dluma_nightly_popup_template), &again_size, NULL)) return 4;
    if (out_size != again_size || memcmp(out, again, out_size)) return 5;
    if (dluma_custom_menu_compose_nightly((const unsigned char *)"<XuiCanvas/>", 12, dluma_nightly_popup_template, sizeof(dluma_nightly_popup_template), again, size + sizeof(dluma_nightly_popup_template), &again_size, NULL)) return 6;
    free(out); free(again); return 0;
}
