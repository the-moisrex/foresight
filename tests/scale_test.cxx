#include "common/tests_common_pch.hpp"

#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
import fs8.mods;

using namespace fs8;

// ---------------------------------------------------------------------------
// scale_pen tests (absolute / tablet events)
// ---------------------------------------------------------------------------

TEST(ScalePenTest, FirstEventPassesThroughUnchanged) {
    // The very first ABS event should be recorded as-is (initialisation),
    // not scaled.
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,      ABS_X, 1000},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | scale_pen[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].type(), EV_ABS);
    EXPECT_EQ(events[0].code(), ABS_X);
    EXPECT_EQ(events[0].value(), 1000); // unchanged
}

TEST(ScalePenTest, HalvesMovementDeltas) {
    // Pen moves 1000→1100→1200 (deltas of 100). With factor 0.5 the
    // written-back positions should be 1000→1050→1100 so that abs2rel
    // would see deltas of 50 each.
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,      ABS_X, 1000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1100},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1200},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | scale_pen[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].value(), 1000); // first: init, unchanged
    EXPECT_EQ(events[1].value(), 1050); // 1000 + 100*0.5
    EXPECT_EQ(events[2].value(), 1100); // 1050 + 100*0.5
}

TEST(ScalePenTest, DoublesMovementDeltas) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,      ABS_X, 1000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1100},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1200},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | scale_pen[2.0f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].value(), 1000); // first: init
    EXPECT_EQ(events[1].value(), 1200); // 1000 + 100*2
    EXPECT_EQ(events[2].value(), 1400); // 1200 + 100*2
}

TEST(ScalePenTest, ScalesBothAxes) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,      ABS_X, 1000},
        {EV_ABS,      ABS_Y, 2000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 1100},
        {EV_ABS,      ABS_Y, 2200},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | scale_pen[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 4U);
    // First pair: init, unchanged
    EXPECT_EQ(events[0].value(), 1000);
    EXPECT_EQ(events[1].value(), 2000);
    // Second pair: deltas halved
    EXPECT_EQ(events[2].value(), 1050); // 1000 + 100*0.5
    EXPECT_EQ(events[3].value(), 2100); // 2000 + 200*0.5
}

TEST(ScalePenTest, ToolChangeResetsState) {
    // After a tool-change event the next ABS event should be treated as
    // the first (init), not scaled.
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,        ABS_X, 1000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1100},
        {EV_SYN,   SYN_REPORT,    0},
        // tool change — resets state
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        // next ABS event should be init again (unscaled)
        {EV_ABS,        ABS_X, 1200},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | scale_pen[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    // 3 ABS events + 1 KEY event = 4
    ASSERT_EQ(events.size(), 4U);
    EXPECT_EQ(events[0].value(), 1000); // init
    EXPECT_EQ(events[1].value(), 1050); // 1000 + 100*0.5
    // events[2] is the BTN_TOOL_PEN key event
    EXPECT_EQ(events[3].value(), 1200); // post-reset: init, unchanged
}

TEST(ScalePenTest, NoMovementProducesNoChange) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS,      ABS_X, 5000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 5000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_ABS,      ABS_X, 5000},
        {EV_SYN, SYN_REPORT,    0},
    }]
      | scale_pen[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].value(), 5000);
    EXPECT_EQ(events[1].value(), 5000);
    EXPECT_EQ(events[2].value(), 5000);
}

// ---------------------------------------------------------------------------
// scale_move tests (relative / mouse events)
// ---------------------------------------------------------------------------

TEST(ScaleMoveTest, HalvesRelativeMovement) {
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
      | scale_move[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Sum all REL_X values (ignoring SYN).
    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (auto const& event : col.without_syn()) {
        if (event.type() == EV_REL && event.code() == REL_X) {
            total_x += event.value();
        }
        if (event.type() == EV_REL && event.code() == REL_Y) {
            total_y += event.value();
        }
    }
    // 3 events of 10 → 30 total. Scaled by 0.5 → 15.
    // Due to integer truncation the per-event values are 5, 5, 5.
    EXPECT_EQ(total_x, 15);
    EXPECT_EQ(total_y, 15);
}

TEST(ScaleMoveTest, DoublesRelativeMovement) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 5},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 5},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | scale_move[2.0f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    std::int32_t total_x = 0;
    for (auto const& event : col.without_syn()) {
        if (event.type() == EV_REL && event.code() == REL_X) {
            total_x += event.value();
        }
    }
    // 2 events of 5 → 10 total. Scaled by 2.0 → 20.
    EXPECT_EQ(total_x, 20);
}

TEST(ScaleMoveTest, EpsilonAccumulatesForSmallMovements) {
    // REL_X=1 with factor 0.5 → 0.5 truncated to 0, epsilon 0.5.
    // Next REL_X=1 → 1*0.5 + 0.5 = 1.0 truncated to 1.
    // Over two events: total should be 1 (not 0).
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | scale_move[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    std::int32_t total_x = 0;
    for (auto const& event : col.without_syn()) {
        if (event.type() == EV_REL && event.code() == REL_X) {
            total_x += event.value();
        }
    }
    // 4 events of 1 → 4 total. Scaled by 0.5 → 2.
    EXPECT_EQ(total_x, 2);
}

TEST(ScaleMoveTest, NonMatchingEventsPassThrough) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,  REL_WHEEL,  1},
        {EV_KEY,      KEY_A,  1},
        {EV_REL,      REL_X, 10},
        {EV_SYN, SYN_REPORT,  0},
    }]
      | scale_move[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 3U);
    // Non-REL_X/REL_Y events pass through unchanged.
    EXPECT_EQ(events[0].type(), EV_REL);
    EXPECT_EQ(events[0].code(), REL_WHEEL);
    EXPECT_EQ(events[0].value(), 1);
    EXPECT_EQ(events[1].type(), EV_KEY);
    EXPECT_EQ(events[1].code(), KEY_A);
    EXPECT_EQ(events[1].value(), 1);
    // REL_X is scaled: 10 * 0.5 = 5
    EXPECT_EQ(events[2].type(), EV_REL);
    EXPECT_EQ(events[2].code(), REL_X);
    EXPECT_EQ(events[2].value(), 5);
}

TEST(ScaleMoveTest, ZeroMovementStaysZero) {
    auto pipeline =
      context
      | emit_all[{
        {EV_REL,      REL_X, 0},
        {EV_SYN, SYN_REPORT, 0},
        {EV_REL,      REL_X, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | scale_move[0.5f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    for (auto const& event : col.without_syn()) {
        if (event.type() == EV_REL && event.code() == REL_X) {
            EXPECT_EQ(event.value(), 0);
        }
    }
}
