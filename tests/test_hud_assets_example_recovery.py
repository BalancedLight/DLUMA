"""Run the public HUD example in the Lua helper with controlled host responses."""
import ctypes
from pathlib import Path

import pytest

import test_lua_runtime_foundation as h

ROOT = Path(__file__).resolve().parents[1]


class Call(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("version", ctypes.c_uint32),
                ("status", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
                ("error", ctypes.c_void_p)]


class AssetResult(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("version", ctypes.c_uint32),
                ("call", Call), ("token", ctypes.c_uint64), ("state", ctypes.c_uint32),
                ("reason", ctypes.c_uint32), ("ready_count", ctypes.c_uint32),
                ("asset_count", ctypes.c_uint32)]


class HandlerEvent(h.Event):
    _fields_ = [("handler_source_kind", ctypes.c_uint32), ("handler_source_mod", h.ByteView)]


def invoke(harness, state, name):
    event = HandlerEvent()
    encoded = name.encode()
    event.struct_size = ctypes.sizeof(event)
    event.kind, event.map_generation, event.name = 15, 17, encoded
    event.quest_phase.struct_size = ctypes.sizeof(h.QuestPhaseEvent)
    event.quest_phase.map_generation = 17
    event.quest_phase.provider = h.ByteView(b"test.runtime", 12)
    event.quest_phase.handler = h.ByteView(encoded, len(encoded))
    event.quest_phase.arguments_json = h.ByteView(b"{}", 2)
    event.handler_source_kind = 1
    harness.begin_pump(state)
    assert harness.api.dispatch(state, ctypes.cast(ctypes.byref(event), ctypes.POINTER(h.Event))) == 1


def tick(harness, state):
    harness.advance(300)
    harness.dispatch(state, h.EVENT_UPDATE)


@pytest.mark.parametrize("handler,begin,status,restore,failed,pending,done", [
    ("swap-document", 14009, 14011, 14010, 8, 5, 6),
    ("swap-flashbacks", 14013, 14014, 14015, 6, 4, 5),
])
def test_example_retains_failed_token_and_resumes_timed_out_restore(
    handler, begin, status, restore, failed, pending, done
):
    harness = h.Harness(manual_clock=True)
    harness.shared_asset_success = True
    current_state = [failed]
    original = h.ApiInvokeFn(harness.host.api_invoke)

    @h.ApiInvokeFn
    def wrapped(user, op, request, request_size, result, result_size, error):
        rc = original(user, op, request, request_size, result, result_size, error)
        if op == status and rc:
            ctypes.cast(result, ctypes.POINTER(AssetResult)).contents.state = current_state[0]
        return rc

    harness.host.api_invoke = ctypes.cast(wrapped, ctypes.c_void_p).value
    source = (ROOT / "examples/hud-assets/lua/main.lua").read_text(encoding="utf-8-sig")
    try:
        state, loaded = harness.create(source, scope=2,
            approved_permissions=h.PERMISSION_UI_OVERLAY | h.PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=h.CAP_RUNTIME, permissions="ui.overlay,quest.phase.handle")
        assert loaded == 1, bytes(harness.status(state).last_error)
        invoke(harness, state, handler)
        tick(harness, state)
        assert (status, 37) in harness.shared_api_calls
        invoke(harness, state, handler)
        assert sum(op == begin for op, _ in harness.shared_api_calls) == 1
        current_state[0] = pending
        invoke(harness, state, "restore-assets")
        assert sum(call == (restore, 37) for call in harness.shared_api_calls) == 1
        for _ in range(120):
            tick(harness, state)
        before = sum(call == (status, 37) for call in harness.shared_api_calls)
        tick(harness, state)
        assert sum(call == (status, 37) for call in harness.shared_api_calls) == before
        invoke(harness, state, "restore-assets")
        tick(harness, state)
        assert sum(call == (status, 37) for call in harness.shared_api_calls) == before + 1
        assert sum(call == (restore, 37) for call in harness.shared_api_calls) == 1
        current_state[0] = done
        tick(harness, state)
        invoke(harness, state, handler)
        assert sum(op == begin for op, _ in harness.shared_api_calls) == 2
    finally:
        harness.close()
