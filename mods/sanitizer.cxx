// Created by moisrex on 8/21/26.

module;
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>
module fs8.mods;

import fs8.event;

namespace fs8 {

    template <>
    struct pimpl_idiom<event_sanitizer_state>::impl {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;
        using msec_type  = std::chrono::microseconds;

        bool      was_syn            = false;
        bool      any_data_since_syn = false;
        msec_type last_syn_time{0};

        value_type data_events_since_syn = 0;
        value_type travel_since_syn      = 0;

        static constexpr std::size_t max_tracked_keys = 64;
        code_type                    pressed_keys[max_tracked_keys]{};
        std::size_t                  num_pressed = 0;

        bool       has_pen_bounds = false;
        value_type pen_x_min      = 0;
        value_type pen_x_max      = 0;
        value_type pen_y_min      = 0;
        value_type pen_y_max      = 0;

        [[nodiscard]] bool is_key_pressed(code_type const code) const noexcept {
            for (std::size_t i = 0; i < num_pressed; ++i) {
                if (pressed_keys[i] == code) {
                    return true;
                }
            }
            return false;
        }

        void track_press(code_type const code) noexcept {
            if (!is_key_pressed(code) && num_pressed < max_tracked_keys) {
                pressed_keys[num_pressed++] = code;
            }
        }

        void track_release(code_type const code) noexcept {
            for (std::size_t i = 0; i < num_pressed; ++i) {
                if (pressed_keys[i] == code) {
                    pressed_keys[i] = pressed_keys[--num_pressed];
                    return;
                }
            }
        }
    };

    std::string_view to_string(sanitizer_issue const issue) noexcept {
        using enum sanitizer_issue;
        switch (issue) {
            case none: return {"event is clean"};
            case adjacent_syn: return {"duplicate SYN_REPORT (no data since last syn)"};
            case orphan_release: return {"key release without a prior press"};
            case late_syn: return {"SYN_REPORT arrived after a long gap with no data"};
            case out_of_resolution: return {"pen ABS value outside device bounds"};
            case big_jump: return {"mouse movement exceeding threshold"};
            case missing_syn_time: return {"data events long after last SYN_REPORT"};
            case missing_syn_count: return {"too many data events without SYN_REPORT"};
            case missing_syn_travel: return {"mouse travel exceeding threshold without SYN_REPORT"};
            default: break;
        }
        return {"<unknown>"};
    }

    void event_sanitizer_state::ensure_initialized() noexcept {
        if (pimpl.get() == nullptr) {
            init_impl();
        }
    }

    std::chrono::microseconds event_sanitizer_state::last_issue_duration() const noexcept {
        return last_issue_duration_;
    }

    void event_sanitizer_state::seed_pen_bounds(
      value_type const x_min,
      value_type const x_max,
      value_type const y_min,
      value_type const y_max) noexcept {
        ensure_initialized();
        pimpl->pen_x_min      = x_min;
        pimpl->pen_x_max      = x_max;
        pimpl->pen_y_min      = y_min;
        pimpl->pen_y_max      = y_max;
        pimpl->has_pen_bounds = true;
    }

