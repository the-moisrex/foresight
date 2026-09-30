#include "./common/test_helpers.hpp"
#include "./common/tests_common_pch.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <span>
#include <sys/time.h>
#include <vector>
import dynamic_scoping;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.mods;

using namespace fs8;

namespace {
    using namespace std::chrono_literals; // NOLINT(*-using-namespace)

    /// Collect all events that match a given type+code, ignoring SYN_REPORT.
    std::vector<event_type> collect(std::span<event_type const> const events, std::uint16_t const type, std::uint16_t const code) {
        std::vector<event_type> out;
        for (auto const& event : events) {
            if (event.type() == type && event.code() == code) {
                out.push_back(event);
            }
        }
        return out;
    }

    /// An event carrying an explicit microsecond timestamp, so tests can
    /// control how much "wall time" passes between press and release.
    consteval event_type timed_ev(
      event_type::type_type const     type,
      event_type::code_type const     code,
      event_type::value_type const    value,
      std::chrono::microseconds const us) {
        event_type ev{type, code, value};
        timeval    t{};
        t.tv_sec  = static_cast<time_t>(us.count() / 1'000'000);
        t.tv_usec = static_cast<suseconds_t>(us.count() % 1'000'000);
        ev.time(t);
        return ev;
    }

    /// A `load_event` provider (like `emit_all`) that feeds pre-timestamped
    /// events instead of stamping them with the current time.
    template <std::size_t N>
    struct timed_sequence {
        std::array<event_type, N> events{};
        std::size_t               index = 0;

        explicit constexpr timed_sequence(std::array<event_type, N> evs) noexcept : events{evs} {}

        template <Context CtxT>
        context_action operator()(CtxT& ctx, control_event const& tag) noexcept {
            using enum context_action;
            if (tag.code != load_event.code) {
                return drop_event;
            }
            if (index == N) {
                return exit;
            }
            ctx.event() = events[index++];
            return next;
        }
    };

    /// A recorded event tagged with the MT slot that was selected when it
    /// was emitted (slot resets to 0 when no ABS_MT_SLOT event precedes it).
    struct slot_event {
        int        slot;
        event_type event;
    };

    /// Collect events of a given type+code together with their MT slot.
    std::vector<slot_event> collect_slotted(std::span<event_type const> const events, std::uint16_t const type, std::uint16_t const code) {
        std::vector<slot_event> out;
        int                     slot = 0;
        for (auto const& event : events) {
            if (event.type() == EV_ABS && event.code() == ABS_MT_SLOT) {
                slot = event.value();
                continue;
            }
            if (event.type() == type && event.code() == code) {
                out.push_back({slot, event});
            }
        }
        return out;
    }

    /// Build an in-memory tablet-like source device: pen tool, stylus
    /// buttons, a BTN_LEFT (some pens expose one), position/pressure/tilt
    /// axes, and the DIRECT property — everything the profile broadcast is
    /// supposed to get rid of.
    fs8::evdev make_tablet_template() {
        auto src = fs8::evdev{libevdev_new(), fs8::evdev_status::success};
        src.device_name("Pen2Touch Test Tablet");
        libevdev_set_id_bustype(src.device_ptr(), BUS_USB);
        src.enable_event_type(EV_SYN);
        src.enable_event_code(EV_KEY, BTN_TOUCH);
        src.enable_event_code(EV_KEY, BTN_TOOL_PEN);
        src.enable_event_code(EV_KEY, BTN_STYLUS);
        src.enable_event_code(EV_KEY, BTN_STYLUS2);
        src.enable_event_code(EV_KEY, BTN_STYLUS3);
        src.enable_event_code(EV_KEY, BTN_LEFT);
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

    /// Assert that `created` is a single-finger multitouch touchpad: what
    /// `profile_device` must turn any tablet clone into before uinput
    /// creates it. Kept range-agnostic where possible so it also works for
    /// clones of a real tablet (whose axis ranges we don't know).
    void expect_touchpad_caps(fs8::evdev const& created) {
        // Fixed virtual identity: never masquerade as (quirk-matched) hardware.
        EXPECT_EQ(created.device_name(), "Foresight Virtual Touchpad");
        // Touchpad keys: fingers yes, pen tools no.
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_TOUCH));
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_TOOL_FINGER));
        // Two contacts need the legacy two-finger key: libinput derives
        // its expected finger count from BTN_TOOL_* and keeps a second
        // contact hovering (inert) while only BTN_TOOL_FINGER is set.
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_TOOL_DOUBLETAP));
        EXPECT_FALSE(created.has_event_code(EV_KEY, BTN_TOOL_PEN));
        EXPECT_FALSE(created.has_event_code(EV_KEY, BTN_STYLUS));
        EXPECT_FALSE(created.has_event_code(EV_KEY, BTN_STYLUS2));
        EXPECT_FALSE(created.has_event_code(EV_KEY, BTN_STYLUS3));
        // Barrel buttons are regular mouse buttons.  BTN_LEFT is the pen
        // tip press; advertising it also flips libinput's tap-to-click
        // default off (tp_tap_default), which is what keeps hover enter/
        // leave from firing spurious taps.
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_RIGHT));
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_MIDDLE));
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_SIDE));
        EXPECT_TRUE(created.has_event_code(EV_KEY, BTN_LEFT));
        EXPECT_FALSE(created.has_property(INPUT_PROP_BUTTONPAD));
        // Tilt is gone, MT is in.
        EXPECT_FALSE(created.has_event_code(EV_ABS, ABS_TILT_X));
        EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_SLOT));
        EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_TRACKING_ID));
        EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_POSITION_X));
        EXPECT_TRUE(created.has_event_code(EV_ABS, ABS_MT_POSITION_Y));
        // No ABS_MT_PRESSURE: advertising it makes libinput switch to
        // pressure-based touch detection, whose begin threshold (12% of
        // range) and palm threshold (~130) pen pressure cannot satisfy.
        EXPECT_FALSE(created.has_event_code(EV_ABS, ABS_MT_PRESSURE));
        // Two contacts: the pen contact plus the synthetic scroll finger.
        ASSERT_NE(created.abs_info(ABS_MT_SLOT), nullptr);
        EXPECT_GE(created.abs_info(ABS_MT_SLOT)->maximum, 1);

        // MT absinfo: bounded tracking ids (profile constant), position
        // copied from the source axis (non-empty range).
        ASSERT_NE(created.abs_info(ABS_MT_TRACKING_ID), nullptr);
        EXPECT_EQ(created.abs_info(ABS_MT_TRACKING_ID)->minimum, 0);
        EXPECT_EQ(created.abs_info(ABS_MT_TRACKING_ID)->maximum, 65'535);
        ASSERT_NE(created.abs_info(ABS_MT_POSITION_X), nullptr);
        EXPECT_GT(created.abs_info(ABS_MT_POSITION_X)->maximum, 0);

        EXPECT_TRUE(created.has_property(INPUT_PROP_POINTER));
        EXPECT_FALSE(created.has_property(INPUT_PROP_DIRECT));
    }

} // namespace

// ---------------------------------------------------------------------------
// Tool rename: BTN_TOOL_* -> BTN_TOOL_FINGER
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, RenamesPenToolToFinger) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_SYN,   SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The tool press also opens the hover contact (see
    // ToolPressBeginsContact), so the renamed key is no longer the first
    // recorded event — look it up instead.
    auto const finger = collect(col.events(), EV_KEY, BTN_TOOL_FINGER);
    ASSERT_EQ(finger.size(), 1U);
    EXPECT_EQ(finger[0].value(), 1);
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_TOOL_PEN).empty());
}

TEST(Pen2TouchTest, RenamesRubberToolToFinger) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_RUBBER, 1},
        {EV_SYN,      SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const finger = collect(col.events(), EV_KEY, BTN_TOOL_FINGER);
    ASSERT_EQ(finger.size(), 1U);
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_TOOL_RUBBER).empty());
}

