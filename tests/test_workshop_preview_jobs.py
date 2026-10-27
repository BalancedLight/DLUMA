from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_preview_job_identity_stays_owned_during_payload_replacement():
    source = (ROOT / "src/dlw_workshop_menu.c").read_text()
    record = source[source.index("typedef struct DlumaWorkshopPreviewPixelJob {"):]
    record = record[:record.index("} DlumaWorkshopPreviewPixelJob;") + len("} DlumaWorkshopPreviewPixelJob;")]
    request = source[source.index("static void dluma_workshop_clear_owned_pixel_job("):
                     source.index("/* Dynamic texture updates consume")]
    take = source[source.index("int dluma_workshop_menu_take_preview_pixels("):
                  source.index("static int dluma_workshop_validate_cached_image(")]
    harness = r'''
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define DLUMA_MAX_WPATH 1024
#define DLUMA_WORKSHOP_MENU_PIXEL_JOBS 4
#define DLUMA_ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
typedef int DlumaWorkshopPixelLayout;
#define DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD 2
#define dluma_atomic_compare_exchange InterlockedCompareExchange
#define dluma_atomic_exchange InterlockedExchange
#define dluma_wstricmp _wcsicmp
#define dluma_free free
static void dluma_wstr_copy(WCHAR* out,size_t size,const WCHAR* in) { wcscpy_s(out,size,in); }
''' + record + r'''
static DlumaWorkshopPreviewPixelJob g_dluma_workshop_pixel_jobs[DLUMA_WORKSHOP_MENU_PIXEL_JOBS];
static int inject, nested_blocked;
int dluma_workshop_menu_request_preview_pixels(ULONGLONG,const WCHAR*);
static void* observed_memset(void* out,int value,size_t size) {
    memset(out,value,size);
    if(inject) { inject=0; nested_blocked=!dluma_workshop_menu_request_preview_pixels(99,L"other.dds"); }
    return out;
}
#define memset observed_memset
''' + request + take + r'''
#undef memset
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void complete(unsigned i,BYTE* pixels) {
    DlumaWorkshopPreviewPixelJob* job=&g_dluma_workshop_pixel_jobs[i];
    InterlockedExchange(&job->state,3);
    job->pixels=pixels;job->width=512;job->height=512;job->stride=2048;
    job->layout=DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD;
    InterlockedExchange(&job->state,2);
}
int main(void) {
    BYTE first[4]={1,2,3,4},second[4]={5,6,7,8},*pixels=0;
    unsigned width,height,stride;
    inject=1;
    CHECK(dluma_workshop_menu_request_preview_pixels(7,L"first.dds"));
    CHECK(nested_blocked && g_dluma_workshop_pixel_jobs[0].state==1);
    CHECK(dluma_workshop_menu_request_preview_pixels(7,L"first.dds"));
    CHECK(g_dluma_workshop_pixel_jobs[1].state==0);
    CHECK(dluma_workshop_menu_request_preview_pixels(7,L"second.dds"));
    complete(0,first);complete(1,second);
    CHECK(!dluma_workshop_menu_take_preview_pixels(8,L"first.dds",&pixels,&width,&height,&stride));
    inject=1;nested_blocked=0;
    CHECK(dluma_workshop_menu_take_preview_pixels(7,L"second.dds",&pixels,&width,&height,&stride));
    CHECK(nested_blocked && pixels==second && width==512 && height==512 && stride==2048);
    CHECK(g_dluma_workshop_pixel_jobs[0].state==2 && g_dluma_workshop_pixel_jobs[1].state==0);
    CHECK(dluma_workshop_menu_take_preview_pixels(7,L"first.dds",&pixels,&width,&height,&stride));
    CHECK(pixels==first && g_dluma_workshop_pixel_jobs[0].state==0);
    g_dluma_workshop_pixel_jobs[0].state=3;
    CHECK(!dluma_workshop_menu_request_preview_pixels(7,L"first.dds"));
    puts("preview job ownership and exact key/path association passed");
    return 0;
}
'''
    output = ROOT / "build/workshop-preview-jobs-fake"
    output.mkdir(parents=True, exist_ok=True)
    (output / "harness.c").write_text(harness)
    subprocess.run(["cmd.exe", "/d", "/c",
                    r"call tools\setup_msvc.bat && cl.exe /nologo /W4 /WX /O2 "
                    r"build\workshop-preview-jobs-fake\harness.c "
                    r"/Fobuild\workshop-preview-jobs-fake\harness.obj "
                    r"/Febuild\workshop-preview-jobs-fake\harness.exe"], cwd=ROOT, check=True)
    subprocess.run([str(output / "harness.exe")], cwd=ROOT, check=True)
