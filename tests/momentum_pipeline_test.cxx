// Integration test: scheduler + momentum + on_held + mouse_to_scroll.
//
// These tests exercise the full scheduler + momentum pipeline.
// Velocity tracking requires events with realistic timestamps (spaced
// ~8ms apart).
//
// The pipeline uses a custom next_event provider (scroll_feeder) that
// feeds scroll events directly to invoke_mods (including momentum).
// emit_all[{syn_user_event}] provides load_event for clean pipeline exit.
// The scheduler fires momentum ticks when scroll_feeder is exhausted.
//
// NOTE: All momentum tests share a single static pipeline because the
// momentum mod uses a `static momentum_context` that holds a reference
// to the scheduler. Creating/destroying multiple pipelines would cause
// use-after-return on the static context's scheduler reference.

#include "common/tests_common_pch.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <thread>
#include <vector>
import fs8.mods;
import fs8.traits;

using namespace fs8;
using namespace std::chrono_literals;

// ── Custom next_event provider ──────────────────────────────────────────────

struct [[nodiscard]] scroll_feeder : fs8::consteval_copyable {
    using fs8::consteval_copyable::consteval_copyable;

    std::span<event_type const> events{};
    std::size_t                 index = 0;

    context_action operator()(event_type& event, next_event_tag) noexcept {
        using enum context_action;
        if (index >= events.size()) {
            return ignore_event;
        }
        event = events[index++];
        return next;
    }
};

static_assert(Modifier<scroll_feeder>);

// ── Helpers ─────────────────────────────────────────────────────────────────

template <typename Pred>
[[nodiscard]] static std::size_t count_if(std::span<event_type const> events, Pred&& pred) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(events, std::forward<Pred>(pred)));
}

[[nodiscard]] static std::size_t count_scroll_events(std::span<event_type const> events) noexcept {
    return count_if(events, [](event_type const& e) noexcept {
        return e.is(EV_REL, REL_WHEEL_HI_RES) || e.is(EV_REL, REL_HWHEEL_HI_RES) || e.is(EV_REL, REL_WHEEL) || e.is(EV_REL, REL_HWHEEL);
    });
}

[[nodiscard]] static event_type make_scroll(int value, long long ts_us) noexcept {
    auto  e        = event_type{EV_REL, REL_WHEEL_HI_RES, value};
    auto& n        = e.native();
    n.time.tv_sec  = static_cast<__time_t>(ts_us / 1'000'000LL);
    n.time.tv_usec = static_cast<__suseconds_t>(ts_us % 1'000'000LL);
    return e;
}

[[nodiscard]] static event_type make_mouse_move(int value, long long ts_us) noexcept {
    auto  e        = event_type{EV_REL, REL_X, value};
    auto& n        = e.native();
    n.time.tv_sec  = static_cast<__time_t>(ts_us / 1'000'000LL);
    n.time.tv_usec = static_cast<__suseconds_t>(ts_us % 1'000'000LL);
    return e;
}

template <std::size_t N>
[[nodiscard]] static std::array<event_type, N> make_scroll_sequence(int value, long long base_ts_us) noexcept {
    std::array<event_type, N> events{};
    for (std::size_t i = 0; i < N; ++i) {
        events[i] = make_scroll(value, base_ts_us + static_cast<long long>(i) * 8000);
    }
    return events;
}

template <std::size_t N>
[[nodiscard]] static std::array<event_type, N> make_mouse_sequence(int value, long long base_ts_us) noexcept {
    std::array<event_type, N> events{};
    for (std::size_t i = 0; i < N; ++i) {
        events[i] = make_mouse_move(value, base_ts_us + static_cast<long long>(i) * 8000);
    }
    return events;
}

