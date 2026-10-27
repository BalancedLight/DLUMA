#define _HAS_EXCEPTIONS 0

#include "MainMenuCreditsMusicFix.h"

extern "C" {
#include "../../dlw_common.h"
#include "../../dlw_custom_main_menu_music.h"
}

#include <cstdint>
#include <cstring>

namespace dluma::fixes::main_menu_credits_music {
namespace {

constexpr std::uint32_t kSupportedGamedllTimestamp = 0x623397AAU;
constexpr std::uint32_t kSupportedGamedllImageSize = 0x02082000U;
constexpr std::uintptr_t kMenuCreditsVtableRva = 0x019D9898U;
constexpr std::uintptr_t kShowScreenSlotOffset = 0x04D0U;
constexpr std::uintptr_t kHideScreenSlotOffset = 0x04D8U;
constexpr std::uintptr_t kCanPlayBackgroundMusicSlotOffset = 0x08B8U;
constexpr std::uintptr_t kAllowBackgroundMusicPlaybackSlotOffset = 0x0E50U;
constexpr std::uintptr_t kShowScreenRva = 0x0124E2C0U;
constexpr std::uintptr_t kHideScreenRva = 0x0124E560U;
constexpr std::uintptr_t kCanPlayBackgroundMusicRva = 0x0055A250U;
constexpr std::uintptr_t kAllowBackgroundMusicPlaybackRva = 0x00937E80U;
constexpr std::uintptr_t kMainMenuModuleVtableRva = 0x01971248U;
constexpr std::uintptr_t kMainMenuModuleVtableFirstFunctionRva = 0x01309668U;
constexpr std::uintptr_t kPlayBackgroundMusicRva = 0x011B70F0U;
constexpr std::uintptr_t kStopBackgroundMusicRva = 0x011B7380U;
constexpr std::size_t kFunctionPrefixSize = 16;
constexpr std::size_t kModeOffset = 0x424U;
constexpr std::size_t kHideModeProbeOffset = 0x15U;
constexpr int kFrontEndCreditsMode = 0;
constexpr int kNoSoundHandle = 0;
constexpr DWORD kShutdownWaitMs = 2000;

constexpr char kCreditsCue[] = "dl_menu_end_credits";
/* The GUI PlaySound2D export ends with the playback-rate parameter.  Use the
 * stock GUI neutral rate rather than declaring a nonexistent trailing
 * start-time argument and passing 0.0 for this final ABI parameter. */
constexpr float kCreditsCueVolume = 1.0f;
constexpr float kCreditsCueFadeIn = 1.0f;
constexpr float kCreditsCueFadeOut = 0.0f;
constexpr unsigned int kCreditsCueBus = 0;
constexpr float kCreditsCuePlaybackRate = 1.0f;
constexpr char kPlaySound2DExport[] =
    "?PlaySound2D@IObject@gui@@QEAAHAEBV?$string_base@D@ttl@@_NMMMIM@Z";
constexpr char kStopSoundExport[] =
    "?StopSound@IObject@gui@@QEAA_NHM@Z";
constexpr char kGetObjectLevelExport[] =
    "?GetLevel@IObject@gui@@QEBAPEAVILevel@@XZ";

constexpr std::uint8_t kShowScreenPrefix[kFunctionPrefixSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x48, 0x83,
    0xEC, 0x50, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xD9};
constexpr std::uint8_t kHideScreenPrefix[kFunctionPrefixSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x0F, 0xB6, 0xDA, 0x48, 0x8B, 0xF9};
constexpr std::uint8_t kCanPlayBackgroundMusicPrefix[] = {
    0x48, 0x8B, 0x01, 0x45, 0x33, 0xC0, 0x48, 0xFF,
    0xA0, 0xA8, 0x08, 0x00, 0x00};
constexpr std::uint8_t kAllowBackgroundMusicPlaybackPrefix[] = {
    0xB0, 0x01, 0xC3};
constexpr std::uint8_t kHideModeProbe[] = {
    0x8B, 0x87, 0x24, 0x04, 0x00, 0x00, 0x84, 0xDB};
constexpr std::uint8_t kPlayBackgroundMusicPrefix[kFunctionPrefixSize] = {
    0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74,
    0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00};
constexpr std::uint8_t kStopBackgroundMusicPrefix[kFunctionPrefixSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x15};

struct TtlStringView {
    const char* data;
    unsigned int size;
    unsigned int capacity;
};
static_assert(sizeof(TtlStringView) == 16, "ttl::string_base<char> ABI");

using ShowScreenFn = void(__fastcall*)(void* screen, void* parameters);
using HideScreenFn = void(__fastcall*)(void* screen, unsigned char animated);
using CanPlayBackgroundMusicFn = unsigned char(__fastcall*)(void* screen);
using AllowBackgroundMusicPlaybackFn = unsigned char(__fastcall*)(void* screen);
using PlaySound2DFn = int(__fastcall*)(
    void* object, const TtlStringView* cue, unsigned char loop, float volume,
    float fade_in, float fade_out, unsigned int bus, float playback_rate);
using StopSoundFn = unsigned char(__fastcall*)(void* object, int handle,
                                                float fade_out);
using GetObjectLevelFn = void*(__fastcall*)(void* object);
using MainMenuMusicFn = void(__fastcall*)(void* main_menu_module);

struct PointerPatch {
    void** slot;
    void* original;
    void* replacement;
    bool owned;
};

bool g_initialized;
bool g_enabled;
bool g_stopping;
std::uintptr_t g_gamedll_base;
std::size_t g_gamedll_size;
PointerPatch g_show_patch = {};
PointerPatch g_hide_patch = {};
PointerPatch g_can_play_patch = {};
PointerPatch g_allow_background_music_patch = {};
ShowScreenFn g_original_show;
HideScreenFn g_original_hide;
CanPlayBackgroundMusicFn g_original_can_play;
AllowBackgroundMusicPlaybackFn g_original_allow_background_music;
PlaySound2DFn g_play_sound_2d;
StopSoundFn g_stop_sound;
GetObjectLevelFn g_get_object_level;
MainMenuMusicFn g_play_background_music;
MainMenuMusicFn g_stop_background_music;
void* g_owned_screen;
void* g_owned_main_menu_module;
int g_owned_sound_handle;
volatile LONG g_active_hook_calls;
FixStatus g_status = {kFixId, kFixDisplayName, FixState::UnsupportedBuild, 0,
                      kShowScreenRva, false};

FixStatus MakeStatus(FixState state, std::uintptr_t address = 0,
                     bool owned = false) noexcept
{
    g_status = {kFixId, kFixDisplayName, state, address, kShowScreenRva,
                owned};
    return g_status;
}

bool ReadExact(std::uintptr_t address, const std::uint8_t* expected,
               std::size_t size) noexcept
{
    return address && expected && size &&
           dluma_is_readable(reinterpret_cast<const void*>(address), size) &&
           std::memcmp(reinterpret_cast<const void*>(address), expected,
                       size) == 0;
}

bool SupportedIdentity(const RuntimeContext& context) noexcept
{
    return context.gamedll_module && context.gamedll_base &&
           context.gamedll_timestamp == kSupportedGamedllTimestamp &&
           context.gamedll_size_of_image == kSupportedGamedllImageSize;
}

bool IsInModule(const RuntimeContext& context, const void* address) noexcept
{
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    return value >= context.engine_base &&
           value < context.engine_base + context.engine_size_of_image;
}

bool IsFrontEndCredits(void* screen) noexcept
{
    int mode = -1;
    const std::uintptr_t mode_address =
        reinterpret_cast<std::uintptr_t>(screen) + kModeOffset;
    return screen && dluma_is_readable(reinterpret_cast<const void*>(mode_address),
                                     sizeof(mode)) &&
           (std::memcpy(&mode, reinterpret_cast<const void*>(mode_address),
                        sizeof(mode)),
            mode == kFrontEndCreditsMode);
}

void PinLoaderForProcessExit() noexcept
{
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&PinLoaderForProcessExit), &pinned))
        dluma_atomic_exchange(&g_dluma.retained_for_process_exit, 1);
}

