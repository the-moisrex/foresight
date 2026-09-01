// Created by moisrex on 8/23/26.

module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sys/timerfd.h>
#include <unistd.h>
module fs8.mods;
import fs8.log;

using fs8::basic_io_manager;
using fs8::basic_scheduler;
using fs8::context_action;
using fs8::event_type;
using fs8::io_event;
using fs8::io_fd;

using steady_clock = std::chrono::steady_clock;

template <>
struct fs8::pimpl_idiom<basic_scheduler>::impl {
    static constexpr std::size_t max_ticks = 8;

    struct tick_entry {
        basic_scheduler::tick_fn    callback = nullptr;
        void*                       data     = nullptr;
        std::chrono::microseconds   interval{};
        steady_clock::time_point    next_fire{};
        std::span<event_type const> remaining{};
    };

    int                               timer_fd = -1;
    std::array<tick_entry, max_ticks> ticks{};
    basic_io_manager*                 io = nullptr;

    [[nodiscard]] bool has_active() const noexcept {
        return std::ranges::any_of(ticks, [](auto const& t) {
            return t.callback != nullptr;
        });
    }

    /// Arm or disarm the timerfd based on the earliest next_fire time.
    void rearm_timer() noexcept {
        if (timer_fd < 0) {
            return;
        }

        auto const now      = steady_clock::now();
        auto       earliest = steady_clock::time_point::max();

        for (auto const& t : ticks) {
            if (t.callback != nullptr && t.next_fire < earliest) {
                earliest = t.next_fire;
            }
        }

        if (earliest == steady_clock::time_point::max()) {
            itimerspec const zero{};
            if (::timerfd_settime(timer_fd, 0, &zero, nullptr) < 0) [[unlikely]] {
                log("scheduler: timerfd disarm failed: {}", std::strerror(errno));
            }
            return;
        }

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(earliest - now);
        if (duration.count() <= 0) {
            duration = std::chrono::microseconds{1};
        }

        auto const us = static_cast<std::uint64_t>(duration.count());
        itimerspec its{};
        its.it_value.tv_sec  = static_cast<time_t>(us / 1'000'000);
        its.it_value.tv_nsec = static_cast<long>((us % 1'000'000) * 1000);
        if (::timerfd_settime(timer_fd, 0, &its, nullptr) < 0) [[unlikely]] {
            log("scheduler: timerfd_settime failed: {}", std::strerror(errno));
        }
    }
};

context_action basic_scheduler::do_start(basic_io_manager& io) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    pimpl->io = &io;

    if (pimpl->timer_fd < 0) {
        pimpl->timer_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (pimpl->timer_fd < 0) [[unlikely]] {
            log("scheduler: timerfd_create failed: {}", std::strerror(errno));
            return exit;
        }
    }

    if (!io.is_watched(pimpl->timer_fd)) {
        if (!io.watch(io_fd{.fd = pimpl->timer_fd, .events = io_event::in}, *this)) [[unlikely]] {
            log("scheduler: failed to register timer fd with io_manager");
            return exit;
        }
    }
    return next;
} catch (...) {
    return context_action::exit;
}

context_action basic_scheduler::operator()(io_fd const& fd) noexcept try {
    using enum context_action;
    if (fd.fd != pimpl->timer_fd) [[unlikely]] {
        return next;
    }
    std::uint64_t expirations = 0;
    auto const    n           = ::read(pimpl->timer_fd, &expirations, sizeof(expirations));
    if (n < 0 && errno != EAGAIN) [[unlikely]] {
        log("scheduler: timerfd read failed: {}", std::strerror(errno));
        return exit;
    }
    return next;
} catch (...) {
    return context_action::next;
}

context_action basic_scheduler::operator()(event_type& event, special_event const& tag) noexcept {
    using enum context_action;
    if (tag.code != next_event.code) {
        return drop_event;
    }

    if (pimpl.get() == nullptr) [[unlikely]] {
        return drop_event;
    }

    auto const now = steady_clock::now();

    std::size_t tick_idx = 0;
    for (auto& tick : pimpl->ticks) {
        // Emit buffered events from the last callback call.
        // This must come before the callback == nullptr check so that
        // remaining events from a cancel_tick response are still drained.
        if (!tick.remaining.empty()) {
            event          = tick.remaining.front();
            tick.remaining = tick.remaining.subspan(1);
            event.source(sid(scheduler, static_cast<std::uint16_t>(tick_idx)));
            return next;
        }

        if (tick.callback == nullptr) {
            ++tick_idx;
            continue;
        }

        // Check if this tick is due.
        if (now < tick.next_fire) {
            ++tick_idx;
            continue;
        }

        // Invoke the callback.
        auto const result = tick.callback(tick.data);

        if (result.events.empty()) {
            // Tick done — remove it.
            tick.callback  = nullptr;
            tick.data      = nullptr;
            tick.remaining = {};
            pimpl->rearm_timer();
            continue;
        }

        // Emit the first event, buffer the rest.
        event          = result.events.front();
        tick.remaining = result.events.subspan(1);

        // Schedule the next invocation or cancel after this batch.
        if (result.next_timeout == cancel_tick) {
            // Last batch — remove after the remaining events are emitted.
            // We mark the callback as nullptr but keep remaining to drain.
            // Actually we need a flag... let's use a special approach:
            // set interval to 0 and next_fire to max so it drains remaining
            // but never fires the callback again.
            tick.callback = nullptr;
            tick.data     = nullptr;
        } else if (result.next_timeout.count() == 0) {
            // Immediate — call again on the next next_event.
            tick.next_fire = now;
        } else {
            tick.next_fire = now + result.next_timeout;
        }

        event.source(sid(scheduler, static_cast<std::uint16_t>(tick_idx)));
        pimpl->rearm_timer();
        ++tick_idx;
        return next;
    }

    return drop_event;
}

basic_scheduler::tick_handle
basic_scheduler::schedule(tick_fn const fn, void* const data, std::chrono::microseconds const interval) noexcept {
    if (pimpl.get() == nullptr) {
        init_impl();
    }

    for (std::size_t i = 0; i < decltype(pimpl)::element_type::max_ticks; ++i) {
        if (pimpl->ticks[i].callback == nullptr) {
            auto const now            = steady_clock::now();
            pimpl->ticks[i].callback  = fn;
            pimpl->ticks[i].data      = data;
            pimpl->ticks[i].interval  = interval;
            pimpl->ticks[i].next_fire = now + interval;
            pimpl->ticks[i].remaining = {};
            pimpl->rearm_timer();
            return {.index = i};
        }
    }

    log("scheduler: no free tick slots (max={})", decltype(pimpl)::element_type::max_ticks);
    return {.index = decltype(pimpl)::element_type::max_ticks};
}

void basic_scheduler::cancel(tick_handle const h) noexcept {
    if (pimpl.get() == nullptr) {
        return;
    }
    if (h.index >= decltype(pimpl)::element_type::max_ticks) {
        return;
    }
    auto& tick     = pimpl->ticks[h.index];
    tick.callback  = nullptr;
    tick.data      = nullptr;
    tick.remaining = {};
    pimpl->rearm_timer();
}

void basic_scheduler::cancel_all() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    for (auto& tick : pimpl->ticks) {
        tick.callback  = nullptr;
        tick.data      = nullptr;
        tick.remaining = {};
    }
    pimpl->rearm_timer();
}

bool basic_scheduler::has_pending() const noexcept {
    return pimpl.get() != nullptr && pimpl->has_active();
}
