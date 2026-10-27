#pragma once

#include <memory>

namespace thirdperson {
class Controller;

using LogFunction = void (*)(const char* level, const char* message);

struct KeyBindings {
    int toggle = 0x54;
    int orbit = 0xA4;
    int left = 0x25;
    int right = 0x27;
    int up = 0x26;
    int down = 0x28;
    int closer = 0x21;
    int farther = 0x22;
    int reset = 0x24;
};

class EngineAdapter {
public:
    EngineAdapter();
    ~EngineAdapter();
    EngineAdapter(const EngineAdapter&) = delete;
    EngineAdapter& operator=(const EngineAdapter&) = delete;

    bool initialize(Controller* controller, const KeyBindings& keys, LogFunction log);
    void setBindings(const KeyBindings& keys);
    void activate();
    void deactivate();
    void tick();
    void shutdown();
    bool active() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace thirdperson