TEST(Pen2TouchTest, RenamesAllPenToolVariants) {
    // Each pipeline is consteval, so we test each tool variant individually.
    {
        auto pipeline =
          context
          | emit_all[{
            {EV_KEY, BTN_TOOL_BRUSH, 1},
            {EV_SYN,     SYN_REPORT, 0}
        }]
          | pen2touch
          | record;
        pipeline();
        auto const finger = collect(pipeline.mod<basic_record>().events(), EV_KEY, BTN_TOOL_FINGER);
        ASSERT_EQ(finger.size(), 1U);
    }
    {
        auto pipeline =
          context
          | emit_all[{
            {EV_KEY, BTN_TOOL_PENCIL, 1},
            {EV_SYN,      SYN_REPORT, 0}
        }]
          | pen2touch
          | record;
        pipeline();
        auto const finger = collect(pipeline.mod<basic_record>().events(), EV_KEY, BTN_TOOL_FINGER);
        ASSERT_EQ(finger.size(), 1U);
    }
    {
        auto pipeline =
          context
          | emit_all[{
            {EV_KEY, BTN_TOOL_AIRBRUSH, 1},
            {EV_SYN,        SYN_REPORT, 0}
        }]
          | pen2touch
          | record;
        pipeline();
        auto const finger = collect(pipeline.mod<basic_record>().events(), EV_KEY, BTN_TOOL_FINGER);
        ASSERT_EQ(finger.size(), 1U);
    }
    {
        auto pipeline =
          context
          | emit_all[{
            {EV_KEY, BTN_TOOL_MOUSE, 1},
            {EV_SYN,     SYN_REPORT, 0}
        }]
          | pen2touch
          | record;
        pipeline();
        auto const finger = collect(pipeline.mod<basic_record>().events(), EV_KEY, BTN_TOOL_FINGER);
        ASSERT_EQ(finger.size(), 1U);
    }
    {
        auto pipeline =
          context
          | emit_all[{
            {EV_KEY, BTN_TOOL_LENS, 1},
            {EV_SYN,    SYN_REPORT, 0}
        }]
          | pen2touch
          | record;
        pipeline();
        auto const finger = collect(pipeline.mod<basic_record>().events(), EV_KEY, BTN_TOOL_FINGER);
        ASSERT_EQ(finger.size(), 1U);
    }
}

// ---------------------------------------------------------------------------
// Contact begin on tip press (tablets without BTN_TOOL_*: the tip alone
// drives the contact — see ToolPressBeginsContact for the proximity path)
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TouchDownEmitsTrackingId) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        // Position before touch
        {EV_ABS,        ABS_X, 1000},
        {EV_ABS,        ABS_Y, 2000},
        {EV_ABS, ABS_PRESSURE,   50},
        {EV_SYN,   SYN_REPORT,    0},
        // Touch down
        {EV_KEY,    BTN_TOUCH,    1},
        {EV_SYN,   SYN_REPORT,    0},
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
        {EV_KEY,  BTN_TOUCH, 1},
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
        {EV_ABS,      ABS_X, 1500},
        {EV_ABS,      ABS_Y, 2500},
        {EV_SYN, SYN_REPORT,    0},
        {EV_KEY,  BTN_TOUCH,    1},
        {EV_SYN, SYN_REPORT,    0},
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

TEST(Pen2TouchTest, TouchDownKeepsPressureLegacyOnly) {
    auto pipeline =
      context
      | emit_all[{
        {EV_ABS, ABS_PRESSURE, 42},
        {EV_SYN,   SYN_REPORT,  0},
        {EV_KEY,    BTN_TOUCH,  1},
        {EV_SYN,   SYN_REPORT,  0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Pressure stays on the legacy axis only: mirroring it into
    // ABS_MT_PRESSURE would flip libinput into pressure-based touch
    // detection, where pen pressure reads as palm or never reaches the
    // begin threshold.
    EXPECT_TRUE(collect(col.events(), EV_ABS, ABS_MT_PRESSURE).empty());
    auto const legacy = collect(col.events(), EV_ABS, ABS_PRESSURE);
    ASSERT_EQ(legacy.size(), 1U);
    EXPECT_EQ(legacy[0].value(), 42);
}

// ---------------------------------------------------------------------------
// Movement: ABS_X/Y while the contact is open (hover or touch) emits
// MT_POSITION_X/Y
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, MovementEmitsMTPosition) {
    auto pipeline =
      context
      | emit_all[{
        // Touch down
        {EV_ABS,      ABS_X, 1000},
        {EV_ABS,      ABS_Y, 2000},
        {EV_SYN, SYN_REPORT,    0},
        {EV_KEY,  BTN_TOUCH,    1},
        {EV_SYN, SYN_REPORT,    0},
        // Movement
        {EV_ABS,      ABS_X, 1100},
        {EV_ABS,      ABS_Y, 2100},
        {EV_SYN, SYN_REPORT,    0},
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
// Contact end on tip lift (tool-less tablets: without a BTN_TOOL_* the tip
// is the only end signal)
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TouchUpEmitsTrackingIdMinus1) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY,  BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        // Touch up
        {EV_KEY,  BTN_TOUCH, 0},
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
// Tip press: source BTN_TOUCH becomes the physical left button, and the
// contact (proximity) stays open across tip press/release
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TipPressMapsToBtnLeft) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_SYN,   SYN_REPORT, 0},
        {EV_KEY,    BTN_TOUCH, 1},
        {EV_SYN,   SYN_REPORT, 0},
        {EV_KEY,    BTN_TOUCH, 0},
        {EV_SYN,   SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The tip is the touchpad's left button — the raw BTN_TOUCH must not
    // leak: the output BTN_TOUCH only carries the contact flag, which
    // rises once at proximity and never dips with the tip cycle.
    auto const left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_EQ(left[0].value(), 1);
    EXPECT_EQ(left[1].value(), 0);
    auto const touch = collect(col.events(), EV_KEY, BTN_TOUCH);
    ASSERT_EQ(touch.size(), 1U);
    EXPECT_EQ(touch[0].value(), 1);

    // Hovering continues after the tip lifts: exactly one contact begin,
    // no contact end.
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tid.size(), 1U);
    EXPECT_GE(tid[0].value(), 0);
}

TEST(Pen2TouchTest, TipWithoutToolStillMapsToBtnLeft) {
    // Tablets that never send BTN_TOOL_* rely on the tip alone; the same
    // remap must apply.
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

    auto const left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_EQ(left[0].value(), 1);
    EXPECT_EQ(left[1].value(), 0);
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tid.size(), 2U);
    EXPECT_GE(tid[0].value(), 0);
    EXPECT_EQ(tid[1].value(), -1);
}

// ---------------------------------------------------------------------------
// Multiple contacts: tracking IDs increment
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, TrackingIdsIncrement) {
    auto pipeline =
      context
      | emit_all[{
        // First contact
        {EV_KEY,  BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY,  BTN_TOUCH, 0},
        {EV_SYN, SYN_REPORT, 0},
        // Second contact
        {EV_KEY,  BTN_TOUCH, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY,  BTN_TOUCH, 0},
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
// Barrel buttons map to regular mouse buttons
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, StylusButtonsMapToMouseButtons) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY,  BTN_STYLUS, 1},
        {EV_SYN,  SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS2, 1},
        {EV_SYN,  SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS3, 1},
        {EV_SYN,  SYN_REPORT, 0},
        {EV_KEY,  BTN_STYLUS, 0},
        {EV_SYN,  SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS2, 0},
        {EV_SYN,  SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS3, 0},
        {EV_SYN,  SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const right  = collect(col.events(), EV_KEY, BTN_RIGHT);
    auto const middle = collect(col.events(), EV_KEY, BTN_MIDDLE);
    auto const side   = collect(col.events(), EV_KEY, BTN_SIDE);
    ASSERT_EQ(right.size(), 2U);
    ASSERT_EQ(middle.size(), 2U);
    ASSERT_EQ(side.size(), 2U);
    EXPECT_EQ(right[0].value(), 1);
    EXPECT_EQ(right[1].value(), 0);
    EXPECT_EQ(middle[0].value(), 1);
    EXPECT_EQ(middle[1].value(), 0);
    EXPECT_EQ(side[0].value(), 1);
    EXPECT_EQ(side[1].value(), 0);

    // The stylus codes themselves must never leak through.
    for (auto const& event : col.without_syn()) {
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
        {EV_ABS,     ABS_TILT_X, 10},
        {EV_ABS,     ABS_TILT_Y, 20},
        {EV_ABS, ABS_TOOL_WIDTH,  5},
        {EV_SYN,     SYN_REPORT,  0},
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
        {EV_SYN,   SYN_REPORT,  0},
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
        {EV_ABS,        ABS_X, 1000},
        {EV_ABS,        ABS_Y, 2000},
        {EV_ABS, ABS_PRESSURE,   50},
        {EV_SYN,   SYN_REPORT,    0},
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
        {EV_KEY, BTN_TOOL_PEN,   1},
        {EV_SYN,   SYN_REPORT,   0},
        // Position + pressure
        {EV_ABS,        ABS_X, 500},
        {EV_ABS,        ABS_Y, 600},
        {EV_ABS, ABS_PRESSURE,  30},
        {EV_SYN,   SYN_REPORT,   0},
        // Touch down
        {EV_KEY,    BTN_TOUCH,   1},
        {EV_SYN,   SYN_REPORT,   0},
        // Movement
        {EV_ABS,        ABS_X, 510},
        {EV_ABS,        ABS_Y, 620},
        {EV_SYN,   SYN_REPORT,   0},
        // Touch up
        {EV_KEY,    BTN_TOUCH,   0},
        {EV_SYN,   SYN_REPORT,   0},
        // Tool released
        {EV_KEY, BTN_TOOL_PEN,   0},
        {EV_SYN,   SYN_REPORT,   0},
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

    // Pressure is never mirrored into ABS_MT_PRESSURE; the legacy axis
    // still carries it.
    EXPECT_TRUE(collect(col.events(), EV_ABS, ABS_MT_PRESSURE).empty());
    auto const abs_p = collect(col.events(), EV_ABS, ABS_PRESSURE);
    ASSERT_EQ(abs_p.size(), 1U);
    EXPECT_EQ(abs_p[0].value(), 30);

    // Check MT slot is always 0
    auto const slots = collect(col.events(), EV_ABS, ABS_MT_SLOT);
    for (auto const& slot : slots) {
        EXPECT_EQ(slot.value(), 0);
    }
}

// ---------------------------------------------------------------------------
// Hover: tool proximity alone opens the MT contact (the cursor only moves
// while libinput sees a contact, so hovering must look like a touch-down)
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ToolPressBeginsContact) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_ABS,        ABS_X, 1000},
        {EV_ABS,        ABS_Y, 2000},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The contact opens on proximity, not on BTN_TOUCH — and the output
    // BTN_TOUCH must rise with it: libinput keeps a tracking-id contact in
    // TOUCH_HOVERING (no motion) until it sees BTN_TOUCH.
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tid.size(), 1U);
    EXPECT_GE(tid[0].value(), 0);
    auto const touch = collect(col.events(), EV_KEY, BTN_TOUCH);
    ASSERT_EQ(touch.size(), 1U);
    EXPECT_EQ(touch[0].value(), 1);
}

