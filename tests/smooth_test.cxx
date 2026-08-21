#include "common/tests_common_pch.hpp"

#include <cstdint>
#include <linux/input-event-codes.h>
#include <ranges>
#include <span>
import fs8.mods;

using namespace fs8;

namespace {

    std::vector<fs8::event_type> subpipeline_out; // NOLINT(*-global-variables)
    std::vector<fs8::event_type> on_block_out;    // NOLINT(*-global-variables)

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

    /// Assert that a frame is exactly one (REL_X, REL_Y, SYN) triple.
    void expect_movement_frame(std::vector<user_event> const& frame) {
        ASSERT_EQ(frame.size(), 3U);
        EXPECT_EQ(frame[0].type, EV_REL);
        EXPECT_EQ(frame[0].code, REL_X);
        EXPECT_EQ(frame[1].type, EV_REL);
        EXPECT_EQ(frame[1].code, REL_Y);
        EXPECT_EQ(frame[2].type, EV_SYN);
    }

} // namespace

// A constant input must come through unchanged: the first frame is emitted at
// full strength and the filter converges to the input.
TEST(SmoothTest, LowPassPassesConstantInput) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 10},
        {EV_REL,      REL_Y, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_REL,      REL_X, 10},
        {EV_REL,      REL_Y, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_REL,      REL_X, 10},
        {EV_REL,      REL_Y, 10},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | mouse_history
      | low_pass_filter[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 3U);
    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (auto const& frame : frames) {
        expect_movement_frame(frame);
        EXPECT_EQ(sum_of(frame, REL_X), 10);
        EXPECT_EQ(sum_of(frame, REL_Y), 10);
        total_x += sum_of(frame, REL_X);
        total_y += sum_of(frame, REL_Y);
    }
    EXPECT_EQ(total_x, 30);
    EXPECT_EQ(total_y, 30);
}

// Alpha > 1 must be clamped so the output never overshoots the input.
TEST(SmoothTest, LowPassClampsAlpha) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 5},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 7},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | mouse_history
      | low_pass_filter[2.0f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(sum_of(frames[0], REL_X), 5);
    EXPECT_EQ(sum_of(frames[1], REL_X), 7);
}

// Frames without movement must not synthesize residual REL events.
TEST(SmoothTest, LowPassNoResidualDrift) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_REL,      REL_X, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_KEY,      KEY_A,  1},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | mouse_history
      | low_pass_filter[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 3U);
    EXPECT_EQ(sum_of(frames[0], REL_X), 10);
    EXPECT_EQ(sum_of(frames[1], REL_X), 10);

    // The key frame carries only the key and its SYN.
    ASSERT_EQ(frames[2].size(), 2U);
    EXPECT_EQ(frames[2][0].code, KEY_A);
    EXPECT_EQ(frames[2][1].type, EV_SYN);
}

// Non-REL mouse events (e.g. the wheel) must pass through untouched.
TEST(SmoothTest, LowPassPassesWheelEvents) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,  REL_WHEEL, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | mouse_history
      | low_pass_filter[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 1U);
    ASSERT_EQ(frames[0].size(), 2U);
    EXPECT_EQ(frames[0][0].type, EV_REL);
    EXPECT_EQ(frames[0][0].code, REL_WHEEL);
    EXPECT_EQ(frames[0][0].value, 1);
    EXPECT_EQ(frames[0][1].type, EV_SYN);
}

// A constant input must be reproduced exactly, starting with the first frame.
TEST(SmoothTest, KalmanPassesConstantInput) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 10},
        {EV_REL,      REL_Y, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_REL,      REL_X, 10},
        {EV_REL,      REL_Y, 10},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | mouse_history
      | kalman_filter[0.1f, 0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 2U);
    for (auto const& frame : frames) {
        expect_movement_frame(frame);
        EXPECT_EQ(sum_of(frame, REL_X), 10);
        EXPECT_EQ(sum_of(frame, REL_Y), 10);
    }
}