bool ReplacePointer(PointerPatch& patch) noexcept
{
    if (!patch.slot || !patch.original || !patch.replacement ||
        !dluma_is_readable(patch.slot, sizeof(*patch.slot)))
        return false;
    DWORD old_protection = 0;
    DWORD ignored_protection = 0;
    if (!VirtualProtect(patch.slot, sizeof(*patch.slot), PAGE_EXECUTE_READWRITE,
                        &old_protection))
        return false;
    const void* observed = dluma_atomic_pointer_compare_exchange(
        reinterpret_cast<PVOID volatile*>(patch.slot), patch.replacement,
        patch.original);
    const bool exchanged = observed == patch.original;
    const bool flushed = !exchanged ||
        FlushInstructionCache(GetCurrentProcess(), patch.slot,
                              sizeof(*patch.slot)) != FALSE;
    const bool restored = VirtualProtect(patch.slot, sizeof(*patch.slot),
                                         old_protection, &ignored_protection) != FALSE;
    patch.owned = exchanged && flushed && restored;
    return patch.owned;
}

bool RestorePointer(PointerPatch& patch) noexcept
{
    if (!patch.owned) return true;
    DWORD old_protection = 0;
    DWORD ignored_protection = 0;
    if (!VirtualProtect(patch.slot, sizeof(*patch.slot), PAGE_EXECUTE_READWRITE,
                        &old_protection))
        return false;
    const void* observed = dluma_atomic_pointer_compare_exchange(
        reinterpret_cast<PVOID volatile*>(patch.slot), patch.original,
        patch.replacement);
    const bool exchanged = observed == patch.replacement;
    const bool flushed = !exchanged ||
        FlushInstructionCache(GetCurrentProcess(), patch.slot,
                              sizeof(*patch.slot)) != FALSE;
    const bool restored = VirtualProtect(patch.slot, sizeof(*patch.slot),
                                         old_protection, &ignored_protection) != FALSE;
    if (exchanged && flushed && restored) patch.owned = false;
    return exchanged && flushed && restored;
}

