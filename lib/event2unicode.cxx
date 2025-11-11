// Created by moisrex on 10/29/25.

module;
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <string>
#include <xkbcommon/xkbcommon.h>
module foresight.lib.xkb.event2unicode;
import foresight.mods.event;
import foresight.lib.xkb;

using foresight::event_type;
using foresight::xkb::state;

namespace {
    constexpr std::uint16_t KEY_STATE_RELEASE = 0;
    constexpr std::uint16_t KEY_STATE_PRESS   = 1;
    constexpr std::uint16_t KEY_STATE_REPEAT  = 2;

    constexpr int evdev_offset = 8;
} // namespace

char32_t foresight::xkb::event2unicode(basic_state const& state_handle, event_type const& event) noexcept {
    auto* handle = state_handle.get();
    // Check if this is a key event and state is valid
    if (handle == nullptr || event.type() != EV_KEY) {
        return U'\0';
    }

    // Map evdev code -> xkb keycode
    auto const              keycode = static_cast<xkb_keycode_t>(evdev_offset + event.code());
    xkb_key_direction const dir     = event.value() == KEY_STATE_RELEASE ? XKB_KEY_UP : XKB_KEY_DOWN;

    // Update the state based on the key event
    xkb_state_update_key(handle, keycode, dir);

    // Only return Unicode for key press events, not releases
    if (dir == XKB_KEY_UP) {
        return U'\0';
    }

    return static_cast<char32_t>(xkb_state_key_get_utf32(handle, keycode));
}

std::u32string foresight::xkb::event2unicode(basic_state const&                state_handle,
                                             std::span<event_type const> const events) {
    std::u32string result;
    result.resize(events.size());
    for (auto const& event : events) {
        auto const code_point = event2unicode(state_handle, event);
        if (code_point != U'\0') {
            result += code_point;
        }
    }
    return result;
}
