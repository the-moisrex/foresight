// Created by moisrex on 6/29/25.

module;
#include <array>
#include <linux/input-event-codes.h>
module foresight.mods.emitter;

using foresight::event_type;
using foresight::user_event;

constexpr void foresight::basic_replace::operator()(event_type& event) const noexcept {
    if (event.is_of(find_type, find_code)) {
        event.set(rep_type, rep_code);
    }
}
