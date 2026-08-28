// Created by moisrex on 8/23/26.

#include "common/tests_common_pch.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>
import fs8.mods;

using namespace fs8;
using namespace std::chrono_literals;

// ── Helpers ────────────────────────────────────────────────────────────────

[[nodiscard]] static event_type make_scroll(int value) noexcept {
    return event_type{EV_REL, REL_WHEEL_HI_RES, value};
}

/// Persistent buffer for simple_tick.
static event_type simple_buf; // NOLINT(*-global-variables)

static basic_scheduler::tick_result simple_tick(void* /*data*/) noexcept {
    simple_buf = make_scroll(42);
    return {
      std::span<event_type const>{&simple_buf, 1},
      100ms
    };
}

/// Persistent buffer for finite_tick.
static event_type finite_buf; // NOLINT(*-global-variables)

static basic_scheduler::tick_result finite_tick(void* data) noexcept {
    auto& count = *static_cast<int*>(data);
    if (count >= 3) {
        return {};
    }
    finite_buf = make_scroll(count + 1);
    ++count;
    return {
      std::span<event_type const>{&finite_buf, 1},
      1ms
    };
}

/// Persistent buffer for second_tick.
static event_type second_buf; // NOLINT(*-global-variables)

static basic_scheduler::tick_result second_tick(void* /*data*/) noexcept {
    second_buf = make_scroll(99);
    return {
      std::span<event_type const>{&second_buf, 1},
      100ms
    };
}

/// Persistent buffer for multi_event_tick.
static std::array<event_type, 3> multi_buf; // NOLINT(*-global-variables)

static basic_scheduler::tick_result multi_event_tick(void* data) noexcept {
    auto& count = *static_cast<int*>(data);
    if (count >= 2) {
        return {};
    }
    multi_buf[0] = make_scroll(10);
    multi_buf[1] = make_scroll(20);
    multi_buf[2] = syn();
    ++count;
    return {
      std::span<event_type const>{multi_buf.data(), 3},
      1ms
    };
}

/// Tick that emits one event then cancels itself.
static event_type cancel_buf; // NOLINT(*-global-variables)

static basic_scheduler::tick_result cancel_after_one_tick(void* /*data*/) noexcept {
    cancel_buf = make_scroll(77);
    return {
      std::span<event_type const>{&cancel_buf, 1},
      basic_scheduler::cancel_tick
    };
}

// ── Tests: scheduler API ───────────────────────────────────────────────────

TEST(SchedulerApi, InitiallyNotPending) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, CancelAllOnEmptyIsNoop) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, ScheduleReturnsHandle) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    auto const handle = sched.schedule(simple_tick, nullptr, 1ms);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel_all();
}

TEST(SchedulerApi, CancelHandleRemovesSingleTick) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    auto const h1 = sched.schedule(simple_tick, nullptr, 1ms);
    auto const h2 = sched.schedule(second_tick, nullptr, 1ms);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel(h1);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel(h2);
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, CancelAllClearsAllTicks) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    sched.schedule(simple_tick, nullptr, 1ms);
    sched.schedule(second_tick, nullptr, 1ms);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel_all();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, FiniteTickDeactivatesWhenDone) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    int counter = 0;
    sched.schedule(finite_tick, &counter, 1ms);

    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(5ms);
        event_type event;
        auto const result = sched(event, next_event);
        EXPECT_EQ(result, context_action::next);
        EXPECT_EQ(event.value(), i + 1);
    }

    std::this_thread::sleep_for(5ms);
    event_type event;
    auto const result = sched(event, next_event);
    EXPECT_EQ(result, context_action::drop_event);
    EXPECT_FALSE(sched.has_pending());

    sched.cancel_all();
}