    sanitizer_issue event_sanitizer_state::check(event_type const& event, config const& cfg) noexcept {
        ensure_initialized();
        using enum sanitizer_issue;

        if (cfg.check_adjacent_syns && event.type() == EV_SYN && event.code() == SYN_REPORT && pimpl->was_syn) [[unlikely]] {
            return adjacent_syn;
        }

        if (cfg.check_orphan_releases && event.type() == EV_KEY && event.value() == 0 && !pimpl->is_key_pressed(event.code())) [[unlikely]]
        {
            return orphan_release;
        }

        bool const is_syn_report = event.type() == EV_SYN && event.code() == SYN_REPORT;
        bool const is_late_syn =
          is_syn_report
          && !pimpl->any_data_since_syn
          && pimpl->was_syn
          && event.micro_time()
          - pimpl->last_syn_time
          >= cfg.late_syn_threshold;
        if (cfg.check_late_syns && is_late_syn) [[unlikely]] {
            return late_syn;
        }

        if (cfg.check_pen_resolution && pimpl->has_pen_bounds && event.type() == EV_ABS) {
            switch (event.code()) {
                case ABS_X:
                    if (event.value() < pimpl->pen_x_min || event.value() > pimpl->pen_x_max) [[unlikely]] {
                        return out_of_resolution;
                    }
                    break;
                case ABS_Y:
                    if (event.value() < pimpl->pen_y_min || event.value() > pimpl->pen_y_max) [[unlikely]] {
                        return out_of_resolution;
                    }
                    break;
                default: break;
            }
        }

        if (cfg.check_big_jumps && is_mouse_movement(event) && std::abs(event.value()) > cfg.big_jump_threshold) [[unlikely]] {
            return big_jump;
        }

        if (!is_syn_report) {
            if (cfg.check_missing_syn_time
                && pimpl->data_events_since_syn
                > 0
                && event.micro_time()
                - pimpl->last_syn_time
                >= cfg.missing_syn_time_threshold) [[unlikely]]
            {
                last_issue_duration_ = event.micro_time() - pimpl->last_syn_time;
                return missing_syn_time;
            }
            if (cfg.check_missing_syn_count && pimpl->data_events_since_syn >= cfg.missing_syn_count_threshold) [[unlikely]] {
                return missing_syn_count;
            }
            if (cfg.check_missing_syn_travel
                && is_mouse_movement(event)
                && pimpl->travel_since_syn
                + std::abs(event.value())
                >= cfg.missing_syn_travel_threshold) [[unlikely]]
            {
                return missing_syn_travel;
            }
        }

        return none;
    }

    void event_sanitizer_state::update(event_type const& event) noexcept {
        ensure_initialized();
        bool const is_syn = event.type() == EV_SYN && event.code() == SYN_REPORT;

        if (!is_syn) {
            pimpl->any_data_since_syn = true;
            if (pimpl->data_events_since_syn == 0) {
                // First data event after SYN: advance last_syn_time to the current
                // event's timestamp so subsequent events in the same frame compare
                // against the frame start rather than the previous frame's SYN_REPORT.
                // This prevents false positives for multi-event frames (e.g. tablet
                // REL_X + REL_Y) after normal idle gaps.
                pimpl->last_syn_time = event.micro_time();
            }
            ++pimpl->data_events_since_syn;
            if (is_mouse_movement(event)) {
                pimpl->travel_since_syn += std::abs(event.value());
            }
        }

        if (event.type() == EV_KEY) {
            switch (event.value()) {
                case 1: pimpl->track_press(event.code()); break;
                case 0: pimpl->track_release(event.code()); break;
                default: break;
            }
        }

        if (is_syn) {
            pimpl->was_syn               = true;
            pimpl->any_data_since_syn    = false;
            pimpl->last_syn_time         = event.micro_time();
            pimpl->data_events_since_syn = 0;
            pimpl->travel_since_syn      = 0;
            return;
        }

        pimpl->was_syn = false;
    }

    void basic_log_diagnostics::operator()(
      event_type const&               event,
      sanitizer_issue const           issue,
      std::chrono::microseconds const duration) const noexcept {
        if (duration.count() > 0) {
            auto const us = duration.count();
            if (us >= 1000) {
                log("{} ({}ms) - {} {} {}",
                    to_string(issue),
                    static_cast<double>(us) / 1000.0,
                    event.type_name(),
                    event.code_name(),
                    event.value());
            } else {
                log("{} ({}us) - {} {} {}", to_string(issue), us, event.type_name(), event.code_name(), event.value());
            }
        } else {
            log("{} - {} {} {}", to_string(issue), event.type_name(), event.code_name(), event.value());
        }
    }

} // namespace fs8
