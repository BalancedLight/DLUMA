#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <stdint.h>
#include <stddef.h>
#include "dlw_editor_ui.h"
#include "dlw_editor_theme.h"
#include "dlw_editor_theme_profile.h"
#include "dlw_editor_splash.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

extern void dluma_log_pair(const char *, const char *, const char *);

#define SPLASH_FRAME_LIMIT 64U
#define SPLASH_MAX_DIMENSION 2048U
#define SPLASH_MAX_DECODED (128ULL * 1024ULL * 1024ULL)
#define SPLASH_WAKE (WM_APP + 0x695U)
#define SPLASH_TICK 1U
#define SPLASH_ARGB 0x000E200BU

extern const char *__cdecl DLUMA_GetVersion(void);
extern const char *dluma_build_git_short(void);
extern int dluma_editor_is_main_window(HWND window, DWORD expected_process_id);

typedef int GpStatus;
typedef struct { UINT version; void *debug; BOOL suppress_thread, suppress_codecs; } GpStartup;
typedef struct { UINT width, height; INT stride, format; void *scan; UINT_PTR reserved; } GpBitmapData;
typedef struct { INT x, y, width, height; } GpRect;
typedef struct {
    HMODULE module;
    GpStatus (WINAPI *startup)(ULONG_PTR *, const GpStartup *, void *);
    void (WINAPI *shutdown)(ULONG_PTR);
    GpStatus (WINAPI *load)(IStream *, void **);
    GpStatus (WINAPI *dispose)(void *);
    GpStatus (WINAPI *width)(void *, UINT *);
    GpStatus (WINAPI *height)(void *, UINT *);
    GpStatus (WINAPI *frame_count)(void *, const GUID *, UINT *);
    GpStatus (WINAPI *select_frame)(void *, const GUID *, UINT);
    GpStatus (WINAPI *lock)(void *, const GpRect *, UINT, INT, GpBitmapData *);
    GpStatus (WINAPI *unlock)(void *, GpBitmapData *);
} GpApi;
typedef struct {
    SRWLOCK lock;
    HANDLE thread, stop_event;
    HWND native, worker;
    int subscribed;
    volatile LONG stopping, callbacks;
    volatile LONG worker_ready;
    volatile LONG worker_failed;
    UINT visibility_message;
    HWND visibility_window;
    LONG_PTR original_exstyle;
    int original_visible, visibility_active, visibility_busy, completed;
    RECT native_rect;
    WCHAR status[2048], version[2048];
} SplashState;
static SplashState splash;
typedef struct {
    SRWLOCK lock;
    HANDLE thread, stop_event;
    HWND owner, window;
    uint64_t epoch;
    int shutting_down;
} AboutState;
static AboutState about;
static const GUID frame_time = {0x6aedbd6d,0x3fb5,0x418a,{0x83,0xa6,0x7f,0x45,0x22,0x9d,0xc8,0x72}};
static const WCHAR splash_class[] = L"DLUMA.Editor.Splash.1";
static const WCHAR about_class[] = L"DLUMA.Editor.About.1";
static const WCHAR about_owner_property[] = L"DLUMA.Editor.About.Owner.1";
static const WCHAR visibility_property[] = L"DLUMA.Editor.Splash.Visibility.1";
static UINT about_failed_message;
static void splash_end_startup(void);
static void about_stop(void);

static int gp_open(GpApi *g)
{
    WCHAR system[MAX_PATH], path[MAX_PATH]; UINT n;
    n=GetSystemDirectoryW(system, MAX_PATH);
    if (!n || n >= MAX_PATH || n+13U >= MAX_PATH) return 0;
    lstrcpyW(path,system); lstrcatW(path,L"\\gdiplus.dll");
    g->module=LoadLibraryW(path);
    if (!g->module) return 0;
#define LOAD(field, name) do { *(FARPROC *)&g->field=GetProcAddress(g->module,name); if (!g->field) return 0; } while (0)
    LOAD(startup,"GdiplusStartup"); LOAD(shutdown,"GdiplusShutdown");
    LOAD(load,"GdipLoadImageFromStream"); LOAD(dispose,"GdipDisposeImage");
    LOAD(width,"GdipGetImageWidth"); LOAD(height,"GdipGetImageHeight");
    LOAD(frame_count,"GdipImageGetFrameCount"); LOAD(select_frame,"GdipImageSelectActiveFrame");
    LOAD(lock,"GdipBitmapLockBits"); LOAD(unlock,"GdipBitmapUnlockBits");
#undef LOAD
    return 1;
}
static int regular_component(const WCHAR *path, int directory)
{
    HANDLE h; BY_HANDLE_FILE_INFORMATION info; int ok;
    h=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,
                  FILE_FLAG_OPEN_REPARSE_POINT|(directory ? FILE_FLAG_BACKUP_SEMANTICS : 0),NULL);
    if (h==INVALID_HANDLE_VALUE) return 0;
    ok=GetFileInformationByHandle(h,&info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        (!!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)==!!directory);
    CloseHandle(h); return ok;
}
static IStream *splash_stream(HMODULE *ole_module)
{
    HMODULE module; WCHAR path[32768]; DWORD n, size, read; HANDLE file=INVALID_HANDLE_VALUE;
    HGLOBAL data=NULL; void *bytes; IStream *stream=NULL; BY_HANDLE_FILE_INFORMATION info;
    HRESULT (WINAPI *create_stream)(HGLOBAL,BOOL,IStream **); WCHAR system[MAX_PATH], ole_path[MAX_PATH];
    n=GetSystemDirectoryW(system,MAX_PATH);
    if (!n || n>=MAX_PATH || n+11U>=MAX_PATH) return NULL;
    lstrcpyW(ole_path,system); lstrcatW(ole_path,L"\\ole32.dll");
    *ole_module=LoadLibraryW(ole_path); if (!*ole_module) return NULL;
    *(FARPROC *)&create_stream=GetProcAddress(*ole_module,"CreateStreamOnHGlobal");
    if (!create_stream) return NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(const void *)&splash_stream,&module)) return NULL;
    n=GetModuleFileNameW(module,path,32768U);
    if (!n || n>=32768U || n+40U>=32768U) return NULL;
    while (n && path[n-1]!=L'\\' && path[n-1]!=L'/') --n;
    if (!n) return NULL;
    path[n]=0; lstrcatW(path,L"dluma_runtime");
    if (!regular_component(path,1)) return NULL;
    lstrcatW(path,L"\\editor"); if (!regular_component(path,1)) return NULL;
    lstrcatW(path,L"\\startup.gif");
    file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (file==INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
        info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow>16U*1024U*1024U) goto done;
    size=info.nFileSizeLow; data=GlobalAlloc(GMEM_MOVEABLE,size); if (!data) goto done;
    bytes=GlobalLock(data); if (!bytes) goto done;
    if (!ReadFile(file,bytes,size,&read,NULL) || read!=size) { GlobalUnlock(data); goto done; }
    GlobalUnlock(data);
    if (FAILED(create_stream(data,TRUE,&stream))) goto done;
    data=NULL;
