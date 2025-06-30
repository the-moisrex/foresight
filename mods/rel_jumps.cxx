//
// Created by moisrex on 6/29/25.
//

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
module foresight.mods.rel_jumps;

using foresight::context_action;
using foresight::ignore_big_jumps_type;
using foresight::ignore_init_moves_type;

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
