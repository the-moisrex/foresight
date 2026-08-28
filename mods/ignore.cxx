// Created by moisrex on 6/29/25.

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
#include <utility>
module fs8.mods;

using fs8::basic_enforce_key_state;
using fs8::basic_ignore_abs;
using fs8::basic_ignore_big_jumps;
using fs8::basic_ignore_fast_repeats;
using fs8::basic_ignore_init_moves;
using fs8::basic_ignore_late_syn;
using fs8::basic_ignore_missing_syns;
using fs8::basic_ignore_pen_out_of_bounds;
using fs8::context_action;
using fs8::event_type;

context_action basic_ignore_abs::operator()(event_type const& event) const noexcept {
    using enum context_action;
    return EV_ABS == event.type() ? ignore_event : next;
}

context_action fs8::basic_ignore_tablet::operator()(event_type const& event) const noexcept {
    using enum context_action;
    auto const type = event.type();
    auto const code = event.code();

    if (EV_ABS == type) {
        return ignore_event;
    }
    if (EV_KEY == type) {
        switch (code) {
            case BTN_TOOL_PEN:
            case BTN_TOOL_RUBBER:
            case BTN_TOOL_BRUSH:
            case BTN_TOOL_PENCIL:
            case BTN_TOOL_AIRBRUSH:
            case BTN_TOOL_FINGER:
            case BTN_TOOL_MOUSE:
            case BTN_TOOL_LENS: return ignore_event;
            default: break;
        }
    }
    return next;
}

context_action basic_ignore_big_jumps::operator()(event_type const& event) const noexcept {
    using enum context_action;
    if (is_mouse_movement(event) && std::abs(event.value()) > threshold) [[unlikely]] {
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

context_action fs8::basic_ignore_mouse_moves::operator()(event_type const& event) const noexcept {
    using enum context_action;
    return is_mouse_movement(event) ? ignore_event : next;
}

context_action fs8::basic_ignore_zero_mouse_moves::operator()(event_type const& event) const noexcept {
    using enum context_action;
    return is_mouse_movement(event) && event.value() == 0 ? ignore_event : next;
}

context_action fs8::basic_ignore_mouse_clicks::operator()(event_type const& event) const noexcept {
    using enum context_action;
    return is_mouse_clicks(event) ? ignore_event : next;
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

context_action fs8::basic_ignore_caps::operator()(event_type const& event) const noexcept {
    // todo: optimize this
    for (auto const cap : caps) {
        for (auto const code : cap.codes) {
            if (event.is(cap.type, code)) {
                return context_action::ignore_event;
            }
        }
    }
    return context_action::next;
}

void fs8::basic_ignore_start_moves::operator()(toggle_on_tag) noexcept {
    emitted_count = 0;
}

context_action fs8::basic_ignore_start_moves::operator()(event_type const& event) noexcept {
    using enum context_action;
    return is_mouse_movement(event) && ++emitted_count < emit_threshold ? ignore_event : next;
}

context_action fs8::basic_ignore_adjacent_repeats::operator()(event_type const& event) noexcept {
    using enum context_action;
    bool const found_asked = event.is_of(asked_event);
    return std::exchange(is_found, found_asked) && found_asked ? ignore_event : next;
}

// --- enforce_key_state ---

context_action basic_enforce_key_state::operator()(event_type const& event) noexcept {
    using enum context_action;
    if (event.type() != EV_KEY) {
        return next;
    }
    auto const code = static_cast<std::size_t>(event.code());
    if (code >= max_keys) [[unlikely]] {
        return next;
    }
    bool invalid = false;
    switch (event.value()) {
        case 1: // press
            if (pressed[code]) [[unlikely]] {
                invalid = true; // double press
            }
            pressed[code] = true;
            break;
        case 2: // repeat
            if (!pressed[code]) [[unlikely]] {
                invalid = true; // orphan repeat
            }
            break;
        case 0: // release
            if (!pressed[code]) [[unlikely]] {
                invalid = true; // orphan release
            }
            pressed[code] = false;
            break;
        default: break;
    }
    return invalid ? ignore_event : next;
}

// --- ignore_late_syn ---

context_action basic_ignore_late_syn::operator()(event_type const& event) noexcept {
    using enum context_action;
    bool const is_syn_report = event.type() == EV_SYN && event.code() == SYN_REPORT;

    if (!is_syn_report) {
        any_data_since_syn = true;
        return next;
    }

    // SYN_REPORT arrives
    bool const is_late =
      !any_data_since_syn && was_syn && event.micro_time() - last_syn_time >= threshold;

    was_syn            = true;
    any_data_since_syn = false;
    last_syn_time      = event.micro_time();

    return is_late ? ignore_event : next;
}

// --- ignore_pen_out_of_bounds ---

context_action fs8::basic_ignore_pen_out_of_bounds::operator()(event_type const& event) const noexcept {
    using enum context_action;
    if (!has_pen_bounds || event.type() != EV_ABS) {
        return next;
    }
    switch (event.code()) {
        case ABS_X:
            return (event.value() < pen_x_min || event.value() > pen_x_max) ? ignore_event : next;
        case ABS_Y:
            return (event.value() < pen_y_min || event.value() > pen_y_max) ? ignore_event : next;
        default: return next;
    }
}

// --- ignore_missing_syns ---

context_action basic_ignore_missing_syns::operator()(event_type const& event) noexcept {
    using enum context_action;
    bool const is_syn_report = event.type() == EV_SYN && event.code() == SYN_REPORT;

    if (!is_syn_report) {
        if (data_events_since_syn == 0) {
            last_syn_time = event.micro_time();
        }
        ++data_events_since_syn;
        if (is_mouse_movement(event)) {
            travel_since_syn += std::abs(event.value());
        }

        if (data_events_since_syn > 1 && event.micro_time() - last_syn_time >= time_threshold) [[unlikely]] {
            return ignore_event;
        }
        if (data_events_since_syn > count_threshold) [[unlikely]] {
            return ignore_event;
        }
        if (travel_since_syn >= travel_threshold) [[unlikely]] {
            return ignore_event;
        }
        return next;
    }

    // SYN_REPORT: reset counters
    last_syn_time         = event.micro_time();
    data_events_since_syn = 0;
    travel_since_syn      = 0;
    return next;
}
