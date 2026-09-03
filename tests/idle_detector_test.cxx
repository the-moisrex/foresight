// Created by moisrex on 9/2/26.

#include "common/tests_common_pch.hpp"

#include <chrono>
#include <cstdint>
#include <thread>
#include <unistd.h>
import fs8.mods;

using namespace fs8;
using namespace std::chrono_literals;

// ── Tests: repeat patterns (compile-time) ──────────────────────────────────

TEST(IdleRepeatPattern, OnceReturnsNegative) {
    constexpr idle_repeat::once r;
    EXPECT_EQ(r(1s).count(), -1);
    EXPECT_EQ(r(0ms).count(), -1);
    EXPECT_EQ(r(5s).count(), -1);
}

TEST(IdleRepeatPattern, ConsistentReturnsFixedPeriod) {
    constexpr idle_repeat::consistent<500'000> r; // 500ms
    EXPECT_EQ(r(1s).count(), 500'000);
    EXPECT_EQ(r(0ms).count(), 500'000);
    EXPECT_EQ(r(10s).count(), 500'000);
}

TEST(IdleRepeatPattern, ExponentialBackoff) {
    constexpr idle_repeat::exponential<1'000'000> r; // 1s base
    // At first fire (idle_duration == 1s): next = 1s * 2 - 1s = 1s
    EXPECT_EQ(r(1s).count(), 1'000'000);
    // At second fire (idle_duration == 2s): next = 2s * 2 - 1s = 3s
    EXPECT_EQ(r(2s).count(), 3'000'000);
    // At third fire (idle_duration == 4s): next = 4s * 2 - 1s = 7s
    EXPECT_EQ(r(4s).count(), 7'000'000);
}

TEST(IdleRepeatPattern, ExponentialNeverBelowBase) {
    constexpr idle_repeat::exponential<1'000'000> r;
    // Even with very small duration, result >= base
    auto const result = r(500ms);
    EXPECT_GE(result.count(), 1'000'000);
}

// ── Tests: idle_detector API ───────────────────────────────────────────────

TEST(IdleDetectorApi, DefaultIdlePeriodIsOneSecond) {
    static constinit auto pipeline = context | io_manager | idle_detector;
    auto&                 det      = pipeline.mod<basic_idle_detector<>>();
    EXPECT_EQ(det.idle_period().count(), 1'000'000);
}

TEST(IdleDetectorApi, SetIdlePeriodChangesValue) {
    static constinit auto pipeline = context | io_manager | idle_detector;
    auto&                 det      = pipeline.mod<basic_idle_detector<>>();
    det.set_idle_period(500ms);
    EXPECT_EQ(det.idle_period().count(), 500'000);
    det.set_idle_period(1s); // restore
}

TEST(IdleDetectorApi, ModifierConceptSatisfied) {
    static_assert(Modifier<basic_idle_detector<>>);
    static_assert(Modifier<basic_idle_detector<idle_repeat::consistent<500'000>>>);
    static_assert(Modifier<basic_idle_detector<idle_repeat::exponential<1'000'000>>>);
}

TEST(IdleDetectorApi, CompileTimeConfiguration) {
    constexpr basic_idle_detector<idle_repeat::consistent<250'000>> det{250ms};
    static_assert(det.idle_period() == 250ms);
}

// ── Tests: io_manager idle callback ────────────────────────────────────────

TEST(IOManagerIdle, CallbackFiresOnTimeout) {
    static constinit auto idle_pipeline = context | io_manager | idle_detector;
    auto&                 mgr           = idle_pipeline.mod<basic_io_manager>();
    mgr.clear();

    // Need at least one fd watched for poll() to actually block.
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    struct noop_handler {
        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            return context_action::next;
        }
    };

    noop_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    bool callback_fired = false;
    mgr.set_idle_callback([&](std::chrono::microseconds) noexcept {
        callback_fired = true;
    });
    mgr.set_idle_timeout(50ms);

    // poll() should block for ~50ms then time out, firing the callback.
    auto const result = mgr(load_event);
    EXPECT_EQ(result, context_action::next);
    EXPECT_TRUE(callback_fired);

    mgr.clear_idle_callback();
    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManagerIdle, ClearCallbackStopsFiring) {
    static constinit auto idle_pipeline = context | io_manager | idle_detector;
    auto&                 mgr           = idle_pipeline.mod<basic_io_manager>();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    struct noop_handler {
        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            return context_action::next;
        }
    };

    noop_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    bool callback_fired = false;
    mgr.set_idle_callback([&](std::chrono::microseconds) noexcept {
        callback_fired = true;
    });
    mgr.set_idle_timeout(50ms);
    mgr.clear_idle_callback();

    // After clearing callback, poll should time out but callback should not fire.
    auto const result = mgr(load_event);
    EXPECT_EQ(result, context_action::next);
    EXPECT_FALSE(callback_fired);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManagerIdle, ClearIdleTimeoutStopsFiring) {
    static constinit auto idle_pipeline = context | io_manager | idle_detector;
    auto&                 mgr           = idle_pipeline.mod<basic_io_manager>();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    struct noop_handler {
        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            return context_action::next;
        }
    };

    noop_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    bool callback_fired = false;
    mgr.set_idle_callback([&](std::chrono::microseconds) noexcept {
        callback_fired = true;
    });
    mgr.set_idle_timeout(50ms);
    mgr.clear_idle_timeout();

    // After clearing timeout, poll blocks indefinitely (no timeout = -1).
    // With no data written, load_event would block forever, so write to unblock.
    ASSERT_EQ(write(fds[1], "x", 1), 1);
    auto const result = mgr(load_event);
    EXPECT_EQ(result, context_action::next);
    EXPECT_FALSE(callback_fired);

    mgr.clear_idle_callback();
    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManagerIdle, ActivityResetsIdleClock) {
    static constinit auto idle_pipeline = context | io_manager | idle_detector;
    auto&                 mgr           = idle_pipeline.mod<basic_io_manager>();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    // Set a short idle timeout.
    mgr.set_idle_timeout(100ms);

    // Write data so poll returns immediately with activity.
    ASSERT_EQ(write(fds[1], "x", 1), 1);

    struct noop_handler {
        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            return context_action::next;
        }
    };

    noop_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    bool callback_fired = false;
    mgr.set_idle_callback([&](std::chrono::microseconds) noexcept {
        callback_fired = true;
    });

    auto const result = mgr(load_event);
    EXPECT_EQ(result, context_action::next);
    // Activity happened, so callback should NOT have fired.
    EXPECT_FALSE(callback_fired);

    mgr.clear_idle_callback();
    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManagerIdle, CallbackReceivesIdleDuration) {
    static constinit auto idle_pipeline = context | io_manager | idle_detector;
    auto&                 mgr           = idle_pipeline.mod<basic_io_manager>();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    struct noop_handler {
        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            return context_action::next;
        }
    };

    noop_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    std::chrono::microseconds received_duration{0};
    mgr.set_idle_callback([&](std::chrono::microseconds duration) noexcept {
        received_duration = duration;
    });
    mgr.set_idle_timeout(75ms);

    auto const result = mgr(load_event);
    EXPECT_EQ(result, context_action::next);
    EXPECT_EQ(received_duration.count(), 75'000);

    mgr.clear_idle_callback();
    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManagerIdle, StartClearsIdleCallback) {
    static constinit auto idle_pipeline = context | io_manager | idle_detector;
    auto&                 mgr           = idle_pipeline.mod<basic_io_manager>();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    struct noop_handler {
        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            return context_action::next;
        }
    };

    noop_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    bool callback_fired = false;
    mgr.set_idle_callback([&](std::chrono::microseconds) noexcept {
        callback_fired = true;
    });
    mgr.set_idle_timeout(10ms);

    // start should clear the idle callback.
    auto const result = mgr(start);
    EXPECT_EQ(result, context_action::next);

    // Trigger poll timeout — callback should NOT fire (was cleared by start).
    static_cast<void>(mgr(load_event));
    EXPECT_FALSE(callback_fired);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

// ── Tests: repeat pattern concepts ─────────────────────────────────────────

TEST(IdleRepeatConcept, OnceSatisfiesPattern) {
    static_assert(idle_repeat::pattern<idle_repeat::once>);
}

TEST(IdleRepeatConcept, ConsistentSatisfiesPattern) {
    static_assert(idle_repeat::pattern<idle_repeat::consistent<100'000>>);
}

TEST(IdleRepeatConcept, ExponentialSatisfiesPattern) {
    static_assert(idle_repeat::pattern<idle_repeat::exponential<1'000'000>>);
}