void StopOwnedCueFor(void* screen) noexcept
{
    if (!screen || screen != g_owned_screen) return;
    const int handle = g_owned_sound_handle;
    g_owned_screen = nullptr;
    g_owned_sound_handle = kNoSoundHandle;
    if (handle > kNoSoundHandle && g_stop_sound)
        g_stop_sound(screen, handle, 0.0f);
}

bool IsInGamedll(const void* address) noexcept
{
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    return value >= g_gamedll_base &&
           value < g_gamedll_base + g_gamedll_size;
}

void* ResolveMainMenuModule(void* screen) noexcept
{
    if (!screen || !g_get_object_level) {
        dluma_log_pair("WARN", "main_menu_credits_music_base_menu",
                     "level_unavailable");
        return nullptr;
    }
    void* const level = g_get_object_level(screen);
    if (!level || !dluma_is_readable(level, sizeof(void*))) {
        dluma_log_pair("WARN", "main_menu_credits_music_base_menu",
                     "level_unavailable");
        return nullptr;
    }
    const void* const actual_vtable = *reinterpret_cast<void**>(level);
    const void* const primary_vtable = reinterpret_cast<const void*>(
        g_gamedll_base + kMainMenuModuleVtableRva);
    if (!IsInGamedll(actual_vtable)) {
        dluma_log_pair("WARN", "main_menu_credits_music_base_menu",
                     "unexpected_vtable");
        return nullptr;
    }
    if (actual_vtable != primary_vtable) {
        /* MainMenuModuleDI owns the live Player level and therefore exposes
         * a derived vtable.  The screen's validated ILevel pointer remains
         * the correct MainMenuModule base for these nonvirtual methods. */
        dluma_log_pair("INFO", "main_menu_credits_music_base_menu",
                     "derived_main_menu_vtable");
        dluma_log_u64("INFO", "main_menu_credits_music_level_vtable_rva",
                    reinterpret_cast<std::uintptr_t>(actual_vtable) -
                        g_gamedll_base);
    }
    return level;
}

