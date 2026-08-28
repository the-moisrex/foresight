module;
#include <algorithm>
#include <array>
#include <cassert>
#include <linux/input-event-codes.h>
#include <span>
module fs8.mods;
using fs8::basic_keys_status;
using fs8::basic_led_status;

bool basic_keys_status::is_pressed(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::all_of(key_codes, [this](code_type const code) noexcept {
        assert(code < KEY_MAX);
        return this->btns.test(static_cast<std::size_t>(code));
    });
}

bool basic_keys_status::is_released(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::all_of(key_codes, [this](code_type const code) noexcept {
        assert(code < KEY_MAX);
        return !this->btns.test(static_cast<std::size_t>(code));
    });
}

bool basic_keys_status::is_pressed_any(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::any_of(key_codes, [this](code_type const code) noexcept {
        assert(code < KEY_MAX);
        return this->btns.test(static_cast<std::size_t>(code));
    });
}

bool basic_keys_status::is_released_any(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::any_of(key_codes, [this](code_type const code) noexcept {
        assert(code < KEY_MAX);
        return !this->btns.test(static_cast<std::size_t>(code));
    });
}

void basic_keys_status::operator()(event_type const& event) noexcept {
    if (event.type() != EV_KEY) {
        return;
    }
    if (event.code() >= KEY_MAX) [[unlikely]] {
        return;
    }
    if (event.value() != 0) {
        btns.set(static_cast<std::size_t>(event.code()));
    } else {
        btns.reset(static_cast<std::size_t>(event.code()));
    }
}

void basic_keys_status::seed_from_device(evdev const& dev) noexcept {
    if (!dev.has_event_type(EV_KEY)) {
        return;
    }
    std::array<unsigned char, key_bitmap_bytes> bitmap{};
    if (!query_key_state(dev, bitmap)) {
        return;
    }
    for (std::size_t byte = 0; byte < key_bitmap_bytes; ++byte) {
        auto const b = bitmap[byte];
        if (b == 0) {
            continue;
        }
        for (int bit = 0; bit < 8; ++bit) {
            if (b & (1 << bit)) {
                btns.set(byte * 8 + bit);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// LEDs ////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

bool basic_led_status::is_on(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::all_of(key_codes, [this](code_type const code) {
        assert(code < LED_MAX);
        return this->leds.at(code) != 0;
    });
}

bool basic_led_status::is_off(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::all_of(key_codes, [this](code_type const code) {
        assert(code < LED_MAX);
        return this->leds.at(code) == 0;
    });
}

void basic_led_status::operator()(event_type const& event) noexcept {
    if (event.type() == EV_LED) {
        if (event.code() >= LED_MAX) [[unlikely]] {
            // Just in case
            return;
        }
        // log("LED event: code={} value={}", event.code(), event.value());
        this->leds.at(event.code()) = event.value();
    }
}