TEST(Pen2TouchTest, HoverMovementEmitsMTPosition) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        // Hovering: position moves without any BTN_TOUCH.
        {EV_ABS,        ABS_X, 1000},
        {EV_ABS,        ABS_Y, 2000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1100},
        {EV_ABS,        ABS_Y, 2100},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Hover motion is the cursor motion: every ABS_X/Y while the pen is in
    // proximity must be mirrored into the MT slot.
    auto const pos_x = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    auto const pos_y = collect(col.events(), EV_ABS, ABS_MT_POSITION_Y);
    ASSERT_EQ(pos_x.size(), 2U);
    ASSERT_EQ(pos_y.size(), 2U);
    EXPECT_EQ(pos_x[0].value(), 1000);
    EXPECT_EQ(pos_x[1].value(), 1100);
    EXPECT_EQ(pos_y[0].value(), 2000);
    EXPECT_EQ(pos_y[1].value(), 2100);
    // No tip press: the contact must stay open (no TRACKING_ID -1).
    for (auto const& event : collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID)) {
        EXPECT_GE(event.value(), 0);
    }
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
// toggle_off releases a held barrel button (no keys_state required)
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ToggleOffReleasesHeldBarrelButton) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_STYLUS3, 1},
        {EV_SYN,  SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline(); // press only; the pipeline exits with the button still held

    // Cleaning up must release the mapped mouse button, or consumers keep
    // seeing a stuck side button.  (BTN_STYLUS / BTN_STYLUS2 are buffered
    // scroll triggers — they never hold a button down, so there is
    // nothing for cleanup to release.)
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(toggle_off), context_action::next);

    auto const side = collect(col.events(), EV_KEY, BTN_SIDE);
    ASSERT_GE(side.size(), 2U);
    EXPECT_EQ(side[0].value(), 1);
    EXPECT_EQ(side[1].value(), 0);
}

// ---------------------------------------------------------------------------
// toggle_off releases a held tip press (no stuck left button)
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ToggleOffReleasesHeldTip) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN, 1},
        {EV_SYN,   SYN_REPORT, 0},
        {EV_KEY,    BTN_TOUCH, 1},
        {EV_SYN,   SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline(); // proximity + tip down; the pipeline exits mid-press

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(toggle_off), context_action::next);

    auto const left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_GE(left.size(), 2U);
    EXPECT_EQ(left[0].value(), 1);
    EXPECT_EQ(left[1].value(), 0);
    // The hover contact goes down with it, frame closed by its own SYN.
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tid.size(), 2U);
    EXPECT_EQ(tid[1].value(), -1);
}

// ---------------------------------------------------------------------------
// toggle_off releases a tip-as-middle click held during a scroll
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, ToggleOffReleasesHeldMiddleClick) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1000},
        {EV_ABS,        ABS_Y, 1000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY,  BTN_STYLUS2,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY,    BTN_TOUCH,    1},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline(); // scroll engaged + tip as middle; exits with the click held

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(toggle_off), context_action::next);

    auto const middle = collect(col.events(), EV_KEY, BTN_MIDDLE);
    ASSERT_GE(middle.size(), 2U);
    EXPECT_EQ(middle[0].value(), 1);
    EXPECT_EQ(middle[1].value(), 0);
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_LEFT).empty());
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

    // ...and the tip press that can never arrive: release BTN_LEFT too.
    auto const left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_EQ(left[0].value(), 1);
    EXPECT_EQ(left[1].value(), 0);
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
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_LEFT));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_SLOT));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_TRACKING_ID));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_POSITION_X));
    EXPECT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_POSITION_Y));
    EXPECT_FALSE(tmpl.has_event_code(EV_ABS, ABS_MT_PRESSURE));
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
    EXPECT_GE(tmpl.abs_info(ABS_MT_SLOT)->maximum, 1); // pen contact + scroll finger
}

TEST(Pen2TouchTest, ProfileDeviceStripsSourceMtPressure) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());
    // A source tablet that advertises MT pressure itself must not leak
    // it into the virtual touchpad profile.
    static constexpr input_absinfo mt_pressure_info{.minimum = 0, .maximum = 8191};
    tmpl.abs_info(ABS_MT_PRESSURE, mt_pressure_info);
    ASSERT_TRUE(tmpl.has_event_code(EV_ABS, ABS_MT_PRESSURE));

    auto          pipeline = context | pen2touch | record;
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);

    EXPECT_FALSE(tmpl.has_event_code(EV_ABS, ABS_MT_PRESSURE));
}

TEST(Pen2TouchTest, ProfileDeviceSetsVirtualTouchpadName) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());
    ASSERT_EQ(tmpl.device_name(), "Pen2Touch Test Tablet");

    auto          pipeline = context | pen2touch | record;
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);

    // A fixed name: the clone must not inherit the tablet's name (which
    // would masquerade as hardware and could match libinput quirks).
    EXPECT_EQ(tmpl.device_name(), "Foresight Virtual Touchpad");
}

TEST(Pen2TouchTest, ProfileDeviceAdvertisesBarrelButtons) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());
    ASSERT_TRUE(tmpl.has_event_code(EV_KEY, BTN_LEFT));

    auto          pipeline = context | pen2touch | record;
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);

    // The stylus buttons become regular mouse buttons; BTN_LEFT stays on
    // because it is the pen tip press — and its presence flips libinput's
    // tap-to-click default off, so hover enter/leave can't fake a tap.
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_RIGHT));
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_MIDDLE));
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_SIDE));
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, BTN_LEFT));
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_STYLUS));
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_STYLUS2));
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_STYLUS3));
    EXPECT_FALSE(tmpl.has_property(INPUT_PROP_BUTTONPAD));
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

TEST(Pen2TouchTest, ProfileDeviceIgnoresKeyboardTemplate) {
    // The keyboard-route virtual device is cloned from a real keyboard:
    // reshaping it into a touchpad would break typing.  A tablet always
    // has ABS_X, a keyboard never does — that is the discriminator.
    auto tmpl = fs8::evdev{libevdev_new(), fs8::evdev_status::success};
    tmpl.device_name("Pen2Touch Test Keyboard");
    libevdev_set_id_bustype(tmpl.device_ptr(), BUS_USB);
    tmpl.enable_event_type(EV_SYN);
    tmpl.enable_event_code(EV_KEY, KEY_A);
    tmpl.enable_event_code(EV_KEY, KEY_CAPSLOCK);
    tmpl.enable_event_code(EV_KEY, BTN_0);
    ASSERT_TRUE(tmpl.is_ok());

    auto          pipeline = context | pen2touch | record;
    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);

    EXPECT_EQ(tmpl.device_name(), "Pen2Touch Test Keyboard");
    EXPECT_TRUE(tmpl.has_event_code(EV_KEY, KEY_A));
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_TOUCH));
    EXPECT_FALSE(tmpl.has_event_code(EV_KEY, BTN_TOOL_FINGER));
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

    expect_touchpad_caps(created);

    // The synthetic source's axis ranges are copied verbatim into MT.
    ASSERT_NE(created.abs_info(ABS_MT_POSITION_X), nullptr);
    EXPECT_EQ(created.abs_info(ABS_MT_POSITION_X)->maximum, 32'767);

    vdev.close();
}