/// Drain all pending scheduler ticks, collecting produced events.
/// Sleeps briefly to allow ticks to fire, then drains buffered events
/// without sleeping.
static std::vector<event_type> drain_scheduler(basic_scheduler& sched, int max_iterations = 500) noexcept {
    std::vector<event_type> events;
    for (int i = 0; i < max_iterations; ++i) {
        event_type event;
        if (sched(event, next_event) == context_action::next) {
            events.push_back(event);
        }
        if (!sched.has_pending()) {
            break;
        }
        // Sleep only when we got no event (waiting for next tick to fire).
        if (events.empty() || sched(event, next_event) != context_action::next) {
            std::this_thread::sleep_for(20ms);
        }
    }
    return events;
}

// ── Shared pipeline (see NOTE above) ────────────────────────────────────────

// All momentum tests use this single pipeline to avoid use-after-return
// on the static momentum_context inside the momentum mod.
static auto& momentum_pipeline() {
    static auto pipeline = context | scroll_feeder{} | emit_all[{syn_user_event}] | scheduler | momentum_scroll | record;
    return pipeline;
}

/// Set scroll_feeder events and reset index, then run the pipeline.
template <std::size_t N>
static void feed_and_run(std::array<event_type, N> const& events) {
    auto& pipeline = momentum_pipeline();
    static_cast<void>(pipeline(start));
    auto& feeder  = pipeline.mod<scroll_feeder>();
    feeder.events = std::span<event_type const>{events};
    feeder.index  = 0;
    pipeline();
}

/// Run the pipeline without re-running start phase (preserves momentum state).
template <std::size_t N>
static void feed_no_init(std::array<event_type, N> const& events) {
    auto& pipeline = momentum_pipeline();
    auto& feeder   = pipeline.mod<scroll_feeder>();
    feeder.events  = std::span<event_type const>{events};
    feeder.index   = 0;
    pipeline(no_init);
}

// ── Tests: mouse_to_scroll (basic pipeline, no scheduler) ───────────────────

TEST(MomentumPipeline, MouseToScrollConvertsRelXToScroll) {
    static std::vector<event_type> captured; // NOLINT(*-global-variables)
    captured.clear();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_CAPSLOCK,  .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT,  .value = 0},
       {.type = EV_KEY,   .code = BTN_MIDDLE,  .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT,  .value = 0},
       {.type = EV_REL,        .code = REL_X, .value = 10},
       {.type = EV_SYN,   .code = SYN_REPORT,  .value = 0},
       {.type = EV_REL,        .code = REL_X, .value = 10},
       {.type = EV_SYN,   .code = SYN_REPORT,  .value = 0},
       {.type = EV_REL,        .code = REL_X, .value = 10},
       {.type = EV_SYN,   .code = SYN_REPORT,  .value = 0},
       {.type = EV_KEY,   .code = BTN_MIDDLE,  .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT,  .value = 0},
    }]
     | keys_status
     | on_held[KEY_CAPSLOCK, BTN_MIDDLE, context | mouse_to_scroll]
     | record[captured])();

    auto const scrolls = count_scroll_events(captured);
    EXPECT_GT(scrolls, 0u) << "mouse_to_scroll should produce scroll events";

    auto const raw_rel = count_if(captured, [](event_type const& e) noexcept {
        return e.is(EV_REL, REL_X);
    });
    EXPECT_EQ(raw_rel, 0u) << "raw REL_X should be swallowed by mouse_to_scroll";
}

TEST(MomentumPipeline, NoScrollWithoutModifier) {
    static constinit auto pipeline =
      context
      | emit_all[{
        {.type = EV_REL,      .code = REL_X, .value = -15},
        {.type = EV_SYN, .code = SYN_REPORT,   .value = 0},
        {.type = EV_REL,      .code = REL_X, .value = -15},
        {.type = EV_SYN, .code = SYN_REPORT,   .value = 0},
    }]
      | keys_status
      | on_held[KEY_CAPSLOCK, BTN_MIDDLE, context | mouse_to_scroll]
      | scheduler
      | momentum_scroll
      | record;

    pipeline();

    auto& sched = pipeline.mod<basic_scheduler>();
    EXPECT_FALSE(sched.has_pending()) << "no momentum without modifier held";
}

