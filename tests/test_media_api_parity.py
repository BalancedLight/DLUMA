import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG = json.loads((ROOT / "api/generated/dluma_api_catalog.json").read_text())
C_API = (ROOT / "sdk/c/dluma_api.h").read_text(encoding="utf-8")
LUA = (ROOT / "src/dlw_lua54_helper.c").read_text(encoding="utf-8")
MANAGED = (ROOT / "sdk/dotnet/Dluma.Managed.Bootstrap/ModContext.cs").read_text(
    encoding="utf-8"
)
MANAGED_GENERATED = (
    ROOT / "sdk/dotnet/Dluma.Managed.Sdk/Dluma.Generated.g.cs"
).read_text(encoding="utf-8")
HANDLES = (ROOT / "sdk/dotnet/Dluma.Managed.Sdk/OpaqueMediaHandles.cs").read_text(
    encoding="utf-8"
)
BINDINGS = (ROOT / "src/generated/dluma_api_service_bindings.generated.inc").read_text(
    encoding="utf-8"
)


EXPECTED = {
    "player.vehicle_state": (3012, "game.read", "player.vehicle"),
    "companion_content.entry.read": (11002, "companion.read", "companion.content"),
    "audio.bank.open": (6004, "audio.play", "audio.stem"),
    "audio.cue.duration": (6005, "audio.play", "audio.stem"),
    "audio.cue.play": (6002, "audio.play", "audio.stem"),
    "audio.cue.state": (6006, "audio.play", "audio.stem"),
    "audio.cue.seek": (6007, "audio.play", "audio.stem"),
    "audio.cue.set_gain": (6008, "audio.play", "audio.stem"),
    "audio.cue.set_filter": (6009, "audio.play", "audio.wave_filter"),
    "audio.cue.stop": (6003, "audio.play", "audio.stem"),
    "audio.music.acquire": (6010, "audio.play", "audio.music_exclusive"),
    "audio.music.state": (6011, "audio.play", "audio.music_exclusive"),
    "audio.music.set_filter": (6012, "audio.play", "audio.music_filter"),
    "audio.music.release": (6013, "audio.play", "audio.music_exclusive"),
}


def test_catalog_contracts_are_stable_and_mod_neutral():
    operations = {
        operation["id"]: operation
        for domain in CATALOG["domains"]
        for operation in domain["operations"]
    }
    for operation_id, (numeric_id, access, capability) in EXPECTED.items():
        operation = operations[operation_id]
        assert operation["numeric_id"] == numeric_id
        assert operation["access"] == [access]
        assert operation["capability"] == capability
    serialized = json.dumps(
        {key: operations[key] for key in EXPECTED}, sort_keys=True
    ).casefold()
    assert "drivinglight" not in serialized
    assert "local_buggy" not in serialized


def test_lua_csharp_and_native_all_dispatch_common_numeric_operations():
    for numeric_id in [value[0] for value in EXPECTED.values()]:
        assert f"{numeric_id}U" in BINDINGS
        assert str(numeric_id) in MANAGED
        assert str(numeric_id) in LUA
    for name in (
        '"vehicle_state"',
        '"companion_content"',
        '"read_entry"',
        '"bank"',
        '"cue"',
        '"music"',
        '"set_filter"',
    ):
        assert name in LUA
    assert "DlumaPlayerV1" in C_API
    assert "DlumaCompanionContentV1" in C_API
    assert "DlumaAudioV1" in C_API


def test_managed_handles_copy_bytes_and_dispose_owned_audio_resources():
    assert "byte[] Bytes" in MANAGED_GENERATED
    assert "EntryRead" in MANAGED
    assert "DlumaCompanionHandle : IDisposable" in HANDLES
    assert "DlumaAudioBankHandle : IDisposable" in HANDLES
    assert "DlumaAudioCueHandle : IDisposable" in HANDLES
    assert "DlumaMusicLease : IDisposable" in HANDLES
    assert "_audio.CueStop" in HANDLES
    assert "_audio.MusicRelease" in HANDLES


def test_private_native_services_are_a_clean_break():
    combined = C_API + BINDINGS
    for removed in (
        "dluma.frame.v1",
        "dluma.local_buggy.v1",
        "dluma.companion_pack.v1",
        "dluma.stem_audio.v1",
        "dluma.music_exclusive.v1",
        "dluma.mod_config.v1",
    ):
        assert removed not in combined
