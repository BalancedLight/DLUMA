"""Create a new disposable project from an Editor-authored template."""
import argparse
import os
import re
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
from scenario import SCENARIOS, render
from hud_documents import marker_documents

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

def no_links(root):
    for base, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(base) / name
            st = p.lstat()
            if p.is_symlink() or getattr(st, 'st_file_attributes', 0) & 0x400:
                raise ValueError(f'Link or reparse point rejected: {p}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--template', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--project', type=Path, help='New Editor-authored disposable project')
    parser.add_argument('--confirm-new-project', action='store_true')
    parser.add_argument('--original-texture-def', type=Path)
    parser.add_argument('--original-hud', type=Path)
    parser.add_argument('--test-hud', type=Path)
    parser.add_argument('--texture-dir', type=Path)
    parser.add_argument('--compiled-texture-dir', type=Path)
    parser.add_argument('--scenario', choices=tuple(SCENARIOS), default='document')
    parser.add_argument('--boss-map', help='Exact Editor-authored unique map identity')
    parser.add_argument('--unique-map-confirmed', action='store_true', help='Attest this map is claimed by no other installed project')
    args = parser.parse_args()
    if args.project:
        if args.template or args.output or not args.confirm_new_project:
            raise ValueError('--project requires --confirm-new-project and excludes --template/--output')
        source = dest = args.project.resolve(strict=True)
    else:
        if not args.template or not args.output or args.confirm_new_project:
            raise ValueError('Provide --project or both --template and --output')
        source, dest = args.template.resolve(strict=True), args.output.resolve()
    if not source.is_dir() or not (source / 'desc.scr').is_file():
        raise ValueError('Template must be an Editor-authored project containing desc.scr')
    if not args.project and (dest.exists() or dest == source or source in dest.parents or dest in source.parents):
        raise ValueError('Output must be a new directory outside the template')
    if (source / '.git').exists():
        raise ValueError('Template must not contain Git metadata; use a clean project copy')
    no_links(source)
    if not args.unique_map_confirmed:
        raise ValueError('Confirm the Editor-authored map has no other active project claim')
    if (source / 'data/menu/hud/__dluma_missing_document__.xui').exists():
        raise ValueError('Negative test document path already exists')
    if args.boss_map:
        if not re.fullmatch(r'[A-Za-z0-9_]{1,64}', args.boss_map):
            raise ValueError('Invalid map name')
        for rel in (f'data/quests/{args.boss_map}/dluma_boss_probe.pxsl',
                    f'data/quests/{args.boss_map}/{args.boss_map}_invoke.scr',
                    'data/scripts/boss/dluma_boss_probe.scr',
                    'data/scripts/boss/dluma_boss_probe_chatters.scr',
                    *(f'data/scripts/boss/{row[3]}' for key,row in SCENARIOS.items() if key != 'callbacks')):
            if (source / rel).exists(): raise ValueError(f'Template already contains {rel}')
    if args.test_hud and not args.original_hud:
        raise ValueError('--test-hud requires --original-hud')
    if bool(args.texture_dir) != bool(args.compiled_texture_dir):
        raise ValueError('Texture setup requires both source DDS and compiled-pair directories')
    if args.original_hud:
        for hud in (args.original_hud, args.test_hud):
            if hud is None: continue
            if not hud.is_file() or hud.is_symlink(): raise ValueError(f'HUD file missing or linked: {hud}')
            root = ET.parse(hud).getroot()
            if root.tag != 'XuiCanvas' or len(root.findall('XuiScene')) != 1:
                raise ValueError(f'Expected one complete XuiCanvas scene in {hud}')
        original = args.original_hud.read_bytes()
        documents = marker_documents(original)
        if not args.test_hud:
            original_root = ET.fromstring(original)
            fonts = [(item.text or '').strip() for item in original_root.iter('Font') if (item.text or '').strip()]
            if not fonts: raise ValueError('Original HUD has no existing Font to reuse')
            panel_font = 'HUDTextNormal' if 'HUDTextNormal' in fonts else fonts[0]
        if not args.test_hud and (original.count(b'</XuiScene>') != 1 or b'<Id>EffectPanel</Id>' in original):
            raise ValueError('HUD scene is ambiguous or already contains EffectPanel')
        for rel in ('data/menu/hud/hud_crane.xui', 'data/menu/hud/hud_test.xui',
                    *(f'data/menu/hud/{name}' for name in documents)):
            if (source / rel).exists(): raise ValueError(f'Template already contains {rel}')
    if args.texture_dir:
        if not args.texture_dir.is_dir(): raise ValueError('Texture directory missing')
        no_links(args.texture_dir)
        if not args.compiled_texture_dir.is_dir():
            raise ValueError('Compiled texture directory missing')
        no_links(args.compiled_texture_dir)
        for i in range(11):
            file = args.texture_dir / f'flash_{i}.dds'
            if not file.is_file(): raise ValueError(f'Missing flash_{i}.dds')
            with file.open('rb') as stream: header = stream.read(128)
            if len(header) < 128 or header[:4] != b'DDS ' or struct.unpack_from('<II', header, 12) != (720, 1280) or header[84:88] != b'\0\0\0\0' or struct.unpack_from('<I', header, 88)[0] != 32 or file.stat().st_size < 128 + 1280*720*4:
                raise ValueError(f'{file.name} must be uncompressed 1280x720 32-bit DDS')
        for i in range(11):
            filename = f'dluma_test_flash_{i}.dds'
            obj = args.compiled_texture_dir / (filename + '_obj')
            dep = args.compiled_texture_dir / (filename + '_obj_dep')
            if not obj.is_file() or not dep.is_file() or obj.stat().st_size == 0:
                raise ValueError(f'Missing compiled object/dependency for {filename}')
            metadata = dep.read_bytes().replace(b'\\', b'/')
            expected_path = f'data/textures/dluma-feature-test/{filename}'.encode()
            for expected in (expected_path, b'Meta("width","1280")', b'Meta("height","720")', b'Meta("format","A8R8G8B8")'):
                if expected not in metadata: raise ValueError(f'Compiled dependency mismatch for {filename}: {expected!r}')
        for rel in ('data/menu/texturedefs/dluma-feature-test.def', 'data/textures/dluma-feature-test', 'assets_pc/textures/dluma-feature-test'):
            if (source / rel).exists(): raise ValueError(f'Template already contains {rel}')
    if args.original_texture_def:
        if not args.original_texture_def.is_file() or args.original_texture_def.is_symlink():
            raise ValueError('Original texture definition missing or linked')
        definition_bytes = args.original_texture_def.read_bytes()
        if b'dluma_test_missing_5' in definition_bytes:raise ValueError('Negative texture alias already exists')
        for i in range(11):
            original_alias = f'hud_bossfight_flash_{i}'.encode()
            replacement_alias = f'dluma_test_flash_{i}'.encode()
            if len(re.findall(rb'Whole\s*\(\s*"' + original_alias + rb'"\s*\)', definition_bytes)) != 1:
                raise ValueError(f'Expected exactly one original alias {original_alias.decode()}')
            if replacement_alias in definition_bytes:
                raise ValueError(f'Replacement alias already exists: {replacement_alias.decode()}')
        if (source / 'data/menu/texturedefs/hudtextures.def').exists():
            raise ValueError('Project already contains hudtextures.def; refusing overwrite')
    if args.texture_dir and not args.original_texture_def:
        raise ValueError('Texture inputs require --original-texture-def')
    # Refuse collisions before making any copy; never replace template files.
    for package in (HERE / 'packages').iterdir():
        if (source / 'data/scripts/lua/project' / package.name).exists():
            raise ValueError(f'Template already contains package {package.name}')
    if not args.project:
        shutil.copytree(source, dest)
    for package in (HERE / 'packages').iterdir():
        target = dest / 'data/scripts/lua/project' / package.name
        shutil.copytree(package, target)
        subprocess.run([sys.executable, str(REPO / 'tools/validate_mod_package.py'), 'lock', str(target)], check=True)
        subprocess.run([sys.executable, str(REPO / 'tools/validate_mod_package.py'), 'validate', str(target), '--require-lock'], check=True)
    if args.boss_map:
        quest = dest / 'data/quests' / args.boss_map
        quest.mkdir(parents=True, exist_ok=True)
        pxsl, invoke = render(args.boss_map, args.scenario)
        (quest / 'dluma_boss_probe.pxsl').write_text(pxsl, encoding='utf-8')
        (quest / f'{args.boss_map}_invoke.scr').write_text(invoke, encoding='utf-8')
        boss = dest / 'data/scripts/boss'; boss.mkdir(parents=True, exist_ok=True)
        for name in ('dluma_boss_probe.scr', 'dluma_boss_probe_chatters.scr', *(row[3] for key,row in SCENARIOS.items() if key != 'callbacks')):
            shutil.copy2(HERE / 'boss' / name, boss / name)
    if args.original_hud:
        hud_dir = dest / 'data/menu/hud'; hud_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.original_hud, hud_dir / 'hud_crane.xui')
        for name, content in documents.items():
            (hud_dir / name).write_bytes(content)
        if args.test_hud:
            shutil.copy2(args.test_hud, hud_dir / 'hud_test.xui')
        else:
            panel = (HERE / 'effect-controls.xui').read_text(encoding='utf-8')
            panel = panel[panel.index('        <AdvGroup>'):panel.index('    </XuiScene>')]
            panel = panel.replace('<Show>false</Show>', '<Show>true</Show>').replace('&amp;DLUMA_Example_EffectOne&amp;', 'DLUMA TEST')
            panel = panel.replace('<Font>HUDTextNormal</Font>', '<Font>' + escape(panel_font) + '</Font>')
            generated = original.replace(b'</XuiScene>', panel.encode('utf-8') + b'\n</XuiScene>', 1)
            ET.fromstring(generated)
            (hud_dir / 'hud_test.xui').write_bytes(generated)
    if args.original_texture_def:
        definition = dest / 'data/menu/texturedefs/hudtextures.def'
        definition.parent.mkdir(parents=True, exist_ok=True)
        extra = ''.join(f'Texture("dluma_test_flash_{i}.dds", 1280, 720) {{ Whole("dluma_test_flash_{i}") }}\n' for i in range(11)).encode()
        definition.write_bytes(definition_bytes + b'\r\n' + extra)
    if args.texture_dir:
        texture_dir = dest / 'data/textures/dluma-feature-test'; texture_dir.mkdir(parents=True)
        for i in range(11):
            shutil.copy2(args.texture_dir / f'flash_{i}.dds', texture_dir / f'dluma_test_flash_{i}.dds')
        objects = dest / 'assets_pc/textures/dluma-feature-test'
        objects.mkdir(parents=True)
        for i in range(11):
            for suffix in ('_obj', '_obj_dep'):
                name = f'dluma_test_flash_{i}.dds{suffix}'
                shutil.copy2(args.compiled_texture_dir / name, objects / name)
    print(f'Created disposable project: {dest}')
    print('Open this copy in the Editor; see test-projects/README.md for setup and tests.')

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as exc:
        print(f'Setup failed: {exc}', file=sys.stderr)
        sys.exit(1)
