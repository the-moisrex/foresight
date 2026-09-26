#include "common/tests_common_pch.hpp"

#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <vector>
import fs8.mods;

using namespace fs8;

namespace {

    /// Collect all events that match a given type+code, ignoring SYN_REPORT.
    std::vector<event_type> collect(std::span<event_type const> const events,
                                    std::uint16_t const               type,
                                    std::uint16_t const               code) {
        std::vector<event_type> out;
        for (auto const& event : events) {
            if (event.type() == type && event.code() == code) {
                out.push_back(event);
            }
        }
        return out;
    }

/// Count events matching a given type+code.

} // namespace

// ---------------------------------------------------------------------------
// Tool rename: BTN_TOOL_* -> BTN_TOOL_FINGER
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, RenamesPenToolToFinger) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_GE(events.size(), 1U);
    EXPECT_EQ(events[0].type(), EV_KEY);
    EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
    EXPECT_EQ(events[0].value(), 1);
}

TEST(Pen2TouchTest, RenamesRubberToolToFinger) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_RUBBER, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_GE(events.size(), 1U);
    EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
}

TEST(Pen2TouchTest, RenamesAllPenToolVariants) {
    // Each pipeline is consteval, so we test each tool variant individually.
    {
        auto pipeline =
          context | emit_all[{{EV_KEY, BTN_TOOL_BRUSH, 1}, {EV_SYN, SYN_REPORT, 0}}]
          | pen2touch | record;
        pipeline();
        auto const events = pipeline.mod<basic_record>().without_syn();
        ASSERT_GE(events.size(), 1U);
        EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
    }
    {
        auto pipeline =
          context | emit_all[{{EV_KEY, BTN_TOOL_PENCIL, 1}, {EV_SYN, SYN_REPORT, 0}}]
          | pen2touch | record;
        pipeline();
        auto const events = pipeline.mod<basic_record>().without_syn();
        ASSERT_GE(events.size(), 1U);
        EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
    }
    {
        auto pipeline =
          context | emit_all[{{EV_KEY, BTN_TOOL_AIRBRUSH, 1}, {EV_SYN, SYN_REPORT, 0}}]
          | pen2touch | record;
        pipeline();
        auto const events = pipeline.mod<basic_record>().without_syn();
        ASSERT_GE(events.size(), 1U);
        EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
    }
    {
        auto pipeline =
          context | emit_all[{{EV_KEY, BTN_TOOL_MOUSE, 1}, {EV_SYN, SYN_REPORT, 0}}]
          | pen2touch | record;
        pipeline();
        auto const events = pipeline.mod<basic_record>().without_syn();
        ASSERT_GE(events.size(), 1U);
        EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
    }
    {
        auto pipeline =
          context | emit_all[{{EV_KEY, BTN_TOOL_LENS, 1}, {EV_SYN, SYN_REPORT, 0}}]
          | pen2touch | record;
        pipeline();
        auto const events = pipeline.mod<basic_record>().without_syn();
        ASSERT_GE(events.size(), 1U);
        EXPECT_EQ(events[0].code(), BTN_TOOL_FINGER);
    }
}

// ---------------------------------------------------------------------------
// Touch-down: BTN_TOUCH=1 emits MT contact start
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TouchDownEmitsTrackingId) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_SYN, SYN_REPORT, 0},
        // Position before touch
        {EV_ABS, ABS_X, 1000},
        {EV_ABS, ABS_Y, 2000},
        {EV_ABS, ABS_PRESSURE, 50},
        {EV_SYN, SYN_REPORT, 0},
        // Touch down
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const tracking_ids = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_FALSE(tracking_ids.empty());
    // First tracking ID should be non-negative (contact start)
    EXPECT_GE(tracking_ids[0].value(), 0);
}

TEST(Pen2TouchTest, TouchDownEmitsSlot) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const slots = collect(col.events(), EV_ABS, ABS_MT_SLOT);
    ASSERT_FALSE(slots.empty());
    EXPECT_EQ(slots[0].value(), 0); // always slot 0
}

