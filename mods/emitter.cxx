// Created by moisrex on 6/29/25.

module;
module foresight.mods.emitter;

using fs8::basic_replace_code;
using fs8::event_type;
using fs8::user_event;

void basic_replace_code::operator()(event_type& event) const noexcept {
    if (event.is_of(find_type, find_code)) {
        event.set(rep_type, rep_code);
    }
}
