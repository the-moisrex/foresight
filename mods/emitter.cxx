// Created by moisrex on 6/29/25.

module;
#include <array>
#include <linux/input-event-codes.h>
module foresight.mods.emitter;

using foresight::event_type;
using foresight::user_event;

constexpr std::array<user_event, 2> foresight::keyup(event_type::code_type const code) noexcept {
    return std::array{
      user_event{EV_KEY, code, 0},
      static_cast<user_event>(syn())
    };
}

constexpr std::array<user_event, 2> foresight::keydown(event_type::code_type const code) noexcept {
    return std::array{
      user_event{EV_KEY, code, 1},
      static_cast<user_event>(syn())
    };
}

constexpr std::array<user_event, 4> foresight::keypress(event_type::code_type const code) noexcept {
    return std::array{
      user_event{EV_KEY, code, 1},
      static_cast<user_event>(syn()),
      user_event{EV_KEY, code, 0},
      static_cast<user_event>(syn()),
    };
}

constexpr void foresight::basic_replace::operator()(event_type& event) const noexcept {
    if (event.is_of(find_type, find_code)) {
        event.set(rep_type, rep_code);
    }
}
