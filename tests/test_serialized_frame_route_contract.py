from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "src/fixes/custom_map_asset_isolation"


def balanced(text, start, opening, closing):
    begin = text.index(opening, start)
    depth = 0
    for end in range(begin, len(text)):
        if text[end] == opening:
            depth += 1
        elif text[end] == closing:
            depth -= 1
            if not depth:
                return text[begin:end + 1]
    raise AssertionError("Unterminated production block")


def test_callers_accept_the_frame_helpers_actual_success_result():
    wrong = re.compile(r"CurrentSerializedGameFrameRouteMatches\((?:[^()]|\([^()]*\))*\)\s*[!=]=\s*statefive::RejectReason::None")
    for path in FOLDER.iterdir():
        if path.suffix in (".cpp", ".inl"):
            assert not wrong.search(path.read_text()), path.name


def test_c10_guard_runs_with_a_matched_frame_and_rejects_invalid_owners():
    source = (FOLDER / "CustomMapAssetIsolationFix.cpp").read_text()
    record_start = source.index("struct SerializedFrameRouteEvidence {")
    record = "struct SerializedFrameRouteEvidence " + balanced(source, record_start, "{", "}") + ";"
    helper_start = source.index("statefive::RejectReason CurrentSerializedGameFrameRouteMatches(")
    body_start = source.index("{", helper_start)
    helper = source[helper_start:body_start] + balanced(source, body_start, "{", "}")
    begin_start = source.index("void BeginOutgoingMapAtC10(")
    predicate_start = source.index("const bool owner_valid = ", begin_start) + len("const bool owner_valid = ")
    predicate = source[predicate_start:source.index(";", predicate_start)]
    harness = r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <intrin.h>
namespace statefive {enum class RejectReason {None,C10FrameRouteUnavailable,C10FrameAuthorityConflict,C10FrameRouteChanged,C10ScopeMatched};}
struct DlumaGameFrameAuthorityState {
    std::uint64_t exact_route_serialization_token,game_frame_authority_sequence;
    std::uint32_t current_dispatch_thread_id,game_frame_authority_conflict,exact_route_identity_seeded;
};
struct Engine {volatile long game_frame_callback_active_thread_id;};
struct {Engine engine;} g_dluma={{7}};
static DlumaGameFrameAuthorityState sample={42,8,7,0,1};
static bool hook_owned=true,snapshot_available=true;
static bool dluma_engine_game_frame_hook_owned(Engine*) {return hook_owned;}
static bool dluma_engine_snapshot_game_frame_authority(Engine*,DlumaGameFrameAuthorityState* out) {*out=sample;return snapshot_available;}
static long dluma_atomic_compare_exchange(volatile long* p,long value,long expected) {return _InterlockedCompareExchange(p,value,expected);}
static std::uint32_t GetCurrentThreadId(){return 7;}
''' + record + helper + r'''
static bool CanNotify(std::uint32_t epoch,void* session,std::uint64_t expected_route_token=42) {
    SerializedFrameRouteEvidence before_frame={};
    return ''' + predicate + r''';
}
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    void* session=reinterpret_cast<void*>(0x1000);SerializedFrameRouteEvidence result={};
    CHECK(CurrentSerializedGameFrameRouteMatches(42,7,&result)==statefive::RejectReason::C10ScopeMatched);
    CHECK(CanNotify(1,session));
    CHECK(!CanNotify(1,session,0));
    CHECK(!CanNotify(1,session,43));
    CHECK(!CanNotify(0,session)&&!CanNotify(1,nullptr)&&!CanNotify(1,reinterpret_cast<void*>(~std::uintptr_t{0})));
    CHECK(CurrentSerializedGameFrameRouteMatches(43,7,&result)==statefive::RejectReason::C10FrameRouteChanged);
    sample.current_dispatch_thread_id=8;CHECK(!CanNotify(1,session));sample.current_dispatch_thread_id=7;
    sample.game_frame_authority_conflict=1;CHECK(!CanNotify(1,session));sample.game_frame_authority_conflict=0;
    sample.exact_route_identity_seeded=0;CHECK(!CanNotify(1,session));sample.exact_route_identity_seeded=1;
    g_dluma.engine.game_frame_callback_active_thread_id=8;CHECK(!CanNotify(1,session));g_dluma.engine.game_frame_callback_active_thread_id=7;
    hook_owned=false;CHECK(!CanNotify(1,session));hook_owned=true;
    snapshot_available=false;CHECK(!CanNotify(1,session));
    std::puts("production frame helper and C10 notification guard passed");return 0;
}
'''
    output = ROOT / "build/serialized-frame-route-fake"
    output.mkdir(parents=True, exist_ok=True)
    (output / "harness.cpp").write_text(harness)
    subprocess.run(["cmd.exe", "/d", "/c",
                    r"call tools\setup_msvc.bat && cl.exe /nologo /W4 /WX /O2 /std:c++17 "
                    r"build\serialized-frame-route-fake\harness.cpp "
                    r"/Fobuild\serialized-frame-route-fake\harness.obj "
                    r"/Febuild\serialized-frame-route-fake\harness.exe"], cwd=ROOT, check=True)
    subprocess.run([str(output / "harness.exe")], cwd=ROOT, check=True)


def test_notifications_precede_native_destroy_only_after_c10_owner_is_armed():
    source = (FOLDER / "CustomMapAssetIsolationFix.cpp").read_text()
    c08 = balanced(source, source.index("void __fastcall HookPleaseWaitFinished("), "{", "}")
    assert "begin_outgoing_map" not in c08
    assert "BeginOutgoingMapAtC10" not in c08
    c10 = balanced(source, source.index("void __fastcall HookDestroyingSessionCallback("), "{", "}")
    notify = c10.index("BeginOutgoingMapAtC10(outgoing_session, epoch, frame.route_token)")
    assert c10.index("if (scope_started_here && armed)") < notify < c10.index("g_original_destroying_session_callback(game_di)")