TEST(SchedulerApi, MultipleTicksFireIndependently) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    sched.schedule(simple_tick, nullptr, 1ms);
    sched.schedule(second_tick, nullptr, 1ms);

    std::this_thread::sleep_for(10ms);

    std::array<event_type, 10> events;
    std::size_t                count = 0;
    for (int i = 0; i < 10; ++i) {
        event_type event;
        auto const result = sched(event, next_event);
        if (result == context_action::next) {
            events[count++] = event;
        } else {
            break;
        }
    }

    EXPECT_GE(count, 2u);

    bool found_42 = false;
    bool found_99 = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (events[i].value() == 42) {
            found_42 = true;
        }
        if (events[i].value() == 99) {
            found_99 = true;
        }
    }
    EXPECT_TRUE(found_42);
    EXPECT_TRUE(found_99);

    sched.cancel_all();
}

TEST(SchedulerApi, MultipleEventsPerTick) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    int counter = 0;
    sched.schedule(multi_event_tick, &counter, 1ms);

    std::this_thread::sleep_for(5ms);

    std::array<event_type, 10> events;
    std::size_t                count = 0;
    for (int i = 0; i < 10; ++i) {
        event_type event;
        auto const result = sched(event, next_event);
        if (result == context_action::next) {
            events[count++] = event;
        } else {
            break;
        }
    }

    EXPECT_GE(count, 3u);
    EXPECT_EQ(events[0].value(), 10);
    EXPECT_EQ(events[1].value(), 20);
    EXPECT_EQ(events[2].type(), EV_SYN);

    sched.cancel_all();
}

TEST(SchedulerApi, CancelTickEmitsEventThenRemoves) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel_all();

    sched.schedule(cancel_after_one_tick, nullptr, 1ms);

    std::this_thread::sleep_for(5ms);

    // The tick should fire once, emit its event, then be removed.
    event_type event;
    auto const result = sched(event, next_event);
    EXPECT_EQ(result, context_action::next);
    EXPECT_EQ(event.value(), 77);
    EXPECT_EQ(event.source(), device_id::scheduler);

    // Next call finds no ticks.
    auto const result2 = sched(event, next_event);
    EXPECT_EQ(result2, context_action::drop_event);
    EXPECT_FALSE(sched.has_pending());

    sched.cancel_all();
}

// ── Tests: io_handler ──────────────────────────────────────────────────────

TEST(SchedulerIo, IoHandlerReturnsNextForTimerFd) {
    static constinit auto pipeline = context | io_manager | scheduler;
    static_cast<void>(pipeline(start));

    auto& sched = pipeline.mod<basic_scheduler>();

    int const tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    ASSERT_GE(tfd, 0);

    itimerspec its{};
    its.it_interval.tv_nsec = 10'000'000;
    its.it_value            = its.it_interval;
    ASSERT_EQ(0, timerfd_settime(tfd, 0, &its, nullptr));

    std::this_thread::sleep_for(25ms);

    io_fd const fd_info{.fd = tfd, .events = io_event::in};
    auto const  result = sched(fd_info);
    EXPECT_EQ(result, context_action::next);

    itimerspec const zero{};
    timerfd_settime(tfd, 0, &zero, nullptr);
    close(tfd);
}

TEST(SchedulerIo, IoHandlerIgnoresUnrelatedFd) {
    static constinit auto pipeline = context | io_manager | scheduler;
    static_cast<void>(pipeline(start));

    auto& sched = pipeline.mod<basic_scheduler>();

    int const pipefd[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(const_cast<int*>(pipefd)));

    io_fd const fd_info{.fd = pipefd[0], .events = io_event::in};
    auto const  result = sched(fd_info);
    EXPECT_EQ(result, context_action::next);

    close(pipefd[0]);
    close(pipefd[1]);
}

// ── Tests: pipeline lifecycle ──────────────────────────────────────────────

TEST(SchedulerLifecycle, StartDoesNotCrash) {
    static constinit auto pipeline = context | io_manager | scheduler;
    EXPECT_NO_THROW(static_cast<void>(pipeline(start)));
}

TEST(SchedulerLifecycle, MultipleStartCallsReset) {
    static constinit auto pipeline = context | io_manager | scheduler;
    static_cast<void>(pipeline(start));
    static_cast<void>(pipeline(start));
}
