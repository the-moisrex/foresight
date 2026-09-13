#include "common/tests_common_pch.hpp"

#include <chrono>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <utility>
import fs8.mods;

using namespace fs8;

namespace {

    /// Shared timeline for `fixed_timeline`; a namespace-scope object so its
    /// address is usable while the (consteval) pipeline is built.
    std::int64_t fixed_clock = 0; // NOLINT(*-global-variables)

    /// Test-only mod: stamp every event with a monotonic clock (1ms apart) so
    /// split_move's timestamp interpolation is deterministic.
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

    using axis_event = std::pair<std::uint16_t, std::int32_t>;

    /// The sequence of REL (code, value) events, ignoring SYNs.
    std::vector<axis_event> axis_order(std::span<event_type const> const events) {
        std::vector<axis_event> out;
        for (auto const& event : events) {
            if (event.type() == EV_REL) {
                out.emplace_back(event.code(), event.value());
            }
        }
        return out;
    }

} // namespace

// A single (5, 3) movement frame becomes single-axis unit frames distributed
// proportionally over the frame so both axes finish together. Each frame is
// one axis plus a SYN.
TEST(SplitMoveTest, DistributesAxesProportionally) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 5},
        {EV_REL,      REL_Y, 3},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | split_move
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    for (auto const& frame : frames) {
        ASSERT_EQ(frame.size(), 2U);
        EXPECT_EQ(frame[0].type, EV_REL);
        EXPECT_EQ(frame[1].type, EV_SYN);
    }

    std::vector<axis_event> const expected{
      {REL_X, 1},
      {REL_Y, 1},
      {REL_X, 1},
      {REL_X, 1},
      {REL_Y, 1},
      {REL_X, 1},
      {REL_Y, 1},
      {REL_X, 1},
    };
    EXPECT_EQ(axis_order(col.events()), expected);

    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (auto const& frame : frames) {
        total_x += sum_of(frame, REL_X);
        total_y += sum_of(frame, REL_Y);
    }
    EXPECT_EQ(total_x, 5);
    EXPECT_EQ(total_y, 3);
}

// When the longer axis is an exact multiple of the shorter, it emits twice for
// every single emission of the shorter axis, still finishing together.
TEST(SplitMoveTest, LongerAxisEmitsTwicePerShorter) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 4},
        {EV_REL,      REL_Y, 2},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | split_move
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    std::vector<axis_event> const expected{
      {REL_X, 1},
      {REL_Y, 1},
      {REL_X, 1},
      {REL_X, 1},
      {REL_Y, 1},
      {REL_X, 1},
    };
    EXPECT_EQ(axis_order(col.events()), expected);
}

// The configured step bounds each emitted chunk; the remainder is emitted last.
TEST(SplitMoveTest, RespectsStep) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 5},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | split_move[2]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 3U);
    EXPECT_EQ(sum_of(frames[0], REL_X), 2);
    EXPECT_EQ(sum_of(frames[1], REL_X), 2);
    EXPECT_EQ(sum_of(frames[2], REL_X), 1);
}

// Negative deltas are decomposed with matching signs, alternating axes, and
// stay drift-free.
TEST(SplitMoveTest, HandlesNegativeValues) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, -3},
        {EV_REL,      REL_Y, -2},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | split_move
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 5U);

    std::vector<axis_event> const expected{
      {REL_X, -1},
      {REL_Y, -1},
      {REL_X, -1},
      {REL_Y, -1},
      {REL_X, -1},
    };
    EXPECT_EQ(axis_order(col.events()), expected);

    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (auto const& frame : frames) {
        EXPECT_EQ(frame.back().type, EV_SYN);
        total_x += sum_of(frame, REL_X);
        total_y += sum_of(frame, REL_Y);
    }
    EXPECT_EQ(total_x, -3);
    EXPECT_EQ(total_y, -2);
}

// Non-movement events and zero deltas pass through untouched, without
// synthesizing any extra frames.
TEST(SplitMoveTest, PassesNonMovementThrough) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY,      KEY_A, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | split_move
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 2U);

    // Key frame is untouched.
    ASSERT_EQ(frames[0].size(), 2U);
    EXPECT_EQ(frames[0][0].type, EV_KEY);
    EXPECT_EQ(frames[0][0].code, KEY_A);
    EXPECT_EQ(frames[0][1].type, EV_SYN);

    // Zero-delta frame: only the original SYN, no REL events.
    ASSERT_EQ(frames[1].size(), 1U);
    EXPECT_EQ(frames[1][0].type, EV_SYN);
    EXPECT_EQ(sum_of(frames[1], REL_X), 0);
    EXPECT_EQ(sum_of(frames[1], REL_Y), 0);
}

// A step below one is clamped to one.
TEST(SplitMoveTest, ClampsStepToOne) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 3},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | split_move[0]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const frames = group_frames(col.events());
    ASSERT_EQ(frames.size(), 3U);
    for (auto const& frame : frames) {
        EXPECT_EQ(sum_of(frame, REL_X), 1);
    }
}

// Emitted unit frames get timestamps spread across the frame interval: the
// first frame spreads over its own duration, later frames spread from the
// previous SYN to the current one.
TEST(SplitMoveTest, SpreadsTimestampsAcrossFrame) {
    fixed_clock = 0;
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 5},
        {EV_REL,      REL_Y, 5},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 3},
        {EV_REL,      REL_Y, 3},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | fixed_timeline{&fixed_clock}
      | split_move
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Frame 1: movement at t=0us, SYN at t=2000us -> ten single-axis frames
    // (five X + five Y, alternating).
    // Frame 2: previous SYN at t=2000us, SYN at t=5000us -> six single-axis
    // frames (three X + three Y).
    std::vector<std::int64_t> syn_times;
    for (auto const& event : col.events()) {
        if (event.is(EV_SYN, SYN_REPORT)) {
            syn_times.push_back(event.micro_time().count());
        }
    }

    std::vector<std::int64_t> const expected{
      200,
      400,
      600,
      800,
      1000,
      1200,
      1400,
      1600,
      1800,
      2000,
      2500,
      3000,
      3500,
      4000,
      4500,
      5000,
    };
    ASSERT_EQ(syn_times.size(), expected.size());
    EXPECT_EQ(syn_times, expected);

    // Timestamps never go backwards.
    std::int64_t previous = 0;
    for (auto const& event : col.events()) {
        auto const t = event.micro_time().count();
        EXPECT_GE(t, previous);
        previous = t;
    }
}
