// Created by moisrex on 6/29/25.

module;
module foresight.mods.emitter;

using foresight::basic_replace_code;
using foresight::event_type;
using foresight::user_event;

void basic_replace_code::operator()(event_type& event) const noexcept {
    if (event.is_of(find_type, find_code)) {
        event.set(rep_type, rep_code);
    }
}
