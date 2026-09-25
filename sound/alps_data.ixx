// Companion header for tools/gen-click-profile.py.
//
// Per-keycode Alps parameter arrays (definitions in alps_data.cxx).

module;
#include <array>
#include <cstdint>

export module fs8.sound:alps_data;

import :bucklespring_data; // for click_params (shared click-engine struct)

export namespace fs8 {

    extern std::array<click_params, 256> const alps_press_params;

    extern std::array<click_params, 256> const alps_release_params;

} // namespace fs8