// A step input must be approached gradually: the first frame passes through at
// full strength and the estimate moves toward the measurement without
// overreacting or drifting backward.
TEST(SmoothTest, KalmanApproachesStepInput) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X,   5},
        {EV_SYN, SYN_REPORT,   0},
        {EV_REL,      REL_X, 100},
        {EV_SYN, SYN_REPORT,   0},
        {EV_REL,      REL_X, 100},
        {EV_SYN, SYN_REPORT,   0},
        {EV_REL,      REL_X, 100},
        {EV_SYN, SYN_REPORT,   0},
    }]
      | mouse_history
      | kalman_filter[0.1f, 0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 4U);
    EXPECT_EQ(sum_of(frames[0], REL_X), 5);

    std::int32_t prev = sum_of(frames[0], REL_X);
    for (std::size_t i = 1; i < frames.size(); ++i) {
        auto const cur = sum_of(frames[i], REL_X);
        EXPECT_GT(cur, prev); // moves toward the measurement
        EXPECT_LT(cur, 100);  // has not overreacted yet
        prev = cur;
    }
}

// Frames without movement must not synthesize residual REL events.
TEST(SmoothTest, KalmanNoResidualDrift) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_KEY,      KEY_A,  1},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | mouse_history
      | kalman_filter[0.1f, 0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(sum_of(frames[0], REL_X), 10);

    ASSERT_EQ(frames[1].size(), 2U);
    EXPECT_EQ(frames[1][0].code, KEY_A);
    EXPECT_EQ(frames[1][1].type, EV_SYN);
}

// Regression: a fork-emitting mod (kalman_filter) placed inside a sub-pipeline
// must fork into the rest of the SUB-pipeline (mouse_to_scroll), not re-enter
// the enclosing on_held block. Previously the sub-pipeline index was misread as
// a full-pipeline index, so the smoothed events were swallowed and never
// converted to scroll.
TEST(SmoothTest, KalmanInSubPipelineForksToMouseToScroll) {
    using namespace fs8;
    subpipeline_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_A,  .value = 1},
       {.type = EV_REL,      .code = REL_X, .value = 10},
       {.type = EV_REL,      .code = REL_Y, .value = 10},
       {.type = EV_SYN, .code = SYN_REPORT,  .value = 0},
       {.type = EV_KEY,      .code = KEY_A,  .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT,  .value = 0},
    }]
     | mice_quantifier
     | mouse_history
     | on_held[KEY_A, context | kalman_filter[0.1f, 0.5f] | mouse_to_scroll]
     | record[subpipeline_out])();

    // The smoothed movement reached mouse_to_scroll and was re-emitted as
    // scroll. No raw REL_X/REL_Y and no modifier key leak past the block.
    EXPECT_FALSE(subpipeline_out.empty());
    EXPECT_TRUE(std::ranges::any_of(subpipeline_out, [](fs8::event_type const& e) {
        return e.code() == REL_WHEEL_HI_RES || e.code() == REL_HWHEEL_HI_RES;
    }));
    EXPECT_TRUE(std::ranges::none_of(subpipeline_out, [](fs8::event_type const& e) {
        return e.type() == EV_REL && (e.code() == REL_X || e.code() == REL_Y);
    }));
    EXPECT_TRUE(std::ranges::none_of(subpipeline_out, [](fs8::event_type const& e) {
        return e.type() == EV_KEY;
    }));
}

// Regression: fork_emit from inside an `on` block must continue through the
// mods AFTER the block in the full pipeline, without re-entering the block.
// The empty `run` before the block keeps the block away from index 0.
TEST(SmoothTest, ForkFromOnBlockContinuesToFullTail) {
    using namespace fs8;
    on_block_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_A, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
     | run{[](auto&) noexcept {
           return context_action::next;
       }}
     | on[always_enable, context | run{[](auto& ctx) noexcept {
                             std::ignore = ctx.fork_emit(EV_KEY, KEY_B, 1);
                             std::ignore = ctx.fork_emit(EV_KEY, KEY_B, 0);
                             return context_action::next;
                         }}]
     | record[on_block_out])();

    EXPECT_TRUE(std::ranges::any_of(on_block_out, [](fs8::event_type const& e) {
        return e.is(EV_KEY, KEY_B, 1);
    }));
    EXPECT_TRUE(std::ranges::any_of(on_block_out, [](fs8::event_type const& e) {
        return e.is(EV_KEY, KEY_B, 0);
    }));
}
