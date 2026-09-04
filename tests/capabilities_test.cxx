// Synthetic tests for the caps-matching semantics:
//   * match_caps uses an append-only denominator;
//   * remove_codes / remove_type penalize the score (negative caps);
//   * empty / remove-only specs never divide by zero;
//   * the default caps_support_percentage is 50 and the low/high presets work.

#include "common/tests_common_pch.hpp"

#include <algorithm>
#include <array>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <ranges>
#include <span>
#include <vector>

import fs8.devices.capabilities;
import fs8.devices.evdev;
import fs8.devices.queries;

using namespace fs8;

namespace {

    /// Enable a single event code on a synthetic device (with absinfo for EV_ABS).
    void emit_code(evdev& dev, evdev::ev_type const type, evdev::code_type const code) noexcept {
        if (type == static_cast<evdev::ev_type>(EV_ABS)) {
            static constexpr input_absinfo abs0{};
            dev.enable_event_code(static_cast<evdev::ev_type>(EV_ABS), code, &abs0);
        } else {
            dev.enable_event_code(type, code);
        }
    }

    evdev synthetic(evdev_status const status = evdev_status::success) {
        return evdev{libevdev_new(), status};
    }

    /// The exact UGTABLET pen caps: X/Y/PRESSURE/TILT + PEN/RUBBER/STYLUS/TOUCH.
    evdev ugtablet_pen() {
        auto dev = synthetic();
        emit_code(dev, EV_SYN, SYN_REPORT);
        emit_code(dev, EV_ABS, ABS_X);
        emit_code(dev, EV_ABS, ABS_Y);
        emit_code(dev, EV_ABS, ABS_PRESSURE);
        emit_code(dev, EV_ABS, ABS_TILT_X);
        emit_code(dev, EV_ABS, ABS_TILT_Y);
        emit_code(dev, EV_KEY, BTN_TOOL_PEN);
        emit_code(dev, EV_KEY, BTN_TOOL_RUBBER);
        emit_code(dev, EV_KEY, BTN_STYLUS);
        emit_code(dev, EV_KEY, BTN_TOUCH);
        return dev;
    }

    /// The exact UGTABLET mouse caps: REL axis + wheels, 5 buttons, some ABS.
    evdev ugtablet_mouse() {
        auto dev = synthetic();
        emit_code(dev, EV_SYN, SYN_REPORT);
        emit_code(dev, EV_REL, REL_X);
        emit_code(dev, EV_REL, REL_Y);
        emit_code(dev, EV_REL, REL_WHEEL);
        emit_code(dev, EV_REL, REL_HWHEEL);
        emit_code(dev, EV_REL, REL_WHEEL_HI_RES);
        emit_code(dev, EV_REL, REL_HWHEEL_HI_RES);
        emit_code(dev, EV_KEY, BTN_LEFT);
        emit_code(dev, EV_KEY, BTN_RIGHT);
        emit_code(dev, EV_KEY, BTN_MIDDLE);
        emit_code(dev, EV_KEY, BTN_SIDE);
        emit_code(dev, EV_KEY, BTN_EXTRA);
        emit_code(dev, EV_ABS, ABS_X);
        emit_code(dev, EV_ABS, ABS_Y);
        emit_code(dev, EV_ABS, ABS_PRESSURE);
        return dev;
    }

    /// A keyboard that has exactly the append caps of `caps::keyboard`.
    evdev perfect_keyboard() {
        auto dev = synthetic();
        for (auto const& [type, codes, action] : caps::keyboard) {
            if (action != caps_action::append) {
                continue;
            }
            for (auto const code : codes) {
                emit_code(dev, type, code);
            }
        }
        return dev;
    }

} // namespace

TEST(CapsMatch, DefaultSupportPercentageIsFifty) {
    EXPECT_EQ(device_query{}.caps_support_percentage, 50);
    EXPECT_EQ(owned_query{}.caps_support_percentage, 50);
}

