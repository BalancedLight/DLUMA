#pragma once

namespace thirdperson {

// Reads the current local player's accepted native action controllers.
bool specialActionActive(void* player);

// Selects the native first-person visual class while the local player's
// third-person mesh is streamed. The game still owns the animation graph.
class ActionVisualClassBridge {
public:
    using LocalPlayer = void* (*)(void* context);
    using WantsFirstPersonClass = bool (*)(void* context);

    bool initialize(unsigned char* game, void* context,
        LocalPlayer localPlayer, WantsFirstPersonClass wantsFirstPersonClass);
    bool shutdown();

private:
    unsigned char* game_ = nullptr;
    void* context_ = nullptr;
    LocalPlayer localPlayer_ = nullptr;
    WantsFirstPersonClass wantsFirstPersonClass_ = nullptr;
    void** slot_ = nullptr;
};

} // namespace thirdperson
