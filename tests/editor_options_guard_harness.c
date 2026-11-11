/* Actual source-control adapter against owned synthetic pages only. */
#include "../src/dlw_common.h"
#include <stdio.h>
#include <stdlib.h>
__declspec(dllimport) void WINAPI RaiseException(DWORD, DWORD, DWORD, const ULONG_PTR *);
unsigned long _exception_code(void);
#pragma intrinsic(_exception_code)
static unsigned int protect_calls, fail_protect;
static BOOL WINAPI test_protect(LPVOID a, SIZE_T n, DWORD p, DWORD *old)
{ ++protect_calls; if (protect_calls == fail_protect) return 0; return VirtualProtect(a,n,p,old); }
#define VirtualProtect test_protect
#include "../src/dlw_editor_source_control.c"
#undef VirtualProtect
void dluma_log_line(const char *level, const char *text) { (void)level; (void)text; }
void dluma_log_pair(const char *level,const char *key,const char *value) { (void)level;(void)key;(void)value; }
void *dluma_editor_theme_profile_address(unsigned int profile, uintptr_t rva, size_t size)
{ (void)profile;(void)rva;(void)size; return NULL; }
static LONG main_calls, anim_calls, raise_main, block_main, nested_main;
static ScOptionsMapEntry *nested_target;
static HANDLE entered, resume_event;
static void __fastcall original_main(void *self)
{
    (void)self; dluma_atomic_increment(&main_calls);
    if (nested_main) ((ScOptionsHandler)nested_target->target)((void *)(UINT_PTR)2);
    if (block_main) { SetEvent(entered); WaitForSingleObject(resume_event, 5000); }
    if (raise_main) RaiseException(0xE0123456U,0,0,NULL);
}
static void __fastcall original_anims(void *self) { (void)self; dluma_atomic_increment(&anim_calls); }
static DWORD WINAPI invoke_main(void *entry)
{ ((ScOptionsHandler)((ScOptionsMapEntry *)entry)->target)((void *)(UINT_PTR)1); return 0; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void)
{
    ScOptionsMapEntry *main_entry, *anims_entry;
    ScOptionsHandler cached, fetched_guard;
    DWORD old; MEMORY_BASIC_INFORMATION memory;
    HANDLE worker; uint64_t epoch;
    BYTE *editor, before[SC_PROVIDER_OFFSET + sizeof(void *)];
    void *table = (void *)(UINT_PTR)0x12340, *editor_global;
    main_entry = (ScOptionsMapEntry *)VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    anims_entry = (ScOptionsMapEntry *)VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    editor = (BYTE *)VirtualAlloc(NULL,12288,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(main_entry && anims_entry && editor);
    *main_entry = (ScOptionsMapEntry){ WM_COMMAND,0,0x8061,0x8061,0x39,(void *)original_main };
    *anims_entry = (ScOptionsMapEntry){ WM_COMMAND,0,3100,3100,0x39,(void *)original_anims };
    CHECK(VirtualProtect(main_entry,4096,PAGE_READONLY,&old));
    CHECK(VirtualProtect(anims_entry,4096,PAGE_READONLY,&old));
    CHECK(!sc_options_install_cells(main_entry, anims_entry, (void *)original_main, (void *)original_anims, 0));
    CHECK(!protect_calls && main_entry->target == (void *)original_main && anims_entry->target == (void *)original_anims);
    CHECK(VirtualProtect(main_entry,4096,PAGE_READWRITE,&old)); main_entry->signature = 0x38;
    CHECK(VirtualProtect(main_entry,4096,PAGE_READONLY,&old));
    CHECK(!sc_options_install_cells(main_entry, anims_entry, (void *)original_main, (void *)original_anims, 1));
    CHECK(!protect_calls && main_entry->target == (void *)original_main && anims_entry->target == (void *)original_anims);
    CHECK(VirtualProtect(main_entry,4096,PAGE_READWRITE,&old)); main_entry->signature = 0x39;
    CHECK(VirtualProtect(main_entry,4096,PAGE_READONLY,&old));
    fail_protect = 3;
    CHECK(!sc_options_install_cells(main_entry, anims_entry, (void *)original_main, (void *)original_anims, 1));
    CHECK(main_entry->target == (void *)original_main && anims_entry->target == (void *)original_anims);
    CHECK(VirtualQuery(main_entry,&memory,sizeof(memory)) && memory.Protect == PAGE_READONLY);
    /* A published-and-rolled-back wrapper may have been fetched already;
     * production never reuses that state. This test knows no callback ran. */
    fail_protect = 0; protect_calls = 0;
    CHECK(!sc_options_install_cells(main_entry, anims_entry, (void *)original_main, (void *)original_anims, 1));
    memset(&sg, 0, sizeof(sg));
    cached = (ScOptionsHandler)main_entry->target;
    CHECK(sc_options_install_cells(main_entry, anims_entry, (void *)original_main, (void *)original_anims, 1));
    fetched_guard = (ScOptionsHandler)main_entry->target;
    ((ScOptionsHandler)main_entry->target)((void *)(UINT_PTR)1);
    ((ScOptionsHandler)anims_entry->target)((void *)(UINT_PTR)2);
    CHECK(main_calls == 1 && anim_calls == 1);
    nested_target = anims_entry; nested_main = 1;
    ((ScOptionsHandler)main_entry->target)((void *)(UINT_PTR)1);
    nested_main = 0; CHECK(main_calls == 2 && anim_calls == 2);
    entered = CreateEventW(NULL,TRUE,FALSE,NULL); resume_event = CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(entered && resume_event);
    block_main = 1; worker = CreateThread(NULL,0,invoke_main,main_entry,0,NULL); CHECK(worker);
    CHECK(WaitForSingleObject(entered,5000) == WAIT_OBJECT_0);
    epoch = sc_options_arm(); CHECK(epoch && !sc_options_arm_ready(epoch));
    CHECK(!sc_options_disarm(epoch) && !sc_options_stop());
    ((ScOptionsHandler)anims_entry->target)((void *)(UINT_PTR)2); CHECK(anim_calls == 2);
    block_main = 0; cached((void *)(UINT_PTR)1); /* A pre-install copy bypasses the wrapper. */
    CHECK(main_calls == 4); SetEvent(resume_event); CHECK(WaitForSingleObject(worker,5000) == WAIT_OBJECT_0); CloseHandle(worker);
    block_main = 0; CHECK(sc_options_arm_ready(epoch)); CHECK(!sc_options_disarm(epoch + 1)); CHECK(sc_options_disarm(epoch));
    raise_main = 1; __try { ((ScOptionsHandler)main_entry->target)((void *)(UINT_PTR)1); CHECK(0); }
    __except (_exception_code() == 0xE0123456U ? 1 : 0) { }
    raise_main = 0; epoch = sc_options_arm(); CHECK(epoch && sc_options_arm_ready(epoch)); CHECK(sc_options_disarm(epoch));
    CHECK(sc_options_stop()); CHECK(main_entry->target == (void *)original_main && anims_entry->target == (void *)original_anims);
    fetched_guard((void *)(UINT_PTR)1); CHECK(main_calls == 6);
    /* Another owner may replace a cell; stop must preserve that pointer. */
    memset(&sg, 0, sizeof(sg));
    CHECK(sc_options_install_cells(main_entry, anims_entry, (void *)original_main, (void *)original_anims, 1));
    CHECK(VirtualProtect(main_entry,4096,PAGE_READWRITE,&old)); main_entry->target = (void *)original_anims;
    CHECK(VirtualProtect(main_entry,4096,PAGE_READONLY,&old));
    CHECK(sc_options_stop());
    CHECK(main_entry->target == (void *)original_anims && anims_entry->target == (void *)original_anims);
    *(void **)editor = table; editor_global = editor; memset(editor + sizeof(void *), 0, sizeof(before) - sizeof(void *));
    memcpy(before,editor,sizeof(before));
    CHECK(sc_options_read_off(&editor_global, table, GetCurrentThreadId()));
    CHECK(!memcmp(before,editor,sizeof(before)));
    editor[SC_LEGACY_OFFSET] = 1; CHECK(!sc_options_read_off(&editor_global,table,GetCurrentThreadId()));
    editor[SC_LEGACY_OFFSET] = 2; CHECK(!sc_options_read_off(&editor_global,table,GetCurrentThreadId()));
    editor[SC_LEGACY_OFFSET] = 0; CHECK(!sc_options_read_off(&editor_global,(void *)(UINT_PTR)0x12348,GetCurrentThreadId())); *(void **)(editor + SC_PROVIDER_OFFSET) = (void *)(UINT_PTR)1;
    CHECK(!sc_options_read_off(&editor_global,table,GetCurrentThreadId()));
    CHECK(!sc_options_read_off(&editor_global,table,GetCurrentThreadId()+1));
    CloseHandle(entered); CloseHandle(resume_event); VirtualFree(editor,0,MEM_RELEASE);
    VirtualFree(anims_entry,0,MEM_RELEASE); VirtualFree(main_entry,0,MEM_RELEASE);
    puts("Editor options guard synthetic contracts passed."); return 0;
}