done:
    if (data) GlobalFree(data);
    CloseHandle(file);
    return stream;
}
static HBITMAP make_dib(UINT width, UINT height, void **bits)
{
    BITMAPINFO info;
    ZeroMemory(&info,sizeof(info)); info.bmiHeader.biSize=sizeof(info.bmiHeader);
    info.bmiHeader.biWidth=(LONG)width; info.bmiHeader.biHeight=-(LONG)height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    return CreateDIBSection(NULL,&info,DIB_RGB_COLORS,bits,NULL,0);
}
static void copy_status(WCHAR *out, const char *source)
{
    int length, converted;
    out[0]=0; if (!source) return;
    __try {
        length=*(const int *)(source-16);
        if (length<0 || length>2047) return;
        converted=MultiByteToWideChar(CP_ACP,0,source,length,out,2047);
        if (converted>=0 && converted<2048) out[converted]=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out[0]=0; }
}
static BYTE *splash_object(HWND window)
{
    BYTE **global=(BYTE **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0x1B709B8U,sizeof(void *));
    BYTE *base=(BYTE *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0,1), *object=NULL;
    if (!global || !base) return NULL;
    __try {
        object=*global;
        if (!object || *(void **)object!=base+0x151BBF8U || *(void **)(object+216U)!=base+0x151BED8U ||
            *(HWND *)(object+64U)!=window || *(int *)(object+224U)!=153) object=NULL;
    } __except(EXCEPTION_EXECUTE_HANDLER) { object=NULL; }
    return object;
}
static void capture_status(HWND window, BYTE *object)
{
    WCHAR status[2048], version[2048]; HWND worker; int changed=0;
    __try { copy_status(status,*(const char **)(object+504U)); copy_status(version,*(const char **)(object+512U)); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.native==window && (lstrcmpW(splash.status,status) || lstrcmpW(splash.version,version))) {
        lstrcpynW(splash.status,status,2048); lstrcpynW(splash.version,version,2048); changed=1;
    }
    worker=splash.worker;
    ReleaseSRWLockExclusive(&splash.lock);
    if (worker && changed) PostMessageW(worker,SPLASH_WAKE,0,0);
}
/* Visibility changes run on the admitted native splash owner thread. */
static int hide_native_splash(HWND window)
{
    LONG_PTR style, exstyle; int hidden;
    if (!window || GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId() || !splash_object(window)) return 0;
    style=GetWindowLongPtrW(window,GWL_STYLE); exstyle=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if (!SetPropW(window,visibility_property,&splash)) return 0;
    AcquireSRWLockExclusive(&splash.lock);
    splash.original_visible=(style & WS_VISIBLE)!=0; splash.original_exstyle=exstyle;
    splash.visibility_window=window; splash.visibility_active=1; splash.visibility_busy=1;
    ReleaseSRWLockExclusive(&splash.lock);
    if (style & WS_VISIBLE) (void)ShowWindow(window,SW_HIDE);
    hidden=IsWindow(window) && GetPropW(window,visibility_property)==&splash &&
        !(GetWindowLongPtrW(window,GWL_STYLE) & WS_VISIBLE);
    AcquireSRWLockExclusive(&splash.lock);
    splash.visibility_busy=0;
    if (!hidden) { splash.visibility_active=0; splash.visibility_window=NULL; }
    ReleaseSRWLockExclusive(&splash.lock);
    if (!hidden && IsWindow(window) && GetPropW(window,visibility_property)==&splash) RemovePropW(window,visibility_property);
    return hidden;
}
static void release_native_splash(HWND window, int restore)
{
    int original, visible, done;
    if (!window || GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return;
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.visibility_busy || !splash.visibility_active || splash.visibility_window!=window) {
        ReleaseSRWLockExclusive(&splash.lock); return;
    }
    splash.visibility_busy=1; original=splash.original_visible;
    ReleaseSRWLockExclusive(&splash.lock);
    done=1;
    if (IsWindow(window) && GetPropW(window,visibility_property)==&splash && restore && original) {
        visible=(GetWindowLongPtrW(window,GWL_STYLE) & WS_VISIBLE)!=0;
        if (!visible) (void)ShowWindow(window,SW_SHOWNOACTIVATE);
        if (IsWindow(window) && GetPropW(window,visibility_property)==&splash &&
            !(GetWindowLongPtrW(window,GWL_STYLE) & WS_VISIBLE)) done=0;
    }
    if (done && IsWindow(window) && GetPropW(window,visibility_property)==&splash)
        RemovePropW(window,visibility_property);
    AcquireSRWLockExclusive(&splash.lock);
    if (done) { splash.visibility_active=0; splash.visibility_window=NULL; }
    splash.visibility_busy=0;
    ReleaseSRWLockExclusive(&splash.lock);
}
static void sync_native_visibility(HWND window)
{
    int release, restore;
    if (!window || GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return;
    AcquireSRWLockShared(&splash.lock);
    release=splash.visibility_active && splash.visibility_window==window &&
        (InterlockedCompareExchange(&splash.stopping,0,0) || InterlockedCompareExchange(&splash.worker_failed,0,0));
    restore=!splash.completed;
    ReleaseSRWLockShared(&splash.lock);
    if (release) release_native_splash(window,restore);
}
static SRWLOCK redraw_lock;
static PVOID volatile *redraw_slot;
static BOOL (WINAPI *redraw_original)(HWND,const RECT *,HRGN,UINT);
static int redraw_installed, redraw_pending;
static DWORD redraw_protection;
static volatile LONG redraw_callbacks;
static BOOL WINAPI splash_redraw(HWND window, const RECT *rect, HRGN region, UINT flags)
{
    HWND native; BYTE *object; BOOL result;
    InterlockedIncrement(&redraw_callbacks);
    if (!InterlockedCompareExchange(&splash.stopping,0,0)) {
        AcquireSRWLockShared(&splash.lock); native=splash.native; ReleaseSRWLockShared(&splash.lock);
        if (!native && window && GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId() &&
            IsWindowVisible(window) && splash_object(window)) {
            /* Startup can redraw synchronously without pumping the queued enrollment. */
            (void)dluma_editor_ui_request_enroll(window);
            AcquireSRWLockShared(&splash.lock); native=splash.native; ReleaseSRWLockShared(&splash.lock);
        }
        if (window && window==native && GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId()) {
            object=splash_object(window); if (object) { capture_status(window,object); sync_native_visibility(window); }
        }
    }
    result=redraw_original(window,rect,region,flags);
    InterlockedDecrement(&redraw_callbacks);
    return result;
}
static void *editor_address(uintptr_t rva, size_t size)
{
    return dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,rva,size);
}
static PVOID volatile *find_redraw_slot(void)
{
    BYTE *base=(BYTE *)editor_address(0,1); IMAGE_DOS_HEADER *dos; IMAGE_NT_HEADERS64 *nt;
    IMAGE_IMPORT_DESCRIPTOR *descriptor; unsigned int i,j; uintptr_t imports;
    if (!base) return NULL;
    dos=(IMAGE_DOS_HEADER *)editor_address(0,sizeof(*dos));
    if (!dos || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0) return NULL;
    nt=(IMAGE_NT_HEADERS64 *)editor_address((uintptr_t)dos->e_lfanew,sizeof(*nt));
    if (!nt || nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return NULL;
    imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    for (i=0; imports && i<512U; ++i) {
        const char *name;
        descriptor=(IMAGE_IMPORT_DESCRIPTOR *)editor_address(imports+(uintptr_t)i*sizeof(*descriptor),sizeof(*descriptor));
        if (!descriptor || !descriptor->Name) break;
        name=(const char *)editor_address(descriptor->Name,16U);
        if (!name || lstrcmpiA(name,"USER32.dll")) continue;
        if (!descriptor->OriginalFirstThunk || !descriptor->FirstThunk) return NULL;
        for (j=0; j<65536U; ++j) {
            IMAGE_THUNK_DATA64 *lookup=(IMAGE_THUNK_DATA64 *)editor_address(descriptor->OriginalFirstThunk+(uintptr_t)j*sizeof(*lookup),sizeof(*lookup));
            IMAGE_IMPORT_BY_NAME *import;
            if (!lookup || !lookup->u1.AddressOfData) break;
            if (IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)) continue;
            import=(IMAGE_IMPORT_BY_NAME *)editor_address((uintptr_t)lookup->u1.AddressOfData,16U);
            if (import && !lstrcmpA((const char *)import->Name,"RedrawWindow"))
                return (PVOID volatile *)editor_address(descriptor->FirstThunk+(uintptr_t)j*sizeof(*lookup),sizeof(void *));
        }
    }
    return NULL;
}
static int redraw_exchange(void *expected, void *replacement)
{
    DWORD old, ignored; void *observed;
    if (!redraw_slot || ((uintptr_t)redraw_slot & (sizeof(void *)-1U)) ||
        !VirtualProtect((void *)redraw_slot,sizeof(void *),PAGE_READWRITE,&old)) return 0;
    if (!redraw_pending) redraw_protection=old;
    redraw_pending=1;
    observed=InterlockedCompareExchangePointer(redraw_slot,replacement,expected);
    if (VirtualProtect((void *)redraw_slot,sizeof(void *),redraw_protection,&ignored)) redraw_pending=0;
    return observed==expected;
}
static int redraw_install(void)
{
    HMODULE user=GetModuleHandleW(L"user32.dll"); int ok;
    AcquireSRWLockExclusive(&redraw_lock);
    redraw_slot=find_redraw_slot();
    *(FARPROC *)&redraw_original=user ? GetProcAddress(user,"RedrawWindow") : NULL;
    if (redraw_slot && redraw_original && !redraw_installed)
        redraw_installed=redraw_exchange((void *)redraw_original,(void *)splash_redraw);
    ok=redraw_installed && !redraw_pending;
    ReleaseSRWLockExclusive(&redraw_lock); return ok;
}
static void redraw_restore(void)
{
    DWORD ignored;
    AcquireSRWLockExclusive(&redraw_lock);
    if (redraw_installed && redraw_exchange((void *)splash_redraw,(void *)redraw_original)) redraw_installed=0;
    if (!redraw_installed && redraw_pending && VirtualProtect((void *)redraw_slot,sizeof(void *),redraw_protection,&ignored)) redraw_pending=0;
    ReleaseSRWLockExclusive(&redraw_lock);
}
typedef struct {
    HBITMAP frames[SPLASH_FRAME_LIMIT], output;
    BYTE *pixels[SPLASH_FRAME_LIMIT], *output_bits;
    UINT count, width, height, out_width, out_height;
    HDC output_dc;
    HGDIOBJ old_output;
    HFONT font, old_font;
    ULONGLONG started;
    WCHAR build_label[128];
} SplashImages;
static void build_label(WCHAR *label)
{
    WCHAR version[64], revision[32];
    const char *v=DLUMA_GetVersion(), *r=dluma_build_git_short();
    if (!v || !MultiByteToWideChar(CP_UTF8,0,v,-1,version,64)) lstrcpyW(version,L"?");
    if (!r || !MultiByteToWideChar(CP_UTF8,0,r,-1,revision,32)) lstrcpyW(revision,L"?");
    lstrcpyW(label,L"DLUMA "); lstrcatW(label,version);
    lstrcatW(label,L" \x00B7 "); lstrcatW(label,revision);
}
static int fit_window(RECT native, UINT width, UINT height, RECT *out)
{
    POINT primary={0,0};
    HMONITOR monitor=MonitorFromPoint(primary,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info; int w, h, workw, workh, cx, cy;
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor,&info)) return 0;
    workw=info.rcWork.right-info.rcWork.left; workh=info.rcWork.bottom-info.rcWork.top;
    if (workw<=0 || workh<=0 || native.right<=native.left || native.bottom<=native.top) return 0;
    w=(int)width; h=(int)height;
    if (w>workw) { h=MulDiv(h,workw,w); w=workw; }
    if (h>workh) { w=MulDiv(w,workh,h); h=workh; }
    if (w<=0 || h<=0) return 0;
    cx=info.rcWork.left+workw/2; cy=info.rcWork.top+workh/2;
    out->left=cx-w/2; out->top=cy-h/2; out->right=out->left+w; out->bottom=out->top+h;
    if (out->left<info.rcWork.left) OffsetRect(out,info.rcWork.left-out->left,0);
    if (out->right>info.rcWork.right) OffsetRect(out,info.rcWork.right-out->right,0);
    if (out->top<info.rcWork.top) OffsetRect(out,0,info.rcWork.top-out->top);
    if (out->bottom>info.rcWork.bottom) OffsetRect(out,0,info.rcWork.bottom-out->bottom);
    return 1;
}
static int ensure_output(SplashImages *im, UINT width, UINT height)
{
    if (im->output && im->out_width==width && im->out_height==height) return 1;
    if (im->output) { SelectObject(im->output_dc,im->old_output); DeleteObject(im->output); im->output=NULL; }
    im->output=make_dib(width,height,(void **)&im->output_bits);
    if (!im->output) return 0;
    im->old_output=SelectObject(im->output_dc,im->output);
    im->out_width=width; im->out_height=height;
    return im->old_output && im->old_output!=HGDI_ERROR;
}
static void restore_text_alpha(SplashImages *im, const BYTE *source, RECT rect)
{
    UINT x, y, left, top, right, bottom;
    left=rect.left>0 ? (UINT)rect.left : 0;
    top=rect.top>0 ? (UINT)rect.top : 0;
    right=rect.right>0 ? (UINT)rect.right : 0;
    bottom=rect.bottom>0 ? (UINT)rect.bottom : 0;
    if (right>im->out_width) right=im->out_width;
    if (bottom>im->out_height) bottom=im->out_height;
    for (y=top; y<bottom; ++y) {
        UINT sy=(UINT)(((uint64_t)y*im->height)/im->out_height);
        for (x=left; x<right; ++x) {
            UINT sx=(UINT)(((uint64_t)x*im->width)/im->out_width);
            uint32_t original=*(const uint32_t *)(source+((size_t)sy*im->width+sx)*4U);
            uint32_t *pixel=(uint32_t *)(im->output_bits+((size_t)y*im->out_width+x)*4U);
            if ((original>>24)==255U) *pixel|=0xFF000000U; else *pixel=original;
        }
    }
}

