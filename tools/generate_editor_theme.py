#!/usr/bin/env python3
"""Generate all language bindings from the editor palette."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "build/generated/editor-theme"
NAMES = ("Window Panel Surface Alternate Text ControlText MutedText DisabledText Border Hover Accent Selection SelectionText InactiveSelection Error Warning Success").split()
METRICS = {"focus_border": 1, "separator": 1, "toolbar_padding": 4, "tab_padding_vertical": 4, "tab_padding_horizontal": 8, "dock_title_padding": 5, "selected_tab_edge": 3}

def generate():
    data = json.loads((ROOT / "assets/editor-theme/palette.json").read_text(encoding="utf-8"))
    roles, metrics = data["roles"], data["metrics"]
    if [r[0] for r in roles] != NAMES or metrics != METRICS:
        raise ValueError("invalid role order or metrics")
    if any(len(r) != 3 or any(not isinstance(c, str) or not re.fullmatch(r"#[0-9A-Fa-f]{6}", c) for c in r[1:]) for r in roles):
        raise ValueError("colors must be opaque RGB hex values")
    native = ["/* Generated; do not edit. */", "#ifndef DLUMA_EDITOR_THEME_PALETTE_GENERATED_H", "#define DLUMA_EDITOR_THEME_PALETTE_GENERATED_H", "enum DlumaEditorThemeRole {"]
    native += [f"    DLUMA_EDITOR_THEME_ROLE_{re.sub(r'(?<!^)(?=[A-Z])', '_', name).upper()} = {i}," for i, name in enumerate(NAMES)]
    native += ["    DLUMA_EDITOR_THEME_ROLE_COUNT = 17", "};"]
    for k, v in metrics.items(): native.append(f"#define DLUMA_EDITOR_THEME_METRIC_{k.upper()} {v}")
    for mode, index in (("dark", 1), ("light", 2)):
        native += [f"static const uint32_t dluma_editor_theme_{mode}_argb[17] = {{", "    " + ", ".join("0xff" + r[index][1:].lower() + "u" for r in roles), "};"]
    native += ["#endif", ""]
    managed = ["// Generated; do not edit.", "namespace Dluma.EditorTheme {", "    internal enum ThemeRole {"] + [f"        {n} = {i}," for i, n in enumerate(NAMES)] + ["        Count = 17", "    }", "    internal static class ThemePalette {"]
    for mode, index in (("Dark", 1), ("Light", 2)):
        managed.append(f"        internal static readonly uint[] {mode} = {{ " + ", ".join("0xff" + r[index][1:].lower() + "u" for r in roles) + " };")
    managed += ["    }", "}", ""]
    xaml = ['<!-- Generated; do not edit. -->', '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">'] + [f'  <SolidColorBrush x:Key="EditorTheme.{r[0]}" Color="{r[2].upper()}" />' for r in roles] + ['</ResourceDictionary>', '']
    return {"editor_theme_palette.generated.h": "\n".join(native), "EditorThemeRoles.g.cs": "\n".join(managed), "ThemeDefaults.xaml": "\n".join(xaml)}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    files = generate()
    if args.check:
        stale = [n for n, content in files.items() if not (OUTPUT / n).is_file() or (OUTPUT / n).read_text(encoding="utf-8") != content]
        if stale: raise SystemExit("stale editor theme outputs: " + ", ".join(stale))
    else:
        OUTPUT.mkdir(parents=True, exist_ok=True)
        for name, content in files.items(): (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
if __name__ == "__main__": main()
