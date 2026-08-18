// Created by moisrex on 8/17/26.

module;
#include <linux/uinput.h>
#include <unistd.h>
module fs8.mods.inout;
import fs8.event;
import fs8.context;

using fs8::context_action;
using fs8::event_type;

bool fs8::basic_output::emit(event_type const& event) const noexcept {
    return write(file_descriptor, &event.native(), sizeof(input_event)) == sizeof(input_event);
}

bool fs8::basic_output::emit(input_event const& event) const noexcept {
    return write(file_descriptor, &event, sizeof(input_event)) == sizeof(input_event);
}

bool fs8::basic_output::emit(ev_type const type, code_type const code, value_type const value) const noexcept {
    input_event event{};
    gettimeofday(&event.time, nullptr);
    event.type  = type;
    event.code  = code;
    event.value = value;
    return write(file_descriptor, &event, sizeof(input_event)) == sizeof(input_event);
}

bool fs8::basic_output::emit_syn() const noexcept {
    return emit(EV_SYN, SYN_REPORT, 0);
}

bool fs8::basic_output::operator()(event_type& event) const noexcept {
    return write(file_descriptor, &event.native(), sizeof(input_event)) == sizeof(input_event);
}

context_action fs8::basic_from_input::operator()(event_type& event, load_event_tag) const noexcept {
    using enum context_action;
    auto const res = read(file_descriptor, &event.native(), sizeof(input_event));
    if (res == 0) [[unlikely]] {
        return exit;
    }
    if (res != sizeof(input_event)) [[unlikely]] {
        return ignore_event;
    }
    event.source(device_id::stdin);
    return next;
}
