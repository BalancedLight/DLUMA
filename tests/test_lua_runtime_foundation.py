from __future__ import annotations

import ctypes
import time
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "build" / "package" / "Release" / "dluma_lua54.dll"
ABI = 0x00040000
SCOPE_MAP = 3
SCOPE_GLOBAL = 1
EVENT_LOAD = 1
EVENT_MAP_LOAD = 3
EVENT_UPDATE = 5
EVENT_SENSOR_ENTER = 7
EVENT_SENSOR_LEAVE = 8
EVENT_INTERACT_BEGIN = 9
EVENT_CONFIG_CHANGED = 12
EVENT_QUEST_PHASE_PROVIDER = 13
EVENT_QUEST_PHASE_OBSERVER = 14
CAP_RUNTIME = 1 << 0
CAP_STORAGE = 1 << 1
CAP_WORLD_FIND = 1 << 2
CAP_EVENTS_SENSOR = 1 << 4
CAP_EVENTS_INTERACT = 1 << 5
CAP_ACTION_CHAIN = 1 << 10
CAP_UI_SUBTITLES = 1 << 12
CAP_AUDIO_RADIO_DIALOG = 1 << 13
CAP_QUEST_NATIVE_CONTROL = 1 << 14
CAP_EVENTS_ENEMY_KILLED = 1 << 15
CAP_UI_QUEST_HUD = 1 << 19
CAP_QUEST_PHASE_HANDLERS = 1 << 20
PERMISSION_GAME_READ = 1 << 0
PERMISSION_WORLD_QUERY = 1 << 2
PERMISSION_EVENTS_SENSOR = 1 << 3
PERMISSION_EVENTS_INTERACT = 1 << 4
PERMISSION_STORAGE = 1 << 5
PERMISSION_DEBUG = 1 << 6
PERMISSION_UI_OVERLAY = 1 << 7
PERMISSION_AUDIO_PLAY = 1 << 8
PERMISSION_WORLD_WRITE = 1 << 9
PERMISSION_QUEST_WRITE = 1 << 19
PERMISSION_EVENTS_ENEMY_KILLED = 1 << 26
PERMISSION_QUEST_PHASE_HANDLE = 1 << 27
PERMISSION_QUEST_PHASE_OBSERVE = 1 << 28
VALUE_NONE = 0
VALUE_STRING = 1
VALUE_BOOLEAN = 3
VALUE_INTEGER = 4


ClockFn = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_void_p)
LogFn = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p)
StorageGetFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_char),
    ctypes.c_uint32,
)
StorageSetFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_uint32,
    ctypes.c_char_p,
)
StorageDeleteFn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p)
DebugSetFn = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_int,
)


class HostError(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("retryable", ctypes.c_uint32),
        ("code", ctypes.c_char * 64),
        ("message", ctypes.c_char * 256),
        ("capability", ctypes.c_char * 64),
        ("permission", ctypes.c_char * 64),
        ("object_name", ctypes.c_char * 96),
    ]


class HostObject(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("handle_id", ctypes.c_uint64),
        ("owner_id", ctypes.c_uint64),
        ("object_id", ctypes.c_int32),
        ("reserved", ctypes.c_uint32),
        ("name", ctypes.c_char * 96),
        ("class_name", ctypes.c_char * 96),
    ]


class SubtitleRequest(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("force", ctypes.c_uint32),
        ("duration_seconds", ctypes.c_double),
        ("text", ctypes.c_char * 1024),
    ]


class SubtitleUpdateRequest(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("fade", ctypes.c_uint32),
        ("text", ctypes.c_char * 1024),
    ]


class RadioDialogRequest(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("speaker", ctypes.c_char * 96),
        ("group", ctypes.c_char * 96),
    ]


ConfigGetFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_char),
    ctypes.c_uint32,
    ctypes.POINTER(HostError),
)


class QuestHudTarget(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("kind", ctypes.c_uint32),
        ("slot", ctypes.c_uint32),
        ("overridden", ctypes.c_uint32),
        ("id", ctypes.c_char * 96),
        ("text", ctypes.c_char * 1025),
        ("display_text", ctypes.c_char * 1025),
    ]


class QuestHudSnapshot(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("revision", ctypes.c_uint64),
        ("has_header", ctypes.c_uint32),
        ("objective_count", ctypes.c_uint32),
        ("has_timer", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("header", QuestHudTarget),
        ("objectives", QuestHudTarget * 4),
        ("timer", QuestHudTarget),
    ]


class QuestHudOverrideRequest(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("owner_id", ctypes.c_uint64),
        ("override_id", ctypes.c_uint64),
        ("target_id", ctypes.c_char * 96),
        ("text", ctypes.c_char * 513),
    ]


class QuestHudOverrideResult(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("active", ctypes.c_uint32),
        ("override_id", ctypes.c_uint64),
    ]


class QuestRequest(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("owner_id", ctypes.c_uint64),
        ("operation", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("native_quest_id", ctypes.c_char * 128),
        ("native_phase_id", ctypes.c_char * 128),
    ]


class QuestResult(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("operation", ctypes.c_uint32),
        ("changed", ctypes.c_uint32),
        ("native_state", ctypes.c_uint32),
        ("native_quest_id", ctypes.c_char * 128),
        ("native_phase_id", ctypes.c_char * 128),
    ]


class QuestStateReport(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("owner_id", ctypes.c_uint64),
        ("revision", ctypes.c_uint64),
        ("active", ctypes.c_uint32),
        ("state", ctypes.c_uint32),
        ("native_state", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("script_id", ctypes.c_char * 128),
        ("quest_id", ctypes.c_char * 128),
        ("title", ctypes.c_char * 160),
        ("version", ctypes.c_char * 64),
        ("phase_id", ctypes.c_char * 128),
        ("phase_title", ctypes.c_char * 160),
        ("objective_summary", ctypes.c_char * 1024),
    ]


class EnemyKilledEvent(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("event_sequence", ctypes.c_uint64),
        ("killed_by_local_player", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("target_name", ctypes.c_char * 96),
        ("target_class", ctypes.c_char * 96),
        ("killer_name", ctypes.c_char * 96),
    ]


class ConfigChangedEvent(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("old_value_type", ctypes.c_uint32),
        ("value_type", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("key", ctypes.c_char * 65),
        ("old_value", ctypes.c_char * 513),
        ("value", ctypes.c_char * 513),
    ]


class ByteView(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_char_p),
        ("size", ctypes.c_size_t),
    ]


class QuestPhaseEvent(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("runtime_kind", ctypes.c_uint32),
        ("completion", ctypes.c_uint32),
        ("sequence", ctypes.c_uint64),
        ("wait_seconds", ctypes.c_double),
        ("local_peer", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("quest_id", ByteView),
        ("phase_id", ByteView),
        ("contract_id", ByteView),
        ("provider", ByteView),
        ("handler", ByteView),
        ("binding_hash", ByteView),
        ("arguments_json", ByteView),
    ]


SubscriptionFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_uint32,
    ctypes.c_char_p,
    ctypes.POINTER(HostError),
)
WorldFindFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.POINTER(HostObject),
    ctypes.POINTER(HostError),
)
SensorInsideFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(HostError),
)
CapabilitiesFn = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p)
SubtitleShowFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(SubtitleRequest),
    ctypes.POINTER(HostError),
)
SubtitleUpdateFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(SubtitleUpdateRequest),
    ctypes.POINTER(HostError),
)
RadioDialogFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(RadioDialogRequest),
    ctypes.POINTER(HostError),
)
ActionRunChainFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p,
    ctypes.POINTER(HostError),
)
QuestMutateFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(QuestRequest),
    ctypes.POINTER(QuestResult), ctypes.POINTER(HostError),
)
QuestStateChangedFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(QuestStateReport),
    ctypes.POINTER(HostError),
)
QuestHudSnapshotFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(QuestHudSnapshot),
    ctypes.POINTER(HostError),
)
QuestHudOverrideSetFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(QuestHudOverrideRequest),
    ctypes.POINTER(QuestHudOverrideResult), ctypes.POINTER(HostError),
)
QuestHudOverrideClearFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32,
    ctypes.c_uint64, ctypes.POINTER(QuestHudOverrideResult),
    ctypes.POINTER(HostError),
)
QuestHudClearOwnerFn = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint64)
ModuleLoadFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(HostError),
)
ModuleReleaseFn = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)
CapabilityQueryFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_char),
    ctypes.c_uint32,
    ctypes.POINTER(HostError),
)
ApiInvokeFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.c_void_p,
    ctypes.c_uint32,
    ctypes.POINTER(HostError),
)


class HostCallbacks(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("clock_ms", ClockFn),
        ("log", LogFn),
        ("storage_get", StorageGetFn),
        ("storage_set", StorageSetFn),
        ("storage_delete", StorageDeleteFn),
        ("debug_set", DebugSetFn),
        ("subscription_added", ctypes.c_void_p),
        ("subscription_removed", ctypes.c_void_p),
        ("world_find", ctypes.c_void_p),
        ("world_get_info", ctypes.c_void_p),
        ("world_get_local_enabled", ctypes.c_void_p),
        ("world_get_position", ctypes.c_void_p),
        ("world_get_forward", ctypes.c_void_p),
        ("world_set_local_enabled", ctypes.c_void_p),
        ("sensor_is_inside", ctypes.c_void_p),
        ("subtitle_show", ctypes.c_void_p),
        ("subtitle_update", ctypes.c_void_p),
        ("radio_dialog", ctypes.c_void_p),
        ("action_run_chain", ctypes.c_void_p),
        ("quest_mutate", ctypes.c_void_p),
        ("quest_state_changed", ctypes.c_void_p),
        ("quest_hud_snapshot", ctypes.c_void_p),
        ("quest_hud_override_set", ctypes.c_void_p),
        ("quest_hud_override_clear", ctypes.c_void_p),
        ("quest_hud_clear_owner", ctypes.c_void_p),
        ("capabilities", ctypes.c_void_p),
        ("config_get", ctypes.c_void_p),
        ("module_load", ctypes.c_void_p),
        ("module_release", ctypes.c_void_p),
        ("capability_query", ctypes.c_void_p),
        ("api_invoke", ctypes.c_void_p),
    ]