void* StopBaseMenuMusic(void* screen) noexcept
{
    void* const main_menu_module = ResolveMainMenuModule(screen);
    if (!main_menu_module || !g_stop_background_music) return nullptr;
    g_stop_background_music(main_menu_module);
    dluma_log_pair("FIX", "main_menu_credits_music_base_menu", "stopped");
    return main_menu_module;
}

void ResumeBaseMenuMusic(void* main_menu_module) noexcept
{
    if (!main_menu_module || !g_play_background_music ||
        !dluma_is_readable(main_menu_module, sizeof(void*)) ||
        !IsInGamedll(*reinterpret_cast<void**>(main_menu_module))) {
        dluma_log_pair("WARN", "main_menu_credits_music_base_menu",
                     "resume_unavailable");
        return;
    }
    g_play_background_music(main_menu_module);
    dluma_log_pair("FIX", "main_menu_credits_music_base_menu", "resumed");
}

void StartOwnedCue(void* screen) noexcept
{
    const char* cue_name = kCreditsCue;
    bool custom_override = false;
    if (!screen || !g_play_sound_2d || !IsFrontEndCredits(screen))
        return;
    StopOwnedCueFor(g_owned_screen);
    if (!dluma_custom_main_menu_music_release_screen_override()) {
        dluma_log_pair(
            "WARN", "main_menu_credits_music_handoff",
            "custom_menu_override_release_failed");
        return;
    }
    g_owned_main_menu_module = StopBaseMenuMusic(screen);
    if (g_owned_main_menu_module) {
        const char* selected_cue = nullptr;
        if (dluma_custom_main_menu_music_resolve_credits_cue(
                g_owned_main_menu_module, &selected_cue) &&
            selected_cue && selected_cue[0]) {
            cue_name = selected_cue;
            custom_override = true;
        }
    }
    const unsigned int cue_length =
        static_cast<unsigned int>(std::strlen(cue_name));
    const TtlStringView cue = {cue_name, cue_length, cue_length};
    const int handle = g_play_sound_2d(
        screen, &cue, 0, kCreditsCueVolume, kCreditsCueFadeIn,
        kCreditsCueFadeOut, kCreditsCueBus, kCreditsCuePlaybackRate);
    if (handle > kNoSoundHandle) {
        g_owned_screen = screen;
        g_owned_sound_handle = handle;
        dluma_log_pair(
            "FIX", "main_menu_credits_music_cue_selection",
            custom_override ? "custom_override" : "stock_fallback");
        dluma_log_pair("FIX", "main_menu_credits_music_cue_name", cue_name);
        dluma_log_pair("FIX", "main_menu_credits_music_cue", "started");
    } else {
        dluma_log_pair("WARN", "main_menu_credits_music_cue", "play_failed");
    }
}

class ActiveHookCall {
public:
    ActiveHookCall() noexcept { dluma_atomic_increment(&g_active_hook_calls); }
    ~ActiveHookCall() { dluma_atomic_decrement(&g_active_hook_calls); }
    ActiveHookCall(const ActiveHookCall&) = delete;
    ActiveHookCall& operator=(const ActiveHookCall&) = delete;
};

void __fastcall HookShowScreen(void* screen, void* parameters) noexcept
{
    ActiveHookCall active;
    if (g_original_show) g_original_show(screen, parameters);
    if (!g_stopping && g_enabled)
        StartOwnedCue(screen);
}

