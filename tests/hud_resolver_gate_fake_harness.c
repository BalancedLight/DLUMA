#include <stdio.h>
#include "../src/game/dlw_hud_resolver_gate.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void)
{
    DlumaHudResolverGate g={0};
    CHECK(dluma_hud_gate_enter(&g,0));
    CHECK(dluma_hud_gate_can_commit(&g));
    CHECK(!dluma_hud_gate_seal(&g));
    CHECK(!dluma_hud_gate_can_commit(&g));
    CHECK(!dluma_hud_gate_enter(&g,0));
    dluma_hud_gate_leave(&g);
    CHECK(dluma_hud_gate_seal(&g));
    CHECK(!dluma_hud_gate_enter(&g,0));
    CHECK(dluma_hud_gate_enter(&g,1));
    dluma_hud_gate_leave(&g);
    CHECK(dluma_hud_gate_retire(&g));
    CHECK(!dluma_hud_gate_enter(&g,1));
    { DlumaHudResolverGate map={0};
      CHECK(dluma_hud_gate_enter(&map,0));
      CHECK(!dluma_hud_gate_reset(&map));
      CHECK(!dluma_hud_gate_can_commit(&map));
      CHECK(!dluma_hud_gate_enter(&map,0));
      dluma_hud_gate_leave(&map);
      CHECK(!dluma_hud_gate_enter(&map,0)); /* pending reset blocks new mutation */
      CHECK(dluma_hud_gate_enter(&map,1));
      CHECK(dluma_hud_gate_take_reset(&map));
      CHECK(!dluma_hud_gate_take_reset(&map));
      dluma_hud_gate_leave(&map);
      CHECK(dluma_hud_gate_reset(&map));
      CHECK(dluma_hud_gate_enter(&map,0));
      dluma_hud_gate_leave(&map); /* error path releases admission */
      CHECK(dluma_hud_gate_enter(&map,0));
      dluma_hud_gate_leave(&map); }
    puts("hud resolver gate passed"); return 0;
}