TEST(Pen2TouchTest, TouchDownEmitsPosition) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_X, 1500},
        {EV_ABS, ABS_Y, 2500},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const pos_x = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    auto const pos_y = collect(col.events(), EV_ABS, ABS_MT_POSITION_Y);
    ASSERT_FALSE(pos_x.empty());
    ASSERT_FALSE(pos_y.empty());
    EXPECT_EQ(pos_x[0].value(), 1500);
    EXPECT_EQ(pos_y[0].value(), 2500);
}

TEST(Pen2TouchTest, TouchDownEmitsPressure) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_PRESSURE, 42},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const mt_pressure = collect(col.events(), EV_ABS, ABS_MT_PRESSURE);
    ASSERT_FALSE(mt_pressure.empty());
    EXPECT_EQ(mt_pressure[0].value(), 42);
}

// ---------------------------------------------------------------------------
// Movement: ABS_X/Y while touching emits MT_POSITION_X/Y
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, MovementEmitsMTPosition) {
    auto pipeline =
      context
      | emit_all[{
        // Touch down
        {EV_ABS, ABS_X, 1000},
        {EV_ABS, ABS_Y, 2000},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        // Movement
        {EV_ABS, ABS_X, 1100},
        {EV_ABS, ABS_Y, 2100},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const pos_x = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    auto const pos_y = collect(col.events(), EV_ABS, ABS_MT_POSITION_Y);
    // At least 2: one from touch-down frame, one from movement frame
    ASSERT_GE(pos_x.size(), 2U);
    ASSERT_GE(pos_y.size(), 2U);
    EXPECT_EQ(pos_x[1].value(), 1100);
    EXPECT_EQ(pos_y[1].value(), 2100);
}

// ---------------------------------------------------------------------------
// Touch-up: BTN_TOUCH=0 emits tracking ID -1
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TouchUpEmitsTrackingIdMinus1) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        // Touch up
        {EV_KEY, BTN_TOUCH, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const tracking_ids = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tracking_ids.size(), 2U);
    // First: contact start (>= 0), second: contact end (-1)
    EXPECT_GE(tracking_ids[0].value(), 0);
    EXPECT_EQ(tracking_ids[1].value(), -1);
}

// ---------------------------------------------------------------------------
// Multiple contacts: tracking IDs increment
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TrackingIdsIncrement) {
    auto pipeline =
      context
      | emit_all[{
        // First contact
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_TOUCH, 0},
        {EV_SYN, SYN_REPORT, 0},
        // Second contact
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_TOUCH, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const tracking_ids = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tracking_ids.size(), 4U);
    // First contact start
    EXPECT_GE(tracking_ids[0].value(), 0);
    // First contact end
    EXPECT_EQ(tracking_ids[1].value(), -1);
    // Second contact start: must be different from first
    EXPECT_GE(tracking_ids[2].value(), 0);
    EXPECT_NE(tracking_ids[0].value(), tracking_ids[2].value());
    // Second contact end
    EXPECT_EQ(tracking_ids[3].value(), -1);
}

// ---------------------------------------------------------------------------
// Stylus buttons are dropped
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, StylusButtonsDropped) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_STYLUS, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS2, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS3, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const keys = col.without_syn();
    // All stylus buttons should be dropped; only SYN events remain
    for (auto const& event : keys) {
        if (event.type() == EV_KEY) {
            EXPECT_NE(event.code(), BTN_STYLUS);
            EXPECT_NE(event.code(), BTN_STYLUS2);
            EXPECT_NE(event.code(), BTN_STYLUS3);
        }
    }
}

// ---------------------------------------------------------------------------
// Tilt and tool width are dropped
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TiltAndWidthDropped) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_TILT_X, 10},
        {EV_ABS, ABS_TILT_Y, 20},
        {EV_ABS, ABS_TOOL_WIDTH, 5},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    EXPECT_TRUE(events.empty());
}

// ---------------------------------------------------------------------------
// Distance passes through
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, DistancePassesThrough) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_DISTANCE, 30},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const events = col.without_syn();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].type(), EV_ABS);
    EXPECT_EQ(events[0].code(), ABS_DISTANCE);
    EXPECT_EQ(events[0].value(), 30);
}

