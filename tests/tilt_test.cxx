#include "common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <vector>
import fs8.mods;

using namespace fs8;

namespace {

    [[nodiscard]] std::vector<event_type> axis_events(basic_record const& col, std::uint16_t const type, std::uint16_t const code) {
        std::vector<event_type> result;
        for (auto const& event : col.without_syn()) {
            if (event.type() == type && event.code() == code) {
                result.push_back(event);
            }
        }
        return result;
    }

} // namespace

// ---------------------------------------------------------------------------
// tilt_state
// ---------------------------------------------------------------------------

TEST(TiltStateTest, NormalizesAxesAndMagnitude) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_ABS, ABS_TILT_Y,    0},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | record;

    pipeline();

    auto const& state = pipeline.mod<basic_tilt_state>();
    EXPECT_FLOAT_EQ(state.norm_x(), 1.0F);
    EXPECT_FLOAT_EQ(state.norm_y(), 0.0F);
    EXPECT_FLOAT_EQ(state.normalized_magnitude(), 1.0F);
}

TEST(TiltStateTest, DerivesMagnitudeFromBothAxes) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_ABS, ABS_TILT_Y, 9000},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | record;

    pipeline();

    auto const& state = pipeline.mod<basic_tilt_state>();
    EXPECT_FLOAT_EQ(state.norm_x(), 1.0F);
    EXPECT_FLOAT_EQ(state.norm_y(), 1.0F);
    EXPECT_FLOAT_EQ(state.normalized_magnitude(), 1.0F); // clamped
}

TEST(TiltStateTest, ToolChangeResetsTilt) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,   ABS_TILT_X, 9000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | tilt_state
      | record;

    pipeline();

    auto const& state = pipeline.mod<basic_tilt_state>();
    EXPECT_FLOAT_EQ(state.normalized_magnitude(), 0.0F);
    EXPECT_FLOAT_EQ(state.norm_x(), 0.0F);
}

// ---------------------------------------------------------------------------
// tilt_speed
// ---------------------------------------------------------------------------

TEST(TiltSpeedTest, AbsAcceleratesWithTilt) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1100},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_speed[tilt_abs, tilt_isotropic, tilt_curve_linear, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_ABS, ABS_X);
    ASSERT_EQ(xs.size(), 2U);
    EXPECT_EQ(xs[0].value(), 1000); // first sample: initialisation
    EXPECT_EQ(xs[1].value(), 1300); // 1000 + (1100 - 1000) * 3
}

TEST(TiltSpeedTest, RelAcceleratesWithTilt) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_speed[tilt_rel, tilt_isotropic, tilt_curve_linear, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 2U);
    EXPECT_EQ(xs[0].value(), 30);
    EXPECT_EQ(xs[1].value(), 30);
}

TEST(TiltSpeedTest, PerAxisOnlyAffectsTiltedAxis) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_ABS, ABS_TILT_Y,    0},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_REL,      REL_Y,   10},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_speed[tilt_rel, tilt_per_axis, tilt_curve_linear, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    auto const ys = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_Y);
    ASSERT_EQ(xs.size(), 1U);
    ASSERT_EQ(ys.size(), 1U);
    EXPECT_EQ(xs[0].value(), 30); // tilted axis accelerated
    EXPECT_EQ(ys[0].value(), 10); // untilted axis untouched
}

TEST(TiltSpeedTest, NoTiltIsPassthroughAtBase) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 10},
        {EV_SYN, SYN_REPORT,  0},
        {EV_REL,      REL_X, 10},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | tilt_state
      | tilt_speed[tilt_rel, tilt_isotropic, tilt_curve_linear, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 2U);
    EXPECT_EQ(xs[0].value(), 10);
    EXPECT_EQ(xs[1].value(), 10);
}

TEST(TiltSpeedTest, DampingBelowOneSlowsMovement) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_REL,      REL_X,   10},
        {EV_REL,      REL_X,   10},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_speed[tilt_rel, tilt_isotropic, tilt_curve_linear, tilt_speed_options{.base = 1.0F, .max = 0.5F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 3U);
    // factor 0.5 with epsilon accumulation: 5, 5, 5
    EXPECT_EQ(xs[0].value(), 5);
    EXPECT_EQ(xs[1].value(), 5);
    EXPECT_EQ(xs[2].value(), 5);
}

// ---------------------------------------------------------------------------
// tilt_freeze
// ---------------------------------------------------------------------------

TEST(TiltFreezeTest, FreezesWhileTiltChanges) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_SYN, SYN_REPORT,    0},
        // same tilt again: change drops back to zero, so movement resumes
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_freeze[0.5F]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 2U);
    EXPECT_EQ(xs[0].value(), 0); // frozen while the tilt is changing
    EXPECT_EQ(xs[1].value(), 10);
}

// ---------------------------------------------------------------------------
// tilt_push
// ---------------------------------------------------------------------------

TEST(TiltPushTest, PushesInTiltDirection) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,    5},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_push[tilt_push_options{.gain = 2.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 1U);
    EXPECT_EQ(xs[0].value(), 7); // 5 + gain * norm_x = 5 + 2 * 1
}

TEST(TiltPushTest, DeadZoneSuppressesSmallTilt) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 900},
        {EV_SYN, SYN_REPORT,   0},
        {EV_REL,      REL_X,   5},
        {EV_SYN, SYN_REPORT,   0},
    }]
      | tilt_state
      | tilt_push[tilt_push_options{.gain = 2.0F, .dead_zone = 0.5F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 1U);
    EXPECT_EQ(xs[0].value(), 5); // norm_x = 0.1 < dead_zone, no push
}
