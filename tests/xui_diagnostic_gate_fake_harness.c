#include <assert.h>
#include <stdio.h>
#include "../src/game/dlw_xui_diagnostic_gate.h"
int main(void)
{
    DlumaXuiDiagnosticFacts f={1,1,1,1,1,1,1,1,17,17}, bad;
    unsigned i;
    assert(dluma_xui_diagnostic_ready(&f));
    assert(dluma_xui_diagnostic_owner_allowed(&f,2,17));
    assert(dluma_xui_diagnostic_owner_allowed(&f,3,17));
    assert(!dluma_xui_diagnostic_owner_allowed(&f,1,17));
    assert(!dluma_xui_diagnostic_owner_allowed(&f,2,16));
    for(i=0;i<8;i++) {
        uint32_t* fields[]={&bad.compiled,&bad.developer,&bad.opted_in,&bad.exact_profile,
            &bad.project_identity_matches,&bad.map_name_matches,&bad.active_frame,&bad.exact_caller};
        bad=f; *fields[i]=0;
        assert(!dluma_xui_diagnostic_ready(&bad));
    }
    bad=f; bad.captured_generation=0; assert(!dluma_xui_diagnostic_ready(&bad));
    bad=f; bad.current_generation=18; assert(!dluma_xui_diagnostic_ready(&bad));
    {
        DlumaXuiDiagnosticBudget budget={0};
        assert(!dluma_xui_diagnostic_budget_claim(&budget,0));
        for(i=0;i<8;i++) assert(dluma_xui_diagnostic_budget_claim(&budget,101));
        assert(!dluma_xui_diagnostic_budget_claim(&budget,101));
        for(i=0;i<32768;i++) assert(dluma_xui_diagnostic_budget_visit(&budget));
        assert(!dluma_xui_diagnostic_budget_visit(&budget));
        assert(dluma_xui_diagnostic_budget_claim(&budget,102));
        assert(dluma_xui_diagnostic_budget_visit(&budget));
    }
    puts("PASS: private XUI diagnostic gate"); return 0;
}
