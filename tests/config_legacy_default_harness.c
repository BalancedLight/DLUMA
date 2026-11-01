#include <stdio.h>
#include "../src/core/dlw_config_legacy_default.h"
int main(void) {
    int out = -1;
    if (!dluma_config_legacy_integer_default(1, 0, 0, 1, &out) || out != 1) return 1;
    if (dluma_config_legacy_integer_default(1, -1, 0, 1, &out)) return 2;
    if (dluma_config_legacy_integer_default(0, 0, 0, 1, &out)) return 3;
    puts("config legacy default contract passed");
    return 0;
}