static BYTE *copy_frame(SplashImages *im, UINT width, UINT height)
{
    UINT frame, x, y; BYTE *dst, *src;
    frame=(UINT)(((GetTickCount64()-im->started)/40ULL)%im->count);
    src=im->pixels[frame]; dst=im->output_bits;
    if (width==im->width && height==im->height) CopyMemory(dst,src,(size_t)width*height*4U);
    else for (y=0; y<height; ++y) {
        UINT sy=(UINT)(((uint64_t)y*im->height)/height);
        for (x=0; x<width; ++x) {
            UINT sx=(UINT)(((uint64_t)x*im->width)/width);
            *(uint32_t *)(dst+((size_t)y*width+x)*4U)=*(const uint32_t *)(src+((size_t)sy*im->width+sx)*4U);
        }
    }
    return src;
}

static int render_splash(HWND window, SplashImages *im)
{
    RECT native, outer, text, status_rect, version_rect, build_rect; WCHAR status[2048], version[2048];
    UINT width, height; BYTE *src; POINT where, from={0,0}; SIZE size;
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    AcquireSRWLockShared(&splash.lock);
    native=splash.native_rect; lstrcpynW(status,splash.status,2048); lstrcpynW(version,splash.version,2048);
    ReleaseSRWLockShared(&splash.lock);
    if (!fit_window(native,im->width,im->height,&outer)) return 0;
    width=(UINT)(outer.right-outer.left); height=(UINT)(outer.bottom-outer.top);
    if (!ensure_output(im,width,height)) return 0;
    src=copy_frame(im,width,height);
    SetBkMode(im->output_dc,TRANSPARENT); SetTextColor(im->output_dc,RGB(255,255,255));
    text.left=MulDiv(50,(int)width,(int)im->width); text.right=(LONG)(width*2U/3U);
    text.top=(LONG)height-88; text.bottom=text.top+22;
    build_rect=text; DrawTextW(im->output_dc,im->build_label,-1,&text,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    text.top=(LONG)height-66; text.bottom=text.top+22;
    version_rect=text; DrawTextW(im->output_dc,version,-1,&text,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    text.top=(LONG)height-44; text.bottom=text.top+22;
    status_rect=text; DrawTextW(im->output_dc,status,-1,&text,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    GdiFlush();
    restore_text_alpha(im,src,build_rect);
    restore_text_alpha(im,src,version_rect);
    restore_text_alpha(im,src,status_rect);
    where.x=outer.left; where.y=outer.top; size.cx=(LONG)width; size.cy=(LONG)height;
    return UpdateLayeredWindow(window,NULL,&where,&size,im->output_dc,&from,0,&blend,ULW_ALPHA)!=0;
}
static LRESULT CALLBACK splash_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    SplashImages *im=(SplashImages *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message==WM_NCCREATE) { im=(SplashImages *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)im); }
    if (message==WM_TIMER && wp==SPLASH_TICK && im) { if (!render_splash(window,im)) DestroyWindow(window); return 0; }
    if (message==SPLASH_WAKE && im) { if (render_splash(window,im)) SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW); else DestroyWindow(window); return 0; }
    if (message==WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
static void release_images(SplashImages *im)
{
    UINT i;
    if (im->old_font) SelectObject(im->output_dc,im->old_font);
    if (im->font) DeleteObject(im->font);
    if (im->output) { if (im->old_output) SelectObject(im->output_dc,im->old_output); DeleteObject(im->output); }
    if (im->output_dc) DeleteDC(im->output_dc);
    for (i=0; i<SPLASH_FRAME_LIMIT; ++i) if (im->frames[i]) DeleteObject(im->frames[i]);
    ZeroMemory(im,sizeof(*im));
}

static int decode_images(SplashImages *im, HANDLE stop_event)
{
    GpApi gp; GpStartup startup; ULONG_PTR token=0; IStream *stream=NULL; void *image=NULL; HMODULE ole_module=NULL;
    UINT width=0,height=0,count=0,i,y; GpBitmapData data; GpRect rect;
    int gdiplus_started=0, ok=0;
    ZeroMemory(&gp,sizeof(gp));
    if (!stop_event || WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT) goto done;
    if (!gp_open(&gp)) goto done;
    ZeroMemory(&startup,sizeof(startup)); startup.version=1; startup.suppress_codecs=TRUE;
    if (gp.startup(&token,&startup,NULL)) goto done;
    gdiplus_started=1; stream=splash_stream(&ole_module); if (!stream) goto done;
    if (gp.load(stream,&image) || !image || gp.width(image,&width) || gp.height(image,&height) ||
        !width || !height || width>SPLASH_MAX_DIMENSION || height>SPLASH_MAX_DIMENSION ||
        gp.frame_count(image,&frame_time,&count) || !count || count>SPLASH_FRAME_LIMIT ||
        (uint64_t)width*height*4U*count>SPLASH_MAX_DECODED) goto done;
    im->width=width; im->height=height; im->count=count;
    rect.x=rect.y=0; rect.width=(INT)width; rect.height=(INT)height;
    for (i=0; i<count; ++i) {
        if (WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT || gp.select_frame(image,&frame_time,i)) goto done;
        im->frames[i]=make_dib(width,height,(void **)&im->pixels[i]);
        if (!im->frames[i]) goto done;
        ZeroMemory(&data,sizeof(data));
        if (gp.lock(image,&rect,1,SPLASH_ARGB,&data)) goto done;
        if (!data.scan || data.width!=width || data.height!=height || data.stride==0 ||
            (uint64_t)(data.stride<0 ? -(int64_t)data.stride : data.stride)<(uint64_t)width*4U) {
            (void)gp.unlock(image,&data); goto done;
        }
        for (y=0; y<height; ++y)
            CopyMemory(im->pixels[i]+(size_t)y*width*4U,(BYTE *)data.scan+(ptrdiff_t)y*data.stride,(size_t)width*4U);
        if (gp.unlock(image,&data)) goto done;
    }
    ok=1;
done:
    if (image && gp.dispose) gp.dispose(image);
    if (stream) stream->lpVtbl->Release(stream);
    if (ole_module) FreeLibrary(ole_module);
    if (gdiplus_started) gp.shutdown(token);
    if (gp.module) FreeLibrary(gp.module);
    if (!ok) release_images(im);
    return ok;
}

static DWORD WINAPI splash_worker(void *unused)
{
    SplashImages images; HWND window=NULL, native;
    WNDCLASSEXW wc; MSG msg; int registered=0, quit=0; RECT native_rect, outer; HANDLE stop_event; DWORD wait_result;
    const char *phase="image_load";
    (void)unused; ZeroMemory(&images,sizeof(images)); ZeroMemory(&wc,sizeof(wc));
    build_label(images.build_label);
    PeekMessageW(&msg,NULL,WM_USER,WM_USER,PM_NOREMOVE);
    AcquireSRWLockShared(&splash.lock); stop_event=splash.stop_event; ReleaseSRWLockShared(&splash.lock);
    if (!decode_images(&images,stop_event)) goto done;
    if (InterlockedCompareExchange(&splash.stopping,0,0)) goto done;
    phase="window_create";
    images.output_dc=CreateCompatibleDC(NULL); if (!images.output_dc) goto done;
    images.font=CreateFontW(-MulDiv(9,GetDeviceCaps(images.output_dc,LOGPIXELSY),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (images.font) images.old_font=(HFONT)SelectObject(images.output_dc,images.font);
    AcquireSRWLockShared(&splash.lock); native=splash.native; native_rect=splash.native_rect; ReleaseSRWLockShared(&splash.lock);
    if (!native || !fit_window(native_rect,images.width,images.height,&outer)) goto done;
    ZeroMemory(&wc,sizeof(wc)); wc.cbSize=sizeof(wc); wc.lpfnWndProc=splash_window_proc; wc.hInstance=GetModuleHandleW(NULL); wc.lpszClassName=splash_class;
    if (!RegisterClassExW(&wc)) goto done;
    registered=1;
    window=CreateWindowExW(WS_EX_LAYERED|WS_EX_APPWINDOW|WS_EX_NOACTIVATE,splash_class,L"Dying Light Developer Tools",WS_POPUP,
                           outer.left,outer.top,outer.right-outer.left,outer.bottom-outer.top,NULL,NULL,wc.hInstance,&images);
    if (!window) goto done;
    images.started=GetTickCount64();
    phase="first_render";
    if (!render_splash(window,&images) || InterlockedCompareExchange(&splash.stopping,0,0)) goto done;
    AcquireSRWLockExclusive(&splash.lock);
    if (InterlockedCompareExchange(&splash.stopping,0,0)) { ReleaseSRWLockExclusive(&splash.lock); goto done; }
    splash.worker=window; ReleaseSRWLockExclusive(&splash.lock);
    phase="window_show";
    if (!SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)) goto done;
    if (!SetTimer(window,SPLASH_TICK,40,NULL)) goto done;
    InterlockedExchange(&splash.worker_ready,1);
    phase="animation";
    dluma_log_pair("INFO","editor_splash","visible");
    while (!quit) {
        wait_result=MsgWaitForMultipleObjects(1,&stop_event,FALSE,INFINITE,QS_ALLINPUT);
        if (wait_result==WAIT_OBJECT_0 || wait_result==WAIT_FAILED) break;
        if (wait_result!=WAIT_OBJECT_0+1U) break;
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {
            if (msg.message==WM_QUIT || WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0) { quit=1; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }
done:
    dluma_log_pair(InterlockedCompareExchange(&splash.stopping,0,0) ? "INFO" : "WARN",
                   "editor_splash_worker_exit",phase);
    InterlockedExchange(&splash.worker_ready,0);
    if (!InterlockedCompareExchange(&splash.stopping,0,0)) InterlockedExchange(&splash.worker_failed,1);
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.worker==window) splash.worker=NULL;
    native=splash.visibility_window ? splash.visibility_window : splash.native;
    ReleaseSRWLockExclusive(&splash.lock);
    if (native && splash.visibility_message) (void)PostMessageW(native,splash.visibility_message,0,0);
    if (window && GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId()) { KillTimer(window,SPLASH_TICK); DestroyWindow(window); }
    if (registered) UnregisterClassW(splash_class,wc.hInstance);
    release_images(&images);
    return 0;
}

typedef struct {
    SplashImages image;
    HWND button;
    int dpi, footer_height;
    int layered, focused, pressed, hot, closing;
    RECT button_rect;
    unsigned int ticks;
    COLORREF background, text, button_background, border, selected, selected_text, disabled_text, hover, accent;
} AboutWindow;

static UINT about_dpi(HWND owner)
{
    HMODULE user=GetModuleHandleW(L"user32.dll");
    UINT (WINAPI *get_dpi)(HWND)=NULL;
    if (user) *(FARPROC *)&get_dpi=GetProcAddress(user,"GetDpiForWindow");
    if (get_dpi) { UINT dpi=get_dpi(owner); if (dpi) return dpi; }
    return 96;
}

static int about_match_dpi(HWND owner)
{
    HMODULE user=GetModuleHandleW(L"user32.dll"); HANDLE context;
    HANDLE (WINAPI *get_window)(HWND)=NULL;
    HANDLE (WINAPI *set_thread)(HANDLE)=NULL;
    HANDLE (WINAPI *get_thread)(void)=NULL;
    BOOL (WINAPI *equal)(HANDLE,HANDLE)=NULL;
    if (!user) return 0;
    *(FARPROC *)&get_window=GetProcAddress(user,"GetWindowDpiAwarenessContext");
    *(FARPROC *)&set_thread=GetProcAddress(user,"SetThreadDpiAwarenessContext");
    *(FARPROC *)&get_thread=GetProcAddress(user,"GetThreadDpiAwarenessContext");
    *(FARPROC *)&equal=GetProcAddress(user,"AreDpiAwarenessContextsEqual");
    if (!get_window || !set_thread || !get_thread || !equal) return 1;
    context=get_window(owner);
    return context && set_thread(context) && equal(get_thread(),context);
}

static int about_owner_valid(HWND owner)
{
    DWORD process_id=0;
    return IsWindow(owner) && GetPropW(owner,about_owner_property)==&about &&
        GetWindowThreadProcessId(owner,&process_id)!=0 && process_id==GetCurrentProcessId();
}

static COLORREF about_argb(uint32_t value)
{
    return RGB((value>>16)&255U,(value>>8)&255U,value&255U);
}

static int about_palette(AboutWindow *view)
{
    DlumaEditorThemeResolved theme; COLORREF values[9];
    ZeroMemory(&theme,sizeof(theme)); theme.size=sizeof(theme); theme.version=1;
    if (dluma_editor_theme_copy_snapshot(&theme)) {
        values[0]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        values[1]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_TEXT]);
        values[2]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_ALTERNATE]);
        values[3]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        values[4]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_SELECTION]);
        values[5]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT]);
        values[6]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT]);
        values[7]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_HOVER]);
        values[8]=about_argb(theme.argb[DLUMA_EDITOR_THEME_ROLE_ACCENT]);
    } else {
        values[0]=GetSysColor(COLOR_WINDOW); values[1]=GetSysColor(COLOR_WINDOWTEXT);
        values[2]=GetSysColor(COLOR_BTNFACE); values[3]=GetSysColor(COLOR_WINDOWFRAME);
        values[4]=GetSysColor(COLOR_HIGHLIGHT); values[5]=GetSysColor(COLOR_HIGHLIGHTTEXT);
        values[6]=GetSysColor(COLOR_GRAYTEXT);
        values[7]=GetSysColor(COLOR_BTNFACE); values[8]=GetSysColor(COLOR_HIGHLIGHT);
    }
    if (view->background==values[0] && view->text==values[1] && view->button_background==values[2] &&
        view->border==values[3] && view->selected==values[4] && view->selected_text==values[5] && view->disabled_text==values[6] &&
        view->hover==values[7] && view->accent==values[8]) return 0;
    view->background=values[0]; view->text=values[1]; view->button_background=values[2]; view->border=values[3];
    view->selected=values[4]; view->selected_text=values[5]; view->disabled_text=values[6];
    view->hover=values[7]; view->accent=values[8];
    return 1;
}