// ---------------------------------------------------------------------------
// App path: output_selector(start) -> tracked_devices -> finalize_device ->
// profile_device broadcast through the full app mod tuple.  This is what
// `pen2touch -o uinput` runs on startup; none of the tests above reach it.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, AppStartProfilesUinputDevice) {
    auto const access = fs8::verify_access_to_uinput();
    if (access != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(access);
    }

    // The app's pipeline minus `singleton` (a system-wide lock, irrelevant
    // here) and `intercept`: uinput's context overload clones the first
    // tracked device unconditionally, so the query layer adds nothing to
    // this path and intercept would only require real hardware.
    static constinit auto pipeline = context | io_manager | input_manager | keys_state | pen2touch | drop_adjacent_syns | sieve | output;

    pipeline.mod(output).set_selected(1); // uinput

    // `im.add` announces the device through dynamic_context, which must be
    // bound even outside run_loop.
    dynamic_scope scope{dynamic_context, pipeline};
    pipeline.mod<basic_input_manager>().add(make_tablet_template());

    ASSERT_EQ(pipeline(start), context_action::next);

    auto& uin = pipeline.mod(output).output<1>();
    ASSERT_TRUE(uin.is_ok());
    if (!fs8::test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    fs8::evdev created{uin.devnode()};
    ASSERT_TRUE(created.is_ok()) << uin.devnode();
    expect_touchpad_caps(created);

    // The synthetic source's axis ranges are copied verbatim into MT.
    ASSERT_NE(created.abs_info(ABS_MT_POSITION_X), nullptr);
    EXPECT_EQ(created.abs_info(ABS_MT_POSITION_X)->maximum, 32'767);

    uin.close();
}

TEST(Pen2TouchTest, StartWithPressedToolIsSilentOnFirstRun) {
    auto  pipeline = context | input_manager | keys_state | pen2touch | record;
    auto& col      = pipeline.mod<basic_record>();

    // The pen is already in proximity before the pipeline starts, so
    // keys_state seeds BTN_TOOL_PEN as pressed.
    pipeline.mod<basic_keys_state>()(event_type{EV_KEY, BTN_TOOL_PEN, 1});

    ASSERT_EQ(pipeline(start), context_action::next);

    // pen2touch has never run before: there is no contact of ours to
    // release, and emitting during start would race the mods further
    // down the pipeline (the output's uinput does not exist yet — it is
    // created when the output's own start runs, after ours).
    EXPECT_TRUE(col.events().empty()) << "forked " << col.events().size() << " events during start";
}

TEST(Pen2TouchTest, AppEventFlowWritesTouchpadEventsToKernel) {
    auto const access = fs8::verify_access_to_uinput();
    if (access != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(access);
    }

    // Same app shape as AppStartProfilesUinputDevice, with `record` behind
    // `output`: output_selector returns drop_event when the kernel rejects a
    // write, and invoke_mods stops at the first non-next mod, so record only
    // ever sees events the uinput device actually accepted.
    static constinit auto pipeline =
      context | io_manager | input_manager | keys_state | pen2touch | drop_adjacent_syns | sieve | output | record;

    pipeline.mod(output).set_selected(1); // uinput

    dynamic_scope scope{dynamic_context, pipeline};
    pipeline.mod<basic_input_manager>().add(make_tablet_template());

    ASSERT_EQ(pipeline(start), context_action::next);

    auto& uin = pipeline.mod(output).output<1>();
    ASSERT_TRUE(uin.is_ok());
    if (!fs8::test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    auto& col = pipeline.mod<basic_record>();
    col.clear();

    // Feed a full pen frame sequence by hand instead of run_loop: no
    // next_event providers are involved, so io_manager never polls.
    auto const push = [&](event_type const& inp_event, std::string_view const what) {
        pipeline.event(inp_event);
        EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next) << what;
    };

    push({EV_KEY, BTN_TOOL_PEN, 1}, "BTN_TOOL_PEN=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_X, 1000}, "ABS_X=1000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_Y, 2000}, "ABS_Y=2000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 1}, "BTN_TOUCH=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_X, 1500}, "ABS_X=1500 while touching");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 0}, "BTN_TOUCH=0");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOOL_PEN, 0}, "BTN_TOOL_PEN=0");

    // The tool became BTN_TOOL_FINGER before reaching record.
    auto const finger = collect(col.events(), EV_KEY, BTN_TOOL_FINGER);
    ASSERT_GE(finger.size(), 2U);
    EXPECT_EQ(finger[0].value(), 1);
    EXPECT_EQ(finger[1].value(), 0);
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_TOOL_PEN).empty());

    // One contact: tracking id start, then end.
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_GE(tid.size(), 2U);
    EXPECT_GE(tid[0].value(), 0);
    EXPECT_EQ(tid[1].value(), -1);

    // MT position: begin_contact forks the cached position, then the move
    // while touching forks the new one — the kernel only accepted those
    // because profile_device advertised ABS_MT_POSITION_X.
    auto const pos_x = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_GE(pos_x.size(), 2U) << "ABS_MT_POSITION_X never reached the kernel (profile missing?)";
    EXPECT_EQ(pos_x.back().value(), 1500);

    // Legacy axes pass through unchanged.
    auto const abs_x = collect(col.events(), EV_ABS, ABS_X);
    ASSERT_FALSE(abs_x.empty());
    EXPECT_EQ(abs_x[0].value(), 1000);

    // The tip press reached the kernel as BTN_LEFT (the profile
    // advertises it), never as raw BTN_TOUCH.
    auto const left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_EQ(left[0].value(), 1);
    EXPECT_EQ(left[1].value(), 0);

    uin.close();
}

