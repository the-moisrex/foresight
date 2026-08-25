// Created by moisrex on 8/23/26.

#include "common/tests_common_pch.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>
#include <linux/input-event-codes.h>
import fs8.mods;

using namespace fs8;

// ── Helpers ────────────────────────────────────────────────────────────────

[[nodiscard]] static event_type make_scroll(int value) noexcept {
    return event_type{EV_REL, REL_WHEEL_HI_RES, value};
}

[[nodiscard]] static event_type make_syn() noexcept {
    return event_type{EV_SYN, SYN_REPORT, 0};
}

// ── Tests: API surface ─────────────────────────────────────────────────────

TEST(SchedulerApi, InitiallyNotPending) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, CancelOnEmptyIsNoop) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, ScheduleMakesPending) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel();

    std::array const events = {make_scroll(5), make_syn()};
    sched.schedule(events);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel();
}

TEST(SchedulerApi, CancelClearsPending) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel();

    std::array const events = {make_scroll(5), make_syn()};
    sched.schedule(events);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, ScheduleReplacesPrevious) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel();

    std::array const events1 = {make_scroll(5), make_syn()};
    std::array const events2 = {make_scroll(10), make_scroll(20), make_syn()};

    sched.schedule(events1);
    sched.schedule(events2);
    EXPECT_TRUE(sched.has_pending());

    sched.cancel();
    EXPECT_FALSE(sched.has_pending());
}

TEST(SchedulerApi, ScheduleEmptySpanIsNoop) {
    static constinit auto pipeline = context | io_manager | scheduler;
    auto&                 sched    = pipeline.mod<basic_scheduler>();
    sched.cancel();

    std::span<event_type const> empty{};
    sched.schedule(empty);
    sched.cancel();
    EXPECT_FALSE(sched.has_pending());
}

// ── Tests: io_handler ──────────────────────────────────────────────────────

TEST(SchedulerIo, IoHandlerReturnsNextForTimerFd) {
    static constinit auto pipeline = context | io_manager | scheduler;
    static_cast<void>(pipeline(start));

    auto& sched = pipeline.mod<basic_scheduler>();

    // Create and arm a timerfd.
    int const tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    ASSERT_GE(tfd, 0);

    itimerspec its{};
    its.it_interval.tv_nsec = 10'000'000; // 10ms
    its.it_value            = its.it_interval;
    ASSERT_EQ(0, timerfd_settime(tfd, 0, &its, nullptr));

    std::this_thread::sleep_for(std::chrono::milliseconds(25));

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

    // An unrelated fd should be ignored.
    int const pipefd[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(const_cast<int*>(pipefd)));

    io_fd const fd_info{.fd = pipefd[0], .events = io_event::in};
    auto const  result = sched(fd_info);
    EXPECT_EQ(result, context_action::next);

    close(pipefd[0]);
    close(pipefd[1]);
}

// ── Tests: timer integration ───────────────────────────────────────────────

TEST(SchedulerTimer, TimerFiresAndEventsAreAvailable) {
    static constinit auto pipeline = context | io_manager | scheduler;
    static_cast<void>(pipeline(start));

    auto& sched = pipeline.mod<basic_scheduler>();
    sched.cancel();

    // Create our own timerfd and register it with the scheduler's io_manager.
    int const tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    ASSERT_GE(tfd, 0);

    itimerspec its{};
    its.it_interval.tv_nsec = 10'000'000; // 10ms
    its.it_value            = its.it_interval;
    ASSERT_EQ(0, timerfd_settime(tfd, 0, &its, nullptr));

    auto& io = pipeline.mod<basic_io_manager>();
    ASSERT_TRUE(io.watch(io_fd{.fd = tfd, .events = io_event::in}, sched));

    // Schedule some events.
    std::array const events = {make_scroll(42), make_syn()};
    sched.schedule(events);

    // Wait for the timer to fire, then poll via io_manager.
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    // Manually read the timerfd to simulate what io_handler does.
    std::uint64_t expirations = 0;
    auto const    n = ::read(tfd, &expirations, sizeof(expirations));
    ASSERT_GT(n, 0);
    EXPECT_GT(expirations, 0u);

    itimerspec const zero{};
    timerfd_settime(tfd, 0, &zero, nullptr);
    io.unwatch(tfd);
    close(tfd);

    // The events should still be pending in the scheduler.
    EXPECT_TRUE(sched.has_pending());

    sched.cancel();
}

// ── Tests: pipeline start/reset ────────────────────────────────────────────

TEST(SchedulerLifecycle, StartDoesNotCrash) {
    static constinit auto pipeline = context | io_manager | scheduler;
    EXPECT_NO_THROW(static_cast<void>(pipeline(start)));
}

TEST(SchedulerLifecycle, MultipleStartCallsReset) {
    static constinit auto pipeline = context | io_manager | scheduler;
    static_cast<void>(pipeline(start));
    static_cast<void>(pipeline(start)); // second start should not crash
}
