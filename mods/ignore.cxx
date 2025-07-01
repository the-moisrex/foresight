// Created by moisrex on 6/29/25.

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
module foresight.mods.ignore;

using foresight::basic_ignore_abs;
using foresight::context_action;
using foresight::event_type;
using foresight::ignore_big_jumps_type;
using foresight::ignore_init_moves_type;

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

context_action ignore_big_jumps_type::operator()(event_type const& event) const noexcept {
    using enum context_action;
    if (is_mouse_movement(event) && std::abs(event.value()) > threshold) {
        return ignore_event;
    }
    return next;
}

context_action ignore_init_moves_type::operator()(event_type const& event) noexcept {
    using enum context_action;
    if (event.type() == EV_KEY && event.code() == BTN_LEFT) {
        init_distance    = 0;
        is_left_btn_down = event.value() == 1;
        return next;
    }
    if (is_left_btn_down && is_mouse_movement(event)) {
        init_distance           += event.value(); // no need for abs
        auto const      ev_time  = event.time();
        msec_type const now_time{std::chrono::seconds{ev_time.tv_sec} + msec_type{ev_time.tv_usec}};

        if (std::abs(init_distance) < threshold && (now_time - last_moved) >= time_threshold) {
            return ignore_event;
        }
        last_moved       = now_time;
        is_left_btn_down = false;
    }
    return next;
}
