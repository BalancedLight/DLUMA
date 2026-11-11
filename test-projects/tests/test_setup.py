import subprocess
import sys
from pathlib import Path
import pytest

SCRIPT = Path(__file__).resolve().parents[1] / 'setup.py'

def test_setup_creates_disposable_copy_and_refuses_existing_destination(tmp_path):
    source = tmp_path / 'template'
    source.mkdir()
    (source / 'desc.scr').write_text('template')
    target = tmp_path / 'project'
    command = [sys.executable, str(SCRIPT), '--template', str(source), '--output', str(target), '--unique-map-confirmed']
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert (target / 'desc.scr').read_text() == 'template'
    assert (target / 'data/scripts/lua/project/probe.hud.asset-a/main.lua').is_file()
    assert (target / 'data/scripts/lua/project/probe.hud.asset-a/mod.dluma.lock.json').is_file()
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert (source / 'desc.scr').read_text() == 'template'

def test_setup_rejects_output_inside_template(tmp_path):
    source = tmp_path / 'template'
    source.mkdir()
    (source / 'desc.scr').write_text('template')
    result = subprocess.run([sys.executable, str(SCRIPT), '--template', str(source), '--output', str(source / 'nested'), '--unique-map-confirmed'], capture_output=True)
    assert result.returncode != 0
    assert not (source / 'nested').exists()

def test_texture_setup_requires_editor_compiler_before_copy(tmp_path):
    source = tmp_path / 'template'; source.mkdir(); (source / 'desc.scr').write_text('template')
    hud = tmp_path / 'hud.xui'; hud.write_text('<hud/>')
    textures = tmp_path / 'textures'; textures.mkdir()
    for i in range(11): (textures / f'flash_{i}.dds').write_bytes(b'DDS ' + bytes([i]))
    target = tmp_path / 'project'
    result = subprocess.run([sys.executable, str(SCRIPT), '--template', str(source), '--output', str(target), '--original-hud', str(hud), '--test-hud', str(hud), '--texture-dir', str(textures), '--unique-map-confirmed'], capture_output=True)
    assert result.returncode != 0
    assert not target.exists()

def test_setup_rejects_existing_repository_metadata(tmp_path):
    source = tmp_path / 'template'; source.mkdir(); (source / 'desc.scr').write_text('template')
    (source / '.git').mkdir()
    target = tmp_path / 'project'
    result = subprocess.run([sys.executable, str(SCRIPT), '--template', str(source), '--output', str(target), '--unique-map-confirmed'], capture_output=True)
    assert result.returncode != 0
    assert not target.exists()

