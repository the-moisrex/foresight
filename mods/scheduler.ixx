// Created by moisrex on 8/23/26.

module;
#include <chrono>
#include <cstddef>
#include <linux/input-event-codes.h>
#include <span>
#include <sys/timerfd.h>
#include <unistd.h>
export module fs8.mods:scheduler;
import fs8.context;
import fs8.pimpl;
import fs8.event;
import :io_manager;

namespace fs8 {

    /**
     * Timer-based event scheduler.
     *
     * Uses `timerfd` registered with `io_manager` to drive tick callbacks.
     * Each tick callback produces a span of events and a timeout for the
     * next invocation.
     *
     * The scheduler does NOT buffer events itself — it calls the callback
     * when a tick is due and emits the returned events immediately.  If
     * the callback returns multiple events, the scheduler stores the
     * remaining span until the next `next_event` call.
     *
     * Supports:
     *  - `schedule(fn, data, interval)` — register a callback, returns handle
     *  - `cancel(handle)` — remove a single callback
     *  - `cancel_all()` — remove all callbacks
     *  - `has_pending()` — whether any callbacks are registered
     */
    export constexpr struct [[nodiscard]] basic_scheduler : pimpl_idiom<basic_scheduler> {
        using pimpl_idiom::pimpl_idiom;

        using clock = std::chrono::steady_clock;

        /// Result returned by a tick callback.
        struct tick_result {
            std::span<event_type const> events;
            std::chrono::microseconds   next_timeout = std::chrono::microseconds{0};
        };

        /// Sentinel value: remove the callback when returned as next_timeout.
        static constexpr auto cancel_tick = std::chrono::microseconds{-1};

        /// Tick callback: produces events for one tick.  Returns a span of
        /// events (may be empty to signal completion) and the delay before
        /// the next invocation.
        using tick_fn = tick_result (*)(void* data) noexcept;

        /// Handle returned by schedule(); used for cancel.
        struct [[nodiscard]] tick_handle {
            std::size_t index = 0;
        };

        /// Register a tick callback.  The callback is first invoked after
        /// `interval` has elapsed.  Returns a handle for later cancel.
        tick_handle schedule(tick_fn fn, void* data, std::chrono::microseconds interval) noexcept;

        /// Cancel a single tick.
        void cancel(tick_handle h) noexcept;

        /// Cancel all ticks and disarm the timer.
        void cancel_all() noexcept;

        /// Whether any ticks are currently active.
        [[nodiscard]] bool has_pending() const noexcept;

        /// Pipeline mod: forward every event (momentum handles velocity
        /// tracking in its own operator).
        template <Context CtxT>
        context_action operator()(CtxT& /*ctx*/) noexcept {
            return context_action::next;
        }

        /// io_handler: drain timer expirations.
        context_action operator()(io_fd const& fd) noexcept;

        /// next_event provider: call the first due callback, emit its events.
        context_action operator()(event_type& event, special_event const& tag) noexcept;

        /// Register the timer fd with io_manager.
        template <Context CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            if constexpr (has_mod<basic_io_manager, CtxT>) {
                return do_start(ctx.mod(io_manager));
            } else {
                return context_action::next;
            }
        }

      private:
        context_action do_start(basic_io_manager& io) noexcept;
    } scheduler;

    static_assert(Modifier<basic_scheduler>);

} // namespace fs8
