#include "./common/test_helpers.hpp"
#include "./common/tests_common_pch.hpp"

#include <cstdint>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <span>
#include <vector>
import dynamic_scoping;
import fs8.devices.evdev;
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

    /// Build an in-memory tablet-like source device: pen tool, stylus
    /// button, position/pressure/tilt axes, and the DIRECT property —
    /// everything the profile broadcast is supposed to get rid of.
    fs8::evdev make_tablet_template() {
        auto src = fs8::evdev{libevdev_new(), fs8::evdev_status::success};
        src.device_name("Pen2Touch Test Tablet");
        libevdev_set_id_bustype(src.device_ptr(), BUS_USB);
        src.enable_event_type(EV_SYN);
        src.enable_event_code(EV_KEY, BTN_TOUCH);
        src.enable_event_code(EV_KEY, BTN_TOOL_PEN);
        src.enable_event_code(EV_KEY, BTN_STYLUS);
        static constexpr input_absinfo x_info{.minimum = 0, .maximum = 32'767, .resolution = 100};
        static constexpr input_absinfo y_info{.minimum = 0, .maximum = 32'767, .resolution = 100};
        static constexpr input_absinfo pressure_info{.minimum = 0, .maximum = 8191};
        static constexpr input_absinfo tilt_info{.minimum = -900, .maximum = 900};
        src.abs_info(ABS_X, x_info);
        src.abs_info(ABS_Y, y_info);
        src.abs_info(ABS_PRESSURE, pressure_info);
        src.abs_info(ABS_TILT_X, tilt_info);
        src.enable_property(INPUT_PROP_DIRECT);
        return src;
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

// ---------------------------------------------------------------------------
// Touch-up selects the slot before writing the tracking ID
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TouchUpSelectsSlotBeforeTrackingId) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY,  BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY,  BTN_TOUCH, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const  events    = col.events();
    std::size_t end_index = events.size();
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].type() == EV_ABS && events[i].code() == ABS_MT_TRACKING_ID && events[i].value() == -1) {
            end_index = i;
        }
    }
    ASSERT_LT(end_index, events.size()) << "no contact end emitted";
    ASSERT_GT(end_index, 0U);
    auto const& prev = events[end_index - 1];
    EXPECT_EQ(prev.type(), EV_ABS);
    EXPECT_EQ(prev.code(), ABS_MT_SLOT);
    EXPECT_EQ(prev.value(), 0);
}

// ---------------------------------------------------------------------------
// toggle_off releases a stale contact (no keys_state required)
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ToggleOffReleasesContact) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY,  BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline(); // touch-down only; emit_all exhausts and the pipeline exits

    // The contact is still open: disabling the mod must close it even
    // though this pipeline has no keys_state.
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(toggle_off), context_action::next);

    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tid.size(), 2U);
    EXPECT_GE(tid[0].value(), 0);
    EXPECT_EQ(tid[1].value(), -1);
}

// ---------------------------------------------------------------------------
// Tool leaving mid-contact ends the contact
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ToolOutEndsContact) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,   1},
        {EV_SYN,   SYN_REPORT,   0},
        {EV_ABS,        ABS_X, 100},
        {EV_ABS,        ABS_Y, 200},
        {EV_SYN,   SYN_REPORT,   0},
        {EV_KEY,    BTN_TOUCH,   1},
        {EV_SYN,   SYN_REPORT,   0},
        // Tool disappears without a BTN_TOUCH=0.
        {EV_KEY, BTN_TOOL_PEN,   0},
        {EV_SYN,   SYN_REPORT,   0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tid.size(), 2U);
    EXPECT_GE(tid[0].value(), 0);
    EXPECT_EQ(tid[1].value(), -1);

    // The mod also synthesizes the missing BTN_TOUCH=0.
    auto const touch = collect(col.events(), EV_KEY, BTN_TOUCH);
    ASSERT_GE(touch.size(), 2U);
    EXPECT_EQ(touch[0].value(), 1);
    EXPECT_EQ(touch[1].value(), 0);
}

