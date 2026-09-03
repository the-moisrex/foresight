// Created by moisrex on 9/2/26.

module;
#include <chrono>
#include <concepts>
#include <cstdint>
#include <utility>
export module fs8.mods:idle_detector;
import fs8.context;
import fs8.event;
import fs8.traits;
import :io_manager;

export namespace fs8 {

    // ── Repeat patterns ──────────────────────────────────────────────────────
    //  A repeat pattern is a constexpr callable: microseconds → microseconds.
    //  Given the current idle duration, it returns the next poll timeout.
    //  Return -1µs to stop repeating.

    namespace idle_repeat {

        template <typename T>
        concept pattern = std::invocable<T const&, std::chrono::microseconds>
                          && std::convertible_to<std::invoke_result_t<T const&, std::chrono::microseconds>, std::chrono::microseconds>;

        /// Fire once when idle begins; do not repeat.
        struct [[nodiscard]] once {
            [[nodiscard]] constexpr std::chrono::microseconds operator()(std::chrono::microseconds) const noexcept {
                return std::chrono::microseconds{-1};
            }
        };

        /// Fire every `PeriodUs` microseconds while idle (constant interval).
        template <std::int64_t PeriodUs>
        struct [[nodiscard]] consistent {
            [[nodiscard]] constexpr std::chrono::microseconds operator()(std::chrono::microseconds) const noexcept {
                return std::chrono::microseconds{PeriodUs};
            }
        };

        /// Exponential back-off: the interval doubles each cycle.
        /// `BaseUs` is the initial idle threshold in microseconds; subsequent
        /// intervals grow as base, 2·base, 4·base, …
        template <std::int64_t BaseUs>
        struct [[nodiscard]] exponential {
            [[nodiscard]] constexpr std::chrono::microseconds operator()(std::chrono::microseconds idle_duration) const noexcept {
                auto const next_interval = idle_duration * 2 - std::chrono::microseconds{BaseUs};
                return std::max(next_interval, std::chrono::microseconds{BaseUs});
            }
        };

    } // namespace idle_repeat

    static_assert(idle_repeat::pattern<idle_repeat::once>);
    static_assert(idle_repeat::pattern<idle_repeat::consistent<500'000>>);
    static_assert(idle_repeat::pattern<idle_repeat::exponential<1'000'000>>);

    // ── basic_idle_detector ──────────────────────────────────────────────────

    /**
     * Detect pipeline idle (no input events for a configurable period) and
     * broadcast an `idle` special event to all mods.
     *
     * Relies on `io_manager` for the actual poll timeout.  The idle event's
     * `.value` carries the idle duration in microseconds so downstream mods
     * can decide their own thresholds.
     *
     * Pipeline form:
     * ```cpp
     * auto pipeline = context | io_manager | idle_detector | ...;
     * // or with a repeat pattern:
     * auto pipeline = context | io_manager
     *     | idle_detector[std::chrono::seconds{1}, idle_repeat::consistent<500'000>>
     *     | ...;
     * ```
     */
    template <idle_repeat::pattern RepeatT = idle_repeat::once>
    struct [[nodiscard]] basic_idle_detector : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::chrono::microseconds     idle_period_{std::chrono::seconds{1}};
        [[no_unique_address]] RepeatT repeat_;

      public:
        constexpr explicit basic_idle_detector(std::chrono::microseconds const period = std::chrono::seconds{1},
                                               RepeatT                         repeat = {}) noexcept
          : idle_period_{period},
            repeat_{repeat} {}

        /// Compile-time configuration: switch the repeat pattern.
        template <idle_repeat::pattern NewRepeat>
        [[nodiscard]] consteval auto operator[](std::chrono::microseconds const period, NewRepeat r) const noexcept {
            return basic_idle_detector<NewRepeat>{period, r};
        }

        /// Change the idle threshold at runtime.
        void set_idle_period(std::chrono::microseconds const period) noexcept {
            idle_period_ = period;
        }

        [[nodiscard]] constexpr std::chrono::microseconds idle_period() const noexcept {
            return idle_period_;
        }

        // ── Pipeline mod interface ───────────────────────────────────────────

        /// Handle start: configure io_manager timeout and register idle callback.
        template <ContextWith<basic_io_manager> CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;

            auto& io = ctx.mod(io_manager);
            switch (tag.code) {
                case start.code: {
                    io.set_idle_timeout(idle_period_);
                    io.set_idle_callback([&](std::chrono::microseconds) noexcept {
                        ctx.broadcast(idle);

                        // Re-arm for the next idle cycle unless the pattern is fire-once.
                        auto const next_timeout = repeat_(idle_period_);
                        if (next_timeout.count() > 0) {
                            io.set_idle_timeout(next_timeout);
                        }
                    });
                    return next;
                }
                default: break;
            }
            return drop_event;
        }

        /// Pass through regular events (no-op).
        context_action operator()() noexcept {
            return context_action::next;
        }
    };

    /// Default instance: fire once after 1 second of idle.
    constexpr basic_idle_detector<> idle_detector;

    static_assert(Modifier<basic_idle_detector<>>);

} // namespace fs8