void __fastcall HookHideScreen(void* screen, unsigned char animated) noexcept
{
    ActiveHookCall active;
    if (IsFrontEndCredits(screen)) {
        StopOwnedCueFor(screen);
        dluma_log_pair("FIX", "main_menu_credits_music_cue", "stopped");
        void* const main_menu_module = g_owned_main_menu_module;
        g_owned_main_menu_module = nullptr;
        if (g_original_hide) g_original_hide(screen, animated);
        ResumeBaseMenuMusic(main_menu_module);
        return;
    }
    if (g_original_hide) g_original_hide(screen, animated);
}

unsigned char __fastcall HookCanPlayBackgroundMusic(void* screen) noexcept
{
    ActiveHookCall active;
    if (g_enabled && !g_stopping && IsFrontEndCredits(screen)) return 0;
    return g_original_can_play ? g_original_can_play(screen) : 1;
}

unsigned char __fastcall HookAllowBackgroundMusicPlayback(void* screen) noexcept
{
    ActiveHookCall active;
    if (g_enabled && !g_stopping && IsFrontEndCredits(screen)) return 0;
    return g_original_allow_background_music
        ? g_original_allow_background_music(screen) : 1;
}

bool ValidateTargets(const RuntimeContext& context) noexcept
{
    const std::uintptr_t vtable = context.gamedll_base + kMenuCreditsVtableRva;
    const std::uintptr_t show = context.gamedll_base + kShowScreenRva;
    const std::uintptr_t hide = context.gamedll_base + kHideScreenRva;
    const std::uintptr_t can_play =
        context.gamedll_base + kCanPlayBackgroundMusicRva;
    const std::uintptr_t allow_background_music =
        context.gamedll_base + kAllowBackgroundMusicPlaybackRva;
    if (kMenuCreditsVtableRva + kCanPlayBackgroundMusicSlotOffset +
            sizeof(void*) > context.gamedll_size_of_image ||
        kMenuCreditsVtableRva + kAllowBackgroundMusicPlaybackSlotOffset +
            sizeof(void*) > context.gamedll_size_of_image ||
        kShowScreenRva + kFunctionPrefixSize > context.gamedll_size_of_image ||
        kHideScreenRva + kHideModeProbeOffset + sizeof(kHideModeProbe) >
            context.gamedll_size_of_image ||
        kCanPlayBackgroundMusicRva + sizeof(kCanPlayBackgroundMusicPrefix) >
            context.gamedll_size_of_image ||
        !ReadExact(show, kShowScreenPrefix, sizeof(kShowScreenPrefix)) ||
        !ReadExact(hide, kHideScreenPrefix, sizeof(kHideScreenPrefix)) ||
        !ReadExact(hide + kHideModeProbeOffset, kHideModeProbe,
                   sizeof(kHideModeProbe)) ||
        !ReadExact(can_play, kCanPlayBackgroundMusicPrefix,
                   sizeof(kCanPlayBackgroundMusicPrefix)) ||
        !ReadExact(allow_background_music,
                   kAllowBackgroundMusicPlaybackPrefix,
                   sizeof(kAllowBackgroundMusicPlaybackPrefix)))
        return false;

    g_show_patch = {reinterpret_cast<void**>(vtable + kShowScreenSlotOffset),
                    reinterpret_cast<void*>(show),
                    reinterpret_cast<void*>(&HookShowScreen), false};
    g_hide_patch = {reinterpret_cast<void**>(vtable + kHideScreenSlotOffset),
                    reinterpret_cast<void*>(hide),
                    reinterpret_cast<void*>(&HookHideScreen), false};
    g_can_play_patch = {
        reinterpret_cast<void**>(vtable + kCanPlayBackgroundMusicSlotOffset),
        reinterpret_cast<void*>(can_play),
        reinterpret_cast<void*>(&HookCanPlayBackgroundMusic), false};
    g_allow_background_music_patch = {
        reinterpret_cast<void**>(
            vtable + kAllowBackgroundMusicPlaybackSlotOffset),
        reinterpret_cast<void*>(allow_background_music),
        reinterpret_cast<void*>(&HookAllowBackgroundMusicPlayback), false};
    if (!dluma_is_readable(g_show_patch.slot, sizeof(void*)) ||
        !dluma_is_readable(g_hide_patch.slot, sizeof(void*)) ||
        !dluma_is_readable(g_can_play_patch.slot, sizeof(void*)) ||
        !dluma_is_readable(g_allow_background_music_patch.slot, sizeof(void*)) ||
        *g_show_patch.slot != g_show_patch.original ||
        *g_hide_patch.slot != g_hide_patch.original ||
        *g_can_play_patch.slot != g_can_play_patch.original ||
        *g_allow_background_music_patch.slot !=
            g_allow_background_music_patch.original)
        return false;
    return true;
}