// ── Tests: scroll_feeder + scheduler + momentum ─────────────────────────────

TEST(MomentumPipeline, VelocityTrackingSchedulesMomentum) {
    static auto scroll_events = make_scroll_sequence<10>(-120, 1'000'000LL);
    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();

    EXPECT_TRUE(sched.has_pending()) << "scheduler should have pending momentum ticks";
    EXPECT_TRUE(momentum.is_animating()) << "momentum should be animating";

    sched.cancel_all();
}

TEST(MomentumPipeline, MomentumTickProducesScrollEvents) {
    static auto scroll_events = make_scroll_sequence<10>(-120, 1'000'000LL);
    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();

    ASSERT_TRUE(sched.has_pending());
    ASSERT_TRUE(momentum.is_animating());

    auto momentum_events = drain_scheduler(sched);

    ASSERT_GT(momentum_events.size(), 0u) << "momentum should produce events";

    for (auto const& e : momentum_events) {
        EXPECT_EQ(e.source(), device_id::scheduler);
        EXPECT_TRUE(e.type() == EV_REL || e.type() == EV_SYN)
          << "momentum events should be EV_REL or EV_SYN (SYN_REPORT), got type="
          << e.type();
    }

    auto const has_hi_res = std::ranges::any_of(momentum_events, [](event_type const& e) {
        return e.is(EV_REL, REL_WHEEL_HI_RES) || e.is(EV_REL, REL_HWHEEL_HI_RES);
    });
    EXPECT_TRUE(has_hi_res) << "momentum should include hi-res scroll events";
}

TEST(MomentumPipeline, MomentumDecayDecreasesOverFrames) {
    static auto scroll_events = make_scroll_sequence<10>(-240, 1'000'000LL);
    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    ASSERT_TRUE(sched.has_pending());

    auto events = drain_scheduler(sched);

    std::vector<int> frame_max_values;
    int              current_max = 0;
    for (auto const& e : events) {
        if (e.is(EV_REL, REL_WHEEL_HI_RES)) {
            current_max = std::max(current_max, std::abs(e.value()));
        } else if (e.is(EV_SYN, SYN_REPORT)) {
            if (current_max > 0) {
                frame_max_values.push_back(current_max);
                current_max = 0;
            }
        }
    }

    ASSERT_GE(frame_max_values.size(), 3u) << "need at least 3 frames to compare decay";

    EXPECT_GE(frame_max_values[0], frame_max_values[1]) << "first frame should have larger scroll value than second";
    EXPECT_GE(frame_max_values[1], frame_max_values[2]) << "second frame should have larger or equal scroll value than third";
}

TEST(MomentumPipeline, LowVelocityDoesNotSchedule) {
    static auto scroll_events = make_scroll_sequence<1>(1, 1'000'000LL);
    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();

    EXPECT_FALSE(sched.has_pending()) << "low velocity should not schedule momentum";
    EXPECT_FALSE(momentum.is_animating());
}

TEST(MomentumPipeline, MouseMoveCancelsMomentum) {
    static auto scroll_events = make_scroll_sequence<10>(-120, 1'000'000LL);
    static auto mouse_events  = make_mouse_sequence<5>(50, 3'000'000LL);

    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();
    // Disable distance tracking so mouse moves cancel momentum.
    momentum.set_max_mouse_distance(0.0f);

    feed_no_init(mouse_events);

    auto& sched = pipeline.mod<basic_scheduler>();

    // After second batch (mouse moves), momentum should be cancelled.
    EXPECT_FALSE(sched.has_pending()) << "mouse move should cancel momentum";
    EXPECT_FALSE(momentum.is_animating());
}

TEST(MomentumPipeline, SchedulerEmittedEventsPassThrough) {
    static auto scroll_events = make_scroll_sequence<10>(-120, 1'000'000LL);
    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    ASSERT_TRUE(sched.has_pending());

    event_type event;
    if (sched(event, next_event) == context_action::next) {
        EXPECT_EQ(event.source(), device_id::scheduler);
        EXPECT_EQ(event.type(), EV_REL);
    }

    sched.cancel_all();
}

