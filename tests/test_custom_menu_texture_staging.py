"""Execute the production texture staging loop with controlled inventory seams."""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_declared_textures_use_windows_inventory_paths(tmp_path: Path) -> None:
    source = (ROOT / "src/dlw_custom_main_menu.c").read_text(encoding="utf-8")
    function = source.index("static int dluma_custom_menu_add_declared_assets(")
    start = source.index(
        "    for (index = 0; index < candidate->manifest.texture_def_count;", function
    )
    end = source.index("    if (candidate->manifest.use_custom_banks)", start)
    loop = source[start:end]
    harness = tmp_path / "texture_staging.c"
    harness.write_text(r"""
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define DLUMA_MAX_WPATH 1024
#define DLUMA_ARRAY_COUNT(x) ((unsigned int)(sizeof(x) / sizeof((x)[0])))
#define DLUMA_CUSTOM_MENU_MAX_RUNTIME_FILE_BYTES 4096U
typedef wchar_t WCHAR;
typedef struct { char destination_utf8[2048]; int owner; } StageFile;
typedef struct { StageFile files[8]; unsigned int count; } DlumaCustomMenuStageInventory;
typedef struct {
    struct { unsigned int texture_def_count;
        struct { const char *source_path; } texture_defs[8]; } manifest;
    const WCHAR *canonical_root;
} DlumaCustomMenuCandidate;
static int add_calls, fail_open;
static WCHAR opened_source[DLUMA_MAX_WPATH];
static int dluma_utf8_to_wide(const char *in, WCHAR *out, unsigned int cap)
{
    unsigned int i;
    for (i = 0; in[i] && i + 1U < cap; ++i) out[i] = (unsigned char)in[i];
    out[i] = 0;
    return !in[i];
}
static int dluma_wide_to_utf8(const WCHAR *in, char *out, unsigned int cap)
{
    unsigned int i;
    for (i = 0; in[i] && i + 1U < cap; ++i) out[i] = (char)in[i];
    out[i] = 0;
    return !in[i];
}
static void dluma_wstr_copy(WCHAR *out, unsigned int cap, const WCHAR *in)
{
    assert(wcslen(in) < cap);
    wcscpy(out, in);
}
static int dluma_custom_menu_join(WCHAR *out, unsigned int cap,
                                  const WCHAR *left, const WCHAR *right)
{
    if (wcslen(left) + wcslen(right) + 2U > cap) return 0;
    wcscpy(out, left);
    wcscat(out, L"\\");
    wcscat(out, right);
    return 1;
}
static int dluma_stricmp(const char *left, const char *right)
{
    while (*left && tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
        ++left; ++right;
    }
    return tolower((unsigned char)*left) - tolower((unsigned char)*right);
}
static int dluma_custom_menu_inventory_add(DlumaCustomMenuStageInventory *inventory,
    const WCHAR *source, const WCHAR *root, const WCHAR *destination,
    unsigned long maximum, unsigned char promote, unsigned char overlay)
{
    ++add_calls;
    assert(!wcschr(source, L'/'));
    assert(!wcschr(destination, L'/'));
    assert(!wcscmp(root, L"\\\\?\\C:\\project"));
    assert(maximum == DLUMA_CUSTOM_MENU_MAX_RUNTIME_FILE_BYTES);
    assert(promote == 0 && overlay == 0);
    wcscpy(opened_source, source);
    if (fail_open) return 0;
    assert(inventory->count < DLUMA_ARRAY_COUNT(inventory->files));
    assert(dluma_wide_to_utf8(destination,
        inventory->files[inventory->count].destination_utf8, 2048));
    inventory->files[inventory->count++].owner = 2;
    return 1;
}
static int stage(DlumaCustomMenuStageInventory *inventory,
                 DlumaCustomMenuCandidate *candidate)
{
    unsigned int index;
    WCHAR relative[DLUMA_MAX_WPATH], source[DLUMA_MAX_WPATH];
    WCHAR destination[DLUMA_MAX_WPATH];
""" + loop + r"""
    return 1;
}
int main(void)
{
    DlumaCustomMenuStageInventory inventory = {0};
    DlumaCustomMenuCandidate candidate = {0};
    candidate.canonical_root = L"\\\\?\\C:\\project";
    candidate.manifest.texture_def_count = 1;
    candidate.manifest.texture_defs[0].source_path = "data/menu/texturedefs/ui_theme.def";
    strcpy(inventory.files[0].destination_utf8, "DATA\\menu\\texturedefs\\UI_THEME.def");
    inventory.files[0].owner = 7;
    inventory.count = 1;
    /* A runtime scan already staged this file. Reuse it without reopening. */
    fail_open = 1;
    assert(stage(&inventory, &candidate));
    assert(inventory.count == 1 && add_calls == 0 && inventory.files[0].owner == 7);
    assert(!strcmp(candidate.manifest.texture_defs[0].source_path,
                   "data/menu/texturedefs/ui_theme.def"));
    /* Absent files must use Windows separators for both opening and staging. */
    inventory.count = 0;
    fail_open = 0;
    assert(stage(&inventory, &candidate));
    assert(inventory.count == 1 && add_calls == 1);
    assert(!strcmp(inventory.files[0].destination_utf8,
                   "data\\menu\\texturedefs\\ui_theme.def"));
    assert(!wcscmp(opened_source, L"\\\\?\\C:\\project\\data\\menu\\texturedefs\\ui_theme.def"));
    /* Repeated declarations, backslashes and mixed separators reuse the record. */
    candidate.manifest.texture_def_count = 3;
    candidate.manifest.texture_defs[1].source_path = "data\\menu/texturedefs\\UI_THEME.def";
    candidate.manifest.texture_defs[2].source_path = "data\\menu\\texturedefs\\ui_theme.def";
    assert(stage(&inventory, &candidate));
    assert(inventory.count == 1 && add_calls == 1);
    /* A different file is still added, and unsafe/missing sources still fail. */
    candidate.manifest.texture_def_count = 1;
    candidate.manifest.texture_defs[0].source_path = "data/menu/texturedefs/other.def";
    assert(stage(&inventory, &candidate));
    assert(inventory.count == 2 && add_calls == 2);
    candidate.manifest.texture_defs[0].source_path = "data/menu/texturedefs/missing.def";
    fail_open = 1;
    assert(!stage(&inventory, &candidate));
    assert(inventory.count == 2 && add_calls == 3);
    candidate.manifest.texture_def_count = 0;
    assert(stage(&inventory, &candidate));
    assert(inventory.count == 2 && add_calls == 3);
    puts("declared texture staging: ok");
    return 0;
}
""", encoding="utf-8")
    executable = tmp_path / ("texture_staging.exe" if os.name == "nt" else "texture_staging")
    if os.name == "nt":
        command = subprocess.list2cmdline([
            "cl.exe", "/nologo", "/TC", "/W4", "/WX", "/O2",
            "/D_CRT_SECURE_NO_WARNINGS", str(harness),
            f"/Fo{tmp_path / 'texture_staging.obj'}", f"/Fe{executable}",
        ])
        build = ["cmd.exe", "/d", "/c", "call tools\\setup_msvc.bat && " + command]
    else:
        build = ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                 str(harness), "-o", str(executable)]
    compiled = subprocess.run(build, cwd=ROOT, capture_output=True, text=True, timeout=180)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
    assert ran.returncode == 0, ran.stdout + ran.stderr