def test_boss_fixture_requires_attested_map_identity_and_stages_sources(tmp_path):
    source = tmp_path / 'template'; source.mkdir(); (source / 'desc.scr').write_text('template')
    target = tmp_path / 'project'
    base = [sys.executable, str(SCRIPT), '--template', str(source), '--output', str(target), '--boss-map', 'unique_map']
    assert subprocess.run(base, capture_output=True).returncode != 0
    result = subprocess.run(base + ['--unique-map-confirmed'], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    quest = target / 'data/quests/unique_map/dluma_boss_probe.pxsl'
    assert 'QuestsDefinitions -level=unique_map' in quest.read_text()
    assert (target / 'data/quests/unique_map/unique_map_invoke.scr').is_file()
    assert (target / 'data/scripts/boss/dluma_boss_probe.scr').is_file()

def test_generate_hud_uses_complete_local_canvas_and_visible_marker(tmp_path):
    source = tmp_path / 'template'; source.mkdir(); (source / 'desc.scr').write_text('template')
    hud = tmp_path / 'hud.xui'; hud.write_text('<XuiCanvas><Properties><Width>1280</Width><Height>720</Height></Properties><XuiScene><Properties><Id>HUD_DI</Id></Properties><IUIAARectangle><Properties><ImagePath>white</ImagePath><Material>menu_antialias.mat</Material></Properties></IUIAARectangle><MyText><Properties><Font>OtherFont</Font></Properties></MyText><MyText><Properties><Font>HUDTextNormal</Font></Properties></MyText></XuiScene></XuiCanvas>')
    target = tmp_path / 'project'
    result = subprocess.run([sys.executable, str(SCRIPT), '--template', str(source), '--output', str(target), '--unique-map-confirmed', '--original-hud', str(hud)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    text = (target / 'data/menu/hud/hud_test.xui').read_text()
    assert '<Id>HUD_DI</Id>' in text and '<Id>EffectPanel</Id>' in text and '<Show>true</Show>' in text
    assert '<Font>HUDTextNormal</Font>' in text and '<Font>HUDNormalOutline</Font>' not in text
    assert (target / 'data/menu/hud/hud_crane.xui').read_text() == hud.read_text()

def test_new_project_install_and_texture_definition_merge(tmp_path):
    project = tmp_path / 'new_project'; project.mkdir(); (project / 'desc.scr').write_text('editor-created')
    original = tmp_path / 'hudtextures.def'
    original.write_text(''.join(f'Texture("stock{i}.dds", 512, 512) {{ Whole("hud_bossfight_flash_{i}") }}\n' for i in range(11)))
    command = [sys.executable, str(SCRIPT), '--project', str(project), '--confirm-new-project', '--unique-map-confirmed', '--original-texture-def', str(original)]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    merged = (project / 'data/menu/texturedefs/hudtextures.def').read_text()
    assert merged.startswith(original.read_text())
    assert all(f'Whole("dluma_test_flash_{i}")' in merged for i in range(11))
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert (project / 'desc.scr').read_text() == 'editor-created'

def test_texture_definition_conflicts_are_rejected_before_install(tmp_path):
    project = tmp_path / 'project'; project.mkdir(); (project / 'desc.scr').write_text('x')
    original = tmp_path / 'hudtextures.def'
    original.write_text('Whole("dluma_test_flash_0")')
    result = subprocess.run([sys.executable, str(SCRIPT), '--project', str(project), '--confirm-new-project', '--unique-map-confirmed', '--original-texture-def', str(original)], capture_output=True)
    assert result.returncode != 0
    assert not (project / 'data').exists()

def test_compiled_texture_dependency_mismatch_rejected_before_project_write(tmp_path):
    import struct
    project = tmp_path / 'project'; project.mkdir(); (project / 'desc.scr').write_text('x')
    source = tmp_path / 'dds'; source.mkdir()
    compiled = tmp_path / 'compiled'; compiled.mkdir()
    header = bytearray(128); header[:4] = b'DDS '; struct.pack_into('<II', header, 12, 720, 1280); struct.pack_into('<I', header, 88, 32)
    pixels = b'\0' * (1280 * 720 * 4)
    for i in range(11):
        (source / f'flash_{i}.dds').write_bytes(header + pixels)
        (compiled / f'dluma_test_flash_{i}.dds_obj').write_bytes(b'bad')
        (compiled / f'dluma_test_flash_{i}.dds_obj_dep').write_text('wrong source')
    definition = tmp_path / 'hudtextures.def'
    definition.write_text(''.join(f'Whole("hud_bossfight_flash_{i}")\n' for i in range(11)))
    result = subprocess.run([sys.executable, str(SCRIPT), '--project', str(project), '--confirm-new-project', '--unique-map-confirmed', '--original-texture-def', str(definition), '--texture-dir', str(source), '--compiled-texture-dir', str(compiled)], capture_output=True, text=True)
    assert result.returncode != 0 and 'dependency mismatch' in result.stderr
    assert not (project / 'data').exists()

@pytest.mark.parametrize('mode,quest', [('document','DlumaBossProbe'),('textures','DlumaBossProbe'),('controls','DlumaBossProbe'),('reset','DlumaBossProbe'),('callbacks','DlumaBossProbe'),('textures-missing','DlumaBossProbe'),('document-missing','DlumaBossProbe'),('textures-interrupt','DlumaBossProbe'),('document-interrupt','DlumaBossProbe')])
def test_setup_stages_only_one_selected_quest(tmp_path,mode,quest):
    source = tmp_path / 'template'; source.mkdir(); (source / 'desc.scr').write_text('template')
    target = tmp_path / 'project'
    result = subprocess.run([sys.executable, str(SCRIPT), '--template', str(source), '--output', str(target), '--boss-map', 'unique_map', '--unique-map-confirmed', '--scenario', mode], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    text = (target / 'data/quests/unique_map/dluma_boss_probe.pxsl').read_text()
    invoke = (target / 'data/quests/unique_map/unique_map_invoke.scr').read_text()
    assert text.count('    quest ') == 1 and f'quest {quest} -final=true' in text
    assert invoke.count('QuestStage( "+workshop') == 1 and quest in invoke
    assert f'dluma_{mode.replace(chr(45), chr(95))}_probe.scr' in text if mode != 'callbacks' else 'dluma_boss_probe.scr' in text

def test_select_scenario_replaces_only_recognized_generated_quest(tmp_path):
    template=tmp_path/'template';template.mkdir();(template/'desc.scr').write_text('x')
    target=tmp_path/'project'
    setup=subprocess.run([sys.executable,str(SCRIPT),'--template',str(template),'--output',str(target),'--boss-map','unique_map','--unique-map-confirmed'],capture_output=True,text=True)
    assert setup.returncode==0,setup.stderr
    select=SCRIPT.parent/'select_scenario.py';backup=tmp_path/'backup'
    cmd=[sys.executable,str(select),'--project',str(target),'--map','unique_map','--scenario','textures','--backup-dir',str(backup),'--confirm-disposable']
    result=subprocess.run(cmd,capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    quest=target/'data/quests/unique_map/dluma_boss_probe.pxsl';invoke=quest.parent/'unique_map_invoke.scr'
    assert quest.read_text().count('    quest ')==1 and 'dluma_textures_probe.scr' in quest.read_text()
    assert 'dluma_document_probe.scr' in (backup/'dluma_boss_probe.pxsl').read_text()
    quest.write_text('user changes')
    result=subprocess.run(cmd[:-3]+['--backup-dir',str(tmp_path/'backup2'),'--confirm-disposable'],capture_output=True)
    assert result.returncode!=0 and quest.read_text()=='user changes'

def test_selector_accepts_only_stock_f7_header_with_known_pxsl(tmp_path):
    template=tmp_path/'template';template.mkdir();(template/'desc.scr').write_text('x')
    target=tmp_path/'project'
    setup=subprocess.run([sys.executable,str(SCRIPT),'--template',str(template),'--output',str(target),'--boss-map','unique_map','--unique-map-confirmed'],capture_output=True,text=True)
    assert setup.returncode==0,setup.stderr
    quest=target/'data/quests/unique_map/dluma_boss_probe.pxsl';invoke=quest.parent/'unique_map_invoke.scr'
    invoke.write_text('!QuestStage( s, s, s )\n\n')
    select=SCRIPT.parent/'select_scenario.py'
    cmd=[sys.executable,str(select),'--project',str(target),'--map','unique_map','--scenario','textures','--backup-dir',str(tmp_path/'backup'),'--confirm-disposable']
    result=subprocess.run(cmd,capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    invoke.write_text('!QuestStage( s, s, s )\nforeign data\n')
    cmd[cmd.index('--backup-dir')+1]=str(tmp_path/'backup2')
    assert subprocess.run(cmd,capture_output=True).returncode!=0
    invoke.write_text('!QuestStage( s, s, s )\n\n');quest.write_text('foreign quest')
    assert subprocess.run(cmd,capture_output=True).returncode!=0


def test_generated_hud_uses_existing_fallback_font_and_refuses_missing_font(tmp_path):
    source=tmp_path/'template';source.mkdir();(source/'desc.scr').write_text('x')
    hud=tmp_path/'hud.xui';hud.write_text('<XuiCanvas><Properties><Width>1280</Width><Height>720</Height></Properties><XuiScene><IUIAARectangle><Properties><ImagePath>white</ImagePath><Material>menu_antialias.mat</Material></Properties></IUIAARectangle><MyText><Properties><Font>LocalFont</Font></Properties></MyText></XuiScene></XuiCanvas>')
    target=tmp_path/'project';command=[sys.executable,str(SCRIPT),'--template',str(source),'--output',str(target),'--unique-map-confirmed','--original-hud',str(hud)]
    result=subprocess.run(command,capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    assert (target/'data/menu/hud/hud_test.xui').read_text().count('<Font>LocalFont</Font>')==2
    hud.write_text('<XuiCanvas><XuiScene/></XuiCanvas>');command[command.index('--output')+1]=str(tmp_path/'other')
    result=subprocess.run(command,capture_output=True,text=True)
    assert result.returncode!=0 and not (tmp_path/'other').exists()


def test_setup_refuses_negative_probe_alias_or_document_collision(tmp_path):
    source=tmp_path/'template';source.mkdir();(source/'desc.scr').write_text('x')
    conflict=source/'data/menu/hud/__dluma_missing_document__.xui';conflict.parent.mkdir(parents=True);conflict.write_text('existing')
    out=tmp_path/'out';r=subprocess.run([sys.executable,str(SCRIPT),'--template',str(source),'--output',str(out),'--unique-map-confirmed'],capture_output=True)
    assert r.returncode!=0 and not out.exists()

def test_setup_refuses_existing_negative_texture_alias(tmp_path):
    source=tmp_path/'template';source.mkdir();(source/'desc.scr').write_text('x')
    definition=tmp_path/'hudtextures.def'
    definition.write_text(''.join(f'Whole("hud_bossfight_flash_{i}")\n' for i in range(11))+'Whole("dluma_test_missing_5")\n')
    out=tmp_path/'out';r=subprocess.run([sys.executable,str(SCRIPT),'--template',str(source),'--output',str(out),'--unique-map-confirmed','--original-texture-def',str(definition)],capture_output=True)
    assert r.returncode!=0 and not out.exists()