class ScriptConfig(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("scope", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("approved_permissions", ctypes.c_uint32),
        ("capabilities", ctypes.c_uint32),
        ("owner_id", ctypes.c_uint64),
        ("source_fingerprint", ctypes.c_uint64),
        ("memory_limit_bytes", ctypes.c_uint64),
        ("instruction_limit", ctypes.c_uint64),
        ("wall_clock_limit_ms", ctypes.c_uint32),
        ("max_callbacks", ctypes.c_uint32),
        ("max_timers", ctypes.c_uint32),
        ("max_rules", ctypes.c_uint32),
        ("max_log_messages_per_second", ctypes.c_uint32),
        ("id", ctypes.c_char_p),
        ("name", ctypes.c_char_p),
        ("version", ctypes.c_char_p),
        ("permissions", ctypes.c_char_p),
        ("file_path", ctypes.c_char_p),
        ("project", ctypes.c_char_p),
        ("map", ctypes.c_char_p),
        ("workshop_id", ctypes.c_char_p),
        ("loader_version", ctypes.c_char_p),
        ("is_editor", ctypes.c_int),
        ("is_player", ctypes.c_int),
        ("host_user", ctypes.c_void_p),
        ("host", HostCallbacks),
    ]


class Event(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("kind", ctypes.c_uint32),
        ("map_generation", ctypes.c_uint32),
        ("state", ctypes.c_int),
        ("number", ctypes.c_double),
        ("name", ctypes.c_char_p),
        ("project", ctypes.c_char_p),
        ("map", ctypes.c_char_p),
        ("enemy_killed", EnemyKilledEvent),
        ("config_changed", ConfigChangedEvent),
        ("quest_phase", QuestPhaseEvent),
    ]


class Status(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("alive", ctypes.c_uint32),
        ("disabled", ctypes.c_uint32),
        ("declaration_seen", ctypes.c_uint32),
        ("has_update_callback", ctypes.c_uint32),
        ("active_callbacks", ctypes.c_uint32),
        ("budget_violations", ctypes.c_uint32),
        ("memory_used_bytes", ctypes.c_uint64),
        ("source_fingerprint", ctypes.c_uint64),
        ("last_callback_lua_us", ctypes.c_uint64),
        ("last_callback_host_excluded_us", ctypes.c_uint64),
        ("last_instruction_count_or_remaining", ctypes.c_uint64),
        ("last_event", ctypes.c_char * 160),
        ("budget_type", ctypes.c_char * 32),
        ("last_error", ctypes.c_char * 1024),
    ]


CreateFn = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.POINTER(ScriptConfig))
LoadFn = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t
)
DispatchFn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(Event))
StatusFn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(Status))
DestroyFn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
BeginPumpFn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)


class Api(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("abi_version", ctypes.c_uint32),
        ("release", ctypes.c_char_p),
        ("create_script", CreateFn),
        ("load_script", LoadFn),
        ("dispatch", DispatchFn),
        ("get_status", StatusFn),
        ("destroy_script", DestroyFn),
        ("begin_pump", BeginPumpFn),
    ]