bool ValidateAudioCalls(const RuntimeContext& context) noexcept
{
    if (!context.engine_module || !context.engine_base ||
        !context.engine_size_of_image)
        return false;
    g_play_sound_2d = reinterpret_cast<PlaySound2DFn>(
        GetProcAddress(context.engine_module, kPlaySound2DExport));
    g_stop_sound = reinterpret_cast<StopSoundFn>(
        GetProcAddress(context.engine_module, kStopSoundExport));
    g_get_object_level = reinterpret_cast<GetObjectLevelFn>(
        GetProcAddress(context.engine_module, kGetObjectLevelExport));
    return g_play_sound_2d && g_stop_sound && g_get_object_level &&
           IsInModule(context, reinterpret_cast<const void*>(g_play_sound_2d)) &&
           IsInModule(context, reinterpret_cast<const void*>(g_stop_sound)) &&
           IsInModule(context, reinterpret_cast<const void*>(g_get_object_level)) &&
           dluma_is_readable(reinterpret_cast<const void*>(g_play_sound_2d),
                           kFunctionPrefixSize) &&
           dluma_is_readable(reinterpret_cast<const void*>(g_stop_sound),
                           kFunctionPrefixSize) &&
           dluma_is_readable(reinterpret_cast<const void*>(g_get_object_level),
                           kFunctionPrefixSize);
}

bool ValidateMainMenuMusicTargets(const RuntimeContext& context) noexcept
{
    const std::uintptr_t vtable = context.gamedll_base + kMainMenuModuleVtableRva;
    const std::uintptr_t play = context.gamedll_base + kPlayBackgroundMusicRva;
    const std::uintptr_t stop = context.gamedll_base + kStopBackgroundMusicRva;
    if (kMainMenuModuleVtableRva + sizeof(void*) > context.gamedll_size_of_image ||
        kPlayBackgroundMusicRva + sizeof(kPlayBackgroundMusicPrefix) >
            context.gamedll_size_of_image ||
        kStopBackgroundMusicRva + sizeof(kStopBackgroundMusicPrefix) >
            context.gamedll_size_of_image ||
        !dluma_is_readable(reinterpret_cast<const void*>(vtable), sizeof(void*)) ||
        *reinterpret_cast<void**>(vtable) !=
            reinterpret_cast<void*>(context.gamedll_base +
                                    kMainMenuModuleVtableFirstFunctionRva) ||
        !ReadExact(play, kPlayBackgroundMusicPrefix,
                   sizeof(kPlayBackgroundMusicPrefix)) ||
        !ReadExact(stop, kStopBackgroundMusicPrefix,
                   sizeof(kStopBackgroundMusicPrefix)))
        return false;
    g_play_background_music = reinterpret_cast<MainMenuMusicFn>(play);
    g_stop_background_music = reinterpret_cast<MainMenuMusicFn>(stop);
    return true;
}

}  // namespace