static int about_text_lines(HDC dc, AboutWindow *view, int width, RECT *draw, int paint)
{
    const WCHAR *lines[3]={L"Copyright (C) Techland, 2001-2015",L"Editor 6.2022.0317.2106 x64",view->image.build_label};
    RECT text; int i, top=draw ? draw->top : 0, gap=MulDiv(2,view->dpi,96);
    for (i=0;i<3;++i) {
        text.left=0; text.top=0; text.right=width>0 ? width : 1; text.bottom=0;
        DrawTextW(dc,lines[i],-1,&text,DT_LEFT|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
        if (text.bottom<=0) text.bottom=MulDiv(15,view->dpi,96);
        if (paint && draw && top<draw->bottom) {
            RECT line=*draw; line.top=top; line.bottom=top+text.bottom;
            if (line.bottom>draw->bottom) line.bottom=draw->bottom;
            DrawTextW(dc,lines[i],-1,&line,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        }
        top+=text.bottom+gap;
    }
    return top-(draw ? draw->top : 0);
}

static int about_render_layered(HWND window, AboutWindow *view)
{
    SplashImages *im=&view->image; RECT outer,text,button; POINT where,from={0,0}; SIZE size;
    UINT width,height,x,y; int pad,button_width,button_height,text_height; BYTE *source;
    HBRUSH brush; HGDIOBJ old_font=NULL; COLORREF face,foreground;
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if (view->closing || !im->count || !im->output_dc || !GetWindowRect(window,&outer) || outer.right<=outer.left || outer.bottom<=outer.top) return 0;
    width=(UINT)(outer.right-outer.left); height=(UINT)(outer.bottom-outer.top);
    if (!ensure_output(im,width,height)) return 0;
    source=copy_frame(im,width,height);
    if (im->font) old_font=SelectObject(im->output_dc,im->font);
    SetBkMode(im->output_dc,TRANSPARENT); SetTextColor(im->output_dc,RGB(255,255,255));
    pad=MulDiv(14,view->dpi,96); if (pad<4) pad=4;
    button_width=MulDiv(100,view->dpi,96); button_height=MulDiv(30,view->dpi,96);
    if (button_width>(int)width/3) button_width=(int)width/3;
    if (button_height>(int)height/5) button_height=(int)height/5;
    if (button_width<1 || button_height<1) { if (old_font && old_font!=HGDI_ERROR) SelectObject(im->output_dc,old_font); return 0; }
    button.left=((int)width-button_width)/2; button.right=button.left+button_width;
    button.bottom=(LONG)height-pad; if (button.bottom<button_height) button.bottom=button_height;
    button.top=button.bottom-button_height; view->button_rect=button;
    text.left=MulDiv(50,(int)width,(int)im->width); if (text.left<pad) text.left=pad;
    text.right=button.left-pad;
    text.bottom=(LONG)height-pad;
    if (text.right-text.left<MulDiv(150,view->dpi,96)) {
        text.right=(LONG)(width*2U/3U); text.bottom=button.top-pad;
    }
    if (text.right>text.left && text.bottom>0) {
        text_height=about_text_lines(im->output_dc,view,text.right-text.left,NULL,0);
        text.top=text.bottom-text_height; if (text.top<pad) text.top=pad;
        (void)about_text_lines(im->output_dc,view,text.right-text.left,&text,1);
    } else SetRectEmpty(&text);
    face=view->pressed ? view->selected : view->hot ? view->hover : view->button_background;
    foreground=view->pressed ? view->selected_text : view->text;
    brush=CreateSolidBrush(face); if (brush) { FillRect(im->output_dc,&button,brush); DeleteObject(brush); }
    brush=CreateSolidBrush(view->focused ? view->accent : view->border);
    if (brush) { FrameRect(im->output_dc,&button,brush); DeleteObject(brush); }
    SetTextColor(im->output_dc,foreground);
    { RECT label=button; DrawTextW(im->output_dc,L"OK",-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); }
    if (view->focused) { RECT focus=button; InflateRect(&focus,-3,-3); DrawFocusRect(im->output_dc,&focus); }
    if (old_font && old_font!=HGDI_ERROR) SelectObject(im->output_dc,old_font);
    GdiFlush();
    if (!IsRectEmpty(&text)) restore_text_alpha(im,source,text);
    for (y=(UINT)button.top; y<(UINT)button.bottom && y<height; ++y)
        for (x=(UINT)button.left; x<(UINT)button.right && x<width; ++x)
            *(uint32_t *)(im->output_bits+((size_t)y*width+x)*4U)|=0xff000000U;
    where.x=outer.left; where.y=outer.top; size.cx=(LONG)width; size.cy=(LONG)height;
    return UpdateLayeredWindow(window,NULL,&where,&size,im->output_dc,&from,0,&blend,ULW_ALPHA)!=0;
}

static int about_footer(HWND window, AboutWindow *view, int width)
{
    HDC dc=GetDC(window); HGDIOBJ old=NULL; int result, pad=MulDiv(14,view->dpi,96);
    if (!dc) return MulDiv(130,view->dpi,96);
    if (view->image.font) old=SelectObject(dc,view->image.font);
    result=about_text_lines(dc,view,width-2*pad,NULL,0)+3*pad+MulDiv(28,view->dpi,96);
    if (old && old!=HGDI_ERROR) SelectObject(dc,old);
    ReleaseDC(window,dc); return result;
}

static void about_layout(HWND window, AboutWindow *view)
{
    RECT client; int pad, button_width, button_height;
    if (!view || !GetClientRect(window,&client)) return;
    view->footer_height=about_footer(window,view,client.right);
    if (!view->button) return;
    pad=MulDiv(14,view->dpi,96); button_width=MulDiv(84,view->dpi,96); button_height=MulDiv(28,view->dpi,96);
    (void)SetWindowPos(view->button,NULL,client.right-pad-button_width,client.bottom-pad-button_height,
                       button_width,button_height,SWP_NOZORDER|SWP_NOACTIVATE);
}

static void about_paint(HWND window, AboutWindow *view)
{
    PAINTSTRUCT paint; RECT client, footer, text, image_area, intersection; BITMAPINFO info; HBRUSH brush; HDC dc; HGDIOBJ old_font=NULL;
    UINT frame; int image_height, draw_height, image_width, image_top, image_left, pad;
    if (!view || !GetClientRect(window,&client)) return;
    dc=BeginPaint(window,&paint); if (!dc) return;
    image_height=view->image.count ? client.bottom-view->footer_height : 0;
    if (image_height<0) image_height=0;
    image_area=client; image_area.bottom=image_height;
    if (view->image.count && image_height>0 && IntersectRect(&intersection,&paint.rcPaint,&image_area) &&
        view->image.output_dc && ensure_output(&view->image,(UINT)client.right,(UINT)image_height)) {
        HDC buffer=view->image.output_dc;
        brush=CreateSolidBrush(view->background);
        if (brush) { FillRect(buffer,&image_area,brush); DeleteObject(brush); }
        image_width=client.right; draw_height=MulDiv(image_width,(int)view->image.height,(int)view->image.width);
        if (draw_height>image_height) { draw_height=image_height; image_width=MulDiv(draw_height,(int)view->image.width,(int)view->image.height); }
        image_left=(client.right-image_width)/2; image_top=(image_height-draw_height)/2;
        frame=(UINT)(((GetTickCount64()-view->image.started)/40ULL)%view->image.count);
        ZeroMemory(&info,sizeof(info)); info.bmiHeader.biSize=sizeof(info.bmiHeader);
        info.bmiHeader.biWidth=(LONG)view->image.width; info.bmiHeader.biHeight=-(LONG)view->image.height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        (void)StretchDIBits(buffer,image_left,image_top,image_width,draw_height,0,0,(int)view->image.width,(int)view->image.height,
                           view->image.pixels[frame],&info,DIB_RGB_COLORS,SRCCOPY);
        BitBlt(dc,0,0,client.right,image_height,buffer,0,0,SRCCOPY);
    }
    footer=client; footer.top=image_height;
    if (IntersectRect(&intersection,&paint.rcPaint,&footer)) {
        brush=CreateSolidBrush(view->background); if (brush) { FillRect(dc,&footer,brush); DeleteObject(brush); }
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,view->text);
        if (view->image.font) old_font=SelectObject(dc,view->image.font);
        pad=MulDiv(14,view->dpi,96); text=footer; text.left+=pad; text.right-=pad; text.top+=pad;
        (void)about_text_lines(dc,view,text.right-text.left,&text,1);
        if (old_font && old_font!=HGDI_ERROR) SelectObject(dc,old_font);
    }
    EndPaint(window,&paint);
}

static void about_button(AboutWindow *view, const DRAWITEMSTRUCT *draw)
{
    HBRUSH brush; RECT text, focus; COLORREF background, foreground; HGDIOBJ old=NULL;
    if (!view || !draw || draw->CtlType!=ODT_BUTTON || draw->CtlID!=IDOK) return;
    background=(draw->itemState & ODS_SELECTED) ? view->selected : view->button_background;
    foreground=(draw->itemState & (ODS_DISABLED|ODS_GRAYED)) ? view->disabled_text :
        (draw->itemState & ODS_SELECTED) ? view->selected_text : view->text;
    brush=CreateSolidBrush(background); if (brush) { FillRect(draw->hDC,&draw->rcItem,brush); DeleteObject(brush); }
    brush=CreateSolidBrush(view->border); if (brush) { FrameRect(draw->hDC,&draw->rcItem,brush); DeleteObject(brush); }
    text=draw->rcItem; SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,foreground);
    if (view->image.font) old=SelectObject(draw->hDC,view->image.font);
    DrawTextW(draw->hDC,L"OK",-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    if (old && old!=HGDI_ERROR) SelectObject(draw->hDC,old);
    if (draw->itemState & ODS_FOCUS) { focus=draw->rcItem; InflateRect(&focus,-3,-3); DrawFocusRect(draw->hDC,&focus); }
}

static int about_rectangle(HWND owner, AboutWindow *view, RECT *outer);

static int about_adjust_rect(RECT *rect, int dpi)
{
    HMODULE user=GetModuleHandleW(L"user32.dll");
    BOOL (WINAPI *adjust)(LPRECT,DWORD,BOOL,DWORD,UINT)=NULL;
    if (user) *(FARPROC *)&adjust=GetProcAddress(user,"AdjustWindowRectExForDpi");
    if (adjust) return adjust(rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME,(UINT)dpi)!=0;
    return AdjustWindowRectEx(rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME)!=0;
}

static void about_set_dpi(HWND window, AboutWindow *view, int dpi)
{
    HFONT font, old;
    if (dpi<48 || dpi>768) return;
    font=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (!font) return;
    old=view->image.font; view->image.font=font; view->dpi=dpi;
    if (view->button) SendMessageW(view->button,WM_SETFONT,(WPARAM)font,TRUE);
    if (old) DeleteObject(old);
    if (!view->layered) about_layout(window,view);
}

static LRESULT CALLBACK about_window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    AboutWindow *view=(AboutWindow *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message==WM_NCCREATE) {
        view=(AboutWindow *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)view);
    }
    if (message==WM_CREATE && view) {
        (void)about_palette(view);
        if (view->layered) return 0;
        view->button=CreateWindowExW(0,L"Button",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                                    0,0,1,1,window,(HMENU)(ULONG_PTR)IDOK,GetModuleHandleW(NULL),NULL);
        if (!view->button) return -1;
        if (view->image.font) SendMessageW(view->button,WM_SETFONT,(WPARAM)view->image.font,TRUE);
        about_layout(window,view); return 0;
    }
    if (message==WM_SIZE && view) { if (!view->layered) about_layout(window,view); return 0; }
    if (message==WM_DPICHANGED && view && lp) {
        RECT rectangle=*(const RECT *)lp;
        about_set_dpi(window,view,(int)HIWORD(wp));
        if (view->layered) {
            RECT native;
            if (GetWindowRect(window,&native) && fit_window(native,view->image.width,view->image.height,&rectangle)) {
                SetWindowPos(window,NULL,rectangle.left,rectangle.top,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
                             SWP_NOZORDER|SWP_NOACTIVATE);
                (void)about_render_layered(window,view);
            }
            return 0;
        }
        SetWindowPos(window,NULL,rectangle.left,rectangle.top,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
                     SWP_NOZORDER|SWP_NOACTIVATE);
        if (about_rectangle(window,view,&rectangle))
            SetWindowPos(window,NULL,rectangle.left,rectangle.top,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
                         SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(window,NULL,FALSE); return 0;
    }
    if (message==WM_DRAWITEM && view && wp==IDOK && lp) { about_button(view,(const DRAWITEMSTRUCT *)lp); return TRUE; }
    if (message==WM_ERASEBKGND) return 1;
    if (message==WM_PAINT && view) {
        if (view->layered) { PAINTSTRUCT paint; BeginPaint(window,&paint); EndPaint(window,&paint); }
        else about_paint(window,view);
        return 0;
    }
    if (view && view->layered && message==WM_NCHITTEST) {
        POINT point; UINT x,y; uint32_t pixel;
        point.x=(short)LOWORD(lp); point.y=(short)HIWORD(lp);
        if (!ScreenToClient(window,&point)) return HTNOWHERE;
        if (PtInRect(&view->button_rect,point)) return HTCLIENT;
        if (!view->image.output_bits || point.x<0 || point.y<0) return HTTRANSPARENT;
        x=(UINT)point.x; y=(UINT)point.y;
        if (x>=view->image.out_width || y>=view->image.out_height) return HTTRANSPARENT;
        pixel=*(const uint32_t *)(view->image.output_bits+((size_t)y*view->image.out_width+x)*4U);
        return (pixel>>24) ? HTCAPTION : HTTRANSPARENT;
    }
    if (view && view->layered && message==WM_LBUTTONDOWN) {
        POINT point={(short)LOWORD(lp),(short)HIWORD(lp)};
        if (PtInRect(&view->button_rect,point)) { view->pressed=1; view->hot=1; SetCapture(window); SetFocus(window); (void)about_render_layered(window,view); }
        return 0;
    }
    if (view && view->layered && message==WM_LBUTTONUP) {
        POINT point={(short)LOWORD(lp),(short)HIWORD(lp)}; int activate=view->pressed && PtInRect(&view->button_rect,point);
        view->pressed=0; if (GetCapture()==window) ReleaseCapture();
        if (activate) { view->closing=1; DestroyWindow(window); } else (void)about_render_layered(window,view);
        return 0;
    }
    if (view && view->layered && message==WM_MOUSEMOVE) {
        POINT point={(short)LOWORD(lp),(short)HIWORD(lp)}; int hot=PtInRect(&view->button_rect,point)!=0;
        if (hot!=view->hot) { TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0}; view->hot=hot; (void)TrackMouseEvent(&track); (void)about_render_layered(window,view); }
        return 0;
    }
    if (view && view->layered && message==WM_MOUSELEAVE) { view->hot=0; (void)about_render_layered(window,view); return 0; }
    if (view && view->layered && message==WM_CAPTURECHANGED && view->pressed) { view->pressed=0; (void)about_render_layered(window,view); return 0; }
    if (view && view->layered && (message==WM_SETFOCUS || message==WM_KILLFOCUS)) {
        view->focused=message==WM_SETFOCUS; if (!view->focused) view->pressed=0;
        (void)about_render_layered(window,view); return 0;
    }
    if (view && view->layered && message==WM_KEYDOWN && wp==VK_SPACE) {
        view->pressed=1; (void)about_render_layered(window,view); return 0;
    }
    if (view && view->layered && message==WM_KEYUP && wp==VK_SPACE && view->pressed) {
        view->pressed=0; view->closing=1; DestroyWindow(window); return 0;
    }
    if (message==WM_TIMER && wp==SPLASH_TICK && view) {
        RECT image;
        if (view->layered) (void)about_render_layered(window,view);
        else if (view->image.count && GetClientRect(window,&image)) {
            image.bottom-=view->footer_height;
            if (image.bottom>0) InvalidateRect(window,&image,FALSE);
        }
        if (++view->ticks>=25U) {
            view->ticks=0;
            if (about_palette(view)) {
                if (view->layered) (void)about_render_layered(window,view);
                else { InvalidateRect(window,NULL,FALSE); InvalidateRect(view->button,NULL,FALSE); }
            }
        }
        return 0;
    }
    if ((message==WM_THEMECHANGED || message==WM_SYSCOLORCHANGE || message==WM_SETTINGCHANGE) && view) {
        (void)about_palette(view);
        if (view->layered) (void)about_render_layered(window,view);
        else { InvalidateRect(window,NULL,FALSE); InvalidateRect(view->button,NULL,FALSE); }
    }
    if (message==SPLASH_WAKE) { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); return 0; }
    if (message==WM_COMMAND && LOWORD(wp)==IDOK) { if (view) view->closing=1; DestroyWindow(window); return 0; }
    if (message==WM_CLOSE) { if (view) view->closing=1; DestroyWindow(window); return 0; }
    if (message==WM_DESTROY) { if (view) view->closing=1; PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}

