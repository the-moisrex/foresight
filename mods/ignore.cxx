// Created by moisrex on 6/29/25.

module;
#include <linux/input-event-codes.h>
module foresight.mods.ignore;

using foresight::basic_ignore_abs;
using foresight::context_action;
using foresight::event_type;

context_action basic_ignore_abs::operator()(event_type const& event) const noexcept {
    using enum context_action;
    auto const type = event.type();
    auto const code = event.code();

    if (EV_ABS == type) {
        return ignore_event;
    }
    if (EV_KEY == type) {
        switch (code) {
            case BTN_TOOL_RUBBER:
            case BTN_TOOL_BRUSH:
            case BTN_TOOL_PEN: return ignore_event;
            default: break;
        }
    }
    return next;
}
