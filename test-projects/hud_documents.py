"""Create complete HUD variants using only dependencies in the original HUD."""
import math
import xml.etree.ElementTree as ET


def marker_documents(original: bytes) -> dict[str, bytes]:
    root = ET.fromstring(original)
    if root.tag != 'XuiCanvas' or len(root.findall('XuiScene')) != 1 or original.count(b'</XuiScene>') != 1:
        raise ValueError('Marker documents require one complete XuiCanvas scene')
    if any((item.text or '').strip() == 'DLUMA_DocumentMarker' for item in root.iter('Id')):
        raise ValueError('Original HUD already contains the document marker')
    for tag, name in [('ImagePath', 'white'), ('Material', 'menu_antialias.mat')]:
        if not any((item.text or '').strip() == name for item in root.iter(tag)):
            raise ValueError(f'Marker documents require existing {tag}: {name}')
    try:
        width = float(root.findtext('Properties/Width', ''))
        height = float(root.findtext('Properties/Height', ''))
    except ValueError as error:
        raise ValueError('Original HUD needs numeric canvas dimensions') from error
    if not all(math.isfinite(value) and 256 <= value <= 16384 for value in (width, height)):
        raise ValueError('Original HUD dimensions cannot contain the test square')
    x, y = f'{(width - 256) / 2:g}', f'{(height - 256) / 2:g}'
    result = {'hud_normal.xui': original}
    for name, color in [('hud_red.xui', '0xffff0000'), ('hud_green.xui', '0xff00ff00')]:
        marker = f'''<AdvGroup><Properties><Id>DLUMA_DocumentMarker</Id><Width>256</Width><Height>256</Height><Position>{x},{y},0</Position><Show>true</Show></Properties>
<IUIAARectangle><Properties><Id>Square</Id><Width>256</Width><Height>256</Height><Position>0,0,0</Position><ImagePath>white</ImagePath><Material>menu_antialias.mat</Material><Color>{color}</Color><Opacity>1</Opacity><UseVertexColor>true</UseVertexColor><Show>true</Show></Properties></IUIAARectangle></AdvGroup>'''.encode('utf-8')
        generated = original.replace(b'</XuiScene>', marker + b'\n</XuiScene>', 1)
        ET.fromstring(generated)
        result[name] = generated
    return result