TEST(Pen2TouchTest, RealTabletThroughInterceptProfilesDevice) {
    auto const access = fs8::verify_access_to_uinput();
    if (access != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(access);
    }

    // Exactly the app's pipeline minus `singleton`.  `pen2touch -o uinput`
    // with no positional argument contributes no second query (an empty
    // parsed_args range yields no queries), so `tablet | required` alone is
    // what the live run uses.
    static constinit auto pipeline =
      context | io_manager | input_manager | intercept[tablet | required] | keys_state | pen2touch | drop_adjacent_syns | sieve | output;

    pipeline.mod(output).set_selected(1); // uinput

    dynamic_scope scope{dynamic_context, pipeline};
    auto const    res = pipeline(start);
    if (res != context_action::next) {
        // No tablet matched (or no input hardware at all): uinput's context
        // overload reports `recovery` when the device list is empty.
        GTEST_SKIP() << "start did not produce a device: " << to_string(res);
    }

    auto& uin = pipeline.mod(output).output<1>();
    ASSERT_TRUE(uin.is_ok());
    if (!fs8::test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    fs8::evdev created{uin.devnode()};
    ASSERT_TRUE(created.is_ok()) << uin.devnode();
    expect_touchpad_caps(created);

    uin.close();
}

// ---------------------------------------------------------------------------
// App path: the source tablet vanishes mid-contact — the kernel must see a
// full release, and the replacement tablet's contact must still get through.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, AppDeviceSwitchReleasesStaleContact) {
    auto const access = fs8::verify_access_to_uinput();
    if (access != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(access);
    }

    static constinit auto pipeline =
      context | io_manager | input_manager | keys_state | pen2touch | drop_adjacent_syns | sieve | output | record;

    pipeline.mod(output).set_selected(1); // uinput

    dynamic_scope scope{dynamic_context, pipeline};
    pipeline.mod<basic_input_manager>().add(make_tablet_template());

    ASSERT_EQ(pipeline(start), context_action::next);

    auto& uin = pipeline.mod(output).output<1>();
    ASSERT_TRUE(uin.is_ok());
    if (!fs8::test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    auto& col = pipeline.mod<basic_record>();
    col.clear();

    auto const push = [&](event_type const& inp_event, std::string_view const what) {
        pipeline.event(inp_event);
        EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next) << what;
    };

    // Open a contact on tablet A.
    push({EV_KEY, BTN_TOOL_PEN, 1}, "BTN_TOOL_PEN=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_X, 1000}, "ABS_X=1000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_Y, 2000}, "ABS_Y=2000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 1}, "BTN_TOUCH=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    ASSERT_FALSE(collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID).empty()) << "contact never opened";

    // Tablet A disappears.
    col.clear();
    std::uint32_t dead_source = 0xDEAD'BEEF;
    EXPECT_EQ(dynamic_context->broadcast(device_disconnected + &dead_source), context_action::next);

    // The kernel must see a complete release frame for the stale contact.
    auto const released_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(released_tid.size(), 1U) << "stale contact was not released";
    EXPECT_EQ(released_tid[0].value(), -1);
    auto const released_touch = collect(col.events(), EV_KEY, BTN_TOUCH);
    ASSERT_EQ(released_touch.size(), 1U);
    EXPECT_EQ(released_touch[0].value(), 0);
    // The tip was down when the tablet vanished; it must be released too.
    auto const released_left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(released_left.size(), 1U);
    EXPECT_EQ(released_left[0].value(), 0);
    EXPECT_TRUE(col.any([](event_type const& e) noexcept {
        return e.code() == BTN_TOOL_FINGER && e.value() == 0;
    }));
    ASSERT_FALSE(col.empty());
    EXPECT_EQ(col.back().type(), EV_SYN);

    // Tablet B takes over: its contact must reach the kernel too.
    col.clear();
    pipeline.mod<basic_input_manager>().add(make_tablet_template());
    push({EV_KEY, BTN_TOOL_PEN, 1}, "tablet B: BTN_TOOL_PEN=1");
    push({EV_SYN, SYN_REPORT, 0}, "tablet B: syn");
    push({EV_ABS, ABS_X, 700}, "tablet B: ABS_X=700");
    push({EV_SYN, SYN_REPORT, 0}, "tablet B: syn");
    push({EV_ABS, ABS_Y, 800}, "tablet B: ABS_Y=800");
    push({EV_SYN, SYN_REPORT, 0}, "tablet B: syn");
    push({EV_KEY, BTN_TOUCH, 1}, "tablet B: BTN_TOUCH=1");
    push({EV_SYN, SYN_REPORT, 0}, "tablet B: syn");

    auto const new_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(new_tid.size(), 1U) << "the second tablet's contact never reached the kernel";
    EXPECT_GE(new_tid[0].value(), 0);
    auto const new_pos = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_FALSE(new_pos.empty());
    EXPECT_EQ(new_pos[0].value(), 700);

    uin.close();
}

// ---------------------------------------------------------------------------
// App path: toggle_off mid-contact writes a complete release to the kernel.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, AppToggleOffWritesReleaseToKernel) {
    auto const access = fs8::verify_access_to_uinput();
    if (access != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(access);
    }

    static constinit auto pipeline =
      context | io_manager | input_manager | keys_state | pen2touch | drop_adjacent_syns | sieve | output | record;

    pipeline.mod(output).set_selected(1); // uinput

    dynamic_scope scope{dynamic_context, pipeline};
    pipeline.mod<basic_input_manager>().add(make_tablet_template());

    ASSERT_EQ(pipeline(start), context_action::next);

    auto& uin = pipeline.mod(output).output<1>();
    ASSERT_TRUE(uin.is_ok());
    if (!fs8::test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    auto& col = pipeline.mod<basic_record>();
    col.clear();

    auto const push = [&](event_type const& inp_event, std::string_view const what) {
        pipeline.event(inp_event);
        EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next) << what;
    };

    // Open a contact.
    push({EV_KEY, BTN_TOOL_PEN, 1}, "BTN_TOOL_PEN=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_X, 1000}, "ABS_X=1000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_Y, 2000}, "ABS_Y=2000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 1}, "BTN_TOUCH=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    ASSERT_FALSE(collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID).empty()) << "contact never opened";

    // Disable the mod mid-contact.
    col.clear();
    EXPECT_EQ(dynamic_context->broadcast(toggle_off), context_action::next);

    // The kernel must see the full release: contact, tool and a frame end.
    auto const released_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(released_tid.size(), 1U) << "stale contact was not released";
    EXPECT_EQ(released_tid[0].value(), -1);
    auto const released_touch = collect(col.events(), EV_KEY, BTN_TOUCH);
    ASSERT_EQ(released_touch.size(), 1U);
    EXPECT_EQ(released_touch[0].value(), 0);
    // The tip was down when the mod was disabled; release it as well.
    auto const released_left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(released_left.size(), 1U);
    EXPECT_EQ(released_left[0].value(), 0);
    EXPECT_TRUE(col.any([](event_type const& e) noexcept {
        return e.code() == BTN_TOOL_FINGER && e.value() == 0;
    }));
    ASSERT_FALSE(col.empty());
    EXPECT_EQ(col.back().type(), EV_SYN);

    // Re-enabled: a fresh contact works again.
    col.clear();
    push({EV_KEY, BTN_TOOL_PEN, 1}, "BTN_TOOL_PEN=1 again");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 1}, "BTN_TOUCH=1 again");
    push({EV_SYN, SYN_REPORT, 0}, "syn");

    auto const reopened_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(reopened_tid.size(), 1U) << "contact did not reopen after toggle_on";
    EXPECT_GE(reopened_tid[0].value(), 0);

    uin.close();
}

// ---------------------------------------------------------------------------
// App path: a pipeline restart (start tag) mid-contact releases the stale
// contact instead of silently forgetting it.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, AppRestartReleasesStaleContact) {
    auto const access = fs8::verify_access_to_uinput();
    if (access != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(access);
    }

    static constinit auto pipeline =
      context | io_manager | input_manager | keys_state | pen2touch | drop_adjacent_syns | sieve | output | record;

    pipeline.mod(output).set_selected(1); // uinput

    dynamic_scope scope{dynamic_context, pipeline};
    pipeline.mod<basic_input_manager>().add(make_tablet_template());

    ASSERT_EQ(pipeline(start), context_action::next);

    auto& uin = pipeline.mod(output).output<1>();
    ASSERT_TRUE(uin.is_ok());
    if (!fs8::test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual touchpad did not become openable.";
    }

    auto& col = pipeline.mod<basic_record>();
    col.clear();

    auto const push = [&](event_type const& inp_event, std::string_view const what) {
        pipeline.event(inp_event);
        EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next) << what;
    };

    // Open a contact, then restart the pipeline underneath it.
    push({EV_KEY, BTN_TOOL_PEN, 1}, "BTN_TOOL_PEN=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_X, 1000}, "ABS_X=1000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_Y, 2000}, "ABS_Y=2000");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 1}, "BTN_TOUCH=1");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    ASSERT_FALSE(collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID).empty()) << "contact never opened";

    col.clear();
    ASSERT_EQ(pipeline(start), context_action::next);

    // The kernel must see the full release written during start.
    auto const released_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(released_tid.size(), 1U) << "stale contact was not released on restart";
    EXPECT_EQ(released_tid[0].value(), -1);
    auto const released_touch = collect(col.events(), EV_KEY, BTN_TOUCH);
    ASSERT_EQ(released_touch.size(), 1U);
    EXPECT_EQ(released_touch[0].value(), 0);
    // The tip was down at restart time; release it as well.
    auto const released_left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(released_left.size(), 1U);
    EXPECT_EQ(released_left[0].value(), 0);
    EXPECT_TRUE(col.any([](event_type const& e) noexcept {
        return e.code() == BTN_TOOL_FINGER && e.value() == 0;
    }));
    ASSERT_FALSE(col.empty());
    EXPECT_EQ(col.back().type(), EV_SYN);

    // A fresh contact after the restart works.
    col.clear();
    push({EV_KEY, BTN_TOOL_PEN, 1}, "BTN_TOOL_PEN=1 after restart");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_X, 700}, "ABS_X=700 after restart");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_ABS, ABS_Y, 800}, "ABS_Y=800 after restart");
    push({EV_SYN, SYN_REPORT, 0}, "syn");
    push({EV_KEY, BTN_TOUCH, 1}, "BTN_TOUCH=1 after restart");
    push({EV_SYN, SYN_REPORT, 0}, "syn");

    auto const new_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(new_tid.size(), 1U) << "contact did not reopen after restart";
    EXPECT_GE(new_tid[0].value(), 0);
    auto const new_pos = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_FALSE(new_pos.empty());
    EXPECT_EQ(new_pos[0].value(), 700);

    uin.close();
}

