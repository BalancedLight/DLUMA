#ifndef DLUMA_FIXES_MAIN_MENU_CREDITS_MUSIC_FIX_H
#define DLUMA_FIXES_MAIN_MENU_CREDITS_MUSIC_FIX_H

#include "../FixRegistry.h"

namespace dluma::fixes::main_menu_credits_music {

static constexpr const char* kFixId = "main_menu_credits_music";
static constexpr const char* kFixDisplayName = "Main-Menu Credits Music Fix";

FixStatus Initialize(const RuntimeContext& context, bool enabled);
FixStatus Shutdown();
FixStatus CurrentStatus();

}  // namespace dluma::fixes::main_menu_credits_music

#endif
