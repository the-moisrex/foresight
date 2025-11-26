module;
#include <algorithm>
#include <cassert>
#include <linux/input-event-codes.h>
#include <span>
module foresight.mods.keys_status;
using fs8::basic_keys_status;
using fs8::basic_led_status;

bool basic_keys_status::is_pressed(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::all_of(key_codes, [this](code_type const code) {
        assert(code < KEY_MAX);
        return this->btns.at(code) != 0;
    });
}

bool basic_keys_status::is_released(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::all_of(key_codes, [this](code_type const code) {
        assert(code < KEY_MAX);
        return this->btns.at(code) == 0;
    });
}

bool basic_keys_status::is_pressed_any(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::any_of(key_codes, [this](code_type const code) {
        assert(code < KEY_MAX);
        return this->btns.at(code) != 0;
    });
}

bool basic_keys_status::is_released_any(std::span<code_type const> const key_codes) const noexcept {
    return std::ranges::any_of(key_codes, [this](code_type const code) {
        assert(code < KEY_MAX);
        return this->btns.at(code) == 0;
    });
}

void basic_keys_status::operator()(event_type const& event) noexcept {
    if (event.type() != EV_KEY) {
        return;
    }
    if (event.code() >= KEY_MAX) [[unlikely]] {
        // Just in case
        return;
    }
    this->btns.at(event.code()) = event.value();
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
    if (event.type() != EV_LED) {
        return;
    }
    if (event.code() >= LED_MAX) [[unlikely]] {
        // Just in case
        return;
    }
    this->leds.at(event.code()) = event.value();
}
