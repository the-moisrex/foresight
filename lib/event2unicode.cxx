// Created by moisrex on 10/29/25.

module;
#include <cassert>
#include <cstdint>
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

    // Shared core
    template <typename Fn>
    [[nodiscard]] char32_t
    event2unicode_impl(fs8::xkb::basic_state const& state_handle, fs8::key_event const event, Fn get_unicode) noexcept {
        auto* handle = state_handle.get();
        assert(handle != nullptr);

        // Map evdev code -> xkb keycode
        auto const              keycode = static_cast<xkb_keycode_t>(evdev_offset + event.code);
        xkb_key_direction const dir     = static_cast<std::uint16_t>(event.value) == KEY_STATE_RELEASE ? XKB_KEY_UP : XKB_KEY_DOWN;

        // Update state
        xkb_state_update_key(handle, keycode, dir);

        // No Unicode for releases
        if (dir == XKB_KEY_UP) {
            return U'\0';
        }

        return get_unicode(handle, keycode);
    }

} // namespace

// Version that uses xkb_state_key_get_utf32
char32_t fs8::xkb::event2unicode(basic_state const& state_handle, key_event const event, handle_modifiers_type) noexcept {
    return event2unicode_impl(state_handle, event, [](xkb_state* handle, xkb_keycode_t const keycode) {
        return static_cast<char32_t>(xkb_state_key_get_utf32(handle, keycode));
    });
}

// Version that resolves via keysym → UTF-32
char32_t fs8::xkb::event2unicode(basic_state const& state_handle, key_event const event) noexcept {
    return event2unicode_impl(state_handle, event, [](xkb_state* handle, xkb_keycode_t const keycode) {
        xkb_keysym_t const sym = xkb_state_key_get_one_sym(handle, keycode);
        if (sym == XKB_KEY_NoSymbol) [[unlikely]] {
            return U'\0';
        }
        return static_cast<char32_t>(xkb_keysym_to_utf32(sym));
    });
}

std::u32string fs8::xkb::event2unicode(basic_state const& state_handle, std::span<event_type const> const events) {
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