// ---------------------------------------------------------------------------
// Unknown control tags: dropped without side effects
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, UnknownControlTagDropsWithoutSideEffects) {
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

    dynamic_scope scope{dynamic_context, pipeline};

    // An unhandled control tag must neither be claimed nor disturb the
    // open contact.
    auto const before = col.size();
    EXPECT_EQ(dynamic_context->broadcast(idle), context_action::drop_event);
    EXPECT_EQ(col.size(), before);
    auto const open_tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(open_tid.size(), 1U);
    EXPECT_GE(open_tid[0].value(), 0);

    // A handled tag still works right after it.
    EXPECT_EQ(dynamic_context->broadcast(toggle_off), context_action::next);
    auto const tid = collect(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tid.size(), 2U);
    EXPECT_GE(tid[0].value(), 0);
    EXPECT_EQ(tid[1].value(), -1);
}

// ---------------------------------------------------------------------------
// Two-finger scroll: holding the barrel button (or capslock) opens a second
// contact next to the pen contact, which libinput reads as a scroll gesture.
// A quick tap still clicks.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, Stylus2QuickTapEmitsMiddleClick) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_STYLUS2, 1},
        {EV_SYN,  SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS2, 0},
        {EV_SYN,  SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const middle = collect(col.events(), EV_KEY, BTN_MIDDLE);
    ASSERT_EQ(middle.size(), 2U);
    EXPECT_EQ(middle[0].value(), 1);
    EXPECT_EQ(middle[1].value(), 0);

    // The barrel code itself must never leak, and a tap must not open a
    // scroll contact.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_STYLUS2).empty());
    for (auto const& slot : collect(col.events(), EV_ABS, ABS_MT_SLOT)) {
        EXPECT_EQ(slot.value(), 0);
    }
}

TEST(Pen2TouchTest, Stylus2HoldScrollsInSecondSlot) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,  0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS2,   1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_ABS,     ABS_X, 1200, 20ms),
        timed_ev(EV_ABS,     ABS_Y, 1400, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        timed_ev(EV_KEY, BTN_STYLUS2,   0, 300ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 300ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The hold became a scroll, not a middle click.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_MIDDLE).empty());
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_STYLUS2).empty());

    // Slot 0 is the pen contact; slot 1 opens on engage and lifts on
    // release, with a tracking id distinct from the pen's.
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 3U);
    EXPECT_EQ(tracking[0].slot, 0);
    EXPECT_GE(tracking[0].event.value(), 0);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_GE(tracking[1].event.value(), 0);
    EXPECT_NE(tracking[1].event.value(), tracking[0].event.value());
    EXPECT_EQ(tracking[2].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);

    // The scroll finger sits a fixed offset away from the pen contact and
    // follows its moves (default offset: range/8 of 32767 = 4095).
    static constexpr event_type::value_type offset = 32'767 / 8;
    auto const                              pos_x  = collect_slotted(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_EQ(pos_x.size(), 4U);
    EXPECT_EQ(pos_x[0].slot, 0);
    EXPECT_EQ(pos_x[0].event.value(), 1000);
    EXPECT_EQ(pos_x[1].slot, 1);
    EXPECT_EQ(pos_x[1].event.value(), 1000 + offset);
    EXPECT_EQ(pos_x[2].slot, 0);
    EXPECT_EQ(pos_x[2].event.value(), 1200);
    EXPECT_EQ(pos_x[3].slot, 1);
    EXPECT_EQ(pos_x[3].event.value(), 1200 + offset);
    auto const pos_y = collect_slotted(col.events(), EV_ABS, ABS_MT_POSITION_Y);
    ASSERT_EQ(pos_y.size(), 4U);
    EXPECT_EQ(pos_y[1].slot, 1);
    EXPECT_EQ(pos_y[1].event.value(), 1000); // only X is offset; Y mirrors
    EXPECT_EQ(pos_y[3].slot, 1);
    EXPECT_EQ(pos_y[3].event.value(), 1400);
}

TEST(Pen2TouchTest, Stylus2HoldWithoutMovementIsSilent) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,  0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS2, 1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,  0, 10ms),
        timed_ev(EV_KEY, BTN_STYLUS2, 0, 400ms),
        timed_ev(EV_SYN, SYN_REPORT,  0, 400ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Too long for a tap: no click, and the scroll contact opened and
    // closed again.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_MIDDLE).empty());
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 3U);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_GE(tracking[1].event.value(), 0);
    EXPECT_EQ(tracking[2].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);
}

TEST(Pen2TouchTest, StylusQuickTapEmitsRightClick) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_STYLUS, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const right = collect(col.events(), EV_KEY, BTN_RIGHT);
    ASSERT_EQ(right.size(), 2U);
    EXPECT_EQ(right[0].value(), 1);
    EXPECT_EQ(right[1].value(), 0);

    // The barrel code itself must never leak, and a tap must not open a
    // scroll contact.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_STYLUS).empty());
    for (auto const& slot : collect(col.events(), EV_ABS, ABS_MT_SLOT)) {
        EXPECT_EQ(slot.value(), 0);
    }
}

// libinput diffs physical button state across frames
// (tp_post_physical_buttons): a press and its release inside one frame
// net to no change and are dropped, so the tap click must span two
// frames — SYN_REPORT between press and release.
TEST(Pen2TouchTest, QuickTapClickSpansTwoFrames) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_STYLUS, 1},
        {EV_SYN, SYN_REPORT, 0},
        {EV_KEY, BTN_STYLUS, 0},
        {EV_SYN, SYN_REPORT, 0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const right = collect(col.events(), EV_KEY, BTN_RIGHT);
    ASSERT_EQ(right.size(), 2U);
    bool syn_between = false;
    bool seen_press  = false;
    for (auto const& e : col.events()) {
        if (e.type() == EV_KEY && e.code() == BTN_RIGHT && e.value() == 1) {
            seen_press = true;
        } else if (seen_press && e.type() == EV_SYN && e.code() == SYN_REPORT) {
            syn_between = true;
        } else if (e.type() == EV_KEY && e.code() == BTN_RIGHT && e.value() == 0) {
            break;
        }
    }
    EXPECT_TRUE(syn_between);
}

// A second contact must be announced through the legacy two-finger key:
// libinput counts BTN_TOOL_* to decide how many hovering slots to wake
// (tp_fake_finger_count), so exactly one of BTN_TOOL_FINGER /
// BTN_TOOL_DOUBLETAP may be lit at any frame boundary — with only
// BTN_TOOL_FINGER set, the scroll finger stays TOUCH_HOVERING forever
// and never produces a scroll gesture.
TEST(Pen2TouchTest, ScrollAnnouncesDoubletapToolKey) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,  0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS,    1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_ABS,     ABS_X, 1200, 20ms),
        timed_ev(EV_ABS,     ABS_Y, 1400, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        timed_ev(EV_KEY, BTN_STYLUS,    0, 300ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 300ms),
        timed_ev(EV_KEY, BTN_TOOL_PEN,  0, 400ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 400ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const finger = collect(col.events(), EV_KEY, BTN_TOOL_FINGER);
    ASSERT_EQ(finger.size(), 4U);
    EXPECT_EQ(finger[0].value(), 1);    // proximity: one finger
    EXPECT_EQ(finger[1].value(), 0);    // scroll engage: two fingers
    EXPECT_EQ(finger[2].value(), 1);    // scroll end: back to one
    EXPECT_EQ(finger[3].value(), 0);    // tool leaves
    auto const doubletap = collect(col.events(), EV_KEY, BTN_TOOL_DOUBLETAP);
    ASSERT_EQ(doubletap.size(), 2U);
    EXPECT_EQ(doubletap[0].value(), 1); // scroll engage
    EXPECT_EQ(doubletap[1].value(), 0); // scroll end

    // Never both tool keys lit at a frame boundary — libinput logs a
    // kernel bug for that ("Invalid fake finger state").
    unsigned lit_finger = 0, lit_doubletap = 0;
    for (auto const& e : col.events()) {
        if (e.type() == EV_KEY && e.code() == BTN_TOOL_FINGER) {
            lit_finger = static_cast<unsigned>(e.value());
        } else if (e.type() == EV_KEY && e.code() == BTN_TOOL_DOUBLETAP) {
            lit_doubletap = static_cast<unsigned>(e.value());
        } else if (e.type() == EV_SYN && e.code() == SYN_REPORT) {
            EXPECT_FALSE(lit_finger != 0 && lit_doubletap != 0);
        }
    }
}

TEST(Pen2TouchTest, StylusHoldScrollsInSecondSlot) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,  0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS,    1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_ABS,     ABS_X, 1200, 20ms),
        timed_ev(EV_ABS,     ABS_Y, 1400, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        timed_ev(EV_KEY, BTN_STYLUS,    0, 300ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 300ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The hold became a scroll, not a right click.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_RIGHT).empty());
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_STYLUS).empty());

    // Slot 0 is the pen contact; slot 1 opens on engage and lifts on
    // release, with a tracking id distinct from the pen's.
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 3U);
    EXPECT_EQ(tracking[0].slot, 0);
    EXPECT_GE(tracking[0].event.value(), 0);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_GE(tracking[1].event.value(), 0);
    EXPECT_NE(tracking[1].event.value(), tracking[0].event.value());
    EXPECT_EQ(tracking[2].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);

    // The scroll finger sits a fixed offset away from the pen contact and
    // follows its moves (default offset: range/8 of 32767 = 4095).
    static constexpr event_type::value_type offset = 32'767 / 8;
    auto const                              pos_x  = collect_slotted(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_EQ(pos_x.size(), 4U);
    EXPECT_EQ(pos_x[0].slot, 0);
    EXPECT_EQ(pos_x[0].event.value(), 1000);
    EXPECT_EQ(pos_x[1].slot, 1);
    EXPECT_EQ(pos_x[1].event.value(), 1000 + offset);
    EXPECT_EQ(pos_x[2].slot, 0);
    EXPECT_EQ(pos_x[2].event.value(), 1200);
    EXPECT_EQ(pos_x[3].slot, 1);
    EXPECT_EQ(pos_x[3].event.value(), 1200 + offset);
    auto const pos_y = collect_slotted(col.events(), EV_ABS, ABS_MT_POSITION_Y);
    ASSERT_EQ(pos_y.size(), 4U);
    EXPECT_EQ(pos_y[1].slot, 1);
    EXPECT_EQ(pos_y[1].event.value(), 1000); // only X is offset; Y mirrors
    EXPECT_EQ(pos_y[3].slot, 1);
    EXPECT_EQ(pos_y[3].event.value(), 1400);
}

