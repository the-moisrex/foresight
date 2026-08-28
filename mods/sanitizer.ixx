// Created by moisrex on 8/21/26.

module;
#include <chrono>
#include <concepts>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <type_traits>
#include <utility>
export module fs8.mods:sanitizer;
import fs8.context;
import fs8.event;
import fs8.pimpl;
import fs8.traits;
import fs8.utils;
import :input_manager;
import :drop;
import fs8.log;

export namespace fs8 {

    /// Describes what kind of problem the sanitizer detected for an event.
    enum struct [[nodiscard]] sanitizer_issue : std::uint8_t {
        none,               ///< event is clean
        adjacent_syn,       ///< duplicate SYN_REPORT (no data since last syn)
        orphan_release,     ///< key release without a prior press
        orphan_repeat,      ///< key repeat without a prior press
        double_press,       ///< key press while already pressed
        late_syn,           ///< SYN_REPORT arrived after a long gap with no data
        out_of_resolution,  ///< pen ABS value outside device bounds
        big_jump,           ///< mouse movement exceeding threshold
        missing_syn_time,   ///< data events long after last SYN_REPORT
        missing_syn_count,  ///< too many data events without SYN_REPORT
        missing_syn_travel, ///< mouse travel exceeding threshold without SYN_REPORT
    };

    [[nodiscard]] std::string_view to_string(sanitizer_issue issue) noexcept;

    struct [[nodiscard]] event_sanitizer_state : pimpl_idiom<event_sanitizer_state> {
        using pimpl_idiom::pimpl_idiom;

        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

        constexpr event_sanitizer_state() noexcept = default;

        void ensure_initialized() noexcept;
        void seed_pen_bounds(value_type x_min, value_type x_max, value_type y_min, value_type y_max) noexcept;

        [[nodiscard]] msec_type last_issue_duration() const noexcept;

        struct [[nodiscard]] config {
            bool       check_adjacent_syns      = true;
            bool       check_orphan_releases    = true;
            bool       check_orphan_repeats     = true;
            bool       check_double_presses     = true;
            bool       check_late_syns          = true;
            bool       check_pen_resolution     = true;
            bool       check_big_jumps          = true;
            bool       check_missing_syn_time   = true;
            bool       check_missing_syn_count  = true;
            bool       check_missing_syn_travel = true;
            value_type big_jump_threshold       = 50;
            msec_type  late_syn_threshold{100'000};
            msec_type  missing_syn_time_threshold{100'000};
            value_type missing_syn_count_threshold  = 20;
            value_type missing_syn_travel_threshold = 500;
        };

        [[nodiscard]] sanitizer_issue check(event_type const& event, config const& cfg) noexcept;
        void                          update(event_type const& event) noexcept;

      private:
        msec_type last_issue_duration_{0};
    };

    /**
     * Sanitize input events by detecting and filtering problematic events.
     *
     * Checks:
     *  - Key state enforcement (orphan releases/repeats, double presses)
     *  - Adjacent SYN_REPORTs (duplicate sync with no data in between)
     *  - Late SYN_REPORTs (sync after a long gap with no data events)
     *  - Out-of-resolution pen locations (ABS values outside device bounds)
     *  - Big mouse jumps (REL movement exceeding a pixel threshold)
     *  - Missing SYN framing (time/count/travel thresholds)
     *
     * The callback receives (event, issue). For two-argument callbacks it is
     * called for every event; for one-argument callbacks (like `log`) it is
     * only called for problematic events.
     *
     * Usage:
     *   event_sanitizer                           // filter bad events, no callback
     *   event_sanitizer[log["sanitized"]]         // log and filter problematic events
     *   event_sanitizer.diagnostics()[log["bad"]] // log problematic events, keep them
     *   event_diagnostics[log["bad"]]             // diagnostics-only shorthand
     *   event_sanitizer[my_callback, .threshold(100)]
     */
    template <typename Callback = basic_noop>
    struct [[nodiscard]] basic_event_sanitizer : consteval_copyable {
        template <typename>
        friend struct basic_event_sanitizer;

        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

      private:
        event_sanitizer_state          state{};
        event_sanitizer_state::config  cfg{};
        bool                           log_events_good  = false;
        bool                           diagnostics_only = false;
        [[no_unique_address]] Callback cb;

        basic_enforce_key_state      key_state_enforcer{};
        basic_drop_late_syn          late_syn_checker{};
        basic_drop_pen_out_of_bounds pen_bounds_checker{};
        basic_drop_missing_syns      missing_syn_checker{};

