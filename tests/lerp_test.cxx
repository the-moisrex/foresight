#include "common/tests_common_pch.hpp"

#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
import fs8.mods;

using namespace fs8;

namespace {

    /// Group a recorded stream into frames, one per SYN_REPORT.
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

    std::int32_t sum_of(std::vector<user_event> const& frame, std::uint16_t const code) {
        std::int32_t total = 0;
        for (auto const& event : frame) {
            if (event.type == EV_REL && event.code == code) {
                total += event.value;
            }
        }
        return total;
    }

} // namespace

// Subdividing a frame must reproduce the exact input movement (drift-free) and
// must pair REL_X with REL_Y inside every emitted SYN frame (issue #68).
TEST(SmoothTest, LerpReproducesMovementAndPairsAxes) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 3},
        {EV_REL,      REL_Y, 3},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 9},
        {EV_REL,      REL_Y, 9},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | lerp
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_FALSE(frames.empty());

    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (auto const& frame : frames) {
        total_x += sum_of(frame, REL_X);
        total_y += sum_of(frame, REL_Y);

        // Skip trailing empty SYN frames (the original event passing through).
        bool const has_movement = std::ranges::any_of(frame, [](user_event const& e) {
            return e.type == EV_REL && (e.code == REL_X || e.code == REL_Y);
        });
        if (!has_movement) {
            continue;
        }

        // Every step frame must contain REL_X and REL_Y and end with SYN.
        EXPECT_TRUE(std::ranges::any_of(frame, [](user_event const& e) {
            return e.type == EV_REL && e.code == REL_X;
        }));
        EXPECT_TRUE(std::ranges::any_of(frame, [](user_event const& e) {
            return e.type == EV_REL && e.code == REL_Y;
        }));
        EXPECT_EQ(frame.back().type, EV_SYN);
    }

    // Total output equals total input: 3 + 9 on each axis.
    EXPECT_EQ(total_x, 12);
    EXPECT_EQ(total_y, 12);

    // The emitted steps accumulate exactly to the input totals; the first
    // input frame (3,3) is fully emitted before the second one (9,9) starts.
    std::int32_t cumulative_x = 0;
    bool         reached_3    = false;
    for (auto const& frame : frames) {
        cumulative_x += sum_of(frame, REL_X);
        if (cumulative_x == 3) {
            reached_3 = true;
        }
    }
    EXPECT_TRUE(reached_3);
}

// Small frames must not be dropped (the old `total_steps <= 2` guard lost them).
TEST(SmoothTest, LerpKeepsSmallMovements) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 1},
        {EV_REL,      REL_Y, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 2},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | lerp
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    // Each input frame produces lerp steps plus a trailing empty SYN from the
    // original event passing through.
    ASSERT_GE(frames.size(), 2U);

    // Total output must equal total input.
    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (auto const& frame : frames) {
        total_x += sum_of(frame, REL_X);
        total_y += sum_of(frame, REL_Y);
    }
    EXPECT_EQ(total_x, 3); // 1 + 2
    EXPECT_EQ(total_y, 1); // 1 + 0
}

// Frames without mouse movement must pass through untouched (no synthesized
// REL events, no dropped keys).
TEST(SmoothTest, LerpLeavesNonMovementFramesAlone) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 5},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY,      KEY_A, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | lerp
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    // 4 interpolation steps for the (5,0) frame plus a trailing empty SYN
    // from the original event passing through, plus the untouched key frame.
    ASSERT_EQ(frames.size(), 6U);

    // The movement frame's steps accumulate to exactly 5 on the X axis.
    std::int32_t total_x = 0;
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        total_x += sum_of(frames[i], REL_X);
    }
    EXPECT_EQ(total_x, 5);

    // The key frame is just the key + its SYN, with no REL events at all.
    auto const& key_frame = frames.back();
    ASSERT_EQ(key_frame.size(), 2U);
    EXPECT_EQ(key_frame[0].type, EV_KEY);
    EXPECT_EQ(key_frame[0].code, KEY_A);
    EXPECT_EQ(key_frame[1].type, EV_SYN);
}
