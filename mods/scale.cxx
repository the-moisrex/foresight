// Created by moisrex on 8/20/26.

module;
#include <cstdint>
#include <linux/input-event-codes.h>
module fs8.mods;

using fs8::basic_scale_move;
using fs8::basic_scale_pen;
using fs8::context_action;
using fs8::event_type;

context_action basic_scale_pen::operator()(event_type& event) noexcept {
    using enum context_action;

    switch (event.hash()) {
        case hashed(EV_ABS, ABS_X): {
            float const  scaled    = static_cast<float>(event.value()) * factor_ + x_epsilon_;
            auto const   truncated = static_cast<value_type>(scaled);
            x_epsilon_             = scaled - static_cast<float>(truncated);
            event.value(truncated);
            return next;
        }
        case hashed(EV_ABS, ABS_Y): {
            float const  scaled    = static_cast<float>(event.value()) * factor_ + y_epsilon_;
            auto const   truncated = static_cast<value_type>(scaled);
            y_epsilon_             = scaled - static_cast<float>(truncated);
            event.value(truncated);
            return next;
        }
        // Reset epsilon on tool change so a new stroke starts fresh.
        case hashed(EV_KEY, BTN_TOOL_PEN):
        case hashed(EV_KEY, BTN_TOOL_RUBBER):
        case hashed(EV_KEY, BTN_TOOL_BRUSH):
        case hashed(EV_KEY, BTN_TOOL_PENCIL):
        case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
        case hashed(EV_KEY, BTN_TOOL_FINGER):
        case hashed(EV_KEY, BTN_TOOL_MOUSE):
        case hashed(EV_KEY, BTN_TOOL_LENS):
            x_epsilon_ = 0.0f;
            y_epsilon_ = 0.0f;
            return next;
        default: return next;
    }
}

context_action basic_scale_move::operator()(event_type& event) noexcept {
    using enum context_action;

    switch (event.hash()) {
        case hashed(EV_REL, REL_X): {
            float const  scaled    = static_cast<float>(event.value()) * factor_ + x_epsilon_;
            auto const   truncated = static_cast<value_type>(scaled);
            x_epsilon_             = scaled - static_cast<float>(truncated);
            event.value(truncated);
            return next;
        }
        case hashed(EV_REL, REL_Y): {
            float const  scaled    = static_cast<float>(event.value()) * factor_ + y_epsilon_;
            auto const   truncated = static_cast<value_type>(scaled);
            y_epsilon_             = scaled - static_cast<float>(truncated);
            event.value(truncated);
            return next;
        }
        default: return next;
    }
}
