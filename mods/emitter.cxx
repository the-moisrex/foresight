// Created by moisrex on 6/29/25.

module;
module foresight.mods.emitter;

using fs8::basic_replace_code;
using fs8::event_type;

fs8::context_action fs8::basic_scheduled_emitter::operator()(event_type& event, next_event_tag) noexcept {
    using enum context_action;
    if (events.empty()) {
        return ignore_event;
    }

    event = events.front();
    event.reset_time();
    events = events.subspan(1); // pop out the first one
    return next;
}

void basic_replace_code::operator()(event_type& event) const noexcept {
    if (event.is_of(find_type, find_code)) {
        event.set(rep_type, rep_code);
    }
}
