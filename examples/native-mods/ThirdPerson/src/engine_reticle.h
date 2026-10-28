#pragma once

namespace thirdperson::reticle {

// MinHook is initialized by the adapter and remains alive while this hook exists.
bool install(unsigned char* gameModule);
bool disable();

// Publish only the current local player while ordinary third-person aim is active.
void setEligiblePlayer(void* player);

} // namespace thirdperson::reticle
