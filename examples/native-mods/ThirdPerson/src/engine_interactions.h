#pragma once

#include "controller.h"

#include <windows.h>

namespace thirdperson::interactions {

// The adapter owns MinHook initialization and keeps published hooks alive.
struct Frame {
    void* player = nullptr;
    void* camera = nullptr;
    Vec3 position{};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    float verticalFovRadians = 1.04719755f;
    float aspect = 1.77777778f;
    bool eligible = false;
};

bool install(unsigned char* gameModule, HMODULE engineModule);
void setFrame(const Frame& frame);
void clear();
bool disable();

} // namespace thirdperson::interactions