class Harness:
    def __init__(
        self,
        *,
        manual_clock: bool = False,
        host_sleep_s: float = 0.0,
        log_sleep_s: float | None = None,
        world_sleep_s: float | None = None,
        dynamic_capabilities: int | None = None,
        quest_mutate_mirrors: bool = False,
        config_get_fail: bool = False,
    ):
        if not HELPER.is_file():
            pytest.fail(f"private Lua helper is not packaged: {HELPER}")
        self.dll = ctypes.CDLL(str(HELPER))
        self.get_api = self.dll.DLUMA_Lua54_GetApi
        self.get_api.argtypes = [ctypes.c_uint32, ctypes.POINTER(Api), ctypes.c_uint32]
        self.get_api.restype = ctypes.c_int
        self.api = Api()
        assert self.get_api(ABI, ctypes.byref(self.api), ctypes.sizeof(self.api)) == 1
        self.logs: list[tuple[str, str]] = []
        self.storage: dict[str, tuple[int, bytes]] = {}
        self.debug: dict[str, str] = {}
        self.debug_lines: list[str] = []
        self.clock_value = int(time.monotonic() * 1000)
        self.manual_clock = manual_clock
        self.host_sleep_s = host_sleep_s
        self.log_sleep_s = host_sleep_s if log_sleep_s is None else log_sleep_s
        self.world_sleep_s = host_sleep_s if world_sleep_s is None else world_sleep_s
        self.dynamic_capabilities = dynamic_capabilities
        self.owner_capabilities: dict[int, int] = {}
        self.config_get_fail = config_get_fail
        self.config: dict[str, tuple[int, bytes]] = {}
        self.subscription_add_calls: list[tuple[int, int, int, str]] = []
        self.subscription_remove_calls: list[tuple[int, int, int, str]] = []
        self.subscription_remove_ok = True
        self.subscription_remove_fail_tokens: set[int] = set()
        self.sensor_inside: dict[str, bool] = {}
        self.sensor_inside_calls: list[str] = []
        self.sensor_query_fail = False
        self.subtitle_show_calls: list[tuple[str, float, bool]] = []
        self.subtitle_update_calls: list[tuple[str, bool]] = []
        self.radio_dialog_calls: list[tuple[str, str]] = []
        self.action_chain_calls: list[str] = []
        self.quest_reports: list[dict[str, object]] = []
        self.quest_mutate_calls: list[tuple[int, str, str]] = []
        self.quest_hud_snapshot_calls = 0
        self.quest_hud_override_calls: list[tuple[int, int, str, str]] = []
        self.quest_hud_clear_calls: list[tuple[int, int, int]] = []
        self.quest_hud_clear_owner_calls: list[int] = []
        self.quest_hud_next_override_id = 100

        def sleep_for(seconds: float | None = None):
            delay = self.host_sleep_s if seconds is None else seconds
            if delay:
                time.sleep(delay)

        @ClockFn
        def clock(_user):
            sleep_for()
            if not self.manual_clock:
                self.clock_value = int(time.monotonic() * 1000)
            return self.clock_value

        @LogFn
        def log(_user, level, text):
            sleep_for(self.log_sleep_s)
            self.logs.append((level.decode(), text.decode(errors="replace")))

        @StorageGetFn
        def storage_get(_user, key, value_type, out, capacity):
            sleep_for()
            item = self.storage.get(key.decode())
            if item is None:
                value_type[0] = VALUE_NONE
                out[0] = b"\0"
                return 1
            if len(item[1]) + 1 > capacity:
                return 0
            value_type[0] = item[0]
            ctypes.memmove(out, item[1] + b"\0", len(item[1]) + 1)
            return 1

        @StorageSetFn
        def storage_set(_user, key, value_type, value):
            sleep_for()
            self.storage[key.decode()] = (value_type, ctypes.string_at(value))
            return 1

        @StorageDeleteFn
        def storage_delete(_user, key):
            sleep_for()
            self.storage.pop(key.decode(), None)
            return 1

        @DebugSetFn
        def debug_set(_user, key, value, remove):
            sleep_for()
            if remove == 2:
                self.debug_lines.append(value.decode(errors="replace"))
            elif remove:
                self.debug.pop(key.decode(), None)
            else:
                self.debug[key.decode()] = value.decode(errors="replace")

        @SubscriptionFn
        def subscription_added(_user, token, kind, exact_name, _error):
            sleep_for()
            self.subscription_add_calls.append(
                (int(_user or 0), token, kind, exact_name.decode())
            )
            return 1

        @SubscriptionFn
        def subscription_removed(_user, token, kind, exact_name, error):
            sleep_for()
            self.subscription_remove_calls.append(
                (int(_user or 0), token, kind, exact_name.decode())
            )
            if self.subscription_remove_ok and token not in self.subscription_remove_fail_tokens:
                return 1
            error.contents.struct_size = ctypes.sizeof(HostError)
            error.contents.code = b"native_unsubscribe_failed"
            error.contents.message = b"test host rejected subscription removal"
            error.contents.capability = b"events.sensor"
            error.contents.permission = b"events.sensor"
            error.contents.object_name = exact_name[:95]
            return 0

        @WorldFindFn
        def world_find(_user, exact_name, expected_class, object_out, error):
            sleep_for(self.world_sleep_s)
            if (
                self.dynamic_capabilities is not None
                and not (self.dynamic_capabilities & CAP_WORLD_FIND)
            ):
                error.contents.struct_size = ctypes.sizeof(HostError)
                error.contents.retryable = 1
                error.contents.code = b"world_index_not_ready"
                error.contents.message = b"world index is still building"
                error.contents.capability = b"world.find"
                error.contents.permission = b"world.query"
                error.contents.object_name = exact_name[:95]
                return 0
            name = exact_name[:95]
            class_name = expected_class[:95] if expected_class else b"SensorGoTo"
            if not class_name:
                class_name = b"SensorGoTo"
            object_out.contents.struct_size = ctypes.sizeof(HostObject)
            object_out.contents.map_generation = 17
            object_out.contents.handle_id = 0xD1A00001
            object_out.contents.owner_id = int(_user or 0)
            object_out.contents.object_id = 7935
            object_out.contents.name = name
            object_out.contents.class_name = class_name
            return 1

        @SensorInsideFn
        def sensor_is_inside(_user, exact_name, inside_out, error):
            sleep_for()
            name = exact_name.decode()
            self.sensor_inside_calls.append(name)
            if self.sensor_query_fail:
                error.contents.struct_size = ctypes.sizeof(HostError)
                error.contents.retryable = 1
                error.contents.code = b"sensor_query_retry"
                error.contents.message = b"test sensor query is not ready"
                error.contents.capability = b"events.sensor"
                error.contents.permission = b"events.sensor"
                error.contents.object_name = exact_name[:95]
                return 0
            inside_out[0] = int(self.sensor_inside.get(name, False))
            return 1

        @CapabilitiesFn
        def capabilities(_user):
            sleep_for()
            return (
                self.dynamic_capabilities
                if self.dynamic_capabilities is not None
                else 0xFFFFFFFF
            )

        @SubtitleShowFn
        def subtitle_show(_user, request, _error):
            value = request.contents
            self.subtitle_show_calls.append((
                bytes(value.text).split(b"\0", 1)[0].decode(),
                value.duration_seconds,
                value.force != 0,
            ))
            return 1

        @SubtitleUpdateFn
        def subtitle_update(_user, request, _error):
            value = request.contents
            self.subtitle_update_calls.append((
                bytes(value.text).split(b"\0", 1)[0].decode(),
                value.fade != 0,
            ))
            return 1

        @RadioDialogFn
        def radio_dialog(_user, request, _error):
            value = request.contents
            self.radio_dialog_calls.append((
                bytes(value.speaker).split(b"\0", 1)[0].decode(),
                bytes(value.group).split(b"\0", 1)[0].decode(),
            ))
            return 1

        @ActionRunChainFn
        def action_run_chain(_user, exact_name, _error):
            self.action_chain_calls.append(exact_name.decode())
            return 1

        @QuestMutateFn
        def quest_mutate(_user, request, result, error):
            value = request.contents
            quest_id = bytes(value.native_quest_id).split(b"\0", 1)[0]
            phase_id = bytes(value.native_phase_id).split(b"\0", 1)[0]
            self.quest_mutate_calls.append((
                value.operation, quest_id.decode(), phase_id.decode()
            ))
            if not quest_mutate_mirrors:
                error.contents.struct_size = ctypes.sizeof(HostError)
                error.contents.code = b"quest_backend_unavailable"
                error.contents.message = b"test backend is unavailable"
                error.contents.capability = b"quest.native"
                error.contents.permission = b"quest.write"
                error.contents.object_name = quest_id[:95]
                return 0
            result.contents.struct_size = ctypes.sizeof(QuestResult)
            result.contents.operation = value.operation
            result.contents.native_quest_id = quest_id
            result.contents.native_phase_id = phase_id
            result.contents.changed = 1
            result.contents.native_state = 2
            return 1

        @QuestStateChangedFn
        def quest_state_changed(_user, report, _error):
            value = report.contents
            self.quest_reports.append({
                "map_generation": value.map_generation,
                "owner_id": value.owner_id,
                "revision": value.revision,
                "active": value.active,
                "state": value.state,
                "native_state": value.native_state,
                "script_id": bytes(value.script_id).split(b"\0", 1)[0].decode(),
                "quest_id": bytes(value.quest_id).split(b"\0", 1)[0].decode(),
                "title": bytes(value.title).split(b"\0", 1)[0].decode(),
                "version": bytes(value.version).split(b"\0", 1)[0].decode(),
                "phase_id": bytes(value.phase_id).split(b"\0", 1)[0].decode(),
                "phase_title": bytes(value.phase_title).split(b"\0", 1)[0].decode(),
                "objective_summary": bytes(value.objective_summary).split(
                    b"\0", 1
                )[0].decode(),
            })
            return 1

        @ConfigGetFn
        def config_get(_user, key, value_type, out, capacity, error):
            sleep_for()
            decoded_key = key.decode()
            item = self.config.get(decoded_key)
            if self.config_get_fail or item is None:
                error.contents.struct_size = ctypes.sizeof(HostError)
                error.contents.code = (
                    b"config_read_failed"
                    if self.config_get_fail
                    else b"config_unknown_key"
                )
                error.contents.message = b"test config value is unavailable"
                error.contents.capability = b"config.read"
                error.contents.object_name = key[:95]
                return 0
            if len(item[1]) + 1 > capacity:
                return 0
            value_type[0] = item[0]
            ctypes.memmove(out, item[1] + b"\0", len(item[1]) + 1)
            return 1

        @QuestHudSnapshotFn
        def quest_hud_snapshot(_user, snapshot, _error):
            self.quest_hud_snapshot_calls += 1
            value = snapshot.contents
            value.struct_size = ctypes.sizeof(QuestHudSnapshot)
            value.map_generation = 17
            value.revision = 12
            value.has_header = 1
            value.objective_count = 2
            value.has_timer = 1

            def fill(target, kind, slot, target_id, native, display, overridden=0):
                target.struct_size = ctypes.sizeof(QuestHudTarget)
                target.kind = kind
                target.slot = slot
                target.overridden = overridden
                target.id = target_id
                target.text = native
                target.display_text = display

            fill(value.header, 1, 0, b"quest-hud-header", b"SAMPLE QUEST",
                 b"SAMPLE QUEST")
            fill(value.objectives[0], 2, 1, b"quest-hud-objective-1",
                 b"Go to the end of the map", b"gO TO tHE EnD", 1)
            fill(value.objectives[1], 2, 2, b"quest-hud-objective-2",
                 b"Find the exit", b"Find the exit")
            fill(value.timer, 3, 0, b"quest-hud-timer", b"01:42", b"00:00", 1)
            return 1

        @QuestHudOverrideSetFn
        def quest_hud_override_set(_user, request, result, error):
            value = request.contents
            target_id = bytes(value.target_id).split(b"\0", 1)[0].decode()
            text = bytes(value.text).split(b"\0", 1)[0].decode()
            if target_id == "expired":
                error.contents.struct_size = ctypes.sizeof(HostError)
                error.contents.code = b"quest_hud_target_expired"
                error.contents.message = b"target expired"
                error.contents.capability = b"ui.quest_hud"
                error.contents.permission = b"ui.overlay"
                error.contents.object_name = b"expired"
                return 0
            override_id = value.override_id
            if override_id == 0:
                override_id = self.quest_hud_next_override_id
                self.quest_hud_next_override_id += 1
            self.quest_hud_override_calls.append(
                (value.owner_id, override_id, target_id, text)
            )
            result.contents.struct_size = ctypes.sizeof(QuestHudOverrideResult)
            result.contents.active = 1
            result.contents.override_id = override_id
            return 1

        @QuestHudOverrideClearFn
        def quest_hud_override_clear(
            _user, owner_id, map_generation, override_id, result, _error
        ):
            self.quest_hud_clear_calls.append(
                (owner_id, map_generation, override_id)
            )
            result.contents.struct_size = ctypes.sizeof(QuestHudOverrideResult)
            result.contents.active = 0
            result.contents.override_id = override_id
            return 1

        @QuestHudClearOwnerFn
        def quest_hud_clear_owner(_user, owner_id):
            self.quest_hud_clear_owner_calls.append(owner_id)

        @ModuleLoadFn
        def module_load(
            _user, module_name, _source_out, _size_out, _token_out, error
        ):
            error.contents.struct_size = ctypes.sizeof(HostError)
            error.contents.code = b"module_not_found"
            error.contents.message = b"test package has no additional modules"
            error.contents.capability = b"lua.runtime"
            error.contents.object_name = module_name[:95]
            return 0

        @ModuleReleaseFn
        def module_release(_user, _token):
            return None

        @CapabilityQueryFn
        def capability_query(
            _user, capability_id, configured, ready, status, capacity, _error
        ):
            legacy_bits = {
                1: CAP_RUNTIME,
                10: CAP_WORLD_FIND,
                30: CAP_EVENTS_SENSOR,
                31: CAP_EVENTS_INTERACT,
                40: CAP_UI_SUBTITLES,
                41: CAP_UI_QUEST_HUD,
                50: CAP_AUDIO_RADIO_DIALOG,
                60: CAP_QUEST_NATIVE_CONTROL,
                70: CAP_ACTION_CHAIN,
                90: CAP_STORAGE,
            }
            available = (
                self.dynamic_capabilities
                if self.dynamic_capabilities is not None
                else self.owner_capabilities.get(int(_user or 0), 0)
            )
            bit = legacy_bits.get(capability_id, 0)
            configured[0] = int(bool(bit and (available & bit)))
            ready[0] = configured[0]
            value = b"ready\0" if ready[0] else b"unavailable\0"
            if capacity < len(value):
                return 0
            ctypes.memmove(status, value, len(value))
            return 1

        @ApiInvokeFn
        def api_invoke(
            _user, _operation_id, _request, _request_size,
            _result, _result_size, error
        ):
            error.contents.struct_size = ctypes.sizeof(HostError)
            error.contents.retryable = 1
            error.contents.code = b"unsupported_capability"
            error.contents.message = b"test shared API backend is unavailable"
            return 0

        self.callbacks = (
            clock,
            log,
            storage_get,
            storage_set,
            storage_delete,
            debug_set,
            subscription_added,
            subscription_removed,
            world_find,
            sensor_is_inside,
            subtitle_show,
            subtitle_update,
            radio_dialog,
            action_run_chain,
            quest_mutate,
            quest_state_changed,
            quest_hud_snapshot,
            quest_hud_override_set,
            quest_hud_override_clear,
            quest_hud_clear_owner,
            capabilities,
            config_get,
            module_load,
            module_release,
            capability_query,
            api_invoke,
        )
        self.host = HostCallbacks(
            ctypes.sizeof(HostCallbacks),
            clock,
            log,
            storage_get,
            storage_set,
            storage_delete,
            debug_set,
            ctypes.cast(subscription_added, ctypes.c_void_p).value,
            ctypes.cast(subscription_removed, ctypes.c_void_p).value,
            ctypes.cast(world_find, ctypes.c_void_p).value,
            None,
            None,
            None,
            None,
            None,
            ctypes.cast(sensor_is_inside, ctypes.c_void_p).value,
            ctypes.cast(subtitle_show, ctypes.c_void_p).value,
            ctypes.cast(subtitle_update, ctypes.c_void_p).value,
            ctypes.cast(radio_dialog, ctypes.c_void_p).value,
            ctypes.cast(action_run_chain, ctypes.c_void_p).value,
            (
                ctypes.cast(quest_mutate, ctypes.c_void_p).value
                if quest_mutate_mirrors
                else None
            ),
            ctypes.cast(quest_state_changed, ctypes.c_void_p).value,
            ctypes.cast(quest_hud_snapshot, ctypes.c_void_p).value,
            ctypes.cast(quest_hud_override_set, ctypes.c_void_p).value,
            ctypes.cast(quest_hud_override_clear, ctypes.c_void_p).value,
            ctypes.cast(quest_hud_clear_owner, ctypes.c_void_p).value,
            ctypes.cast(capabilities, ctypes.c_void_p).value,
            ctypes.cast(config_get, ctypes.c_void_p).value,
            ctypes.cast(module_load, ctypes.c_void_p).value,
            ctypes.cast(module_release, ctypes.c_void_p).value,
            ctypes.cast(capability_query, ctypes.c_void_p).value,
            ctypes.cast(api_invoke, ctypes.c_void_p).value,
        )
        self.states: list[int] = []

    def create(
        self,
        source: str,
        *,
        script_id: str = "test.runtime",
        name: str = "Runtime Test",
        version: str = "1.0.0",
        memory: int = 1024 * 1024,
        instructions: int = 50_000,
        wall_ms: int = 20,
        max_rules: int = 16,
        max_log_messages_per_second: int = 20,
        owner_id: int = 1,
        scope: int = SCOPE_MAP,
        approved_permissions: int = PERMISSION_STORAGE | PERMISSION_DEBUG,
        capabilities: int = CAP_RUNTIME | CAP_STORAGE,
        permissions: str = "storage.mod, ui.debug",
        require_state: bool = True,
    ) -> tuple[int, int]:
        self.owner_capabilities[owner_id] = capabilities
        encoded = {
            "id": script_id.encode(),
            "name": name.encode(),
            "version": version.encode(),
            "permissions": permissions.encode(),
        }
        config = ScriptConfig(
            ctypes.sizeof(ScriptConfig),
            scope,
            17,
            approved_permissions,
            capabilities,
            owner_id,
            0x12345678,
            memory,
            instructions,
            wall_ms,
            64,
            32,
            max_rules,
            max_log_messages_per_second,
            encoded["id"],
            encoded["name"],
            encoded["version"],
            encoded["permissions"],
            b"test.lua",
            b"SampleProject",
            b"nowhere",
            b"",
            b"test-loader",
            0,
            1,
            ctypes.c_void_p(owner_id),
            self.host,
        )
        state = self.api.create_script(ctypes.byref(config))
        if not state:
            assert not require_state
            return 0, 0
        self.states.append(state)
        source_bytes = source.encode()
        loaded = self.api.load_script(state, source_bytes, len(source_bytes))
        return state, loaded

    def dispatch(
        self,
        state: int,
        kind: int,
        *,
        delta: float = 0.0,
        name: str | None = None,
        generation: int = 17,
        new_pump: bool = True,
    ) -> int:
        if new_pump:
            assert self.api.begin_pump(state) == 1
        encoded_name = (
            name.encode()
            if name is not None
            else (b"on_load" if kind == EVENT_LOAD else b"test_event")
        )
        event = Event(
            ctypes.sizeof(Event),
            kind,
            generation,
            0,
            delta,
            encoded_name,
            b"SampleProject",
            b"nowhere",
            EnemyKilledEvent(),
            ConfigChangedEvent(),
        )
        return self.api.dispatch(state, ctypes.byref(event))

    def dispatch_config(
        self,
        state: int,
        key: str,
        old_type: int,
        old_value: str,
        value_type: int,
        value: str,
        *,
        new_pump: bool = True,
    ) -> int:
        if new_pump:
            assert self.api.begin_pump(state) == 1
        change = ConfigChangedEvent()
        change.struct_size = ctypes.sizeof(ConfigChangedEvent)
        change.old_value_type = old_type
        change.value_type = value_type
        change.key = key.encode()
        change.old_value = old_value.encode()
        change.value = value.encode()
        event = Event(
            ctypes.sizeof(Event),
            EVENT_CONFIG_CHANGED,
            17,
            0,
            0.0,
            b"on_config_changed",
            b"SampleProject",
            b"nowhere",
            EnemyKilledEvent(),
            change,
        )
        return self.api.dispatch(state, ctypes.byref(event))

    def dispatch_quest_phase(
        self,
        state: int,
        kind: int,
        registration_key: str,
        *,
        arguments_json: bytes,
        sequence: int = 1,
        wait_seconds: float = 0.0,
        runtime_kind: int = 1,
        completion: int = 1,
        new_pump: bool = True,
    ) -> int:
        if new_pump:
            assert self.api.begin_pump(state) == 1

        def view(value: bytes):
            buffer = ctypes.create_string_buffer(value, len(value) + 1)
            buffers.append(buffer)
            return ByteView(ctypes.cast(buffer, ctypes.c_char_p), len(value))

        buffers: list[ctypes.Array] = []
        phase = QuestPhaseEvent()
        phase.struct_size = ctypes.sizeof(QuestPhaseEvent)
        phase.map_generation = 17
        phase.runtime_kind = runtime_kind
        phase.completion = completion
        phase.sequence = sequence
        phase.wait_seconds = wait_seconds
        phase.local_peer = 1
        phase.quest_id = view(b"DLUMA_TestQuest")
        phase.phase_id = view(b"DLUMA_TestPhase")
        phase.contract_id = view(b"example.quest:start_intro")
        phase.provider = view(b"example.quest")
        phase.handler = view(b"start_intro")
        phase.binding_hash = view(b"a" * 64)
        phase.arguments_json = view(arguments_json)
        event = Event(
            ctypes.sizeof(Event),
            kind,
            17,
            0,
            0.0,
            registration_key.encode(),
            b"SampleProject",
            b"nowhere",
            EnemyKilledEvent(),
            ConfigChangedEvent(),
            phase,
        )
        return self.api.dispatch(state, ctypes.byref(event))

    def begin_pump(self, state: int) -> None:
        assert self.api.begin_pump(state) == 1

    def advance(self, milliseconds: int):
        self.clock_value += milliseconds

    def destroy(self, state: int) -> int:
        result = self.api.destroy_script(state)
        if result == 1 and state in self.states:
            self.states.remove(state)
        return result

    def status(self, state: int) -> Status:
        status = Status()
        status.struct_size = ctypes.sizeof(status)
        assert self.api.get_status(state, ctypes.byref(status)) == 1
        return status

    def close(self):
        for state in list(self.states):
            assert self.destroy(state) == 1