TEST(Pen2TouchTest, StylusHoldWithMovementNeverClicks) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,  0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS,    1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_ABS,     ABS_X, 1300, 30ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 30ms),
        timed_ev(EV_KEY, BTN_STYLUS,    0, 80ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 80ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Movement marks the hold as used: even a release inside the tap
    // window must not click, and the scroll contact opened and closed.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_RIGHT).empty());
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_STYLUS).empty());
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 3U);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_GE(tracking[1].event.value(), 0);
    EXPECT_EQ(tracking[2].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);
}

TEST(Pen2TouchTest, TipAlreadyDownLiftsOnScrollEngage) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,   0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY,    BTN_TOUCH,  1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS2,   1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_ABS, ABS_X, 1200, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        timed_ev(EV_KEY,    BTN_TOUCH,  0, 30ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 30ms),
        timed_ev(EV_KEY, BTN_STYLUS2,   0, 300ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 300ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The tip drag is lifted when the scroll engages, and its release
    // while scrolling stays swallowed (no extra BTN_LEFT, no click).
    auto const left = collect(col.events(), EV_KEY, BTN_LEFT);
    ASSERT_EQ(left.size(), 2U);
    EXPECT_EQ(left[0].value(), 1);
    EXPECT_EQ(left[1].value(), 0);
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_MIDDLE).empty());
}

TEST(Pen2TouchTest, TipClickDuringScrollEmitsMiddle) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,   0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS2,   1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_KEY,    BTN_TOUCH,  1, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        timed_ev(EV_ABS, ABS_X, 1200, 30ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 30ms),
        timed_ev(EV_KEY,    BTN_TOUCH,  0, 40ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 40ms),
        timed_ev(EV_KEY, BTN_STYLUS2,   0, 300ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 300ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The tip can never start a left click while the scroll trigger is
    // held; clicking it mid-scroll is a middle click instead.
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_LEFT).empty());
    auto const middle = collect(col.events(), EV_KEY, BTN_MIDDLE);
    ASSERT_EQ(middle.size(), 2U);
    EXPECT_EQ(middle[0].value(), 1);
    EXPECT_EQ(middle[1].value(), 0);
}

TEST(Pen2TouchTest, TipMiddleReleasesAfterScrollEnds) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,   0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS2,   1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_KEY,    BTN_TOUCH,  1, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        // The scroll ends while the tip is still down: the release must
        // still lift the middle button it pressed.
        timed_ev(EV_KEY, BTN_STYLUS2,   0, 300ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 300ms),
        timed_ev(EV_KEY,    BTN_TOUCH,  0, 400ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 400ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_LEFT).empty());
    auto const middle = collect(col.events(), EV_KEY, BTN_MIDDLE);
    ASSERT_EQ(middle.size(), 2U);
    EXPECT_EQ(middle[0].value(), 1);
    EXPECT_EQ(middle[1].value(), 0);
}

TEST(Pen2TouchTest, ToolGoneLiftsScrollFinger) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,   0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, BTN_STYLUS2,   1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_KEY, BTN_TOOL_PEN,  0, 20ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 20ms),
        timed_ev(EV_KEY, BTN_STYLUS2,   0, 400ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 400ms),
    }}
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Both contacts die with the tool: no stuck scroll finger.
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 4U);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_GE(tracking[1].event.value(), 0);
    EXPECT_EQ(tracking[2].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);
    EXPECT_EQ(tracking[3].slot, 0);
    EXPECT_EQ(tracking[3].event.value(), -1);
    EXPECT_TRUE(collect(col.events(), EV_KEY, BTN_MIDDLE).empty());
}

TEST(Pen2TouchTest, CapsLockEngagesScroll) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1000},
        {EV_ABS,        ABS_Y, 1000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, KEY_CAPSLOCK,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, KEY_CAPSLOCK,    0},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // Capslock opens and closes the scroll finger...
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 3U);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_GE(tracking[1].event.value(), 0);
    EXPECT_EQ(tracking[2].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);
    // ...while the key itself passes through untouched (on_held downstream
    // owns the swallow/LED logic).
    auto const caps = collect(col.events(), EV_KEY, KEY_CAPSLOCK);
    ASSERT_EQ(caps.size(), 2U);
    EXPECT_EQ(caps[0].value(), 1);
    EXPECT_EQ(caps[1].value(), 0);
}

// The app composition: pen2touch sees the key first (scroll trigger),
// then on_held owns the swallow/tap-re-emit.  A hold past the threshold
// never reaches the output, while the scroll finger keeps working.
TEST(Pen2TouchTest, CapsLockHoldIsSwallowedByOnHeld) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,   0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, KEY_CAPSLOCK,  1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_ABS,     ABS_X, 1500, 50ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 50ms),
        timed_ev(EV_KEY, KEY_CAPSLOCK,  0, 400ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 400ms),
    }}
      | pen2touch
      | on_held[KEY_CAPSLOCK, context]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The key itself is gone: a hold is a modifier, not a caps toggle.
    EXPECT_TRUE(collect(col.events(), EV_KEY, KEY_CAPSLOCK).empty());
    // ...but pen2touch still engaged the scroll and the move passed.
    auto const tracking = collect_slotted(col.events(), EV_ABS, ABS_MT_TRACKING_ID);
    ASSERT_EQ(tracking.size(), 3U);
    EXPECT_EQ(tracking[1].slot, 1);
    EXPECT_EQ(tracking[2].event.value(), -1);
    auto const pos_x = collect(col.events(), EV_ABS, ABS_X);
    ASSERT_FALSE(pos_x.empty());
    EXPECT_EQ(pos_x.back().value(), 1500);
}

// A quick tap must still toggle caps: on_held buffers the press and
// re-emits a real press+release on release.
TEST(Pen2TouchTest, CapsLockQuickTapReemitsThroughOnHeld) {
    auto pipeline =
      context
      | timed_sequence{std::array{
        timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
        timed_ev(EV_SYN, SYN_REPORT,   0, 0us),
        timed_ev(EV_ABS,     ABS_X, 1000, 0us),
        timed_ev(EV_ABS,     ABS_Y, 1000, 0us),
        timed_ev(EV_SYN, SYN_REPORT,    0, 0us),
        timed_ev(EV_KEY, KEY_CAPSLOCK,  1, 10ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 10ms),
        timed_ev(EV_KEY, KEY_CAPSLOCK,  0, 50ms),
        timed_ev(EV_SYN, SYN_REPORT,    0, 50ms),
    }}
      | pen2touch
      | on_held[KEY_CAPSLOCK, context]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    auto const caps = collect(col.events(), EV_KEY, KEY_CAPSLOCK);
    ASSERT_EQ(caps.size(), 2U);
    EXPECT_EQ(caps[0].value(), 1);
    EXPECT_EQ(caps[1].value(), 0);
}

