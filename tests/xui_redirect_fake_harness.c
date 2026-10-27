#include "../src/dlw_ui_redirect_core.h"

#include <stdio.h>
#include <string.h>

static int g_tests;
static int g_passed;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        ++g_tests;                                                            \
        if (expression)                                                       \
            ++g_passed;                                                       \
        else                                                                  \
        {                                                                     \
            printf("failed at line %d: %s\n", __LINE__, #expression);         \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main(void)
{
    static const char stock[] = "MenuSteamWorkshop.xui";
    static const char alias[] = "DLM_SteamWorkshop.xui";
    unsigned char bytes[sizeof(stock)];

    CHECK(dluma_xui_redirect_identity_matches(
        0x623397AAUL, 0x02082000UL, 0x623397AAUL, 0x02082000UL));
    CHECK(!dluma_xui_redirect_identity_matches(
        0x623397ABUL, 0x02082000UL, 0x623397AAUL, 0x02082000UL));
    CHECK(!dluma_xui_redirect_identity_matches(
        0x623397AAUL, 0x02083000UL, 0x623397AAUL, 0x02082000UL));
    CHECK(dluma_xui_redirect_names_same_size(sizeof(stock), sizeof(alias)));
    CHECK(!dluma_xui_redirect_names_same_size(sizeof(stock), sizeof(alias) - 1));

    memcpy(bytes, stock, sizeof(stock));
    CHECK(dluma_xui_redirect_decide(
              bytes, sizeof(bytes), stock, alias, sizeof(stock)) ==
          DLUMA_XUI_REDIRECT_APPLY);
    memcpy(bytes, alias, sizeof(alias));
    CHECK(dluma_xui_redirect_decide(
              bytes, sizeof(bytes), stock, alias, sizeof(stock)) ==
          DLUMA_XUI_REDIRECT_ALREADY_PRESENT);
    memset(bytes, 'X', sizeof(bytes));
    CHECK(dluma_xui_redirect_decide(
              bytes, sizeof(bytes), stock, alias, sizeof(stock)) ==
          DLUMA_XUI_REDIRECT_MISMATCH);
    CHECK(dluma_xui_redirect_decide(
              bytes, sizeof(bytes) - 1, stock, alias, sizeof(stock)) ==
          DLUMA_XUI_REDIRECT_INVALID);

    memcpy(bytes, alias, sizeof(alias));
    CHECK(dluma_xui_redirect_should_restore(
        1, bytes, sizeof(bytes), alias, sizeof(alias)));
    CHECK(!dluma_xui_redirect_should_restore(
        0, bytes, sizeof(bytes), alias, sizeof(alias)));
    bytes[0] = 'X';
    CHECK(!dluma_xui_redirect_should_restore(
        1, bytes, sizeof(bytes), alias, sizeof(alias)));

    printf("xui_redirect_fake_harness: %d/%d ok\n", g_passed, g_tests);
    return 0;
}