// ---------------------------------------------------------------------------
// Legacy ABS_X/Y/ABS_PRESSURE are retained
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, LegacyAxesRetained) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_X, 1000},
        {EV_ABS, ABS_Y, 2000},
        {EV_ABS, ABS_PRESSURE, 50},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const abs_x = collect(col.events(), EV_ABS, ABS_X);
    auto const abs_y = collect(col.events(), EV_ABS, ABS_Y);
    auto const abs_p = collect(col.events(), EV_ABS, ABS_PRESSURE);
    ASSERT_EQ(abs_x.size(), 1U);
    ASSERT_EQ(abs_y.size(), 1U);
    ASSERT_EQ(abs_p.size(), 1U);
    EXPECT_EQ(abs_x[0].value(), 1000);
    EXPECT_EQ(abs_y[0].value(), 2000);
    EXPECT_EQ(abs_p[0].value(), 50);
}

// ---------------------------------------------------------------------------
// SYN_REPORT passes through
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, SynPassesThrough) {
    auto pipeline =
      context
      | emit_all[{
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    EXPECT_EQ(col.size(), 1U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[0].code(), SYN_REPORT);
}

// ---------------------------------------------------------------------------
// Full pen lifecycle: tool detect -> touch -> move -> lift -> tool release
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, FullLifecycle) {
    auto pipeline =
      context
      | emit_all[{
        // Tool detected
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_SYN, SYN_REPORT, 0},
        // Position + pressure
        {EV_ABS, ABS_X, 500},
        {EV_ABS, ABS_Y, 600},
        {EV_ABS, ABS_PRESSURE, 30},
        {EV_SYN, SYN_REPORT, 0},
        // Touch down
        {EV_KEY, BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        // Movement
        {EV_ABS, ABS_X, 510},
        {EV_ABS, ABS_Y, 620},
        {EV_SYN, SYN_REPORT, 0},
        // Touch up
        {EV_KEY, BTN_TOUCH, 0},
        {EV_SYN, SYN_REPORT, 0},
        // Tool released
        {EV_KEY, BTN_TOOL_PEN, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Check BTN_TOOL_FINGER appears (renamed from BTN_TOOL_PEN)
    auto const finger = collect(col.events(), EV_KEY, BTN_TOOL_FINGER);
    ASSERT_GE(finger.size(), 2U);
    EXPECT_EQ(finger[0].value(), 1); // tool detected
    EXPECT_EQ(finger[1].value(), 0); // tool released

    // Check tracking ID lifecycle: start, then end
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tid.size(), 2U);
    EXPECT_GE(tid[0].value(), 0);  // contact start
    EXPECT_EQ(tid[1].value(), -1); // contact end

    // Check MT position during movement
    auto const pos_x = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    auto const pos_y = collect(col.events(), EV_ABS, ABS_MT_POSITION_Y);
    ASSERT_GE(pos_x.size(), 2U);
    ASSERT_GE(pos_y.size(), 2U);
    // The movement frame should have the updated position
    EXPECT_EQ(pos_x[1].value(), 510);
    EXPECT_EQ(pos_y[1].value(), 620);

    // Check MT pressure
    auto const mt_pressure = collect(col.events(), EV_ABS, ABS_MT_PRESSURE);
    ASSERT_FALSE(mt_pressure.empty());
    EXPECT_EQ(mt_pressure[0].value(), 30);

    // Check MT slot is always 0
    auto const slots = collect(col.events(), EV_ABS, ABS_MT_SLOT);
    for (auto const& slot : slots) {
        EXPECT_EQ(slot.value(), 0);
    }
}

// ---------------------------------------------------------------------------
// No touch events: no MT events emitted
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, NoTouchDownNoMTEvents) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_ABS, ABS_X, 1000},
        {EV_ABS, ABS_Y, 2000},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Without BTN_TOUCH=1, no MT tracking ID or position should appear
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    EXPECT_TRUE(tid.empty());
    auto const pos_x = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    EXPECT_TRUE(pos_x.empty());
}