TEST(CapsMatch, UgtabletPenMatchesTabletButNotMouseOrKeyboard) {
    auto pen = ugtablet_pen();

    // Append-only denominator: 12 of the 17 tablet append codes => 70 (truncated).
    EXPECT_EQ(pen.match_caps(caps::tablet), 70);
    EXPECT_GE(pen.match_caps(caps::tablet), 50);

    // Negative caps: -tablet_tool_btns collapses a pen's mouse score to 0.
    EXPECT_EQ(pen.match_caps(caps::mouse), 0);
    // Negative caps: -touch_abs_axes collapses a pen's keyboard score to 0.
    EXPECT_EQ(pen.match_caps(caps::keyboard), 0);
}

TEST(CapsMatch, UgtabletMouseMatchesMouseButNotTabletOrKeyboard) {
    auto mouse_dev = ugtablet_mouse();

    // 12 of the 15 mouse append codes => 80.
    EXPECT_EQ(mouse_dev.match_caps(caps::mouse), 80);
    EXPECT_GE(mouse_dev.match_caps(caps::mouse), 50);

    // Negative caps: -pointer_rel_all and -EV_REL collapses a mouse's tablet score.
    EXPECT_EQ(mouse_dev.match_caps(caps::tablet), 0);
    // Negative caps: -pointer_btns collapses a mouse's keyboard score.
    EXPECT_EQ(mouse_dev.match_caps(caps::keyboard), 0);
}

TEST(CapsMatch, PerfectKeyboardMatchesKeyboardOnly) {
    auto kb = perfect_keyboard();

    EXPECT_EQ(kb.match_caps(caps::keyboard), 100);
    EXPECT_GE(kb.match_caps(caps::keyboard), 50);

    // A keyboard is not a tablet or a mouse.
    EXPECT_LT(kb.match_caps(caps::tablet), 50);
    EXPECT_LT(kb.match_caps(caps::mouse), 50);
}

TEST(CapsMatch, EmptySpecScoresSatisfiedWithoutDivisionByZero) {
    auto dev = synthetic();
    emit_code(dev, EV_KEY, BTN_LEFT);

    EXPECT_EQ(dev.match_caps(+caps::nothing), 100);
}

TEST(CapsMatch, ThresholdPresetsWriteSupportPercentage) {
    std::array<std::string_view, 1> strs{"keyboard"};

    auto lenient = (strs | low_threshold) | std::ranges::to<std::vector<owned_query>>();
    ASSERT_EQ(lenient.size(), 1U);
    EXPECT_EQ(lenient[0].value().caps_support_percentage, 40);

    auto strict = (strs | high_threshold) | std::ranges::to<std::vector<owned_query>>();
    ASSERT_EQ(strict.size(), 1U);
    EXPECT_EQ(strict[0].value().caps_support_percentage, 80);

    // matches_percentage is consteval_copyable, so it cannot survive a runtime
    // range-pipe capture; verify it through a compile-time application instead.
    auto apply_explicit = [](std::uint8_t const percentage) consteval {
        auto q = (fs8::query | caps::tablet);
        q      = (q | matches_percentage[percentage]);
        return q.caps_support_percentage;
    };

    EXPECT_EQ(apply_explicit(70), 70);
    EXPECT_EQ(apply_explicit(100), 100);
    EXPECT_EQ(apply_explicit(0), 0);
}

TEST(CapsMatch, MatchesHonoursSupportPercentage) {
    auto pen = ugtablet_pen(); // 70% vs caps::tablet

    device_query base{};
    base.caps = caps::tablet;

    EXPECT_TRUE(matches(pen, base));  // default 50 <= 70

    base.caps_support_percentage = 80;
    EXPECT_FALSE(matches(pen, base)); // 70 < 80

    base.caps_support_percentage = 40;
    EXPECT_TRUE(matches(pen, base));  // 40 <= 70

    base.caps_support_percentage = 70;
    EXPECT_TRUE(matches(pen, base));  // boundary passes

    base.caps_support_percentage = 71;
    EXPECT_FALSE(matches(pen, base)); // one above the score fails
}