static int about_rectangle(HWND owner, AboutWindow *view, RECT *outer)
{
    HMONITOR monitor=MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST); MONITORINFO info;
    RECT frame={0,0,0,0}; int work_width,work_height,width,height,footer,pad;
    if (view->layered) {
        RECT native;
        return GetWindowRect(owner,&native) && fit_window(native,view->image.width,view->image.height,outer);
    }
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor,&info) || !about_adjust_rect(&frame,view->dpi)) return 0;
    pad=MulDiv(16,view->dpi,96);
    work_width=info.rcWork.right-info.rcWork.left-(frame.right-frame.left)-2*pad;
    work_height=info.rcWork.bottom-info.rcWork.top-(frame.bottom-frame.top)-2*pad;
    if (work_width<=0 || work_height<=0) return 0;
    width=view->image.count ? (int)view->image.width : MulDiv(520,view->dpi,96);
    if (width>work_width) width=work_width;
    footer=about_footer(owner,view,width);
    if (footer>work_height && width<work_width) { width=work_width; footer=about_footer(owner,view,width); }
    if (footer>work_height) return 0;
    height=view->image.count ? MulDiv(width,(int)view->image.height,(int)view->image.width) : 0;
    if (height>work_height-footer) height=work_height-footer;
    outer->left=0; outer->top=0; outer->right=width; outer->bottom=height+footer;
    if (!about_adjust_rect(outer,view->dpi)) return 0;
    width=outer->right-outer->left; height=outer->bottom-outer->top;
    outer->left=info.rcWork.left+(info.rcWork.right-info.rcWork.left-width)/2;
    outer->top=info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-height)/2;
    outer->right=outer->left+width; outer->bottom=outer->top+height;
    return 1;
}

