#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <stdint.h>
#include <stddef.h>
#include "dlw_editor_ui.h"
#include "dlw_editor_theme_profile.h"
#include "dlw_editor_splash.h"

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
static const GUID frame_time = {0x6aedbd6d,0x3fb5,0x418a,{0x83,0xa6,0x7f,0x45,0x22,0x9d,0xc8,0x72}};
static const WCHAR splash_class[] = L"DLUMA.Editor.Splash.1";
static const WCHAR visibility_property[] = L"DLUMA.Editor.Splash.Visibility.1";

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
static SplashImages images;

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
static int render_splash(HWND window)
{
    SplashImages *im=&images; RECT native, outer, text, status_rect, version_rect, build_rect; WCHAR status[2048], version[2048];
    UINT frame, x, y, width, height; BYTE *dst, *src; POINT where, from={0,0}; SIZE size;
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    AcquireSRWLockShared(&splash.lock);
    native=splash.native_rect; lstrcpynW(status,splash.status,2048); lstrcpynW(version,splash.version,2048);
    ReleaseSRWLockShared(&splash.lock);
    if (!fit_window(native,im->width,im->height,&outer)) return 0;
    width=(UINT)(outer.right-outer.left); height=(UINT)(outer.bottom-outer.top);
    if (!ensure_output(im,width,height)) return 0;
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
    if (message==WM_TIMER && wp==SPLASH_TICK) { if (!render_splash(window)) DestroyWindow(window); return 0; }
    if (message==SPLASH_WAKE) { if (render_splash(window)) SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW); else DestroyWindow(window); return 0; }
    if (message==WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
static DWORD WINAPI splash_worker(void *unused)
{
    GpApi gp; GpStartup startup; ULONG_PTR token=0; IStream *stream=NULL; void *image=NULL; HMODULE ole_module=NULL;
    UINT width=0,height=0,count=0,i,y; GpBitmapData data; GpRect rect; HWND window=NULL, native;
    WNDCLASSEXW wc; MSG msg; int gdiplus_started=0, registered=0, quit=0; RECT native_rect, outer; HANDLE stop_event; DWORD wait_result;
    (void)unused; ZeroMemory(&gp,sizeof(gp)); ZeroMemory(&images,sizeof(images)); ZeroMemory(&wc,sizeof(wc));
    build_label(images.build_label);
    PeekMessageW(&msg,NULL,WM_USER,WM_USER,PM_NOREMOVE);
    AcquireSRWLockShared(&splash.lock); stop_event=splash.stop_event; ReleaseSRWLockShared(&splash.lock);
    if (!stop_event) goto done;
    if (InterlockedCompareExchange(&splash.stopping,0,0)) goto done;
    if (!gp_open(&gp)) goto done;
    ZeroMemory(&startup,sizeof(startup)); startup.version=1; startup.suppress_codecs=TRUE;
    if (gp.startup(&token,&startup,NULL)) goto done;
    gdiplus_started=1; stream=splash_stream(&ole_module); if (!stream) goto done;
    if (gp.load(stream,&image) || !image || gp.width(image,&width) || gp.height(image,&height) ||
        !width || !height || width>SPLASH_MAX_DIMENSION || height>SPLASH_MAX_DIMENSION ||
        gp.frame_count(image,&frame_time,&count) || !count || count>SPLASH_FRAME_LIMIT ||
        (uint64_t)width*height*4U*count>SPLASH_MAX_DECODED) goto done;
    images.width=width; images.height=height; images.count=count;
    rect.x=rect.y=0; rect.width=(INT)width; rect.height=(INT)height;
    for (i=0; i<count; ++i) {
        if (InterlockedCompareExchange(&splash.stopping,0,0) || gp.select_frame(image,&frame_time,i)) goto done;
        images.frames[i]=make_dib(width,height,(void **)&images.pixels[i]);
        if (!images.frames[i]) goto done;
        ZeroMemory(&data,sizeof(data));
        if (gp.lock(image,&rect,1,SPLASH_ARGB,&data)) goto done;
        if (!data.scan || data.width!=width || data.height!=height || data.stride==0 ||
            (uint64_t)(data.stride<0 ? -(int64_t)data.stride : data.stride)<(uint64_t)width*4U) {
            (void)gp.unlock(image,&data); goto done;
        }
        for (y=0; y<height; ++y)
            CopyMemory(images.pixels[i]+(size_t)y*width*4U,(BYTE *)data.scan+(ptrdiff_t)y*data.stride,(size_t)width*4U);
        if (gp.unlock(image,&data)) goto done;
    }
    gp.dispose(image); image=NULL;
    stream->lpVtbl->Release(stream); stream=NULL;
    if (ole_module) { FreeLibrary(ole_module); ole_module=NULL; }
    gp.shutdown(token); gdiplus_started=0; FreeLibrary(gp.module); gp.module=NULL;
    if (InterlockedCompareExchange(&splash.stopping,0,0)) goto done;
    images.output_dc=CreateCompatibleDC(NULL); if (!images.output_dc) goto done;
    images.font=CreateFontW(-MulDiv(9,GetDeviceCaps(images.output_dc,LOGPIXELSY),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (images.font) images.old_font=(HFONT)SelectObject(images.output_dc,images.font);
    AcquireSRWLockShared(&splash.lock); native=splash.native; native_rect=splash.native_rect; ReleaseSRWLockShared(&splash.lock);
    if (!native || !fit_window(native_rect,width,height,&outer)) goto done;
    ZeroMemory(&wc,sizeof(wc)); wc.cbSize=sizeof(wc); wc.lpfnWndProc=splash_window_proc; wc.hInstance=GetModuleHandleW(NULL); wc.lpszClassName=splash_class;
    if (!RegisterClassExW(&wc)) goto done;
    registered=1;
    window=CreateWindowExW(WS_EX_LAYERED|WS_EX_APPWINDOW|WS_EX_NOACTIVATE,splash_class,L"Dying Light Developer Tools",WS_POPUP,
                           outer.left,outer.top,outer.right-outer.left,outer.bottom-outer.top,NULL,NULL,wc.hInstance,NULL);
    if (!window) goto done;
    images.started=GetTickCount64();
    if (!render_splash(window) || InterlockedCompareExchange(&splash.stopping,0,0)) goto done;
    AcquireSRWLockExclusive(&splash.lock);
    if (InterlockedCompareExchange(&splash.stopping,0,0)) { ReleaseSRWLockExclusive(&splash.lock); goto done; }
    splash.worker=window; ReleaseSRWLockExclusive(&splash.lock);
    if (!SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)) goto done;
    if (!SetTimer(window,SPLASH_TICK,40,NULL)) goto done;
    InterlockedExchange(&splash.worker_ready,1);
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
    InterlockedExchange(&splash.worker_ready,0);
    if (!InterlockedCompareExchange(&splash.stopping,0,0)) InterlockedExchange(&splash.worker_failed,1);
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.worker==window) splash.worker=NULL;
    native=splash.visibility_window ? splash.visibility_window : splash.native;
    ReleaseSRWLockExclusive(&splash.lock);
    if (native && splash.visibility_message) (void)PostMessageW(native,splash.visibility_message,0,0);
    if (window && GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId()) { KillTimer(window,SPLASH_TICK); DestroyWindow(window); }
    if (registered) UnregisterClassW(splash_class,wc.hInstance);
    if (image && gp.dispose) gp.dispose(image);
    if (stream) stream->lpVtbl->Release(stream);
    if (ole_module) FreeLibrary(ole_module);
    if (gdiplus_started) gp.shutdown(token);
    if (gp.module) FreeLibrary(gp.module);
    if (images.old_font) SelectObject(images.output_dc,images.old_font);
    if (images.font) DeleteObject(images.font);
    if (images.output) { if (images.old_output) SelectObject(images.output_dc,images.old_output); DeleteObject(images.output); }
    if (images.output_dc) DeleteDC(images.output_dc);
    for (i=0; i<SPLASH_FRAME_LIMIT; ++i) if (images.frames[i]) DeleteObject(images.frames[i]);
    ZeroMemory(&images,sizeof(images));
    return 0;
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
            InterlockedExchange(&splash.stopping,1);
            AcquireSRWLockExclusive(&splash.lock); splash.completed=1; ReleaseSRWLockExclusive(&splash.lock);
            release_native_splash(window,0);
            AcquireSRWLockExclusive(&splash.lock);
            splash.native=NULL;
            ReleaseSRWLockExclusive(&splash.lock);
            dluma_editor_splash_stop();
        } else if (stage==DLUMA_EDITOR_UI_DESTROY && message==0) dluma_editor_splash_stop();
    }
    if (native && window!=native && !InterlockedCompareExchange(&splash.stopping,0,0) &&
        ((stage==DLUMA_EDITOR_UI_AFTER && message==WM_SHOWWINDOW && wp) ||
         (stage==DLUMA_EDITOR_UI_BEFORE && (message==DLUMA_EDITOR_UI_ENROLLED || message==DLUMA_EDITOR_UI_REFRESH))) &&
        IsWindowVisible(window) && dluma_editor_is_main_window(window,GetCurrentProcessId())) {
        AcquireSRWLockExclusive(&splash.lock); splash.completed=1; ReleaseSRWLockExclusive(&splash.lock);
        dluma_editor_splash_stop();
    }
    InterlockedDecrement(&splash.callbacks);
    return handled;
}
int dluma_editor_splash_start(void)
{
    BYTE **global, *object; HWND native=NULL;
    splash.visibility_message=RegisterWindowMessageW(L"DLUMA.Editor.Splash.Visibility.Sync.1");
    if (!splash.visibility_message) return 0;
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR) ||
        !dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_SPLASH,splash_listener,NULL)) return 0;
    splash.subscribed=1;
    if (!redraw_install()) { dluma_editor_splash_stop(); return 0; }
    global=(BYTE **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0x1B709B8U,sizeof(void *));
    __try { object=global ? *global : NULL; if (object) native=*(HWND *)(object+64U); }
    __except(EXCEPTION_EXECUTE_HANDLER) { native=NULL; }
    if (native && splash_object(native)) (void)dluma_editor_ui_request_enroll(native);
    return 1;
}
void dluma_editor_splash_stop(void)
{
    HWND native; int visibility_pending;
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
    AcquireSRWLockShared(&splash.lock); visibility_pending=splash.visibility_active || splash.visibility_busy; ReleaseSRWLockShared(&splash.lock);
    AcquireSRWLockShared(&redraw_lock);
    if (!visibility_pending && !redraw_installed && !redraw_pending && splash.subscribed && dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_SPLASH)) splash.subscribed=0;
    ReleaseSRWLockShared(&redraw_lock);
}
int dluma_editor_splash_callbacks_drained(void)
{
    int drained, redraw_drained;
    AcquireSRWLockShared(&redraw_lock); redraw_drained=!redraw_installed && !redraw_pending; ReleaseSRWLockShared(&redraw_lock);
    AcquireSRWLockExclusive(&splash.lock);
    if (splash.thread && WaitForSingleObject(splash.thread,0)==WAIT_OBJECT_0) {
        CloseHandle(splash.thread); splash.thread=NULL;
        if (splash.stop_event) { CloseHandle(splash.stop_event); splash.stop_event=NULL; }
    }
    drained=!splash.thread && !splash.subscribed && !splash.visibility_active && !splash.visibility_busy;
    ReleaseSRWLockExclusive(&splash.lock);
    return drained && redraw_drained && InterlockedCompareExchange(&redraw_callbacks,0,0)==0 && InterlockedCompareExchange(&splash.callbacks,0,0)==0;
}
