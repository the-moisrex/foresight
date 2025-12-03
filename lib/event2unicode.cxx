// Created by moisrex on 10/29/25.

module;
#include <cassert>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <string>
#include <xkbcommon/xkbcommon.h>
module foresight.lib.xkb.event2unicode;
import foresight.mods.event;
import foresight.lib.xkb;

namespace {
    constexpr int           evdev_offset      = 8;
    constexpr std::uint16_t KEY_STATE_RELEASE = 0;
    // constexpr std::uint16_t KEY_STATE_PRESS   = 1;
    // constexpr std::uint16_t KEY_STATE_REPEAT  = 2;
} // namespace

char32_t fs8::xkb::event2unicode(basic_state const& state_handle, key_event const event) noexcept {
    auto* handle = state_handle.get();
    assert(handle != nullptr);
    // assert(event.type() == EV_KEY);

    // Map evdev code -> xkb keycode
    auto const              keycode = static_cast<xkb_keycode_t>(evdev_offset + event.code);
    xkb_key_direction const dir =
      static_cast<std::uint16_t>(event.value) == KEY_STATE_RELEASE ? XKB_KEY_UP : XKB_KEY_DOWN;

    // Update the state based on the key event
    xkb_state_update_key(handle, keycode, dir);

    // Only return Unicode for key press events, not releases
    if (dir == XKB_KEY_UP) {
        return U'\0';
    }

    // this commented code handles Ctrl and what not as well, which we don't need
    // I believe these things are not good enough for our use cases where we want uniqueness
    // https://www.x.org/releases/current/doc/kbproto/xkbproto.html#Interpreting_the_Control_Modifier
    // return static_cast<char32_t>(xkb_state_key_get_utf32(handle, keycode));

    xkb_keysym_t const sym = xkb_state_key_get_one_sym(handle, keycode);
    if (sym == XKB_KEY_NoSymbol) [[unlikely]] {
        return U'\0';
    }
    return static_cast<char32_t>(xkb_keysym_to_utf32(sym));
}

std::u32string fs8::xkb::event2unicode(basic_state const&                state_handle,
                                       std::span<event_type const> const events) {
    std::u32string result;
    result.resize(events.size());
    for (auto const& event : events) {
        auto const code_point = event2unicode(state_handle, static_cast<key_event>(event));
        if (code_point != U'\0') {
            result += code_point;
        }
    }
    return result;
}