// ---------------------------------------------------------------------------
// The app's router: pen/touch codes fall through to the top-level output,
// keyboard codes are consumed by the keyboard route (uinput in the app,
// record here).  Later routes win shared codes, so the keyboard route
// must be listed second.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, RouterSplitsKeyboardAndTouch) {
    static constexpr auto all_keys      = caps_range<EV_KEY, 0, KEY_MAX + 1>();
    static constexpr auto distance_cap  = cap(EV_ABS, ABS_DISTANCE);
    static constexpr auto no_btn_zero   = cap(EV_KEY, BTN_0);
    static constexpr auto touch_caps    = caps::tablet + caps::mt_abs_axes + all_keys + distance_cap;
    static constexpr auto keyboard_caps = caps::keyboard - no_btn_zero;

    // Static: `record[...]` is consteval, so the sinks' addresses must
    // be constants (same trick as HoldModTest's global sinks).
    static auto top_out = std::vector<event_type>{};
    static auto kb_out  = std::vector<event_type>{};
    top_out.clear();
    kb_out.clear();
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, KEY_CAPSLOCK,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY,        KEY_A,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY,     BTN_LEFT,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY,        BTN_0,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1000},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | router[touch_caps >> context, keyboard_caps >> (context | enforce_key_state | record[kb_out] | drop_event)]
      | record[top_out];
    pipeline();

    // Keyboard codes went to the keyboard route and stopped there.
    auto const kb_caps = collect(kb_out, EV_KEY, KEY_CAPSLOCK);
    ASSERT_EQ(kb_caps.size(), 1U);
    EXPECT_EQ(kb_caps[0].value(), 1);
    EXPECT_EQ(collect(kb_out, EV_KEY, KEY_A).size(), 1U);
    EXPECT_TRUE(collect(kb_out, EV_KEY, BTN_LEFT).empty());
    EXPECT_TRUE(collect(kb_out, EV_KEY, BTN_0).empty());

    // Touch codes fall through the empty touch route to the top-level
    // output; keyboard codes must never leak there.
    EXPECT_EQ(collect(top_out, EV_KEY, BTN_LEFT).size(), 1U);
    EXPECT_EQ(collect(top_out, EV_KEY, BTN_0).size(), 1U); // BTN_0 stays on the touch route
    EXPECT_EQ(collect(top_out, EV_ABS, ABS_X).size(), 1U);
    EXPECT_TRUE(collect(top_out, EV_KEY, KEY_CAPSLOCK).empty());
    EXPECT_TRUE(collect(top_out, EV_KEY, KEY_A).empty());
}

TEST(Pen2TouchTest, ScrollOffsetFollowsProfiledAxisRange) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());
    static constexpr input_absinfo narrow_x{.minimum = 0, .maximum = 999};
    tmpl.abs_info(ABS_X, narrow_x);

    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,   1},
        {EV_SYN,   SYN_REPORT,   0},
        {EV_ABS,        ABS_X, 800},
        {EV_ABS,        ABS_Y, 800},
        {EV_SYN,   SYN_REPORT,   0},
        {EV_KEY,  BTN_STYLUS2,   1},
        {EV_SYN,   SYN_REPORT,   0},
    }]
      | pen2touch
      | record;
    auto& col = pipeline.mod<basic_record>();

    // uinput broadcasts this before creating the device; the mod records
    // the profiled axis range to size the scroll-finger offset.
    {
        dynamic_scope scope{dynamic_context, pipeline};
        ASSERT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);
    }

    pipeline();

    // offset = (999 - 0) / 8 = 124, not the unprofiled default 4095.
    auto const pos_x = collect_slotted(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_EQ(pos_x.size(), 2U);
    EXPECT_EQ(pos_x[0].slot, 0);
    EXPECT_EQ(pos_x[0].event.value(), 800);
    EXPECT_EQ(pos_x[1].slot, 1);
    EXPECT_EQ(pos_x[1].event.value(), 800 + 999 / 8);
}

// ---------------------------------------------------------------------------
// Speed parameter: pen2touch[factor] and the runtime speed() setter.
// The factor divides the profiled axis resolution libinput derives
// pointer/scroll motion from; coordinates always pass through raw.
// ---------------------------------------------------------------------------

TEST(Pen2TouchTest, SpeedScalesProfiledResolution) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());

    auto pipeline = context | pen2touch[2.0f] | record;
    {
        dynamic_scope scope{dynamic_context, pipeline};
        ASSERT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);
    }

    // Factor 2 -> half the resolution -> libinput doubles the motion.
    // Ranges and coordinates stay put: no wall, no drift.
    for (auto const code : {ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_X, ABS_Y}) {
        auto const* info = tmpl.abs_info(static_cast<fs8::evdev::code_type>(code));
        ASSERT_NE(info, nullptr) << "code " << code;
        EXPECT_EQ(info->resolution, 50) << "code " << code;
        EXPECT_EQ(info->minimum, 0) << "code " << code;
        EXPECT_EQ(info->maximum, 32'767) << "code " << code;
    }
}

TEST(Pen2TouchTest, SpeedKeepsCoordinatesRaw) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());

    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,      1},
        {EV_SYN,   SYN_REPORT,      0},
        {EV_ABS,        ABS_X,    100},
        {EV_SYN,   SYN_REPORT,      0},
        {EV_ABS,        ABS_X, 20'000},
        {EV_SYN,   SYN_REPORT,      0},
        {EV_ABS,        ABS_X, 32'000},
        {EV_SYN,   SYN_REPORT,      0},
    }]
      | pen2touch[10.0f]
      | record;
    auto& col = pipeline.mod<basic_record>();
    {
        dynamic_scope scope{dynamic_context, pipeline};
        ASSERT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);
    }

    pipeline();

    // A factor of 10 must not move a single unit of the stream: the
    // delta scaling and the mid-tablet wall clamp are gone, the factor
    // lives in the profiled resolution alone.
    auto const mtx = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_EQ(mtx.size(), 3U);
    EXPECT_EQ(mtx[0].value(), 100);
    EXPECT_EQ(mtx[1].value(), 20'000);
    EXPECT_EQ(mtx[2].value(), 32'000);
    auto const legacy = collect(col.events(), EV_ABS, ABS_X);
    ASSERT_EQ(legacy.size(), 3U);
    EXPECT_EQ(legacy[0].value(), 100);
    EXPECT_EQ(legacy[1].value(), 20'000);
    EXPECT_EQ(legacy[2].value(), 32'000);
}

TEST(Pen2TouchTest, SpeedRuntimeSetterScalesProfiledResolution) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());

    auto pipeline = context | pen2touch | record;

    // Apps configure the constinit pipeline before running it.
    pipeline.mod(pen2touch).speed(3.0f);
    {
        dynamic_scope scope{dynamic_context, pipeline};
        ASSERT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);
    }

    auto const* info = tmpl.abs_info(ABS_MT_POSITION_X);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->resolution, 33); // lround(100 / 3)
    auto const* legacy = tmpl.abs_info(ABS_X);
    ASSERT_NE(legacy, nullptr);
    EXPECT_EQ(legacy->resolution, 33);
}

TEST(Pen2TouchTest, SamePositionSameOutputAcrossStrokes) {
    auto pipeline =
      context
      | emit_all[{
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1000},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1500},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, BTN_TOOL_PEN,    0},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_KEY, BTN_TOOL_PEN,    1},
        {EV_SYN,   SYN_REPORT,    0},
        {EV_ABS,        ABS_X, 1500},
        {EV_SYN,   SYN_REPORT,    0},
    }]
      | pen2touch[2.0f]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The output depends on the source position only — never on where
    // the stroke started or which factor is configured.
    auto const mtx = collect(col.events(), EV_ABS, ABS_MT_POSITION_X);
    ASSERT_EQ(mtx.size(), 3U);
    EXPECT_EQ(mtx[0].value(), 1000);
    EXPECT_EQ(mtx[1].value(), 1500);
    EXPECT_EQ(mtx[2].value(), 1500);
}

TEST(Pen2TouchTest, DefaultSpeedLeavesProfileAlone) {
    auto tmpl = make_tablet_template();
    ASSERT_TRUE(tmpl.is_ok());

    auto pipeline = context | pen2touch | record;
    {
        dynamic_scope scope{dynamic_context, pipeline};
        ASSERT_EQ(dynamic_context->broadcast(profile_device + &tmpl), context_action::next);
    }

    auto const* info = tmpl.abs_info(ABS_MT_POSITION_X);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->resolution, 100);
}
