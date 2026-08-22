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

export namespace fs8 {

    /// Describes what kind of problem the sanitizer detected for an event.
    enum struct [[nodiscard]] sanitizer_issue : std::uint8_t {
        none,              ///< event is clean
        adjacent_syn,      ///< duplicate SYN_REPORT (no data since last syn)
        orphan_release,    ///< key release without a prior press
        late_syn,          ///< SYN_REPORT arrived after a long gap with no data
        out_of_resolution, ///< pen ABS value outside device bounds
        big_jump,          ///< mouse movement exceeding threshold
    };

    struct [[nodiscard]] event_sanitizer_state : pimpl_idiom<event_sanitizer_state> {
        using pimpl_idiom::pimpl_idiom;

        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

        constexpr event_sanitizer_state() noexcept = default;

        void ensure_initialized() noexcept;
        void seed_pen_bounds(value_type x_min, value_type x_max, value_type y_min, value_type y_max) noexcept;

        struct [[nodiscard]] config {
            bool       check_adjacent_syns   = true;
            bool       check_orphan_releases = true;
            bool       check_late_syns       = true;
            bool       check_pen_resolution  = true;
            bool       check_big_jumps       = true;
            value_type big_jump_threshold    = 50;
            msec_type  late_syn_threshold{100'000};
        };

        [[nodiscard]] sanitizer_issue check(event_type const& event, config const& cfg) noexcept;
        void                          update(event_type const& event) noexcept;
    };

    /**
     * Sanitize input events by detecting and filtering problematic events.
     *
     * Checks:
     *  - Adjacent SYN_REPORTs (duplicate sync with no data in between)
     *  - Orphan key releases (EV_KEY release for a key that was never pressed)
     *  - Late SYN_REPORTs (sync after a long gap with no data events)
     *  - Out-of-resolution pen locations (ABS values outside device bounds)
     *  - Big mouse jumps (REL movement exceeding a pixel threshold)
     *
     * The callback receives (event, issue). For two-argument callbacks it is
     * called for every event; for one-argument callbacks (like `log`) it is
     * only called for problematic events.
     *
     * Usage:
     *   event_sanitizer                           // filter bad events, no callback
     *   event_sanitizer[log["sanitized"]]         // log problematic events
     *   event_sanitizer[my_callback, .threshold(100)]
     */
    template <typename Callback = basic_noop>
    struct [[nodiscard]] basic_event_sanitizer : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

      private:
        event_sanitizer_state          state{};
        event_sanitizer_state::config  cfg{};
        bool                           log_events_good = false;
        [[no_unique_address]] Callback cb;

        /// Dispatch the callback based on its arity.
        constexpr void invoke_callback(event_type const& event, sanitizer_issue const issue) noexcept {
            if constexpr (std::invocable<Callback&, event_type const&, sanitizer_issue>) {
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

        // --- Compile-time factory ---

        template <typename C>
            requires(!std::same_as<std::remove_cvref_t<C>, get_variables_tag>)
        consteval basic_event_sanitizer operator[](C&& inp_cb) const noexcept {
            auto copy = *this;
            copy.cb   = std::forward<C>(inp_cb);
            return copy;
        }

        // --- Start: seed pen resolution from input_manager ---

        template <Context CtxT>
            requires has_mod<basic_input_manager, CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            state.ensure_initialized();
            for (auto const& dev : ctx.mod(input_manager).devices()) {
                if (auto const* x = dev.abs_info(ABS_X); x != nullptr) {
                    if (auto const* y = dev.abs_info(ABS_Y); y != nullptr) {
                        state.seed_pen_bounds(x->minimum, x->maximum, y->minimum, y->maximum);
                        break;
                    }
                }
            }
            return context_action::next;
        }

        // --- Main handler ---

        context_action operator()(event_type const& event) noexcept {
            auto const issue = state.check(event, cfg);
            invoke_callback(event, issue);
            state.update(event);
            return issue == sanitizer_issue::none ? context_action::next : context_action::ignore_event;
        }
    };

    constexpr basic_event_sanitizer<> event_sanitizer;

} // namespace fs8
