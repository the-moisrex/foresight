// Companion header for tools/gen-click-profile.py.
//
// Per-keycode Typewriter parameter arrays (definitions in typewriter_data.cxx).

module;
#include <array>
#include <cstdint>

export module fs8.sound:typewriter_data;

import :bucklespring_data; // for click_params (shared click-engine struct)

export namespace fs8 {

    extern std::array<click_params, 256> const typewriter_press_params;

    extern std::array<click_params, 256> const typewriter_release_params;

} // namespace fs8