        /// Dispatch the callback based on its arity.
        constexpr void invoke_callback(event_type const& event, sanitizer_issue const issue) noexcept {
            if constexpr (std::invocable<Callback&, event_type const&, sanitizer_issue, event_sanitizer_state::msec_type>) {
                if (issue == sanitizer_issue::none && !log_events_good) {
                    return;
                }
                cb(event, issue, state.last_issue_duration());
            } else if constexpr (std::invocable<Callback&, event_type const&, sanitizer_issue>) {
                if (issue == sanitizer_issue::none && !log_events_good) {
                    return;
                }
                cb(event, issue);
            } else if constexpr (std::invocable<Callback&, event_type const&>) {
                if (issue != sanitizer_issue::none) {
                    cb(event);
                }
            }
        }

      public:
        constexpr basic_event_sanitizer() noexcept = default;

        explicit consteval basic_event_sanitizer(Callback inp_cb) noexcept : cb{std::move(inp_cb)} {}

      private:
        consteval basic_event_sanitizer(
          Callback                      inp_cb,
          event_sanitizer_state::config inp_cfg,
          bool                          inp_log_events_good,
          bool                          inp_diagnostics_only) noexcept
          : cfg{inp_cfg},
            log_events_good{inp_log_events_good},
            diagnostics_only{inp_diagnostics_only},
            cb{std::move(inp_cb)} {}

      public:
        // --- Config builders ---

        consteval basic_event_sanitizer threshold(value_type const t) const noexcept {
            auto copy                   = *this;
            copy.cfg.big_jump_threshold = t;
            return copy;
        }

        consteval basic_event_sanitizer late_syn(msec_type const d) const noexcept {
            auto copy                   = *this;
            copy.cfg.late_syn_threshold = d;
            return copy;
        }

        consteval basic_event_sanitizer only_bad() const noexcept {
            auto copy            = *this;
            copy.log_events_good = false;
            return copy;
        }

        consteval basic_event_sanitizer all_events() const noexcept {
            auto copy            = *this;
            copy.log_events_good = true;
            return copy;
        }

        /// Report sanitizer issues without filtering or rewriting events.
        consteval basic_event_sanitizer diagnostics(bool const v = true) const noexcept {
            auto copy             = *this;
            copy.diagnostics_only = v;
            return copy;
        }

        /// Re-enable sanitizing after diagnostics-only mode was selected.
        consteval basic_event_sanitizer sanitize() const noexcept {
            return diagnostics(false);
        }

        consteval basic_event_sanitizer adjacent_syns(bool const v = true) const noexcept {
            auto copy                    = *this;
            copy.cfg.check_adjacent_syns = v;
            return copy;
        }

        consteval basic_event_sanitizer orphan_releases(bool const v = true) const noexcept {
            auto copy                      = *this;
            copy.cfg.check_orphan_releases = v;
            return copy;
        }

        consteval basic_event_sanitizer orphan_repeats(bool const v = true) const noexcept {
            auto copy                     = *this;
            copy.cfg.check_orphan_repeats = v;
            return copy;
        }

        consteval basic_event_sanitizer double_presses(bool const v = true) const noexcept {
            auto copy                     = *this;
            copy.cfg.check_double_presses = v;
            return copy;
        }

        consteval basic_event_sanitizer late_syns(bool const v = true) const noexcept {
            auto copy                = *this;
            copy.cfg.check_late_syns = v;
            return copy;
        }

        consteval basic_event_sanitizer pen_resolution(bool const v = true) const noexcept {
            auto copy                     = *this;
            copy.cfg.check_pen_resolution = v;
            return copy;
        }

        consteval basic_event_sanitizer big_jumps(bool const v = true) const noexcept {
            auto copy                = *this;
            copy.cfg.check_big_jumps = v;
            return copy;
        }

        consteval basic_event_sanitizer missing_syn_time(bool const v = true) const noexcept {
            auto copy                       = *this;
            copy.cfg.check_missing_syn_time = v;
            return copy;
        }

        consteval basic_event_sanitizer missing_syn_count(bool const v = true) const noexcept {
            auto copy                        = *this;
            copy.cfg.check_missing_syn_count = v;
            return copy;
        }

        consteval basic_event_sanitizer missing_syn_travel(bool const v = true) const noexcept {
            auto copy                         = *this;
            copy.cfg.check_missing_syn_travel = v;
            return copy;
        }

        consteval basic_event_sanitizer missing_syn_time_threshold(msec_type const d) const noexcept {
            auto copy                           = *this;
            copy.cfg.missing_syn_time_threshold = d;
            return copy;
        }

