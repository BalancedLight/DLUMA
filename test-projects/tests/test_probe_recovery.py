"""Exercise the shipped probe inside the actual Lua helper with a controlled host."""
import ctypes
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('lua_foundation', ROOT / 'tests/test_lua_runtime_foundation.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)

class Call(ctypes.Structure):
    _fields_ = [('size', ctypes.c_uint32), ('version', ctypes.c_uint32), ('status', ctypes.c_uint32), ('reserved', ctypes.c_uint32), ('error', ctypes.c_void_p)]

class AssetResult(ctypes.Structure):
    _fields_ = [('size', ctypes.c_uint32), ('version', ctypes.c_uint32), ('call', Call), ('token', ctypes.c_uint64), ('state', ctypes.c_uint32), ('reason', ctypes.c_uint32), ('ready_count', ctypes.c_uint32), ('asset_count', ctypes.c_uint32)]

@pytest.mark.parametrize('kind,failed,status_op,restore_op,restored', [('document',8,14011,14010,6),('textures',6,14014,14015,5)])
def test_failed_asset_retains_token_until_explicit_reset_and_confirmed_stop(kind, failed, status_op, restore_op, restored):
    harness = h.Harness(manual_clock=True)
    harness.shared_asset_success = True
    next_state = [failed]
    original = h.ApiInvokeFn(harness.host.api_invoke)
    @h.ApiInvokeFn
    def wrapped(user, operation, request, request_size, result, result_size, error):
        rc = original(user, operation, request, request_size, result, result_size, error)
        if operation == status_op and rc:
            ctypes.cast(result, ctypes.POINTER(AssetResult)).contents.state = next_state[0]
        return rc
    harness.host.api_invoke = ctypes.cast(wrapped, ctypes.c_void_p).value
    source = (ROOT / 'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
    try:
        state, loaded = harness.create(source, approved_permissions=h.PERMISSION_UI_OVERLAY | h.PERMISSION_QUEST_PHASE_HANDLE,
            scope=2, capabilities=h.CAP_RUNTIME, permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded == 1, bytes(harness.status(state).last_error)
        assert harness.dispatch_config(state, kind, h.VALUE_BOOLEAN, 'false', h.VALUE_BOOLEAN, 'true') == 1, (bytes(harness.status(state).last_error), harness.logs)
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert (status_op, 37) in harness.shared_api_calls
        assert harness.dispatch_config(state, kind, h.VALUE_BOOLEAN, 'true', h.VALUE_BOOLEAN, 'false') == 1
        assert (restore_op, 37) in harness.shared_api_calls
        # If recovery itself reports failure, keep the same token and wait for
        # a new explicit off transition instead of silently retrying.
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert sum(c == (restore_op, 37) for c in harness.shared_api_calls) == 1
        assert harness.dispatch_config(state, kind, h.VALUE_BOOLEAN, 'false', h.VALUE_BOOLEAN, 'true') == 1
        assert harness.dispatch_config(state, kind, h.VALUE_BOOLEAN, 'true', h.VALUE_BOOLEAN, 'false') == 1
        assert sum(c == (restore_op, 37) for c in harness.shared_api_calls) == 2
        next_state[0] = restored
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        # A subsequent request proves the token was released only on confirmation.
        before = len([c for c in harness.shared_api_calls if c[0] in (14009,14013)])
        assert harness.dispatch_config(state, kind, h.VALUE_BOOLEAN, 'false', h.VALUE_BOOLEAN, 'true') == 1
        assert len([c for c in harness.shared_api_calls if c[0] in (14009,14013)]) == before + 1
    finally:
        harness.close()


def test_explicit_reset_resumes_timed_out_restore_polling_without_duplicate_restore():
    harness = h.Harness(manual_clock=True)
    harness.shared_asset_success = True
    source = (ROOT / 'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
    try:
        state, loaded = harness.create(source, scope=2, approved_permissions=h.PERMISSION_UI_OVERLAY | h.PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=h.CAP_RUNTIME, permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded == 1
        harness.dispatch_config(state, 'document', h.VALUE_BOOLEAN, 'false', h.VALUE_BOOLEAN, 'true')
        harness.dispatch_config(state, 'document', h.VALUE_BOOLEAN, 'true', h.VALUE_BOOLEAN, 'false')
        for _ in range(240):
            harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        before = sum(c == (14011, 37) for c in harness.shared_api_calls)
        harness.dispatch_config(state, 'document', h.VALUE_BOOLEAN, 'false', h.VALUE_BOOLEAN, 'true')
        harness.dispatch_config(state, 'document', h.VALUE_BOOLEAN, 'true', h.VALUE_BOOLEAN, 'false')
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert sum(c == (14011, 37) for c in harness.shared_api_calls) == before + 1
        assert sum(c == (14010, 37) for c in harness.shared_api_calls) == 1
    finally:
        harness.close()

class HandlerEvent(h.Event):
    _fields_ = [('handler_source_kind', ctypes.c_uint32), ('handler_source_mod', h.ByteView)]

def dispatch_handler(harness, state, name):
    event = HandlerEvent()
    encoded = name.encode()
    event.struct_size = ctypes.sizeof(HandlerEvent); event.kind = 15; event.map_generation = 17
    event.name = encoded
    event.quest_phase.struct_size = ctypes.sizeof(h.QuestPhaseEvent); event.quest_phase.map_generation = 17
    event.quest_phase.provider = h.ByteView(b'test.runtime', 12); event.quest_phase.handler = h.ByteView(encoded, len(encoded))
    event.quest_phase.arguments_json = h.ByteView(b'{}', 2); event.handler_source_kind = 1
    harness.begin_pump(state)
    return harness.api.dispatch(state, ctypes.cast(ctypes.byref(event), ctypes.POINTER(h.Event)))

@pytest.mark.parametrize('handler,begin_op,restore_op', [('document',14009,14010),('textures',14013,14015)])
def test_boss_handlers_run_and_reset_real_asset_requests(handler, begin_op, restore_op):
    harness=h.Harness(manual_clock=True);harness.shared_asset_success=True
    source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
    try:
        state, loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded == 1
        assert dispatch_handler(harness,state,handler)==1
        assert any(c[0]==begin_op for c in harness.shared_api_calls)
        assert dispatch_handler(harness,state,'reset')==1
        assert (restore_op,37) in harness.shared_api_calls
        assert any('FEATURE_TEST run=' + handler in line for _,line in harness.logs)
        assert any('FEATURE_TEST reset' in line for _,line in harness.logs)
    finally:
        harness.close()

def test_boss_controls_handler_reports_unavailable_without_faking_success():
    harness=h.Harness(manual_clock=True)
    source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
    try:
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'controls')==1
        assert any('FEATURE_TEST run=controls' in line for _,line in harness.logs)
        assert any('show failed' in line for _,line in harness.logs)
    finally:
        harness.close()

@pytest.mark.parametrize('letter', ['a','b'])
def test_shared_handler_logs_boss_event_provenance(letter):
    harness=h.Harness()
    source=(ROOT/f'test-projects/packages/probe.hud.asset-{letter}/main.lua').read_text(encoding='utf-8')
    try:
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'shared')==1
        line=next(line for _,line in harness.logs if f'handler={letter.upper()} shared' in line)
        assert all(field in line for field in ('source=1','map=17','provider=test.runtime','handler=shared','quest=','phase='))
    finally:
        harness.close()


def test_document_sequence_uses_authored_red_then_green_and_confirms_normal_between():
    class SwapRequest(ctypes.Structure):
        _fields_ = [('size', ctypes.c_uint32), ('version', ctypes.c_uint32),
                    ('original', h.ByteView), ('replacement', h.ByteView)]
    harness = h.Harness(manual_clock=True)
    harness.shared_asset_success = True
    status = [4]
    paths = []
    original = h.ApiInvokeFn(harness.host.api_invoke)

    @h.ApiInvokeFn
    def wrapped(user, operation, request, request_size, result, result_size, error):
        rc = original(user, operation, request, request_size, result, result_size, error)
        if operation == 14009:
            r = ctypes.cast(request, ctypes.POINTER(SwapRequest)).contents
            paths.append((ctypes.string_at(r.original.data, r.original.size).decode(),
                          ctypes.string_at(r.replacement.data, r.replacement.size).decode()))
        if operation == 14011 and rc:
            ctypes.cast(result, ctypes.POINTER(AssetResult)).contents.state = status[0]
        return rc

    harness.host.api_invoke = ctypes.cast(wrapped, ctypes.c_void_p).value
    try:
        source = (ROOT / 'test-projects/packages/probe.hud.asset-a/main.lua').read_text()
        state, loaded = harness.create(source, scope=2,
            approved_permissions=h.PERMISSION_UI_OVERLAY | h.PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=h.CAP_RUNTIME, permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded == 1 and dispatch_handler(harness, state, 'document') == 1
        for _ in range(48):
            harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert len(paths) == 1
        assert sum(x == (14010, 37) for x in harness.shared_api_calls) == 1
        # A restore acknowledgement alone must not advance to green.
        status[0] = 5
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert len(paths) == 1
        status[0] = 6
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert paths == [('menu/hud/hud_normal.xui', 'menu/hud/hud_red.xui'),
                         ('menu/hud/hud_normal.xui', 'menu/hud/hud_green.xui')]
        status[0] = 4
        for _ in range(48):
            harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert sum(x == (14010, 37) for x in harness.shared_api_calls) == 2
        status[0] = 6
        harness.advance(300); harness.dispatch(state, h.EVENT_UPDATE)
        assert any('restoration confirmed; normal HUD' in text for _, text in harness.logs)
        assert len(paths) == 2
    finally:
        harness.close()

def test_missing_texture_uses_eleven_canonical_pairs_and_retains_token_until_stopped():
    harness=h.Harness(manual_clock=True);harness.shared_asset_success=True
    status=[6];old=h.ApiInvokeFn(harness.host.api_invoke)
    @h.ApiInvokeFn
    def wrapped(user,op,request,request_size,result,result_size,error):
        rc=old(user,op,request,request_size,result,result_size,error)
        if op==14014 and rc:
            value=ctypes.cast(result,ctypes.POINTER(AssetResult)).contents
            value.state=status[0];value.reason=2;value.ready_count=5;value.asset_count=22
        return rc
    harness.host.api_invoke=ctypes.cast(wrapped,ctypes.c_void_p).value
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'textures-missing')==1
        calls=[x for x in harness.shared_api_calls if x[0]==14013];assert len(calls)==1
        pairs=calls[0][1];assert len(pairs)==11
        assert [v[0] for v in pairs]==[f'hud_bossfight_flash_{i}'.encode() for i in range(11)]
        assert [v[1] for v in pairs]==[(b'dluma_test_missing_5' if i==5 else f'dluma_test_flash_{i}'.encode()) for i in range(11)]
        harness.advance(300);harness.dispatch(state,h.EVENT_UPDATE)
        assert any('ready=5' in line and 'assets=22' in line for _,line in harness.logs)
        assert dispatch_handler(harness,state,'textures')==1
        assert len([x for x in harness.shared_api_calls if x[0]==14013])==1
        assert dispatch_handler(harness,state,'reset')==1 and (14015,37) in harness.shared_api_calls
        status[0]=5;harness.advance(300);harness.dispatch(state,h.EVENT_UPDATE)
        assert dispatch_handler(harness,state,'textures')==1
        assert len([x for x in harness.shared_api_calls if x[0]==14013])==2
    finally:harness.close()

def test_missing_document_and_interrupted_busy_requests_use_same_guarded_token():
    harness=h.Harness(manual_clock=True);harness.shared_asset_success=True
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'document-missing')==1
        calls=[x for x in harness.shared_api_calls if x[0]==14009]
        assert calls==[(14009,(b'menu/hud/__dluma_missing_document__.xui',b'menu/hud/hud_red.xui'))]
        assert dispatch_handler(harness,state,'document')==1
        assert len([x for x in harness.shared_api_calls if x[0]==14009])==1
        assert dispatch_handler(harness,state,'reset')==1 and (14010,37) in harness.shared_api_calls
        assert dispatch_handler(harness,state,'reset')==1
        assert sum(x==(14010,37) for x in harness.shared_api_calls)==1
    finally:harness.close()

def test_missing_document_immediate_failure_does_not_create_busy_state():
    harness=h.Harness(manual_clock=True)
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'document-missing')==1
        assert any('document request failed:' in line for _,line in harness.logs)
        harness.shared_asset_success=True
        assert dispatch_handler(harness,state,'textures')==1
        assert any(op==14013 for op,_ in harness.shared_api_calls)
    finally:harness.close()

def test_interrupted_document_repeats_only_after_confirmed_restore_without_green():
    harness=h.Harness(manual_clock=True);harness.shared_asset_success=True;status=[4]
    old=h.ApiInvokeFn(harness.host.api_invoke)
    @h.ApiInvokeFn
    def wrapped(user,op,request,request_size,result,result_size,error):
        rc=old(user,op,request,request_size,result,result_size,error)
        if op==14011 and rc:ctypes.cast(result,ctypes.POINTER(AssetResult)).contents.state=status[0]
        return rc
    harness.host.api_invoke=ctypes.cast(wrapped,ctypes.c_void_p).value
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'document')==1
        assert dispatch_handler(harness,state,'document')==1
        assert sum(op==14009 for op,_ in harness.shared_api_calls)==1
        assert dispatch_handler(harness,state,'reset')==1
        assert dispatch_handler(harness,state,'document')==1
        assert sum(op==14009 for op,_ in harness.shared_api_calls)==1
        status[0]=6;harness.advance(300);harness.dispatch(state,h.EVENT_UPDATE)
        assert sum(op==14009 for op,_ in harness.shared_api_calls)==1
        assert dispatch_handler(harness,state,'document')==1
        assert sum(op==14009 for op,_ in harness.shared_api_calls)==2
    finally:harness.close()

