// Created by moisrex on 8/23/26.

module;
#include <array>
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
     * Uses `timerfd` registered with `io_manager` to emit events on a
     * configurable periodic interval.  Events are queued and drained as a
     * `next_event` provider — the same pattern as `intercept`.
     *
     * Supports:
     *  - `schedule(events)` — arm the timer and queue events
     *  - `cancel()` — disarm the timer and clear the queue
     *  - `has_pending()` — whether events are queued
     */
    export constexpr struct [[nodiscard]] basic_scheduler : pimpl_idiom<basic_scheduler> {
        using pimpl_idiom::pimpl_idiom;

        /// Arm the timer and queue events for emission.
        /// Replaces any previously scheduled events.
        void schedule(std::span<event_type const> events, int interval_ms = 16) noexcept;

        /// Disarm the timer and clear the queue.
        void cancel() noexcept;

        /// Whether there are events waiting to be emitted.
        [[nodiscard]] bool has_pending() const noexcept;

        /// Register the timer fd with io_manager.
        template <Context CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            return do_start(ctx.mod(io_manager));
        }

        /// io_handler: drain timer expirations, set ready flag.
        context_action operator()(io_fd const& fd) noexcept;

        /// next_event provider: pop one queued event, else ignore_event.
        context_action operator()(event_type& event, next_event_tag) noexcept;

      private:
        context_action do_start(basic_io_manager& io) noexcept;
    } scheduler;

    static_assert(Modifier<basic_scheduler>);

} // namespace fs8