def declaration(body: str = "") -> str:
    return f"""
local mod = dluma.mod()
{body}
return mod
"""


def test_subtitle_and_radio_dialog_requests_cross_the_private_abi_verbatim():
    harness = Harness()
    source = declaration("""
assert(dluma.ui.send_subtitle("&DLUMA_INTRO_LINE&", 4.25, true) == true)
assert(dluma.ui.update_subtitle("A replacement line", true) == true)
assert(dluma.audio.play_radio_dialog("voice2d", "dluma_intro_radio") == true)
""")
    state, loaded = harness.create(
        source,
        approved_permissions=(
            PERMISSION_UI_OVERLAY | PERMISSION_AUDIO_PLAY
        ),
        capabilities=(
            CAP_RUNTIME | CAP_UI_SUBTITLES | CAP_AUDIO_RADIO_DIALOG
        ),
        permissions="ui.overlay, audio.play",
    )
    assert loaded == 1
    assert harness.subtitle_show_calls == [("&DLUMA_INTRO_LINE&", 4.25, True)]
    assert harness.subtitle_update_calls == [("A replacement line", True)]
    assert harness.radio_dialog_calls == [("voice2d", "dluma_intro_radio")]
    assert harness.destroy(state) == 1


def test_action_chain_exact_name_crosses_the_private_abi():
    harness = Harness()
    source = declaration("""
assert(dluma.actions.run_chain("DLUMA_TestActionChain") == true)
""")
    state, loaded = harness.create(
        source,
        approved_permissions=PERMISSION_WORLD_WRITE,
        capabilities=CAP_RUNTIME | CAP_ACTION_CHAIN,
        permissions="world.write",
    )
    assert loaded == 1
    assert harness.action_chain_calls == ["DLUMA_TestActionChain"]
    assert harness.destroy(state) == 1


def test_action_chain_binding_fails_closed_without_world_write():
    harness = Harness()
    source = declaration("""
local ok, err = dluma.actions.run_chain("DLUMA_BlockedChain")
assert(ok == nil and err.code == "permission_denied")
""")
    state, loaded = harness.create(
        source,
        approved_permissions=0,
        capabilities=CAP_RUNTIME | CAP_ACTION_CHAIN,
    )
    assert loaded == 1
    assert harness.action_chain_calls == []
    assert harness.destroy(state) == 1


def test_action_chain_event_handlers_route_each_event_to_its_chain():
    harness = Harness()
    source = declaration(
        """
mod:on_sensor_enter("DLUMA_LuaQuestSensor", function()
  dluma.actions.run_chain("DLUMA_LuaSensorEnterChain")
end)
mod:on_sensor_leave("DLUMA_LuaQuestSensor", function()
  dluma.actions.run_chain("DLUMA_LuaSensorLeaveChain")
end)
mod:on_interact_begin("DLUMA_LuaTestInteract", function()
  dluma.actions.run_chain("DLUMA_LuaInteractionChain")
end)
"""
    )
    state, loaded = harness.create(
        source,
        script_id="test.action_chain_events",
        name="Action Chain Events",
        version="1.0.0",
        approved_permissions=(
            PERMISSION_EVENTS_INTERACT
            | PERMISSION_EVENTS_SENSOR
            | PERMISSION_DEBUG
            | PERMISSION_WORLD_WRITE
        ),
        capabilities=(
            CAP_RUNTIME
            | CAP_EVENTS_INTERACT
            | CAP_EVENTS_SENSOR
            | CAP_ACTION_CHAIN
        ),
        permissions="events.interact, events.sensor, ui.debug, world.write",
    )
    assert loaded == 1
    assert [call[2:] for call in harness.subscription_add_calls] == [
        (EVENT_SENSOR_ENTER, "DLUMA_LuaQuestSensor"),
        (EVENT_SENSOR_LEAVE, "DLUMA_LuaQuestSensor"),
        (EVENT_INTERACT_BEGIN, "DLUMA_LuaTestInteract"),
    ]

    assert harness.dispatch(
        state, EVENT_SENSOR_ENTER, name="DLUMA_LuaQuestSensor"
    ) == 1
    assert harness.dispatch(
        state, EVENT_SENSOR_LEAVE, name="DLUMA_LuaQuestSensor"
    ) == 1
    assert harness.dispatch(
        state, EVENT_INTERACT_BEGIN, name="DLUMA_LuaTestInteract"
    ) == 1
    assert harness.action_chain_calls == [
        "DLUMA_LuaSensorEnterChain",
        "DLUMA_LuaSensorLeaveChain",
        "DLUMA_LuaInteractionChain",
    ]
    assert harness.destroy(state) == 1