def test_controls_sequence_is_visible_and_reset_cancels_timers():
    harness=h.Harness(manual_clock=True);calls=[];positions=[]
    class PositionRequest(ctypes.Structure):
        _fields_=[('size',ctypes.c_uint32),('version',ctypes.c_uint32),('screen',h.ByteView),('target',h.ByteView),('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
    @h.ApiInvokeFn
    def api(user,op,request,request_size,result,result_size,error):
        if op in (14001,14002,14003,14004,14005,14006):
            if op==14004:positions.append(ctypes.cast(request,ctypes.POINTER(PositionRequest)).contents.x)
            calls.append(op);ctypes.cast(result+32,ctypes.POINTER(ctypes.c_uint32))[0]=1;return 1
        return 0
    harness.host.api_invoke=ctypes.cast(api,ctypes.c_void_p).value
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'controls')==1
        assert calls==[14001]
        harness.advance(2000);harness.dispatch(state,h.EVENT_UPDATE)
        assert calls==[14001,14004,14006,14005]
        harness.advance(2000);harness.dispatch(state,h.EVENT_UPDATE)
        assert calls[-1]==14002
        harness.advance(2000);harness.dispatch(state,h.EVENT_UPDATE)
        assert calls[-1]==14001
        harness.advance(2000);harness.dispatch(state,h.EVENT_UPDATE)
        assert calls[-3:]==[14004,14006,14005] and positions==[600.0,400.0]
        assert dispatch_handler(harness,state,'reset')==1
        before=len(calls);harness.advance(10000);harness.dispatch(state,h.EVENT_UPDATE)
        assert len(calls)==before
    finally:harness.close()

def test_controls_failure_stops_future_mutations_and_unload_never_retries():
    harness=h.Harness(manual_clock=True);calls=[]
    @h.ApiInvokeFn
    def api(user,op,request,request_size,result,result_size,error):
        calls.append(op)
        if op==14004:
            error.contents.code=b'xui_result_uncertain';return 0
        ctypes.cast(result+32,ctypes.POINTER(ctypes.c_uint32))[0]=1;return 1
    harness.host.api_invoke=ctypes.cast(api,ctypes.c_void_p).value
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'controls')==1
        harness.advance(2000);harness.dispatch(state,h.EVENT_UPDATE)
        assert calls==[14001,14004]
        harness.advance(10000);harness.dispatch(state,h.EVENT_UPDATE)
        harness.dispatch(state,4)
        assert calls==[14001,14004]
    finally:harness.close()