static DWORD WINAPI about_worker(void *unused)
{
    AboutWindow view; WNDCLASSEXW wc; RECT outer; HWND window=NULL,owner; HANDLE stop_event; uint64_t epoch;
    MSG msg; DWORD wait_result; int registered=0,quit=0,shown=0;
    (void)unused; ZeroMemory(&view,sizeof(view)); ZeroMemory(&wc,sizeof(wc));
    AcquireSRWLockShared(&about.lock); stop_event=about.stop_event; owner=about.owner; epoch=about.epoch; ReleaseSRWLockShared(&about.lock);
    if (!about_owner_valid(owner) || !about_match_dpi(owner)) goto done;
    (void)decode_images(&view.image,stop_event);
    if (!about_owner_valid(owner) || WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT) goto done;
    if (view.image.count) {
        view.image.output_dc=CreateCompatibleDC(NULL);
        if (!view.image.output_dc) release_images(&view.image);
    }
    view.layered=view.image.count!=0;
    view.dpi=(int)about_dpi(owner); build_label(view.image.build_label);
    view.image.font=CreateFontW(-MulDiv(9,view.dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (!about_rectangle(owner,&view,&outer)) goto done;
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=about_window_proc; wc.hInstance=GetModuleHandleW(NULL);
    wc.lpszClassName=about_class; wc.hCursor=LoadCursorW(NULL,MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&wc)) goto done;
    registered=1;
    window=CreateWindowExW(view.layered ? WS_EX_LAYERED : WS_EX_DLGMODALFRAME,about_class,L"About Dying Light Developer Tools",
        view.layered ? WS_POPUP : WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
        outer.left,outer.top,outer.right-outer.left,outer.bottom-outer.top,
        owner,NULL,wc.hInstance,&view);
    if (!window || WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT || !about_owner_valid(owner)) goto done;
    view.image.started=GetTickCount64();
    if (view.layered && !about_render_layered(window,&view)) goto done;
    AcquireSRWLockExclusive(&about.lock); about.window=window; ReleaseSRWLockExclusive(&about.lock);
    if (!SetTimer(window,SPLASH_TICK,40,NULL)) goto done;
    ShowWindow(window,SW_SHOW); SetForegroundWindow(window); SetFocus(view.layered ? window : view.button); shown=1;
    while (!quit) {
        wait_result=MsgWaitForMultipleObjects(1,&stop_event,FALSE,INFINITE,QS_ALLINPUT);
        if (wait_result!=WAIT_OBJECT_0+1U) break;
        while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {
            if (msg.message==WM_QUIT || WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0) { quit=1; break; }
            if (msg.message==WM_KEYDOWN && (msg.wParam==VK_ESCAPE || msg.wParam==VK_RETURN)) { view.closing=1; DestroyWindow(window); continue; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }
done:
    AcquireSRWLockExclusive(&about.lock); if (about.window==window) about.window=NULL; ReleaseSRWLockExclusive(&about.lock);
    if (!shown && stop_event && WaitForSingleObject(stop_event,0)==WAIT_TIMEOUT && about_owner_valid(owner) && about_failed_message)
        (void)PostMessageW(owner,about_failed_message,(WPARAM)epoch,0);
    if (window && IsWindow(window)) { view.closing=1; KillTimer(window,SPLASH_TICK); DestroyWindow(window); }
    if (registered) UnregisterClassW(about_class,wc.hInstance);
    release_images(&view.image);
    return 0;
}

static void about_reap(void)
{
    if (about.thread && WaitForSingleObject(about.thread,0)==WAIT_OBJECT_0) {
        CloseHandle(about.thread); about.thread=NULL;
        if (about.stop_event) { CloseHandle(about.stop_event); about.stop_event=NULL; }
        if (GetPropW(about.owner,about_owner_property)==&about) RemovePropW(about.owner,about_owner_property);
        about.owner=NULL;
    }
}

static int about_open(HWND owner)
{
    int ok=1;
    AcquireSRWLockExclusive(&about.lock);
    about_reap();
    if (about.shutting_down) ok=0;
    else if (about.thread) {
        if (about.window) (void)PostMessageW(about.window,SPLASH_WAKE,0,0);
    } else {
        if (!++about.epoch) ++about.epoch;
        about.owner=owner;
        if (!SetPropW(owner,about_owner_property,&about)) { about.owner=NULL; ok=0; }
        if (ok) about.stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
        if (about.stop_event) about.thread=CreateThread(NULL,0,about_worker,NULL,0,NULL);
        if (!about.thread) {
            if (about.stop_event) CloseHandle(about.stop_event);
            if (GetPropW(owner,about_owner_property)==&about) RemovePropW(owner,about_owner_property);
            about.stop_event=NULL; about.owner=NULL; ok=0;
        }
    }
    ReleaseSRWLockExclusive(&about.lock);
    return ok;
}

static void about_stop(void)
{
    AcquireSRWLockExclusive(&about.lock);
    about.shutting_down=1;
    if (about.stop_event) (void)SetEvent(about.stop_event);
    ReleaseSRWLockExclusive(&about.lock);
}

static void splash_admit(HWND window)
{
    BYTE *object; RECT rect; HANDLE thread, stop_event;
    if (InterlockedCompareExchange(&splash.stopping,0,0) || !IsWindowVisible(window) ||
        GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return;
    object=splash_object(window); if (!object || !GetWindowRect(window,&rect)) return;
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.thread || InterlockedCompareExchange(&splash.stopping,0,0)) { ReleaseSRWLockExclusive(&splash.lock); return; }
    splash.native=window; splash.native_rect=rect;
    ReleaseSRWLockExclusive(&splash.lock);
    dluma_log_pair("INFO","editor_splash","native_admitted");
    capture_status(window,object);
    if (InterlockedCompareExchange(&splash.stopping,0,0) || !hide_native_splash(window)) {
        AcquireSRWLockExclusive(&splash.lock); splash.native=NULL; ReleaseSRWLockExclusive(&splash.lock); return;
    }
    AcquireSRWLockExclusive(&splash.lock);
    if (InterlockedCompareExchange(&splash.stopping,0,0)) { ReleaseSRWLockExclusive(&splash.lock); sync_native_visibility(window); return; }
    stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!stop_event) { InterlockedExchange(&splash.worker_failed,1); ReleaseSRWLockExclusive(&splash.lock); sync_native_visibility(window); return; }
    splash.stop_event=stop_event;
    thread=CreateThread(NULL,0,splash_worker,NULL,0,NULL);
    if (!thread) { splash.stop_event=NULL; CloseHandle(stop_event); InterlockedExchange(&splash.worker_failed,1); ReleaseSRWLockExclusive(&splash.lock); sync_native_visibility(window); return; }
    splash.thread=thread;
    ReleaseSRWLockExclusive(&splash.lock);
}
static int splash_listener(HWND window, unsigned int stage, UINT message, WPARAM wp, LPARAM lp, LRESULT *result, void *context)
{
    HWND native, worker, visibility_window; BYTE *object; RECT rect; int handled=0;
    (void)context;
    InterlockedIncrement(&splash.callbacks);
    AcquireSRWLockShared(&splash.lock); native=splash.native; worker=splash.worker; visibility_window=splash.visibility_window; ReleaseSRWLockShared(&splash.lock);
    if (stage==DLUMA_EDITOR_UI_BEFORE && message==WM_COMMAND && LOWORD(wp)==57664U && HIWORD(wp)<=1U && !lp &&
        dluma_editor_is_main_window(window,GetCurrentProcessId()) && about_open(window)) {
        if (result) *result=0; handled=1;
    }
    if (stage==DLUMA_EDITOR_UI_BEFORE && message==about_failed_message &&
        dluma_editor_is_main_window(window,GetCurrentProcessId())) {
        int current;
        AcquireSRWLockShared(&about.lock);
        current=about.owner==window && about.epoch==(uint64_t)wp && !about.shutting_down;
        ReleaseSRWLockShared(&about.lock);
        if (current) MessageBoxW(window,L"About could not be opened.",L"About",MB_OK|MB_ICONINFORMATION);
        if (result) *result=0; handled=1;
    }
    if (stage==DLUMA_EDITOR_UI_BEFORE && (message==WM_DESTROY || message==WM_NCDESTROY)) {
        int is_owner;
        AcquireSRWLockShared(&about.lock); is_owner=about.owner==window; ReleaseSRWLockShared(&about.lock);
        if (is_owner) about_stop();
    }
    if (stage==DLUMA_EDITOR_UI_BEFORE && message==splash.visibility_message && (window==native || window==visibility_window)) {
        sync_native_visibility(window); if (result) *result=0; handled=1;
    }
    if (!native && ((stage==DLUMA_EDITOR_UI_BEFORE && (message==DLUMA_EDITOR_UI_ENROLLED || message==DLUMA_EDITOR_UI_REFRESH)) ||
        ((stage==DLUMA_EDITOR_UI_BEFORE || stage==DLUMA_EDITOR_UI_AFTER) &&
         (message==WM_CREATE || message==WM_SHOWWINDOW)))) splash_admit(window);
    if (window==native && native) {
        if (stage==DLUMA_EDITOR_UI_BEFORE && message==WM_WINDOWPOSCHANGING && lp &&
            !InterlockedCompareExchange(&splash.stopping,0,0) && !InterlockedCompareExchange(&splash.worker_failed,0,0) &&
            GetPropW(window,visibility_property)==&splash) {
            WINDOWPOS *position=(WINDOWPOS *)lp;
            if (position->flags & SWP_SHOWWINDOW) position->flags=(position->flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
        }
        if ((stage==DLUMA_EDITOR_UI_BEFORE || stage==DLUMA_EDITOR_UI_AFTER) && message==WM_PAINT) {
            object=splash_object(window); if (object) { capture_status(window,object); sync_native_visibility(window); }
        }
        if (stage==DLUMA_EDITOR_UI_AFTER && message==WM_WINDOWPOSCHANGED && GetWindowRect(window,&rect)) {
            AcquireSRWLockExclusive(&splash.lock); splash.native_rect=rect; worker=splash.worker; ReleaseSRWLockExclusive(&splash.lock);
            if (worker) PostMessageW(worker,SPLASH_WAKE,0,0);
        }
        if (stage==DLUMA_EDITOR_UI_BEFORE && (message==WM_DESTROY || message==WM_NCDESTROY)) {
            dluma_log_pair("INFO","editor_splash_stop","native_destroyed");
            InterlockedExchange(&splash.stopping,1);
            AcquireSRWLockExclusive(&splash.lock); splash.completed=1; ReleaseSRWLockExclusive(&splash.lock);
            release_native_splash(window,0);
            AcquireSRWLockExclusive(&splash.lock);
            splash.native=NULL;
            ReleaseSRWLockExclusive(&splash.lock);
            splash_end_startup();
        } else if (stage==DLUMA_EDITOR_UI_DESTROY && message==0) splash_end_startup();
    }
    if (native && window!=native && !InterlockedCompareExchange(&splash.stopping,0,0) &&
        ((stage==DLUMA_EDITOR_UI_AFTER && message==WM_SHOWWINDOW && wp) ||
         (stage==DLUMA_EDITOR_UI_BEFORE && (message==DLUMA_EDITOR_UI_ENROLLED || message==DLUMA_EDITOR_UI_REFRESH))) &&
        IsWindowVisible(window) && dluma_editor_is_main_window(window,GetCurrentProcessId())) {
        dluma_log_pair("INFO","editor_splash_stop","main_window_visible");
        AcquireSRWLockExclusive(&splash.lock); splash.completed=1; ReleaseSRWLockExclusive(&splash.lock);
        splash_end_startup();
    }
    InterlockedDecrement(&splash.callbacks);
    return handled;
}
int dluma_editor_splash_start(void)
{
    BYTE **global, *object; HWND native=NULL;
    AcquireSRWLockExclusive(&about.lock);
    about_reap();
    if (about.thread) { ReleaseSRWLockExclusive(&about.lock); return 0; }
    about.shutting_down=0;
    ReleaseSRWLockExclusive(&about.lock);
    InterlockedExchange(&splash.stopping,0);
    splash.visibility_message=RegisterWindowMessageW(L"DLUMA.Editor.Splash.Visibility.Sync.1");
    about_failed_message=RegisterWindowMessageW(L"DLUMA.Editor.About.Failed.1");
    if (!splash.visibility_message || !about_failed_message) return 0;
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR)) {
        dluma_log_pair("WARN","editor_splash","profile_unavailable"); return 0;
    }
    if (!dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_SPLASH,splash_listener,NULL)) {
        dluma_log_pair("WARN","editor_splash","subscription_failed"); return 0;
    }
    splash.subscribed=1;
    if (!redraw_install()) {
        dluma_log_pair("WARN","editor_splash","progress_hook_unavailable");
        dluma_editor_splash_stop(); return 0;
    }
    dluma_log_pair("INFO","editor_splash","watching_startup");
    global=(BYTE **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0x1B709B8U,sizeof(void *));
    __try { object=global ? *global : NULL; if (object) native=*(HWND *)(object+64U); }
    __except(EXCEPTION_EXECUTE_HANDLER) { native=NULL; }
    if (native && splash_object(native)) (void)dluma_editor_ui_request_enroll(native);
    return 1;
}
static void splash_end_startup(void)
{
    HWND native;
    InterlockedExchange(&splash.stopping,1);
    redraw_restore();
    AcquireSRWLockShared(&splash.lock);
    if (splash.stop_event) (void)SetEvent(splash.stop_event);
    native=splash.visibility_window;
    ReleaseSRWLockShared(&splash.lock);
    if (native) {
        if (GetWindowThreadProcessId(native,NULL)==GetCurrentThreadId()) sync_native_visibility(native);
        else if (splash.visibility_message) (void)PostMessageW(native,splash.visibility_message,0,0);
    }
}
void dluma_editor_splash_stop(void)
{
    int visibility_pending;
    about_stop();
    splash_end_startup();
    AcquireSRWLockShared(&splash.lock); visibility_pending=splash.visibility_active || splash.visibility_busy; ReleaseSRWLockShared(&splash.lock);
    AcquireSRWLockShared(&redraw_lock);
    if (!visibility_pending && !redraw_installed && !redraw_pending && splash.subscribed && dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_SPLASH)) splash.subscribed=0;
    ReleaseSRWLockShared(&redraw_lock);
}
int dluma_editor_splash_callbacks_drained(void)
{
    int drained, redraw_drained, about_drained;
    AcquireSRWLockShared(&redraw_lock); redraw_drained=!redraw_installed && !redraw_pending; ReleaseSRWLockShared(&redraw_lock);
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.thread && WaitForSingleObject(splash.thread,0)==WAIT_OBJECT_0) {
        CloseHandle(splash.thread); splash.thread=NULL;
        if (splash.stop_event) { CloseHandle(splash.stop_event); splash.stop_event=NULL; }
    }
    drained=!splash.thread && !splash.subscribed && !splash.visibility_active && !splash.visibility_busy;
    ReleaseSRWLockExclusive(&splash.lock);
    AcquireSRWLockExclusive(&about.lock); about_reap(); about_drained=!about.thread; ReleaseSRWLockExclusive(&about.lock);
    return drained && about_drained && redraw_drained && InterlockedCompareExchange(&redraw_callbacks,0,0)==0 && InterlockedCompareExchange(&splash.callbacks,0,0)==0;
}
