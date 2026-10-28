"""Exercise the native storage quota scan against real files."""
import ctypes
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.skipif(os.name != "nt", reason="native Windows storage")
def test_storage_scan_accepts_multiple_and_replaced_values(tmp_path):
    source = (ROOT / "src/game/dlw_game_host_context.c").read_text(encoding="utf-8")
    extension = source[source.index("static const WCHAR dluma_game_host_storage_extension"):].split(";", 1)[0] + ";"
    scan = source[source.index("static uint64_t dluma_game_host_storage_usage("):source.index("static DlumaStatus dluma_game_host_config_get(")]
    operations = source[source.index("static int dluma_game_host_storage_encode("):source.index("static DlumaStatus dluma_game_host_storage_delete(")]
    harness = r"""
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dluma_api.h"
#define DLUMA_MAX_WPATH 32768
#define DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT 4096U
#define DLUMA_GAME_HOST_STORAGE_KEY_LIMIT 128U
#define DLUMA_GAME_HOST_STORAGE_TOTAL_LIMIT 65536U
#define DLUMA_GAME_HOST_SCRATCH_CAPACITY 4097U
typedef struct { WCHAR directory[DLUMA_MAX_WPATH]; char scratch[DLUMA_GAME_HOST_SCRATCH_CAPACITY]; } DlumaGameHostState;
#define dluma_alloc malloc
#define dluma_free free
#define DLUMA_ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
static void dluma_path_join(WCHAR *out, size_t cap, const WCHAR *dir, const WCHAR *name) {
    _snwprintf_s(out,cap,_TRUNCATE,L"%ls\\%ls",dir,name);
}
static int dluma_wstricmp(const WCHAR *a,const WCHAR *b) { return _wcsicmp(a,b); }
static HANDLE dluma_game_host_open_storage_file(const WCHAR *path,const WCHAR *directory,int unused,uint64_t limit,uint64_t *size) {
    LARGE_INTEGER n; HANDLE h; (void)directory; (void)unused;
    h=CreateFileW(path,unused ? GENERIC_WRITE : GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,unused ? CREATE_NEW : OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(unused) return h;
    if(h==INVALID_HANDLE_VALUE) return h;
    if(!GetFileSizeEx(h,&n) || n.QuadPart<0 || (uint64_t)n.QuadPart>limit){CloseHandle(h); return INVALID_HANDLE_VALUE;}
    *size=(uint64_t)n.QuadPart; return h;
}
static int dluma_game_host_copy_view(char *out,size_t cap,DlumaStringView v) { if(!v.data || v.size>=cap) return 0; memcpy(out,v.data,(size_t)v.size);out[v.size]=0;return 1; }
static int dluma_game_host_storage_paths(DlumaGameHostState *s,const char *key,WCHAR *dir,size_t dc,WCHAR *path,size_t pc) { WCHAR wkey[160]; if(!MultiByteToWideChar(CP_UTF8,0,key,-1,wkey,160)) return 0; wcsncpy_s(dir,dc,s->directory,_TRUNCATE); _snwprintf_s(path,pc,_TRUNCATE,L"%ls\\%ls.dlumas",dir,wkey);return 1; }
static void dluma_wstr_copy(WCHAR *a,size_t n,const WCHAR *b) { wcsncpy_s(a,n,b,_TRUNCATE); }
static void dluma_wstr_append(WCHAR *a,size_t n,const WCHAR *b) { wcsncat_s(a,n,b,_TRUNCATE); }
static void dluma_str_append(char *a,size_t n,const char *b) { strncat_s(a,n,b,_TRUNCATE); }
static void dluma_u64_to_text(uint64_t v,char *out,unsigned int n) { _snprintf_s(out,n,_TRUNCATE,"%llu",(unsigned long long)v); }
static int dluma_utf8_to_wide(const char *s,WCHAR *out,unsigned int n) {return MultiByteToWideChar(CP_UTF8,0,s,-1,out,(int)n)!=0;}
""" + extension + "\n" + scan + operations + r"""
__declspec(dllexport) int roundtrip(const WCHAR *dir) {
    DlumaGameHostState state; DlumaStorageSetRequest set; DlumaStorageGetRequest get;
    DlumaBoolResult b; DlumaScalarResult result; const char *k1="first",*k2="second",*value="v2 0.45 0.2 2.5";
    memset(&state,0,sizeof(state)); wcsncpy_s(state.directory,DLUMA_ARRAY_COUNT(state.directory),dir,_TRUNCATE);
    memset(&set,0,sizeof(set)); memset(&b,0,sizeof(b)); set.key.data=k1;set.key.size=strlen(k1);set.value.kind=DLUMA_SCALAR_STRING;set.value.string_value.data=value;set.value.string_value.size=strlen(value);
    if(dluma_game_host_storage_set(&state,&set,&b)!=DLUMA_STATUS_OK || !b.value) return 1;
    set.key.data=k2;set.key.size=strlen(k2); if(dluma_game_host_storage_set(&state,&set,&b)!=DLUMA_STATUS_OK) return 2;
    value="v2 -0.3 0.5 3.5";set.key.data=k1;set.key.size=strlen(k1);set.value.string_value.data=value;set.value.string_value.size=strlen(value);
    if(dluma_game_host_storage_set(&state,&set,&b)!=DLUMA_STATUS_OK) return 3;
    memset(&state,0,sizeof(state));wcsncpy_s(state.directory,DLUMA_ARRAY_COUNT(state.directory),dir,_TRUNCATE);memset(&get,0,sizeof(get));memset(&result,0,sizeof(result)); get.key=set.key;
    if(dluma_game_host_storage_get(&state,&get,&result)!=DLUMA_STATUS_OK || result.value.kind!=DLUMA_SCALAR_STRING || strcmp(result.value.string_value.data,value)) return 4;
    {
        WCHAR obstacle[DLUMA_MAX_WPATH];
        _snwprintf_s(obstacle,DLUMA_ARRAY_COUNT(obstacle),_TRUNCATE,L"%ls\\blocked",dir);
        if(!CreateDirectoryW(obstacle,0)) return 5;
        if(dluma_game_host_storage_set(&state,&set,&b)==DLUMA_STATUS_OK) return 6;
        if(!RemoveDirectoryW(obstacle)) return 7;
        if(dluma_game_host_storage_set(&state,&set,&b)!=DLUMA_STATUS_OK) return 8;
    }
    return 0;
}
"""
    cfile = tmp_path / "storage_scan.c"
    dll = tmp_path / "storage_scan.dll"
    cfile.write_text(harness, encoding="utf-8")
    setup = ROOT / "tools/setup_msvc.bat"
    cmd = f'call "{setup}" >nul && cl.exe /nologo /LD /W4 /WX /TC /I"{ROOT / "sdk/c"}" "{cfile}" /Fe:"{dll}" /Fo:"{tmp_path / "storage_scan.obj"}" /link kernel32.lib'
    subprocess.run(f'cmd.exe /d /s /c "{cmd}"', cwd=tmp_path, check=True)
    check = ctypes.WinDLL(str(dll)).roundtrip
    check.argtypes = [ctypes.c_wchar_p]
    check.restype = ctypes.c_int
    folder = tmp_path / "values"
    folder.mkdir()
    assert check(str(folder)) == 0