// ---------------------------------------------------------------------------
// profile_device: the tablet template becomes a touchpad
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ProfileDeviceReshapesTabletTemplate) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());

    auto          pipeline = context | pen2touch | record;
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);

    // Tablet parts are gone.
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_TOOL_PEN));
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_STYLUS));
    EXPECT_FALSE(tmpl.has_event_code(EV_ABS, ABS_TILT_X));
    EXPECT_FALSE(tmpl.has_property(INPUT_PROP_DIRECT));

    // Touchpad parts are there.
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_TOUCH));
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_TOOL_FINGER));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_SLOT));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_TRACKING_ID));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_POSITION_X));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_POSITION_Y));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_PRESSURE));
    EXPECT_TRUE(tmpl.has_property(INPUT_PROP_POINTER));

    // Legacy axes keep the source ranges (values pass through 1:1).
    ASSERT_NE(tmpl.abs_info(ABS_X), nullptr);
    EXPECT_EQ(tmpl.abs_info(ABS_X)->maximum, 32'767);

    // MT absinfo: bounded tracking ids, position copied from ABS_X.
    ASSERT_NE(tmpl.abs_info(ABS_MT_TRACKING_ID), nullptr);
    EXPECT_EQ(tmpl.abs_info(ABS_MT_TRACKING_ID)->minimum, 0);
    EXPECT_EQ(tmpl.abs_info(ABS_MT_TRACKING_ID)->maximum, 65'535);
    ASSERT_NE(tmpl.abs_info(ABS_MT_POSITION_X), nullptr);
    EXPECT_EQ(tmpl.abs_info(ABS_MT_POSITION_X)->maximum, 32'767);
    EXPECT_EQ(tmpl.abs_info(ABS_MT_POSITION_X)->resolution, 100);
    ASSERT_NE(tmpl.abs_info(ABS_MT_SLOT), nullptr);
    EXPECT_EQ(tmpl.abs_info(ABS_MT_SLOT)->maximum, 0); // single contact
}

TEST(Pen2TouchTest, ProfileDeviceIgnoredWithoutMod) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());

    auto          pipeline = context | record;
    dynamic_scope scope{dynamic_context, pipeline};
    std::ignore = dynamic_context->broadcast(profile_device + &tmpl);

    // A general control event that nobody handles must leave the template
    // alone (and must not log a "required event not handled" warning).
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_TOOL_PEN));
    EXPECT_TRUE(tmpl.has_property(INPUT_PROP_DIRECT));
    EXPECT_FALSE(tmpl.has_event_code(EV_ABS, ABS_MT_SLOT));
    EXPECT_FALSE(tmpl.has_property(INPUT_PROP_POINTER));
}

// ---------------------------------------------------------------------------
// End-to-end: finalize_device creates a real multitouch touchpad
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, FinalizeDeviceCreatesTouchpad) {
    auto const res = fs8::verify_access_to_uinput();
    if (res != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(res);
    }

    auto src = make_tablet_template();
    ASSERT_TRUE(src.is_ok());

    fs8::basic_uinput vdev;
    {
        auto          pipeline = context | pen2touch | record;
        dynamic_scope scope{dynamic_context, pipeline};
        ASSERT_TRUE(fs8::finalize_device(vdev, src, {}));
    }
    ASSERT_TRUE(vdev.is_ok());
    if (!fs8::test::wait_for_openable(vdev.devnode(), 3000)) {
        vdev.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    fs8::evdev created{vdev.devnode()};
    ASSERT_TRUE(created.is_ok()) << vdev.devnode();

    // Touchpad keys: fingers yes, pen tools no.
    EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_TOUCH));
    EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_TOOL_FINGER));
    EXPECT_FALSE(created.has_event_code(EV_KEY, BTN_TOOL_PEN));
    EXPECT_FALSE(created.has_event_code(EV_KEY, BTN_STYLUS));
    // Tilt is gone, MT is in.
    EXPECT_FALSE(created.has_event_code(EV_ABS, ABS_TILT_X));
    EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_SLOT));
    EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_TRACKING_ID));
    EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_POSITION_X));
    EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_POSITION_Y));
    EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_PRESSURE));

    ASSERT_NE(created.abs_info(ABS_MT_TRACKING_ID), nullptr);
    EXPECT_EQ(created.abs_info(ABS_MT_TRACKING_ID)->maximum, 65'535);
    ASSERT_NE(created.abs_info(ABS_MT_POSITION_X), nullptr);
    EXPECT_EQ(created.abs_info(ABS_MT_POSITION_X)->maximum, 32'767);

    EXPECT_TRUE(created.has_property(INPUT_PROP_POINTER));
    EXPECT_FALSE(created.has_property(INPUT_PROP_DIRECT));

    vdev.close();
}