        consteval basic_event_sanitizer missing_syn_count_threshold(value_type const n) const noexcept {
            auto copy                            = *this;
            copy.cfg.missing_syn_count_threshold = n;
            return copy;
        }

        consteval basic_event_sanitizer missing_syn_travel_threshold(value_type const d) const noexcept {
            auto copy                             = *this;
            copy.cfg.missing_syn_travel_threshold = d;
            return copy;
        }

        // --- Compile-time factory ---

        template <typename C>
            requires(!std::same_as<std::remove_cvref_t<C>, get_variables_tag>)
        consteval auto operator[](C&& inp_cb) const noexcept {
            return basic_event_sanitizer<std::remove_cvref_t<C>>{
              std::forward<C>(inp_cb),
              cfg,
              log_events_good,
              diagnostics_only,
            };
        }

        // --- Start: seed pen bounds from input_manager ---

        template <Context CtxT>
            requires has_mod<basic_input_manager, CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            state.ensure_initialized();
            key_state_enforcer(start);
            return pen_bounds_checker(ctx, start);
        }

        // --- Main handler ---

        context_action operator()(event_type const& event) noexcept {
            using enum context_action;

            // 1. Key state enforcement (always updates state; filtering respects config)
            if (event.type() == EV_KEY) {
                auto const key_action = key_state_enforcer(event);
                if (key_action == drop_event) [[unlikely]] {
                    auto const issue = determine_key_issue(event);
                    bool const should_filter =
                      (issue == sanitizer_issue::orphan_release && cfg.check_orphan_releases)
                      || (issue == sanitizer_issue::orphan_repeat && cfg.check_orphan_repeats)
                      || (issue == sanitizer_issue::double_press && cfg.check_double_presses);
                    invoke_callback(event, issue);
                    if (!diagnostics_only && should_filter) {
                        return drop_event;
                    }
                }
            }

            // 2. Late SYN
            if (cfg.check_late_syns) {
                if (late_syn_checker(event) == drop_event) [[unlikely]] {
                    invoke_callback(event, sanitizer_issue::late_syn);
                    if (!diagnostics_only) {
                        return drop_event;
                    }
                }
            }

            // 3. Pen bounds
            if (cfg.check_pen_resolution) {
                if (pen_bounds_checker(event) == drop_event) [[unlikely]] {
                    invoke_callback(event, sanitizer_issue::out_of_resolution);
                    if (!diagnostics_only) {
                        return drop_event;
                    }
                }
            }

            // 4. Missing SYN
            if (cfg.check_missing_syn_time || cfg.check_missing_syn_count || cfg.check_missing_syn_travel) {
                if (missing_syn_checker(event) == drop_event) [[unlikely]] {
                    auto const issue = determine_missing_syn_issue(event);
                    invoke_callback(event, issue);
                    if (!diagnostics_only) {
                        return drop_event;
                    }
                }
            }

            // 5. Existing checks: adjacent SYN, big jumps (delegated to state)
            auto const issue = state.check(event, cfg);
            invoke_callback(event, issue);
            state.update(event);
            if (diagnostics_only) {
                return next;
            }
            return issue == sanitizer_issue::none ? next : drop_event;
        }

      private:
        /// Map a rejected key event to the specific sanitizer issue.
        [[nodiscard]] sanitizer_issue determine_key_issue(event_type const& event) const noexcept {
            using enum sanitizer_issue;
            switch (event.value()) {
                case 0: return orphan_release;
                case 2: return orphan_repeat;
                case 1: return double_press;
                default: return none;
            }
        }

        /// Map a rejected event from `drop_missing_syns` to the specific issue.
        [[nodiscard]] sanitizer_issue determine_missing_syn_issue(event_type const& event) const noexcept {
            using enum sanitizer_issue;
            if (event.type() == EV_SYN) {
                return none; // should not happen
            }
            // We can't distinguish which threshold fired inside drop_missing_syns,
            // so report the most relevant one based on event type.
            if (is_mouse_movement(event)) {
                return missing_syn_travel;
            }
            return missing_syn_time;
        }
    };

    constexpr struct [[nodiscard]] basic_log_diagnostics : basic_log {
        void operator()(event_type const& event, sanitizer_issue issue, std::chrono::microseconds duration) const noexcept;
    } log_diagnostics;

    constexpr basic_event_sanitizer<> event_sanitizer;
    constexpr auto                    event_diagnostics = event_sanitizer.diagnostics()[log_diagnostics];

} // namespace fs8
