// Companion header for tools/gen-click-profile.py.
//
// Per-keycode MX Blue parameter arrays (definitions in mx_blue_data.cxx).

module;
#include <array>
#include <cstdint>

export module fs8.sound:mx_blue_data;

import :bucklespring_data; // for click_params (shared click-engine struct)

export namespace fs8 {

    extern std::array<click_params, 256> const mx_blue_press_params;

    extern std::array<click_params, 256> const mx_blue_release_params;

} // namespace fs8
