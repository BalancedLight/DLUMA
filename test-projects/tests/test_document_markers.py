import importlib.util
from pathlib import Path
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parents[1]


def test_complete_documents_show_centered_red_green_and_restore_original():
    spec = importlib.util.spec_from_file_location('hud_documents', HERE / 'hud_documents.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    source = b'<XuiCanvas><Properties><Width>1280</Width><Height>720</Height></Properties><XuiScene><Properties><Id>HUD_DI</Id></Properties><IUIAARectangle><Properties><Id>Existing</Id><ImagePath>white</ImagePath><Material>menu_antialias.mat</Material></Properties></IUIAARectangle></XuiScene></XuiCanvas>'
    documents = module.marker_documents(source)
    assert documents['hud_normal.xui'] == source
    for name, color in [('hud_red.xui', '0xffff0000'), ('hud_green.xui', '0xff00ff00')]:
        root = ET.fromstring(documents[name])
        panel = next(x for x in root.iter('AdvGroup') if x.findtext('Properties/Id') == 'DLUMA_DocumentMarker')
        assert panel.findtext('Properties/Position') == '512,232,0'
        assert panel.findtext('Properties/Show') == 'true'
        square = panel.find('IUIAARectangle')
        assert square.findtext('Properties/Color') == color
        assert square.findtext('Properties/Opacity') == '1'
        assert square.findtext('Properties/Width') == '256'
        assert square.findtext('Properties/Height') == '256'
        assert root.find('.//IUIAARectangle/Properties/Id').text == 'Existing'
        assert {x.text for x in root.iter('ImagePath')} == {'white'}
        assert {x.text for x in root.iter('Material')} == {'menu_antialias.mat'}