TEST(MomentumPipeline, NewScrollRestartsMomentum) {
    static auto scroll_events1 = make_scroll_sequence<10>(-120, 1'000'000LL);
    static auto scroll_events2 = make_scroll_sequence<10>(-240, 2'000'000LL);

    feed_and_run(scroll_events1);
    feed_no_init(scroll_events2);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();

    EXPECT_TRUE(sched.has_pending());
    EXPECT_TRUE(momentum.is_animating());

    sched.cancel_all();
}

TEST(MomentumPipeline, CancelAllStopsMomentum) {
    static auto scroll_events = make_scroll_sequence<10>(-240, 1'000'000LL);
    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    ASSERT_TRUE(sched.has_pending());

    sched.cancel_all();
    EXPECT_FALSE(sched.has_pending());
}

TEST(MomentumPipeline, DistanceTrackingAccumulates) {
    static auto scroll_events = make_scroll_sequence<10>(-120, 1'000'000LL);
    static auto mouse_events  = make_mouse_sequence<3>(10, 2'000'000LL);

    feed_and_run(scroll_events);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();
    momentum.set_max_mouse_distance(500.0f);
    ASSERT_TRUE(sched.has_pending());
    ASSERT_TRUE(momentum.is_animating());

    // Feed small mouse moves via no_init to avoid re-running start (which
    // would reset is_animating).  Should accumulate, not cancel.
    feed_no_init(mouse_events);

    EXPECT_TRUE(sched.has_pending()) << "small mouse moves should not cancel momentum with distance tracking";
    EXPECT_TRUE(momentum.is_animating());

    momentum.set_max_mouse_distance(0.0f);
    sched.cancel_all();
}

TEST(MomentumPipeline, OpposingScrollReducesMomentum) {
    // Feed a strong scroll in the negative direction to start momentum.
    static auto scroll_down = make_scroll_sequence<10>(-120, 1'000'000LL);
    feed_and_run(scroll_down);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();
    ASSERT_TRUE(sched.has_pending());
    ASSERT_TRUE(momentum.is_animating());

    // Feed a stronger opposing scroll (positive direction) while momentum is active.
    // With reversal_scale=0.3, each +120 scroll reduces vel by 120*0.3=36.
    // Momentum vel_x ~ -800, 10 scrolls of +120 = -800 + 10*36 = -440, still negative.
    static auto opposing_scroll_1 = make_scroll_sequence<10>(120, 5'000'000LL);
    feed_no_init(opposing_scroll_1);

    // Momentum should still be active (reduced, not cancelled).
    EXPECT_TRUE(momentum.is_animating()) << "opposing scroll should reduce, not cancel, momentum";

    sched.cancel_all();
}

TEST(MomentumPipeline, OpposingScrollCancelsAndRestarts) {
    // Feed a weak scroll in the negative direction to start momentum.
    static auto scroll_down_weak = make_scroll_sequence<3>(-120, 10'000'000LL);
    feed_and_run(scroll_down_weak);

    auto& pipeline = momentum_pipeline();
    auto& sched    = pipeline.mod<basic_scheduler>();
    auto& momentum = pipeline.mod<basic_momentum_scroll>();
    ASSERT_TRUE(sched.has_pending());
    ASSERT_TRUE(momentum.is_animating());

    // Feed enough opposing scrolls to fully deplete the momentum velocity,
    // then continue scrolling in the new direction.  Momentum should restart
    // in the new direction once the tracker velocity exceeds the threshold.
    static auto opposing_scroll_cancel = make_scroll_sequence<80>(120, 10'100'000LL);
    feed_no_init(opposing_scroll_cancel);

    // Momentum should be animating again (restarted in the new direction)
    // because the remaining opposing scrolls restarted it after cancellation.
    EXPECT_TRUE(momentum.is_animating()) << "opposing scrolls should restart momentum in new direction";

    sched.cancel_all();
}