@pytest.mark.parametrize('event', ['reset','unload'])
def test_controls_early_cleanup_cancels_pending_timer(event):
    harness=h.Harness(manual_clock=True);calls=[]
    @h.ApiInvokeFn
    def api(user,op,request,request_size,result,result_size,error):
        calls.append(op);ctypes.cast(result+32,ctypes.POINTER(ctypes.c_uint32))[0]=1;return 1
    harness.host.api_invoke=ctypes.cast(api,ctypes.c_void_p).value
    try:
        source=(ROOT/'test-projects/packages/probe.hud.asset-a/main.lua').read_text(encoding='utf-8')
        state,loaded=harness.create(source,scope=2,approved_permissions=h.PERMISSION_UI_OVERLAY|h.PERMISSION_QUEST_PHASE_HANDLE,capabilities=h.CAP_RUNTIME,permissions='ui.overlay,quest.phase.handle,configuration.read')
        assert loaded==1 and dispatch_handler(harness,state,'controls')==1
        if event=='reset':assert dispatch_handler(harness,state,'reset')==1
        else:harness.dispatch(state,4)
        assert calls==[14001,14004,14006,14005,14003]
        before=list(calls);harness.advance(10000);harness.dispatch(state,h.EVENT_UPDATE)
        assert calls==before
    finally:harness.close()
