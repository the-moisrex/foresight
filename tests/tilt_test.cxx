#include "common/tests_common_pch.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <linux/input-event-codes.h>
#include <vector>
import fs8.mods;
import fs8.traits;
import fs8.easings;

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

    /// Build an event with an explicit timestamp (microseconds), so tests can
    /// drive the time-constant recenter deterministically.
    [[nodiscard]] consteval event_type
    timed(std::uint16_t const type, std::uint16_t const code, int const value, long long const ts_us) noexcept {
        event_type ev{static_cast<event_type::type_type>(type),
                      static_cast<event_type::code_type>(code),
                      static_cast<event_type::value_type>(value)};
        auto&      native   = ev.native();
        native.time.tv_sec  = static_cast<__time_t>(ts_us / 1'000'000LL);
        native.time.tv_usec = static_cast<__suseconds_t>(ts_us % 1'000'000LL);
        return ev;
    }

    /// A `load_event` provider like `emit_all`, but it preserves the events'
    /// timestamps instead of resetting them.
    template <std::size_t N>
    struct [[nodiscard]] basic_tilt_feed : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<event_type, N> events{};
        std::size_t               index = 0;

      public:
        explicit consteval basic_tilt_feed(std::array<event_type, N> const inp) noexcept : events{inp} {}

        template <Context CtxT>
        context_action operator()(CtxT& ctx, control_event const& tag) noexcept {
            using enum context_action;
            if (tag.code != load_event.code) {
                return drop_event;
            }
            if (index == N) {
                return exit;
            }
            ctx.event() = events.at(index);
            ++index;
            return next;
        }
    };

    struct [[nodiscard]] tilt_feed_builder {
        template <std::size_t N>
        [[nodiscard]] consteval auto operator[](std::array<event_type, N> const events) const noexcept {
            return basic_tilt_feed<N>{events};
        }
    };

    inline constexpr tilt_feed_builder tilt_feed{};

    /// A custom easing, to prove any `float(float)` function can be passed.
    [[nodiscard]] float custom_ease_in_quad(float const t) noexcept {
        return t * t;
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
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
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
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
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
// base tilt (proximity capture + recenter)
// ---------------------------------------------------------------------------

TEST(TiltBaseTest, ProximityCapturesNaturalTiltAsBase) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,   ABS_TILT_X, 6000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | tilt_state
      | record;

    pipeline();

    auto const& state = pipeline.mod<basic_tilt_state>();
    EXPECT_NEAR(state.base_x(), 6000.0F / 9000.0F, 1e-6F);
    EXPECT_FLOAT_EQ(state.norm_x(), 0.0F);
    EXPECT_FLOAT_EQ(state.normalized_magnitude(), 0.0F);
}

TEST(TiltBaseTest, EffectiveTiltIsMeasuredFromBase) {
    static constinit auto pipeline =
      context
      | tilt_feed[std::array{
        timed(EV_ABS, ABS_TILT_X, 6000, 1'000'000),
        timed(EV_SYN, SYN_REPORT, 0, 1'000'000),
        timed(EV_KEY, BTN_TOOL_PEN, 1, 1'000'000),
        timed(EV_SYN, SYN_REPORT, 0, 1'000'000),
        timed(EV_ABS, ABS_TILT_X, 9000, 1'000'000),
        timed(EV_SYN, SYN_REPORT, 0, 1'000'000),
      }]
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
      | record;

    pipeline();

    auto const& state = pipeline.mod<basic_tilt_state>();
    EXPECT_NEAR(state.base_x(), 6000.0F / 9000.0F, 1e-6F);
    EXPECT_NEAR(state.norm_x(), 1.0F - (6000.0F / 9000.0F), 1e-6F);
}

TEST(TiltBaseTest, RecenterConvergesToCurrentTilt) {
    static constinit auto pipeline =
      context
      | tilt_feed[std::array{
        timed(EV_ABS, ABS_TILT_X, 0, 1'000'000),
        timed(EV_SYN, SYN_REPORT, 0, 1'000'000),
        timed(EV_ABS, ABS_TILT_X, 9000, 1'100'000),
        timed(EV_SYN, SYN_REPORT, 0, 1'100'000),
      }]
      | tilt_state[tilt_base_options{.recenter_time = 0.1F}]
      | record;

    pipeline();

    auto const& state = pipeline.mod<basic_tilt_state>();
    float const alpha = 1.0F - std::exp(-1.0F); // 0.1s step, 0.1s time constant
    EXPECT_NEAR(state.base_x(), alpha, 1e-4F);
    EXPECT_NEAR(state.norm_x(), 1.0F - alpha, 1e-4F);
}

TEST(TiltBaseTest, BaseMakesTiltPushNeutralAtNaturalHold) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,   ABS_TILT_X, 6000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_REL,        REL_X,    5},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | tilt_state
      | tilt_push[tilt_push_options{.gain = 2.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 1U);
    EXPECT_EQ(xs[0].value(), 5); // base cancels the natural tilt, so no push
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
      | tilt_speed[tilt_abs, tilt_isotropic, linear<float>, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
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
      | tilt_speed[tilt_rel, tilt_isotropic, linear<float>, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
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
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
      | tilt_speed[tilt_rel, tilt_per_axis, linear<float>, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    auto const ys = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_Y);
    ASSERT_EQ(xs.size(), 1U);
    ASSERT_EQ(ys.size(), 1U);
    EXPECT_EQ(xs[0].value(), 30); // tilted axis accelerated
    EXPECT_EQ(ys[0].value(), 10); // untilted axis untouched
}

TEST(TiltSpeedTest, AcceptsCustomCurveFunction) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 4500}, // norm 0.5
        {EV_SYN, SYN_REPORT,    0},
        {EV_REL,      REL_X,   10},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
      | tilt_speed[tilt_rel, tilt_isotropic, custom_ease_in_quad, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_REL, REL_X);
    ASSERT_EQ(xs.size(), 1U);
    EXPECT_EQ(xs[0].value(), 15); // 0.5^2 = 0.25 -> factor 1 + 2*0.25 = 1.5
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
      | tilt_speed[tilt_rel, tilt_isotropic, linear<float>, tilt_speed_options{.base = 1.0F, .max = 3.0F, .start = 0.0F, .end = 1.0F}]
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
      | tilt_speed[tilt_rel, tilt_isotropic, linear<float>, tilt_speed_options{.base = 1.0F, .max = 0.5F, .start = 0.0F, .end = 1.0F}]
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

TEST(TiltFreezeTest, AbsDomainHoldsWhileChanging) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 9000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1100},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS, ABS_TILT_X, 9000}, // same tilt: change drops, unfreezes
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1200},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
      | tilt_freeze[tilt_abs, 0.5F]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_ABS, ABS_X);
    ASSERT_EQ(xs.size(), 3U);
    EXPECT_EQ(xs[0].value(), 1000);
    EXPECT_EQ(xs[1].value(), 1000); // frozen: held, so abs2rel sees zero delta
    EXPECT_EQ(xs[2].value(), 1200); // unfrozen: passes through
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

TEST(TiltPushTest, AbsDomainPushesAlongTilt) {
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
      | tilt_state[tilt_base_options{.recenter_time = 0.0F}]
      | tilt_push[tilt_abs, tilt_push_options{.gain = 2.0F}]
      | record;

    pipeline();

    auto const xs = axis_events(pipeline.mod<basic_record>(), EV_ABS, ABS_X);
    ASSERT_EQ(xs.size(), 2U);
    EXPECT_EQ(xs[0].value(), 1000); // first sample initialises
    EXPECT_EQ(xs[1].value(), 1102); // 1100 + gain * norm_x
}