def test_sensor_enter_initialization_queries_occupied_state_once_and_deduplicates_edges():
    harness = Harness()
    harness.sensor_inside["DLUMA_AlreadyInside"] = True
    harness.sensor_inside["DLUMA_RealEdge"] = True
    source = declaration(
        """
local occupied_hits, edge_hits = 0, 0
mod:on_sensor_enter("DLUMA_AlreadyInside", function(_, sensor)
  assert(sensor.name == "DLUMA_AlreadyInside")
  occupied_hits = occupied_hits + 1
end)
mod:on_sensor_enter("DLUMA_RealEdge", function(_, sensor)
  assert(sensor.name == "DLUMA_RealEdge")
  edge_hits = edge_hits + 1
end)
mod:on_update(function()
  assert(occupied_hits == 1 and edge_hits == 1)
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_EVENTS_SENSOR,
            capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR,
            permissions="events.sensor",
        )
        assert loaded == 1
        # A real queued edge wins the race and clears its initial-occupancy
        # pending marker; only the other occupied sensor is queried.
        assert harness.dispatch(
            state, EVENT_SENSOR_ENTER, name="DLUMA_RealEdge"
        ) == 1
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 1
        assert harness.sensor_inside_calls == ["DLUMA_AlreadyInside"]
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 1
        assert harness.sensor_inside_calls == ["DLUMA_AlreadyInside"]
    finally:
        harness.close()


def test_sensor_enter_initialization_retries_a_transient_query_without_an_update_handler():
    harness = Harness()
    harness.sensor_inside["DLUMA_RetryInside"] = True
    harness.sensor_query_fail = True
    source = declaration(
        """
mod:on_sensor_enter("DLUMA_RetryInside", function()
  dluma.log("occupied-retry-delivered")
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_EVENTS_SENSOR,
            capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR,
            permissions="events.sensor",
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.sensor_inside_calls == ["DLUMA_RetryInside"]
        assert not any(text == "occupied-retry-delivered" for _, text in harness.logs)

        harness.sensor_query_fail = False
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.sensor_inside_calls == [
            "DLUMA_RetryInside", "DLUMA_RetryInside"
        ]
        assert sum(
            text == "occupied-retry-delivered" for _, text in harness.logs
        ) == 1
    finally:
        harness.close()


def test_subtitle_and_radio_dialog_bindings_fail_closed_without_permissions():
    harness = Harness()
    source = declaration("""
local subtitle, subtitle_err = dluma.ui.send_subtitle("blocked", 1, false)
assert(subtitle == nil and subtitle_err.code == "permission_denied")
assert(type(subtitle_err.message) == "string")
local radio, radio_err = dluma.audio.play_radio_dialog("voice2d", "blocked")
assert(radio == nil and radio_err.code == "permission_denied")
assert(type(radio_err.message) == "string")
""")
    state, loaded = harness.create(
        source,
        approved_permissions=0,
        capabilities=(
            CAP_RUNTIME | CAP_UI_SUBTITLES | CAP_AUDIO_RADIO_DIALOG
        ),
    )
    assert loaded == 1, bytes(harness.status(state).last_error)
    assert harness.subtitle_show_calls == []
    assert harness.radio_dialog_calls == []
    assert harness.destroy(state) == 1


def test_quest_hud_snapshot_preserves_native_display_and_multiple_rows():
    harness = Harness()
    source = declaration("""
local hud = assert(dluma.ui.quest_hud())
assert(hud.revision == 12)
assert(hud.header.text == "SAMPLE QUEST")
assert(hud.header.display_text == "SAMPLE QUEST")
assert(#hud.objectives == 2)
assert(hud.objectives[1].slot == 1)
assert(hud.objectives[1].text == "Go to the end of the map")
assert(hud.objectives[1].display_text == "gO TO tHE EnD")
assert(hud.objectives[1].overridden == true)
assert(hud.objectives[2].slot == 2)
assert(hud.timer.text == "01:42")
assert(hud.timer.display_text == "00:00")
""")
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_GAME_READ,
            capabilities=CAP_RUNTIME | CAP_UI_QUEST_HUD,
            permissions="game.read",
        )
        assert loaded == 1
        assert harness.quest_hud_snapshot_calls == 1
    finally:
        harness.close()


def test_quest_hud_override_update_clear_and_owner_cleanup_cross_the_abi():
    harness = Harness()
    source = declaration("""
local handle = assert(dluma.ui.override_quest_hud(
  "quest-hud-objective-1", "gO TO tHE EnD"))
assert(handle:update("Go To ThE eNd") == true)
assert(handle:clear() == true)
assert(handle:clear() == false)
local expired, expired_err =
  dluma.ui.override_quest_hud("expired", "ignored")
assert(expired == nil and expired_err.code == "quest_hud_target_expired")
""")
    state, loaded = harness.create(
        source,
        owner_id=77,
        approved_permissions=PERMISSION_UI_OVERLAY,
        capabilities=CAP_RUNTIME | CAP_UI_QUEST_HUD,
        permissions="ui.overlay",
    )
    assert loaded == 1, bytes(harness.status(state).last_error)
    assert harness.quest_hud_override_calls == [
        (77, 100, "quest-hud-objective-1", "gO TO tHE EnD"),
        (77, 100, "quest-hud-objective-1", "Go To ThE eNd"),
    ]
    assert harness.quest_hud_clear_calls == [(77, 17, 100)]
    assert harness.destroy(state) == 1
    assert harness.quest_hud_clear_owner_calls == [77]


def test_quest_hud_override_rejects_global_scope_nul_invalid_utf8_and_limits():
    harness = Harness()
    source = declaration("""
local function rejected(value)
  local ok = pcall(function()
    dluma.ui.override_quest_hud("quest-hud-objective-1", value)
  end)
  assert(ok == false)
end
rejected("")
rejected(string.rep("a", 513))
rejected("a" .. string.char(0) .. "b")
rejected(string.char(0xC0, 0xAF))
assert(dluma.ui.override_quest_hud(
  "quest-hud-objective-1", string.rep("a", 512)))
""")
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_UI_OVERLAY,
            capabilities=CAP_RUNTIME | CAP_UI_QUEST_HUD,
            permissions="ui.overlay",
        )
        assert loaded == 1
        assert len(harness.quest_hud_override_calls) == 1
        assert len(harness.quest_hud_override_calls[0][3].encode()) == 512
    finally:
        harness.close()

    global_source = declaration("""
local hud = assert(dluma.ui.quest_hud())
assert(#hud.objectives == 2)
local handle, err = dluma.ui.override_quest_hud(
  hud.objectives[1].id, "blocked")
assert(handle == nil and err.code == "quest_hud_scope_not_supported")
""")
    try:
        state, loaded = harness.create(
            global_source,
            scope=SCOPE_GLOBAL,
            approved_permissions=(
                PERMISSION_GAME_READ | PERMISSION_UI_OVERLAY
            ),
            capabilities=CAP_RUNTIME | CAP_UI_QUEST_HUD,
            permissions="game.read, ui.overlay",
        )
        assert loaded == 1
    finally:
        harness.close()


def test_declared_local_permission_reports_when_player_policy_disables_it():
    harness = Harness()
    source = declaration("""
local subtitle, subtitle_err = dluma.ui.send_subtitle("blocked", 1, false)
assert(subtitle == nil and subtitle_err.code == "permission_denied")
assert(subtitle_err.message == "the required permission is disabled for project and map scripts")
""")
    state, loaded = harness.create(
        source,
        approved_permissions=0,
        capabilities=CAP_RUNTIME | CAP_UI_SUBTITLES,
        permissions="ui.overlay",
    )
    assert loaded == 1
    assert harness.subtitle_show_calls == []
    assert harness.destroy(state) == 1


def test_private_helper_reports_exact_lua_548_and_bounded_abi():
    harness = Harness()
    try:
        assert harness.api.release == b"Lua 5.4.8"
        assert harness.api.abi_version == ABI
        assert harness.api.struct_size == ctypes.sizeof(Api)
        assert harness.api.begin_pump
        old_api = Api()
        assert harness.get_api(0x00020000, ctypes.byref(old_api), ctypes.sizeof(old_api)) == 0
        short_api = Api()
        assert harness.get_api(
            ABI,
            ctypes.byref(short_api),
            ctypes.sizeof(short_api) - 1,
        ) == 0
    finally:
        harness.close()


@pytest.mark.parametrize(
    "missing_callback",
    ["capabilities", "config_get"],
)
def test_private_helper_rejects_null_mandatory_host_callbacks(
    missing_callback: str,
):
    harness = Harness()
    try:
        setattr(harness.host, missing_callback, None)
        state, loaded = harness.create(
            declaration(), require_state=False
        )
        assert state == 0
        assert loaded == 0
    finally:
        harness.close()


def test_private_helper_rejects_truncated_host_callback_table():
    harness = Harness()
    try:
        harness.host.struct_size = (
            ctypes.sizeof(HostCallbacks)
            - ctypes.sizeof(ctypes.c_void_p)
        )
        state, loaded = harness.create(
            declaration(), require_state=False
        )
        assert state == 0
        assert loaded == 0
    finally:
        harness.close()


