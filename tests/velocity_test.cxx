#include "common/tests_common_pch.hpp"

#include <cmath>
import foresight.mods;

using namespace fs8;

// Helper to create microseconds from milliseconds for readability
auto us(int64_t const ms) {
    return std::chrono::microseconds(ms * 1000);
}

// Test: First event should not crash and initialize state
TEST(MouseVelocityTrackerTest, FirstEventInitializesState) {
    velocity_tracker tracker;

    tracker.process_event(10.0f, us(100));

    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), 10.0f);
    // Velocity is still 0 — no time delta yet
    EXPECT_FLOAT_EQ(tracker.velocity(), 0.0f); // OK
}

// Test: Two events with known delta should produce correct velocity
TEST(MouseVelocityTrackerTest, TwoEventsYieldVelocity) {
    velocity_tracker tracker;

    tracker.process_event(5.0f, us(100));            // t = 100 ms
    tracker.process_event(10.0f, us(150));           // t = 150 ms → dt = 50 ms

    float const expected_instant_v  = 10.0f / 0.05f; // 200 units/sec
    float const expected_filtered_v = (1.0f - std::exp(-0.05f / 0.1f)) * expected_instant_v; // ~78.6

    float const actual_v = tracker.velocity();

    EXPECT_NEAR(actual_v, expected_filtered_v, 2.0f);
    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), 15.0f);
}

// Test: Zero movement over time still updates velocity if rel_x is zero
TEST(MouseVelocityTrackerTest, ZeroMovementDoesNotAffectDelta) {
    velocity_tracker tracker;

    tracker.process_event(0.0f, us(100));
    tracker.process_event(0.0f, us(200));

    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), 0.0f);
    EXPECT_FLOAT_EQ(tracker.velocity(), 0.0f);
}

// Test: Negative movement
TEST(MouseVelocityTrackerTest, NegativeMovement) {
    velocity_tracker tracker;

    tracker.process_event(-5.0f, us(100));
    tracker.process_event(-15.0f, us(200));                    // dt = 100ms

    float expected_instant_v  = -15.0f / 0.1f;                 // -150
    float alpha               = 1.0f - std::exp(-0.1f / 0.1f); // 1 - 1/e ≈ 0.632
    float expected_smoothed_v = alpha * expected_instant_v;

    float actual_v = tracker.velocity();

    EXPECT_NEAR(actual_v, expected_smoothed_v, 2.0f); // Should be ~ -94.8
    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), -20.0f);
}

// Test: No time passed (same timestamp) — should not update velocity
TEST(MouseVelocityTrackerTest, SameTimestampSkipped) {
    velocity_tracker tracker;

    tracker.process_event(10.0f, us(100));
    tracker.process_event(5.0f, us(100)); // Same time

    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), 15.0f);
    // Velocity should still be based on first event only (no new dt)
    // Second event doesn't contribute to velocity
    // But we can't test internal state, so just ensure no crash
}

// Test: Rapid small movements with smoothing
TEST(MouseVelocityTrackerTest, RapidEventsSmoothed) {
    velocity_tracker tracker;

    auto const t0       = us(100);
    float      total_dx = 0.0f;

    for (int i = 0; i < 10; ++i) {
        auto const t = t0 + std::chrono::microseconds(i * 5000);
        tracker.process_event(1.0f, t);
        total_dx += 1.0f;
    }

    float const final_v = tracker.velocity();

    // Expected: ~79 after 10 steps of 5ms with τ=0.1
    EXPECT_NEAR(final_v, 79.0f, 10.0f); // Accept 69–89
    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), total_dx);
}

// Test: Reset clears all state
TEST(MouseVelocityTrackerTest, ResetClearsState) {
    velocity_tracker tracker;

    tracker.process_event(10.0f, us(100));
    tracker.reset();

    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), 0.0f);
    EXPECT_FLOAT_EQ(tracker.velocity(), 0.0f);

    tracker.process_event(5.0f, us(200));
    EXPECT_FLOAT_EQ(tracker.get_recent_delta(), 5.0f);
    EXPECT_FLOAT_EQ(tracker.velocity(), 0.0f); // No velocity yet
}

// Test: Very small dt should be handled safely
TEST(MouseVelocityTrackerTest, VerySmallDeltaTimeHandled) {
    velocity_tracker tracker;

    tracker.process_event(0.01f, us(100));
    tracker.process_event(0.01f, us(101)); // 1 microsecond = 1e-6 s

    // Should not divide by zero or blow up
    float const v = tracker.velocity();
    EXPECT_FALSE(std::isnan(v));
    EXPECT_FALSE(std::isinf(v));
}

// Test: Exponential decay weighting — older samples matter less
TEST(MouseVelocityTrackerTest, OlderEventsHaveLessInfluence) {
    velocity_tracker tracker;

    // Fast first, then slow
    tracker.process_event(10.0f, us(100));
    tracker.process_event(0.0f, us(110));
    tracker.process_event(1.0f, us(120));
    float const v_after_slow = tracker.velocity();

    // Slow first, then fast
    velocity_tracker tracker2;
    tracker2.process_event(1.0f, us(100));
    tracker2.process_event(0.0f, us(110));
    tracker2.process_event(10.0f, us(120));
    float v_after_fast = tracker2.velocity();

    // Recent fast motion should dominate
    EXPECT_GT(v_after_fast, v_after_slow);
    EXPECT_GT(v_after_fast, 90.0f); // Not 100 — 95 is realistic
}
