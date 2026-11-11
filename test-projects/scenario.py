"""Render one selectable, non-overlapping disposable quest scenario."""
import re

SCENARIOS = {
    'callbacks': ('DlumaBossProbe', 'Wait for local handlers', 'Exercise scoped boss actions', 'dluma_boss_probe.scr', 'DLUMA Boss Callbacks'),
    'document': ('DlumaBossProbe', 'Start document test', 'Observe document swap and reset', 'dluma_document_probe.scr', 'DLUMA Document Test'),
    'textures': ('DlumaBossProbe', 'Start texture test', 'Observe flashback replacement and reset', 'dluma_textures_probe.scr', 'DLUMA Texture Test'),
    'controls': ('DlumaBossProbe', 'Start control test', 'Observe panel controls and reset', 'dluma_controls_probe.scr', 'DLUMA Controls Test'),
    'textures-missing': ('DlumaBossProbe', 'Start missing texture test', 'Observe failure and cleanup', 'dluma_textures_missing_probe.scr', 'DLUMA Missing Texture Test'),
    'document-missing': ('DlumaBossProbe', 'Start missing document test', 'Observe failure and cleanup', 'dluma_document_missing_probe.scr', 'DLUMA Missing Document Test'),
    'textures-interrupt': ('DlumaBossProbe', 'Start interrupted texture test', 'Observe interruption and repeat', 'dluma_textures_interrupt_probe.scr', 'DLUMA Interrupted Texture Test'),
    'document-interrupt': ('DlumaBossProbe', 'Start interrupted document test', 'Observe interruption and repeat', 'dluma_document_interrupt_probe.scr', 'DLUMA Interrupted Document Test'),
    'reset': ('DlumaBossProbe', 'Reset active test', 'Request test cleanup', 'dluma_reset_probe.scr', 'DLUMA Reset'),
}

def render(map_name: str, scenario: str) -> tuple[str, str]:
    if not re.fullmatch(r'[A-Za-z0-9_]{1,64}', map_name): raise ValueError('Invalid map name')
    if scenario not in SCENARIOS: raise ValueError('Invalid scenario')
    quest, entry, action, script, label = SCENARIOS[scenario]
    pxsl = f'<<<?xml version="1.0"?>>>\n\nQuestsDefinitions -level={map_name}\n\n    quest {quest} -final=true\n        ,wait <<{entry}>> 5\n        ,bossfight <<{action}>> {script}\n'
    invoke = f'!QuestStage( s, s, s )\n\nQuestStage( "+workshop 00x01s  {label} ({quest}/{entry})", "{quest}", "{entry}" )\n'
    return pxsl, invoke
