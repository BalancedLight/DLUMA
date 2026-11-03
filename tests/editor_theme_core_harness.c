#include "../src/core/dlw_editor_theme_core.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static double channel(unsigned int c) { double x = c / 255.0; return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4); }
static double luminance(uint32_t c) { return .2126*channel((c>>16)&255) + .7152*channel((c>>8)&255) + .0722*channel(c&255); }
static double contrast(uint32_t a, uint32_t b) { double x=luminance(a), y=luminance(b), t; if(x<y){t=x;x=y;y=t;} return (x+.05)/(y+.05); }
int main(void) {
    DlumaEditorThemeSystemState s; DlumaEditorThemeResolved a,b; DlumaEditorThemeSchedule q; unsigned int i;
    CHECK(sizeof(s)==104 && offsetof(DlumaEditorThemeSystemState,system_argb)==32 && sizeof(a)==96 && offsetof(DlumaEditorThemeResolved,argb)==24);
    memset(&s,0,sizeof(s)); s.size=sizeof(s); s.version=1; s.apps_mode_available=1;
    memset(&a,0,sizeof(a)); a.size=sizeof(a); a.version=1;
    CHECK(dluma_editor_theme_core_resolve(0,&s,&a) && a.kind==0);
    for(i=0;i<2;i++){s.apps_dark=i; CHECK(dluma_editor_theme_core_resolve(0,&s,&a) && a.kind==i); CHECK(contrast(a.argb[12],a.argb[11])>=4.5); CHECK(contrast(a.argb[4],a.argb[0])>=4.5); CHECK(contrast(a.argb[6],a.argb[0])>=4.5);}
    CHECK(dluma_editor_theme_core_resolve(1,&s,&a) && a.kind==0); CHECK(dluma_editor_theme_core_resolve(2,&s,&a) && a.kind==1);
    s.apps_mode_available=0; CHECK(dluma_editor_theme_core_resolve(0,&s,&a) && a.kind==0); CHECK(dluma_editor_theme_core_resolve(999,&s,&a) && a.kind==0 && a.preference==0);
    s.high_contrast=1; for(i=0;i<17;i++) s.system_argb[i]=0xff000000u+i; for(i=0;i<3;i++){CHECK(dluma_editor_theme_core_resolve(i,&s,&a) && a.kind==2 && a.argb[16]==s.system_argb[16]);}
    dluma_editor_theme_schedule_initialize(&q); CHECK(dluma_editor_theme_schedule_publish(&q,&a)==1); CHECK(dluma_editor_theme_schedule_publish(&q,&a)==0); b=a; b.argb[0]++; CHECK(dluma_editor_theme_schedule_publish(&q,&b)==1); CHECK(dluma_editor_theme_schedule_take(&q,&a)==1 && a.argb[0]==b.argb[0]); CHECK(dluma_editor_theme_schedule_take(&q,&a)==0); CHECK(dluma_editor_theme_schedule_publish(&q,&b)==0); b.argb[0]++; CHECK(dluma_editor_theme_schedule_publish(&q,&b)==1); dluma_editor_theme_schedule_stop(&q); CHECK(dluma_editor_theme_schedule_take(&q,&a)==0); CHECK(dluma_editor_theme_schedule_publish(&q,&b)==-1);
    puts("editor theme core contracts passed"); return 0;
}
