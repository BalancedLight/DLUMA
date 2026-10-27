from __future__ import annotations

from pathlib import Path

from test_lua_runtime_foundation import EVENT_LOAD, Harness, declaration


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "src" / "dlw_lua_runtime.c").read_text(encoding="utf-8")


def _dispatch_slot_source() -> str:
    return RUNTIME[
        RUNTIME.index("static int dluma_lua_runtime_dispatch_slot") :
        RUNTIME.index("static void dluma_lua_runtime_destroy_slot")
    ]


def _dispatch_helper_source() -> str:
    return RUNTIME[
        RUNTIME.index("static int dluma_lua_runtime_dispatch_helper_event") :
        RUNTIME.index("static int dluma_lua_runtime_dispatch_slot")
    ]


def _queued_dispatch_source() -> str:
    return RUNTIME[
        RUNTIME.index("static void dluma_lua_runtime_dispatch_queued_events") :
        RUNTIME.index("int dluma_lua_runtime_start")
    ]


def test_helper_reports_zero_one_and_two_successful_callbacks_exactly():
    harness = Harness()
    try:
        no_match, no_match_loaded = harness.create(declaration())
        one, one_loaded = harness.create(
            declaration("mod:on_load(function() end)")
        )
        two, two_loaded = harness.create(
            declaration(
                """
mod:on_load(function() end)
mod:on_load(function() end)
"""
            )
        )

        assert no_match_loaded == one_loaded == two_loaded == 1
        assert harness.dispatch(no_match, EVENT_LOAD) == 0
        assert harness.dispatch(one, EVENT_LOAD) == 1
        assert harness.dispatch(two, EVENT_LOAD) == 2
    finally:
        harness.close()


def test_two_states_sum_to_two_successful_callbacks():
    harness = Harness()
    try:
        first, first_loaded = harness.create(
            declaration("mod:on_load(function() end)")
        )
        second, second_loaded = harness.create(
            declaration("mod:on_load(function() end)")
        )

        assert first_loaded == second_loaded == 1
        assert sum(
            harness.dispatch(state, EVENT_LOAD) for state in (first, second)
        ) == 2
    finally:
        harness.close()


def test_throwing_or_disabled_state_does_not_report_success():
    harness = Harness()
    try:
        throwing, throwing_loaded = harness.create(
            declaration('mod:on_load(function() error("delivery failure") end)')
        )
        disabled, disabled_loaded = harness.create(
            'dluma.mod { id = "wrong.owner", name = "Runtime Test", '
            'version = "1.0.0" }'
        )

        assert throwing_loaded == 1
        assert disabled_loaded == 0
        assert harness.dispatch(throwing, EVENT_LOAD) == 0
        assert b"delivery failure" in bytes(harness.status(throwing).last_error)
        assert harness.dispatch(disabled, EVENT_LOAD) == 0
        assert harness.status(disabled).disabled == 1
        assert harness.dispatch(disabled, EVENT_LOAD) == 0
    finally:
        harness.close()


def test_runtime_propagates_helper_count_and_refreshes_status_after_dispatch():
    dispatch = _dispatch_helper_source()
    helper_call = "callback_count = g_dluma_lua_runtime.api.dispatch("
    assert helper_call in dispatch
    assert "slot->helper_script, event);" in dispatch
    assert "return callback_count > 0 ? callback_count : 0;" in dispatch
    assert dispatch.index(helper_call) < dispatch.index(
        "g_dluma_lua_runtime.api.get_status(slot->helper_script, &slot->status);"
    )
    assert dispatch.index("api.get_status") < dispatch.index("return callback_count")


def test_runtime_sums_per_slot_counts_and_logs_only_real_delivery():
    queued = _queued_dispatch_source()
    assert "int slot_delivered = dluma_lua_runtime_dispatch_slot(" in queued
    assert "if (slot_delivered > 0)" in queued
    assert "delivered += slot_delivered;" in queued
    assert "dluma_lua_runtime_log_native_event_slot_delivery(" in queued
    assert "if (delivered > 0)" in queued
    assert "dluma_lua_runtime_log_native_event_result(&event, delivered);" in queued
    assert "delivered = 1" not in queued


def test_old_generation_is_discarded_before_any_state_dispatch():
    queued = _queued_dispatch_source()
    discard = "if (event.map_generation != g_dluma_lua_runtime.map_generation ||"
    assert discard in queued
    assert "event.native_index_epoch !=" in queued
    assert "dluma_native_gameplay_event_subscription_current(" in queued
    assert queued.index(discard) < queued.index(
        "int slot_delivered = dluma_lua_runtime_dispatch_slot("
    )


def test_delivery_logs_have_event_identity_counts_and_bounded_unhandled_path():
    identity = RUNTIME[
        RUNTIME.index("static void dluma_lua_runtime_append_native_event_identity") :
        RUNTIME.index("static void dluma_lua_runtime_dispatch_queued_events")
    ]
    queued = _queued_dispatch_source()

    for field in ('"kind="', 'name=', 'generation='):
        assert field in identity
    assert '" script_id="' in identity
    assert '" callback_count="' in identity
    assert '"lua_native_event_slot_delivered"' in identity
    assert '"lua_native_event_delivered"' in identity
    assert '"lua_native_event_unhandled"' in identity

    assert "#define DLUMA_LUA_RUNTIME_MAX_UNHANDLED_EVENT_LOGS 8u" in RUNTIME
    assert (
        "g_dluma_lua_runtime.unhandled_event_log_count <\n"
        "                   DLUMA_LUA_RUNTIME_MAX_UNHANDLED_EVENT_LOGS"
    ) in queued
    assert "++g_dluma_lua_runtime.unhandled_event_log_count;" in queued
    assert "dluma_lua_runtime_log_native_event_result(&event, 0);" in queued
    assert "g_dluma_lua_runtime.unhandled_event_log_count = 0;" in RUNTIME
