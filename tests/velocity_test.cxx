#include "common/tests_common_pch.hpp"

#include <chrono>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <utility>
import fs8.mods;

using namespace fs8;

namespace {

    std::int64_t fixed_clock = 0; // NOLINT(*-global-variables)

    struct fixed_timeline {
        std::int64_t* usec = nullptr;

        context_action operator()(event_type& event) noexcept {
            auto const            t = std::exchange(*usec, *usec + 1000);
            event_type::time_type tv{};
            tv.tv_sec  = static_cast<decltype(tv.tv_sec)>(t / 1'000'000);
            tv.tv_usec = static_cast<decltype(tv.tv_usec)>(t % 1'000'000);
            event.time(tv);
            return context_action::next;
        }
    };

    std::vector<std::vector<user_event>> group_frames(std::span<event_type const> const events) {
        std::vector<std::vector<user_event>> frames;
        std::vector<user_event>              current;
        for (auto const& event : events) {
            current.push_back(static_cast<user_event>(event));
            if (event.is(EV_SYN, SYN_REPORT)) {
                frames.push_back(std::move(current));
                current.clear();
            }
        }
        if (!current.empty()) {
            frames.push_back(std::move(current));
        }
        return frames;
    }

    } // namespace

// ── Slow movement passes through without split_move decomposition ───────────

// With a very high threshold, velocity[split_move] should never delegate,
// so the output should contain simple REL+SYN frames (no unit decomposition).
TEST(VelocityTest, SlowMovementPassesThrough) {
    auto pipeline =
      context
      | emit_all[{
        // Slow: 1 unit per frame, 1ms apart = 1000 units/sec
        // But with threshold=10000, EMA velocity stays below threshold
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | velocity[10000.0f, split_move]
      | record;
    auto& col = pipeline.mod<basic_record>();

    fixed_clock = 0;
    pipeline();

    auto const frames = group_frames(col.events());
    // Each frame should be exactly 2 events: REL_X + SYN (no decomposition)
    for (auto const& frame : frames) {
        ASSERT_EQ(frame.size(), 2U);
        EXPECT_EQ(frame[0].type, EV_REL);
        EXPECT_EQ(frame[0].code, REL_X);
        EXPECT_EQ(frame[1].type, EV_SYN);
    }
}

// ── Fast movement delegates to split_move ────────────────────────────────────

// With default threshold (500), fast movement should trigger split_move.
// Velocity is computed on SYN and gates the NEXT frame: frame 1 seeds the
// timestamp, frame 2 computes velocity, frame 3 is delegated to split_move.
TEST(VelocityTest, FastMovementDelegatesToSplitMove) {
    auto pipeline =
      context
      | emit_all[{
        // Frame 1: big movement — seeds the EMA timestamp.
        {EV_REL,      REL_X, 50},
        {EV_REL,      REL_Y, 30},
        {EV_SYN, SYN_REPORT, 0},
        // Frame 2: another big movement — computes velocity (fast).
        {EV_REL,      REL_X, 80},
        {EV_REL,      REL_Y, 40},
        {EV_SYN, SYN_REPORT, 0},
        // Frame 3: velocity from frame 2 is above threshold → delegated to split_move.
        {EV_REL,      REL_X, 60},
        {EV_REL,      REL_Y, 20},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | velocity[split_move]
      | record;
    auto& col = pipeline.mod<basic_record>();

    fixed_clock = 0;
    pipeline();

    auto const frames = group_frames(col.events());
    // Frame 3 should be decomposed by split_move into many sub-frames.
    // Without decomposition: 3 input frames → 3 output frames.
    // With decomposition: frame 3 (REL_X=60, REL_Y=20, step=1) → 80 sub-frames,
    // each with 1 REL + 1 SYN = 2 events.
    // So total frames should be much more than 3.
    EXPECT_GT(frames.size(), 3U);
}

// ── First frame always passes through ────────────────────────────────────────

// Even with a huge first-frame movement, velocity[split_move] should not
// delegate because there's no velocity history yet.
TEST(VelocityTest, FirstFrameAlwaysPassesThrough) {
    auto pipeline =
      context
      | emit_all[{
        // Huge movement but it's the first frame
        {EV_REL,      REL_X, 1000},
        {EV_REL,      REL_Y, 500},
        {EV_SYN, SYN_REPORT, 0},
        // Small second frame — velocity from first frame seeds EMA
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | velocity[split_move]
      | record;
    auto& col = pipeline.mod<basic_record>();

    fixed_clock = 0;
    pipeline();

    auto const frames = group_frames(col.events());
    // First frame: exactly 3 events (REL_X + REL_Y + SYN) — no decomposition
    ASSERT_GE(frames.size(), 1U);
    EXPECT_EQ(frames[0].size(), 3U);
}

// ── Non-movement events pass through ────────────────────────────────────────

TEST(VelocityTest, NonMovementEventsPassThrough) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY,      KEY_A, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 5},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | velocity[split_move]
      | record;
    auto& col = pipeline.mod<basic_record>();

    fixed_clock = 0;
    pipeline();

    auto const events = col.events();
    // Key event should pass through untouched
    bool found_key = false;
    for (auto const& event : events) {
        if (event.type() == EV_KEY && event.code() == KEY_A) {
            found_key = true;
            break;
        }
    }
    EXPECT_TRUE(found_key);
}

// ── Zero-delta frames ───────────────────────────────────────────────────────

TEST(VelocityTest, ZeroDeltaFramesAreHarmless) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 0},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | velocity[split_move]
      | record;
    auto& col = pipeline.mod<basic_record>();

    fixed_clock = 0;
    pipeline();

    auto const frames = group_frames(col.events());
    // Each frame should be just REL_X=0 + SYN (no decomposition)
    for (auto const& frame : frames) {
        EXPECT_EQ(frame.size(), 2U);
    }
}

// ── Velocity with default threshold ─────────────────────────────────────────

// Default threshold (500): slow movement should not trigger split_move.
TEST(VelocityTest, DefaultThresholdSlowNoSplit) {
    auto pipeline =
      context
      | emit_all[{
        // 1 unit per frame, 1ms apart = 1000 units/sec
        // EMA with tau=0.1s will smooth this down
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | velocity[split_move]
      | record;
    auto& col = pipeline.mod<basic_record>();

    fixed_clock = 0;
    pipeline();

    auto const frames = group_frames(col.events());
    // Each frame should be REL_X=1 + SYN (no decomposition)
    for (auto const& frame : frames) {
        ASSERT_EQ(frame.size(), 2U);
        EXPECT_EQ(frame[0].type, EV_REL);
        EXPECT_EQ(frame[0].value, 1);
    }
}
