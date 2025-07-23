// Created by moisrex on 6/29/25.

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
#include <utility>
module foresight.mods.ignore;

using foresight::basic_ignore_abs;
using foresight::basic_ignore_big_jumps;
using foresight::basic_ignore_fast_repeats;
using foresight::basic_ignore_init_moves;
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

context_action basic_ignore_big_jumps::operator()(event_type const& event) const noexcept {
    using enum context_action;
    if (is_mouse_movement(event) && std::abs(event.value()) > threshold) {
        return ignore_event;
    }
    return next;
}

context_action basic_ignore_init_moves::operator()(event_type const& event) noexcept {
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

context_action foresight::basic_ignore_mouse_moves::operator()(event_type const& event) noexcept {
    using enum context_action;
    return is_mouse_movement(event) ? ignore_event : next;
}

context_action basic_ignore_fast_repeats::operator()(event_type const& event) noexcept {
    using enum context_action;

    if (!event.is(code)) {
        return next;
    }

    auto const now = event.micro_time();
    if (now - std::exchange(last_emitted, now) < time_threshold) [[unlikely]] {
        return ignore_event;
    }

    return next;
}

context_action foresight::basic_ignore_start_moves::operator()(event_type const& event) noexcept {
    using enum context_action;

    if (!is_mouse_movement(event)) {
        return next;
    }


    auto const now = event.micro_time();
    if (now - std::exchange(last_emitted, now) >= rest_time) {
        emitted_count = 0;
    }

    if (++emitted_count < emit_threshold) [[unlikely]] {
        return ignore_event;
    }

    return next;
}

context_action foresight::basic_ignore_adjacent_repeats::operator()(event_type const& event) noexcept {
    using enum context_action;
    bool const found_asked = event.is_of(asked_event);
    return std::exchange(is_found, found_asked) && found_asked ? ignore_event : next;
}