def test_safe_libraries_only_and_immutable_context():
    harness = Harness()
    source = declaration(
        """
mod:on_load(function(ctx)
  assert(package == nil and io == nil and os == nil and debug == nil)
  assert(load == nil and loadfile == nil and dofile == nil)
  assert(type(require) == "function")
  assert(type(table) == "table" and type(string) == "table")
  assert(type(math) == "table" and type(utf8) == "table")
  assert(type(coroutine) == "table" and type(dluma.std.once) == "function")
  local ok = pcall(function() ctx.map = "forged" end)
  assert(ok == false)
  dluma.log("sandbox-ok")
end)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert any(text == "sandbox-ok" for _, text in harness.logs)
    finally:
        harness.close()


def test_embedded_conditions_preserve_errors_and_watch_uses_mod_off_tokens():
    harness = Harness(manual_clock=True)
    source = declaration(
        """
local failure = { code = "not_ready", message = "test predicate" }
local combined = dluma.cond.all(
  function() return true end,
  function() return nil, failure end
)
local value, problem = combined()
assert(value == nil and problem == failure)

local entered = 0
local token, watch_err = mod:watch(dluma.cond.true_(), {
  interval = 0.05,
  immediate = true,
  on_enter = function(ctx)
    entered = entered + 1
    assert(mod:off(ctx.watch_token) == true)
    assert(dluma.storage.set("watch_entered", entered))
  end,
})
assert(type(token) == "number" and watch_err == nil)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1, bytes(harness.status(state).last_error)
        harness.advance(50)
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.storage["watch_entered"] == (2, b"1")
        harness.advance(100)
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.storage["watch_entered"] == (2, b"1")
    finally:
        harness.close()


def test_elapsed_conditions_are_one_shot_restartable_and_deterministically_bounded():
    source = declaration(
        """
local one_shot = dluma.cond.elapsed(0.05)
local repeating = dluma.cond.elapsed(0.05, { restart = true, random_add = 0 })
assert(not pcall(function() dluma.cond.elapsed(1, { random_add = 3601 }) end))
assert(dluma._source_fingerprint == nil)
local one_hits, repeat_hits = 0, 0
mod:on_update(function()
  if one_shot() then one_hits = one_hits + 1 end
  if repeating() then repeat_hits = repeat_hits + 1 end
  assert(dluma.storage.set("elapsed_one_hits", one_hits))
  assert(dluma.storage.set("elapsed_repeat_hits", repeat_hits))
end)
"""
    )
    first = Harness(manual_clock=True)
    second = Harness(manual_clock=True)
    randomized = declaration(
        """
local value = dluma.cond.elapsed(0, { random_add = 0.10 })
mod:on_update(function()
  assert(dluma.storage.set("elapsed_randomized", value() and "true" or "false"))
end)
"""
    )
    try:
        state, loaded = first.create(source)
        assert loaded == 1
        assert first.dispatch(state, EVENT_UPDATE) == 1
        first.advance(51)
        assert first.dispatch(state, EVENT_UPDATE) == 1
        assert first.dispatch(state, EVENT_UPDATE) == 1
        first.advance(51)
        assert first.dispatch(state, EVENT_UPDATE) == 1
        assert first.storage["elapsed_one_hits"] == (2, b"3")
        assert first.storage["elapsed_repeat_hits"] == (2, b"2")

        left, left_loaded = first.create(randomized)
        right, right_loaded = second.create(randomized)
        assert left_loaded == right_loaded == 1
        assert first.dispatch(left, EVENT_UPDATE) == 1
        assert second.dispatch(right, EVENT_UPDATE) == 1
        first.advance(50)
        second.advance(50)
        assert first.dispatch(left, EVENT_UPDATE) == 1
        assert second.dispatch(right, EVENT_UPDATE) == 1
        assert first.storage["elapsed_randomized"] == second.storage["elapsed_randomized"]
    finally:
        first.close()
        second.close()


def test_watch_edges_validate_callbacks_and_cancel_safely_from_leave():
    harness = Harness(manual_clock=True)
    source = declaration(
        """
local polls, enters, leaves, changes = 0, 0, 0, 0
local token, err = mod:watch(function()
  polls = polls + 1
  return polls == 3 or polls == 4
end, {
  interval = 0.05,
  immediate = false,
  on_enter = function(ctx)
    enters = enters + 1
    assert(dluma.storage.set("watch_enters", enters))
  end,
  on_change = function(ctx, current, previous)
    changes = changes + 1
    assert(current ~= previous)
    assert(dluma.storage.set("watch_changes", changes))
  end,
  on_leave = function(ctx)
    leaves = leaves + 1
    assert(mod:off(ctx.watch_token) == true)
    assert(dluma.storage.set("watch_leaves", leaves))
  end,
})
assert(type(token) == "number" and err == nil)
local bad, bad_err = mod:watch(dluma.cond.true_(), { on_enter = true })
assert(bad == nil and bad_err.code == "watch_callback_invalid")
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1, bytes(harness.status(state).last_error)
        for _ in range(5):
            harness.advance(50)
            assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.storage["watch_enters"] == (2, b"1")
        assert harness.storage["watch_changes"] == (2, b"2")
        assert harness.storage["watch_leaves"] == (2, b"1")
        assert any(text == "watch_baseline" for _, text in harness.logs)
        assert any(text == "watch_enter" for _, text in harness.logs)
        assert any(text == "watch_leave" for _, text in harness.logs)
        assert any(text == "watch_removed" for _, text in harness.logs)
    finally:
        harness.close()


def test_flow_nonblocking_steps_wait_for_pending_work_before_completion():
    harness = Harness(manual_clock=True)
    source = declaration(
        """
local delayed = dluma.flow.storage_set("flow_delayed", "done")
delayed.delay = 0.10
delayed.waits = false
local completed = 0
local sequence = dluma.flow.sequence {
  steps = {
    dluma.flow.storage_set("flow_now", "first"),
    delayed,
    dluma.flow.storage_set("flow_now", "second"),
  },
  on_complete = function()
    completed = completed + 1
    assert(dluma.storage.set("flow_completed", completed))
  end,
}
local token, err = sequence:start()
assert(type(token) == "number" and err == nil)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1, bytes(harness.status(state).last_error)
        harness.advance(50)
        assert harness.dispatch(state, EVENT_UPDATE) == 0
        assert harness.storage["flow_now"] == (1, b"first")
        harness.advance(50)
        assert harness.dispatch(state, EVENT_UPDATE) == 0
        assert "flow_delayed" not in harness.storage
        harness.advance(50)
        assert harness.dispatch(state, EVENT_UPDATE) == 0
        assert harness.storage["flow_now"] == (1, b"second")
        assert "flow_completed" not in harness.storage
        harness.advance(50)
        assert harness.dispatch(state, EVENT_UPDATE) == 0
        assert harness.storage["flow_delayed"] == (1, b"done")
        assert harness.storage["flow_completed"] == (2, b"1")
        assert any(text == "flow_started" for _, text in harness.logs)
        assert any(text == "flow_step_started" for _, text in harness.logs)
        assert any(text == "flow_step_completed" for _, text in harness.logs)
        assert any(text == "flow_completed" for _, text in harness.logs)
    finally:
        harness.close()


def test_embedded_flow_runs_typed_blocking_steps_on_the_bounded_timer_pump():
    harness = Harness(manual_clock=True)
    source = declaration(
        """
local sequence = dluma.flow.sequence {
  steps = {
    dluma.flow.wait(0.05),
    dluma.flow.storage_set("flow_result", "complete"),
  },
}
local token, flow_err = sequence:start()
assert(type(token) == "number" and flow_err == nil)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1, bytes(harness.status(state).last_error)
        for _ in range(3):
            harness.advance(50)
            assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.storage["flow_result"] == (1, b"complete")
        harness.advance(50)
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        assert harness.storage["flow_result"] == (1, b"complete")
        assert harness.destroy(state) == 1
    finally:
        harness.close()


def test_one_state_per_script_and_error_isolation():
    harness = Harness()
    good = declaration('mod:on_load(function() dluma.log("good-state") end)')
    bad = declaration('mod:on_load(function() error("owned failure") end)')
    try:
        good_state, good_loaded = harness.create(good)
        bad_state, bad_loaded = harness.create(bad)
        assert good_loaded == bad_loaded == 1
        assert harness.dispatch(bad_state, EVENT_LOAD) == 0
        assert b"owned failure" in bytes(harness.status(bad_state).last_error)
        assert harness.dispatch(good_state, EVENT_LOAD) == 1
        assert any(text == "good-state" for _, text in harness.logs)
    finally:
        harness.close()


def test_traceback_attributes_callback_and_source_line():
    harness = Harness()
    source = declaration(
        'mod:on_load(function()\n  error("line-attribution")\nend)'
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 0
        error = bytes(harness.status(state).last_error).split(b"\0", 1)[0]
        assert b"on_load" in error
        assert b"line-attribution" in error
        assert b"test.lua:" in error
        assert b"stack traceback" in error
    finally:
        harness.close()


def test_runtime_declaration_must_match_header_identity():
    harness = Harness()
    source = """
dluma.mod { id = "other.id", name = "Runtime Test", version = "1.0.0" }
"""
    try:
        state, loaded = harness.create(source)
        assert loaded == 0
        status = harness.status(state)
        assert status.disabled == 1
        assert b"metadata belongs in mod.dluma.json" in bytes(status.last_error)
    finally:
        harness.close()


def test_instruction_budget_interrupts_only_the_callback():
    harness = Harness()
    source = declaration("mod:on_load(function() while true do end end)")
    try:
        state, loaded = harness.create(source, instructions=10_000, wall_ms=100)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 0
        status = harness.status(state)
        assert bytes(status.budget_type).split(b"\0", 1)[0] == b"instructions"
        assert status.budget_violations >= 1
    finally:
        harness.close()


def test_many_legal_callbacks_share_one_aggregate_pump_budget():
    harness = Harness()
    source = declaration(
        'mod:on_load(function() local value = 1 + 1; assert(value == 2) end)'
    )
    try:
        state, loaded = harness.create(
            source, instructions=10_000, wall_ms=100
        )
        assert loaded == 1
        harness.begin_pump(state)
        results = [
            harness.dispatch(
                state, EVENT_LOAD, new_pump=False
            )
            for _ in range(48)
        ]
        assert 1 in results
        assert 0 in results
        assert all(
            result == 0
            for result in results[
                results.index(0):
            ]
        )
        status = harness.status(state)
        assert (
            bytes(status.budget_type).split(b"\0", 1)[0]
            == b"aggregate_instructions"
        )
        assert status.budget_violations == 1
        assert status.disabled == 0

        harness.begin_pump(state)
        assert harness.dispatch(
            state, EVENT_LOAD, new_pump=False
        ) == 1
        reset_status = harness.status(state)
        assert (
            bytes(reset_status.budget_type).split(
                b"\0", 1
            )[0]
            == b""
        )
    finally:
        harness.close()


def test_wall_clock_budget_is_qpc_lua_only_time():
    harness = Harness()
    source = declaration(
        "mod:on_load(function() local n=0; for i=1,1000000 do n=n+i end end)"
    )
    try:
        state, loaded = harness.create(source, instructions=10_000_000, wall_ms=1)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 0
        status = harness.status(state)
        assert bytes(status.budget_type).split(b"\0", 1)[0] == b"wall_clock_lua_only"
        assert status.last_callback_lua_us >= 1000
        assert status.last_instruction_count_or_remaining <= 10_000_000
    finally:
        harness.close()


def test_nested_sleeping_clock_and_log_are_excluded_once_from_lua_budget():
    harness = Harness(host_sleep_s=0.03, log_sleep_s=0.03)
    source = declaration('mod:on_load(function() assert(dluma.log("slow-host")) end)')
    try:
        state, loaded = harness.create(source, wall_ms=10)
        assert loaded == 1
        started = time.perf_counter()
        assert harness.dispatch(state, EVENT_LOAD) == 1, bytes(
            harness.status(state).last_error
        )
        elapsed_s = time.perf_counter() - started
        assert elapsed_s >= 0.055
        status = harness.status(state)
        assert status.disabled == 0
        assert status.budget_violations == 0
        assert bytes(status.budget_type).split(b"\0", 1)[0] == b""
        assert 50_000 <= status.last_callback_host_excluded_us
        assert status.last_callback_host_excluded_us < elapsed_s * 1_000_000 * 1.35
        assert status.last_callback_lua_us < 10_000
    finally:
        harness.close()


def test_sleeping_world_lookup_is_excluded_from_lua_wall_budget():
    harness = Harness(world_sleep_s=0.05)
    source = declaration(
        """
mod:on_load(function()
  local object, err = dluma.world.find("DLUMA_TestSensor", "SensorArea")
  assert(err == nil and object.name == "DLUMA_TestSensor" and object.id == 7935)
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            wall_ms=10,
            approved_permissions=(
                PERMISSION_STORAGE | PERMISSION_DEBUG | PERMISSION_WORLD_QUERY
            ),
            capabilities=CAP_RUNTIME | CAP_STORAGE | CAP_WORLD_FIND,
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        status = harness.status(state)
        assert status.budget_violations == 0
        assert status.last_callback_host_excluded_us >= 40_000
        assert status.last_callback_lua_us < 10_000
    finally:
        harness.close()


def test_fifty_timer_callbacks_with_host_work_do_not_false_fail_budget():
    harness = Harness(manual_clock=True, host_sleep_s=0.002)
    source = declaration(
        """
local count = 0
local timer, timer_err = dluma.timer.every(0.01, function()
  count = count + 1
  local previous = dluma.storage.get("timer_count", 0)
  assert(dluma.storage.set("timer_count", previous + 1))
  dluma.log("timer-" .. count)
end)
assert(type(timer) == "number" and timer_err == nil)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            instructions=50_000,
            wall_ms=5,
            max_log_messages_per_second=100,
        )
        assert loaded == 1
        for _ in range(50):
            harness.advance(1001)
            assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
        status = harness.status(state)
        assert status.disabled == 0
        assert status.budget_violations == 0
        assert bytes(status.budget_type).split(b"\0", 1)[0] == b""
        assert status.last_callback_host_excluded_us >= 5_000
        assert status.last_instruction_count_or_remaining <= 50_000
        assert harness.storage["timer_count"] == (2, b"50")
        assert harness.debug_lines == []
        assert sum(text.startswith("timer-") for _, text in harness.logs) == 50
    finally:
        harness.close()


def test_memory_cap_reports_owned_state_failure():
    harness = Harness()
    source = declaration(
        'mod:on_load(function() local t={}; for i=1,10000 do t[i]=string.rep("x", 1024) end end)'
    )
    try:
        state, loaded = harness.create(source, memory=256 * 1024, instructions=2_000_000)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 0
        status = harness.status(state)
        assert bytes(status.budget_type).split(b"\0", 1)[0] == b"memory"
        assert status.memory_used_bytes <= 256 * 1024
        error = bytes(status.last_error).split(b"\0", 1)[0]
        assert b"not enough memory" in error
        assert b"stack traceback" not in error
    finally:
        harness.close()


def test_config_get_returns_typed_values_and_rejects_bad_keys_and_values():
    harness = Harness()
    harness.config.update({
        "effects": (VALUE_BOOLEAN, b"true"),
        "spawn_count": (VALUE_INTEGER, b"-2147483648"),
        "player_tag": (VALUE_STRING, "Crâne".encode("utf-8")),
        "overflow": (VALUE_INTEGER, b"2147483648"),
    })
    source = declaration(
        r'''
assert(dluma.configuration.get("effects") == true)
assert(dluma.configuration.get("spawn_count") == -2147483648)
assert(dluma.configuration.get("player_tag") == "Crâne")

local missing, missing_err = dluma.configuration.get("missing")
assert(missing == nil and missing_err.code == "config_unknown_key")
assert(missing_err.capability == "config.read")

local invalid, invalid_err = dluma.configuration.get("bad\0key")
assert(invalid == nil and invalid_err.code == "config_key_invalid")

local overflow, overflow_err = dluma.configuration.get("overflow")
assert(overflow == nil and overflow_err.code == "config_value_invalid")
'''
    )
    try:
        state, loaded = harness.create(
            source, scope=SCOPE_GLOBAL
        )
        assert loaded == 1
        assert harness.destroy(state) == 1
    finally:
        harness.close()


def test_config_change_is_typed_immutable_and_mutation_safe():
    harness = Harness()
    source = declaration(
        r'''
local first
first = mod:on_config_changed(function(ctx, change)
  assert(ctx.scope == "global")
  assert(change.key == "effects")
  assert(change.old_value == false)
  assert(change.value == true)
  local mutable = pcall(function()
    change.value = false
  end)
  assert(mutable == false)
  assert(mod:off(first) == true)
  local replacement, replacement_err =
    mod:on_config_changed(function(_, next_change)
      assert(next_change.key == "spawn_count")
      assert(next_change.old_value == 3)
      assert(next_change.value == 5)
      dluma.log("config-replacement")
    end)
  assert(type(replacement) == "number")
  assert(replacement_err == nil)
  dluma.log("config-first")
end)
'''
    )
    try:
        state, loaded = harness.create(
            source, scope=SCOPE_GLOBAL
        )
        assert loaded == 1
        assert harness.dispatch_config(
            state,
            "effects",
            VALUE_BOOLEAN,
            "false",
            VALUE_BOOLEAN,
            "true",
        ) == 1
        assert [
            text for _, text in harness.logs
            if text.startswith("config-")
        ] == ["config-first"]

        assert harness.dispatch_config(
            state,
            "spawn_count",
            VALUE_INTEGER,
            "3",
            VALUE_INTEGER,
            "5",
        ) == 1
        assert [
            text for _, text in harness.logs
            if text.startswith("config-")
        ] == [
            "config-first",
            "config-replacement",
        ]
    finally:
        harness.close()


def test_storage_is_scalar_and_state_owned():
    harness = Harness()
    source = declaration(
        """
mod:on_load(function()
  assert(dluma.storage.set("counter", 42))
  assert(dluma.storage.get("counter", 0) == 42)
  assert(dluma.storage.delete("counter"))
  assert(dluma.storage.get("counter", "missing") == "missing")
end)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert harness.storage == {}
    finally:
        harness.close()


def test_retired_debug_namespace_is_not_exposed_to_authored_lua():
    harness = Harness()
    source = declaration(
        """
mod:on_load(function()
  assert(dluma.debug == nil)
end)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert harness.debug == {}
        assert harness.debug_lines == []
    finally:
        harness.close()


def test_unverified_gameplay_api_returns_structured_error_without_throwing():
    harness = Harness()
    source = declaration(
        """
mod:on_load(function()
  local capability = dluma.capability(10)
  assert(capability.configured == false and capability.ready == false)
  local object, err = dluma.world.find("Missing", "SensorArea")
  assert(object == nil and err.code == "unsupported_capability")
  assert(err.capability == "world.find" and err.permission == "world.query")
  assert(err.retryable == false)
  dluma.log("structured-unavailable-ok")
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_STORAGE | PERMISSION_DEBUG | (1 << 2),
            capabilities=CAP_RUNTIME | CAP_STORAGE,
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert any(text == "structured-unavailable-ok" for _, text in harness.logs)
    finally:
        harness.close()


def test_dynamic_capability_snapshots_refresh_without_replacing_api_functions():
    harness = Harness(dynamic_capabilities=CAP_RUNTIME | CAP_STORAGE)
    source = declaration(
        """
local capability = dluma.capability
local find = dluma.world.find
mod:on_load(function()
  local ready = capability(10).ready
  local object, err = find("DLUMA_TestSensor", "SensorArea")
  if ready then
    assert(object ~= nil and err == nil)
    dluma.log("capability-ready")
  else
    assert(object == nil and err.code == "world_index_not_ready" and err.retryable)
    dluma.log("capability-waiting")
  end
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=(
                PERMISSION_STORAGE | PERMISSION_DEBUG | PERMISSION_WORLD_QUERY
            ),
            capabilities=CAP_RUNTIME | CAP_STORAGE | CAP_WORLD_FIND,
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        harness.dynamic_capabilities |= CAP_WORLD_FIND
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert [text for _, text in harness.logs[-2:]] == [
            "capability-waiting",
            "capability-ready",
        ]
    finally:
        harness.close()


def test_mod_off_native_removal_is_idempotent_and_two_phase():
    harness = Harness()
    source = declaration(
        """
local token = mod:on_sensor_enter("DLUMA_TestSensor", function() end)
mod:on_load(function()
  assert(mod:off(token) == true)
  local removed, err = mod:off(token)
  assert(removed == nil and err.code == "unknown_subscription")
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_EVENTS_SENSOR,
            capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR,
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert len(harness.subscription_add_calls) == 1
        assert len(harness.subscription_remove_calls) == 1
        assert harness.dispatch(
            state, EVENT_SENSOR_ENTER, name="DLUMA_TestSensor"
        ) == 0
    finally:
        harness.close()


def test_mod_off_native_failure_rolls_back_helper_callback_entry():
    harness = Harness()
    source = declaration(
        """
local token = mod:on_sensor_enter("DLUMA_TestSensor", function()
  dluma.log("sensor-edge")
end)
mod:on_load(function()
  local removed, err = mod:off(token)
  if removed then
    dluma.log("subscription-removed")
  else
    assert(err.code == "native_unsubscribe_failed")
    dluma.log("subscription-retained")
  end
end)
"""
    )
    try:
        harness.subscription_remove_ok = False
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_EVENTS_SENSOR,
            capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR,
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert harness.dispatch(
            state, EVENT_SENSOR_ENTER, name="DLUMA_TestSensor"
        ) == 1
        harness.subscription_remove_ok = True
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert harness.dispatch(
            state, EVENT_SENSOR_ENTER, name="DLUMA_TestSensor"
        ) == 0
        assert [text for _, text in harness.logs if text.startswith("subscription-")] == [
            "subscription-retained",
            "subscription-removed",
        ]
        assert sum(text == "sensor-edge" for _, text in harness.logs) == 1
        assert len(harness.subscription_remove_calls) == 2
    finally:
        harness.close()


def test_mod_off_during_native_observation_blocks_future_events():
    harness = Harness()
    source = declaration(
        """
local token
token = mod:on_sensor_enter("DLUMA_TestSensor", function()
  dluma.log("observed-once")
  assert(mod:off(token) == true)
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_EVENTS_SENSOR,
            capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR,
        )
        assert loaded == 1
        assert harness.dispatch(
            state, EVENT_SENSOR_ENTER, name="DLUMA_TestSensor"
        ) == 1
        assert harness.dispatch(
            state, EVENT_SENSOR_ENTER, name="DLUMA_TestSensor"
        ) == 0
        assert sum(text == "observed-once" for _, text in harness.logs) == 1
        assert len(harness.subscription_remove_calls) == 1
    finally:
        harness.close()


def test_subscription_tokens_are_process_unique_across_script_owners():
    harness = Harness()
    first_source = declaration(
        """
local token = mod:on_update(function() end)
mod:on_load(function() assert(dluma.storage.set("foreign_token", token)) end)
"""
    )
    try:
        first, loaded = harness.create(first_source, owner_id=1)
        assert loaded == 1
        assert harness.dispatch(first, EVENT_LOAD) == 1
        foreign_token = int(harness.storage["foreign_token"][1])

        second_source = declaration(
            f"""
local own_token = mod:on_update(function() end)
mod:on_load(function()
  local removed, err = mod:off({foreign_token})
  assert(removed == nil and err.code == "unknown_subscription")
end)
"""
        )
        second, loaded = harness.create(second_source, owner_id=2)
        assert loaded == 1
        assert harness.dispatch(second, EVENT_LOAD) == 1
        assert harness.dispatch(second, EVENT_UPDATE, delta=0.016) == 1
    finally:
        harness.close()


def test_mod_off_recomputes_update_callback_presence():
    harness = Harness()
    source = declaration(
        """
local first = mod:on_update(function() end)
local second = mod:on_update(function() end)
local phase = 0
mod:on_load(function()
  phase = phase + 1
  assert(mod:off(phase == 1 and first or second) == true)
end)
"""
    )
    try:
        state, loaded = harness.create(source)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert harness.status(state).has_update_callback == 1
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
        assert harness.status(state).has_update_callback == 0
        assert harness.dispatch(state, EVENT_UPDATE, delta=0.016) == 0
    finally:
        harness.close()


def test_state_destruction_unregisters_all_native_subscriptions_before_close():
    harness = Harness()
    source = declaration(
        """
mod:on_sensor_enter("DLUMA_TestSensor", function() end)
mod:on_sensor_leave("DLUMA_TestSensor", function() end)
mod:on_interact_begin("DLUMA_TestInteract", function() end)
"""
    )
    state, loaded = harness.create(
        source,
        approved_permissions=(
            PERMISSION_EVENTS_SENSOR | PERMISSION_EVENTS_INTERACT
        ),
        capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR | CAP_EVENTS_INTERACT,
    )
    assert loaded == 1
    assert len(harness.subscription_add_calls) == 3
    assert harness.destroy(state) == 1
    assert [call[2:] for call in harness.subscription_remove_calls] == [
        (EVENT_SENSOR_ENTER, "DLUMA_TestSensor"),
        (EVENT_SENSOR_LEAVE, "DLUMA_TestSensor"),
        (EVENT_INTERACT_BEGIN, "DLUMA_TestInteract"),
    ]


def test_state_teardown_retries_only_native_removals_that_failed():
    harness = Harness()
    source = declaration(
        """
mod:on_sensor_enter("DLUMA_TestSensor", function() end)
mod:on_sensor_leave("DLUMA_TestSensor", function() end)
"""
    )
    state, loaded = harness.create(
        source,
        approved_permissions=PERMISSION_EVENTS_SENSOR,
        capabilities=CAP_RUNTIME | CAP_EVENTS_SENSOR,
    )
    assert loaded == 1
    failed_token = harness.subscription_add_calls[0][1]
    successful_token = harness.subscription_add_calls[1][1]
    harness.subscription_remove_fail_tokens.add(failed_token)
    assert harness.destroy(state) == 0
    status = harness.status(state)
    assert status.alive == 0
    assert b"native subscription removal failed" in bytes(status.last_error)
    harness.subscription_remove_fail_tokens.clear()
    assert harness.destroy(state) == 1
    removed_tokens = [call[1] for call in harness.subscription_remove_calls]
    assert removed_tokens.count(failed_token) == 2
    assert removed_tokens.count(successful_token) == 1


def test_audio_rejects_raw_paths_before_native_capability_gate():
    harness = Harness()
    source = declaration(
        """
mod:on_load(function()
  local sound, err = dluma.audio.play("mods/raw/bank.csb")
  assert(sound == nil and err.code == "invalid_registered_cue")
  assert(err.capability == "audio.registered_cues")
end)
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_STORAGE | PERMISSION_DEBUG | (1 << 8),
            capabilities=CAP_RUNTIME | CAP_STORAGE,
        )
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
    finally:
        harness.close()


def test_std_rule_uses_its_own_bounded_registry():
    harness = Harness()
    source = declaration(
        """
mod:on_load(function()
  for i = 1, 2 do
    local timer, err = dluma.std.rule(mod, { predicate = function() return false end })
    if i == 1 then assert(type(timer) == "number" and err == nil)
    else assert(timer == nil and err.code == "rule_limit") end
  end
end)
"""
    )
    try:
        state, loaded = harness.create(source, max_rules=1)
        assert loaded == 1
        assert harness.dispatch(state, EVENT_LOAD) == 1
    finally:
        harness.close()


def test_direct_native_quest_api_retires_lua_graphs_and_forwards_typed_requests():
    harness = Harness(quest_mutate_mirrors=True)
    source = declaration(
        """
assert(dlua == nil)
assert(dluma.quest == nil)
assert(mod.on_quest_changed == nil)
local removed, removed_err = dluma.quests.start("DLUMA_TestNativeQuest")
assert(removed == nil and removed_err.code == "removed_operation")
assert(dluma.quests.advance_to_phase("DLUMA_TestNativeQuest", "DLUMA_TestPhase"))
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_QUEST_WRITE,
            capabilities=CAP_RUNTIME | CAP_QUEST_NATIVE_CONTROL,
            permissions="quest.write",
        )
        assert loaded == 1
        assert harness.quest_mutate_calls == [
            (3, "DLUMA_TestNativeQuest", "DLUMA_TestPhase"),
        ]
        assert harness.quest_reports == []
        assert harness.destroy(state) == 1
    finally:
        harness.close()


def test_enemy_killed_registration_fails_closed_until_a_verified_adapter_exists():
    harness = Harness()
    source = declaration(
        """
local token, err = mod:on_enemy_killed(function() end)
assert(token == nil and err.code == "unsupported_capability")
assert(err.capability == "events.enemy_killed")
"""
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_EVENTS_ENEMY_KILLED,
            capabilities=CAP_RUNTIME,
            permissions="events.enemy_killed",
        )
        assert loaded == 1
    finally:
        harness.close()


def test_quest_phase_provider_receives_length_aware_recursively_immutable_payload():
    harness = Harness()
    source = declaration(
        r'''
local provider = assert(mod:handle_quest_phase("start_intro", function(ctx, phase)
  assert(ctx.map_generation == 17 and ctx.is_player == true)
  assert(phase.quest_id == "DLUMA_TestQuest")
  assert(phase.phase_id == "DLUMA_TestPhase")
  assert(phase.contract_id == "example.quest:start_intro")
  assert(phase.provider == "example.quest" and phase.handler == "start_intro")
  assert(phase.wait == 0 and phase.completion == "fire_and_forget")
  assert(phase.sequence == 41 and phase.local_peer == true)
  assert(#phase.binding_hash == 64)
  assert(#phase.arguments.nested.value == 3)
  assert(string.byte(phase.arguments.nested.value, 2) == 0)
  assert(phase.arguments.nested.items[1] == 1)
  assert(phase.arguments.nested.items[2].enabled == true)
  assert(pcall(function() phase.quest_id = "forged" end) == false)
  assert(pcall(function() phase.arguments.nested.value = "forged" end) == false)
  assert(pcall(function() phase.arguments.nested.items[1] = 99 end) == false)
  assert(pcall(function() phase.arguments.nested.items[2].enabled = false end) == false)
  dluma.log("quest-phase-provider-delivered")
end))
'''
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_QUEST_PHASE_HANDLE,
            capabilities=CAP_RUNTIME | CAP_QUEST_PHASE_HANDLERS,
            permissions="quest.phase.handle",
        )
        assert loaded == 1
        assert [call[2:] for call in harness.subscription_add_calls] == [
            (EVENT_QUEST_PHASE_PROVIDER, "start_intro")
        ]
        assert harness.dispatch_quest_phase(
            state,
            EVENT_QUEST_PHASE_PROVIDER,
            "start_intro",
            arguments_json=(
                b'{"nested":{"value":"a\\u0000b",'
                b'"items":[1,{"enabled":true}]}}'
            ),
            sequence=41,
        ) == 1
        assert any(
            text == "quest-phase-provider-delivered"
            for _, text in harness.logs
        )
    finally:
        harness.close()


def test_quest_phase_observer_is_owner_gated_and_payload_validation_fails_closed():
    harness = Harness()
    source = declaration(
        r'''
local observer = assert(mod:observe_quest_phase(
  "example.quest:start_intro",
  function(_, phase)
    assert(phase.wait == 2.5)
    assert(phase.completion == "stock_timed_wait")
    dluma.log("quest-phase-observer-delivered")
  end))
'''
    )
    try:
        state, loaded = harness.create(
            source,
            approved_permissions=PERMISSION_QUEST_PHASE_OBSERVE,
            capabilities=CAP_RUNTIME | CAP_QUEST_PHASE_HANDLERS,
            permissions="quest.phase.observe",
        )
        assert loaded == 1
        assert [call[2:] for call in harness.subscription_add_calls] == [
            (EVENT_QUEST_PHASE_OBSERVER, "example.quest:start_intro")
        ]
        assert harness.dispatch_quest_phase(
            state,
            EVENT_QUEST_PHASE_OBSERVER,
            "example.quest:start_intro",
            arguments_json=b'{"valid":true}',
            sequence=42,
            wait_seconds=2.5,
            completion=2,
        ) == 1
        assert harness.dispatch_quest_phase(
            state,
            EVENT_QUEST_PHASE_OBSERVER,
            "example.quest:start_intro",
            arguments_json=b'{"duplicate":1,"duplicate":2}',
            sequence=43,
            wait_seconds=2.5,
            completion=2,
        ) == 0
        assert [
            text for _, text in harness.logs
            if text == "quest-phase-observer-delivered"
        ] == ["quest-phase-observer-delivered"]
    finally:
        harness.close()


def test_runtime_source_contains_worker_to_game_thread_queue_contracts():
    runtime = (ROOT / "src" / "dlw_lua_runtime.c").read_text(encoding="utf-8")
    engine = (ROOT / "src" / "dlw_engine.c").read_text(encoding="utf-8")
    host = (ROOT / "src" / "dlw_host.c").read_text(encoding="utf-8")
    assert "dluma_lua_runtime_publish_context" in host
    assert "dluma_lua_runtime_game_thread_pump" in runtime
    assert "dluma_lua_runtime_enqueue_event" in runtime
    assert "Only identical sensor edges coalesce" in runtime
    assert "dluma_lua_runtime_game_thread_pump" not in host.split("DWORD WINAPI dluma_worker_thread", 1)[1].split("int dluma_host_start", 1)[0]
    assert "dluma_ui_game_thread_tick(" in engine


def test_package_contains_helper_and_lua_license():
    mods = ROOT / "build" / "package" / "Release"
    assert (mods / "dluma_lua54.dll").is_file()
    license_text = (mods / "LICENSE_LUA_5.4.8.txt").read_text(encoding="utf-8")
    assert "Copyright (C) 1994-2025 Lua.org, PUC-Rio" in license_text
    assert "Permission is hereby granted" in license_text