FixStatus Initialize(const RuntimeContext& context, bool enabled)
{
    if (g_initialized) return g_status;
    g_initialized = true;
    g_enabled = enabled;
    g_stopping = false;
    if (!enabled) return MakeStatus(FixState::Disabled);
    if (!SupportedIdentity(context)) {
        dluma_log_pair("FIX", "main_menu_credits_music", "unsupported_build");
        return MakeStatus(FixState::UnsupportedBuild);
    }
    if (!ValidateTargets(context)) {
        dluma_log_pair("FIX", "main_menu_credits_music", "target_validation_failed");
        return MakeStatus(FixState::OriginalBytesMismatch);
    }
    if (!ValidateAudioCalls(context)) {
        dluma_log_pair("FIX", "main_menu_credits_music", "audio_validation_failed");
        return MakeStatus(FixState::OriginalBytesMismatch);
    }
    if (!ValidateMainMenuMusicTargets(context)) {
        dluma_log_pair("FIX", "main_menu_credits_music",
                     "main_menu_music_validation_failed");
        return MakeStatus(FixState::OriginalBytesMismatch);
    }

    g_original_show = reinterpret_cast<ShowScreenFn>(g_show_patch.original);
    g_original_hide = reinterpret_cast<HideScreenFn>(g_hide_patch.original);
    g_original_can_play =
        reinterpret_cast<CanPlayBackgroundMusicFn>(g_can_play_patch.original);
    g_original_allow_background_music =
        reinterpret_cast<AllowBackgroundMusicPlaybackFn>(
            g_allow_background_music_patch.original);
    g_gamedll_base = context.gamedll_base;
    g_gamedll_size = context.gamedll_size_of_image;
    if (!ReplacePointer(g_allow_background_music_patch) ||
        !ReplacePointer(g_can_play_patch) || !ReplacePointer(g_show_patch) ||
        !ReplacePointer(g_hide_patch)) {
        RestorePointer(g_hide_patch);
        RestorePointer(g_show_patch);
        RestorePointer(g_can_play_patch);
        RestorePointer(g_allow_background_music_patch);
        dluma_log_pair("FIX", "main_menu_credits_music", "hook_install_failed");
        return MakeStatus(FixState::ApplyFailed, g_gamedll_base + kShowScreenRva);
    }
    dluma_log_pair("FIX", "main_menu_credits_music", "installed");
    return MakeStatus(FixState::Applied, g_gamedll_base + kShowScreenRva, true);
}

FixStatus Shutdown()
{
    if (!g_initialized) return g_status;
    g_stopping = true;
    const ULONGLONG deadline = GetTickCount64() + kShutdownWaitMs;
    while (dluma_atomic_compare_exchange(&g_active_hook_calls, 0, 0) != 0 &&
           GetTickCount64() < deadline)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&g_active_hook_calls, 0, 0) != 0) {
        PinLoaderForProcessExit();
        return MakeStatus(FixState::RuntimeFailed,
                          g_gamedll_base + kShowScreenRva, true);
    }
    StopOwnedCueFor(g_owned_screen);
    g_owned_main_menu_module = nullptr;
    const bool restored = RestorePointer(g_hide_patch) &&
                          RestorePointer(g_show_patch) &&
                          RestorePointer(g_can_play_patch) &&
                          RestorePointer(g_allow_background_music_patch);
    if (!restored) {
        PinLoaderForProcessExit();
        return MakeStatus(FixState::RuntimeFailed,
                          g_gamedll_base + kShowScreenRva, true);
    }
    g_enabled = false;
    dluma_log_pair("FIX", "main_menu_credits_music", "restored");
    return MakeStatus(FixState::Restored, g_gamedll_base + kShowScreenRva);
}

FixStatus CurrentStatus()
{
    return g_status;
}

}  // namespace dluma::fixes::main_menu_credits_music
