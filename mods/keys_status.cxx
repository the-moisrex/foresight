module;
#include <linux/input-event-codes.h>
#include <span>
module foresight.mods.keys_status;
using foresight::basic_keys_status;
using foresight::basic_led_status;

bool basic_keys_status::is_pressed(std::span<code_type const> const key_codes) const noexcept {
    for (auto const code : key_codes) {
        if (code >= KEY_MAX || this->btns.at(code) == 0) {
            return false;
        }
    }
    return true;
}

bool basic_keys_status::is_released(std::span<code_type const> const key_codes) const noexcept {
    for (auto const code : key_codes) {
        if (code >= KEY_MAX || this->btns.at(code) != 0) {
            return false;
        }
    }
    return true;
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

bool foresight::basic_led_status::is_on(std::span<code_type const> const key_codes) const noexcept {
    for (auto const code : key_codes) {
        if (code >= LED_MAX || this->leds.at(code) == 0) {
            return false;
        }
    }
    return true;
}

bool foresight::basic_led_status::is_off(std::span<code_type const> const key_codes) const noexcept {
    for (auto const code : key_codes) {
        if (code >= LED_MAX || this->leds.at(code) != 0) {
            return false;
        }
    }
    return true;
}

void foresight::basic_led_status::operator()(event_type const& event) noexcept {
    if (event.type() != EV_LED) {
        return;
    }
    if (event.code() >= LED_MAX) [[unlikely]] {
        // Just in case
        return;
    }
    this->leds.at(event.code()) = event.value();
}
