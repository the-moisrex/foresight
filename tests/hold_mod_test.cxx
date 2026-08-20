#include "./common/tests_common_pch.hpp"

#include <chrono>
#include <linux/input-event-codes.h>
#include <ranges>

import fs8.mods;

namespace {
    /// Events recorded by the mod gated inside `hold_mod`.
    std::vector<fs8::event_type> held_out; // NOLINT(*-global-variables)
    /// Events that reach the pipeline after `hold_mod` (incl. re-emitted taps).
    std::vector<fs8::event_type> tap_out; // NOLINT(*-global-variables)
} // namespace

TEST(HoldModTest, QuickTapReEmitsPressAndRelease) {
    using namespace fs8;
    held_out.clear();
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 1},
       {.type = EV_KEY, .code = KEY_A, .value = 0},
    }]
     | on_held[KEY_A, record[held_out]]
     | record[tap_out])();

    // The press was buffered; on the quick release it's re-emitted as a real
    // press+release so the OS still sees a tap.
    ASSERT_EQ(tap_out.size(), 2U);
    EXPECT_EQ(tap_out.at(0).type(), EV_KEY);
    EXPECT_EQ(tap_out.at(0).code(), KEY_A);
    EXPECT_EQ(tap_out.at(0).value(), 1);
    EXPECT_EQ(tap_out.at(1).type(), EV_KEY);
    EXPECT_EQ(tap_out.at(1).code(), KEY_A);
    EXPECT_EQ(tap_out.at(1).value(), 0);
    EXPECT_TRUE(held_out.empty());
}

TEST(HoldModTest, HoldPastThresholdSwallows) {
    using namespace fs8;
    held_out.clear();
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 1},
       {.type = EV_KEY, .code = KEY_A, .value = 0},
    }]
     | on_held[KEY_A, record[held_out]].hold(std::chrono::microseconds{0})
     | record[tap_out])();

    // Held (>= the 0us threshold): treated as a modifier, everything swallowed.
    EXPECT_TRUE(held_out.empty());
    EXPECT_TRUE(tap_out.empty());
}

TEST(HoldModTest, RepeatsAreSwallowed) {
    using namespace fs8;
    held_out.clear();
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 1},
       {.type = EV_KEY, .code = KEY_A, .value = 2},
       {.type = EV_KEY, .code = KEY_A, .value = 0},
    }]
     | on_held[KEY_A, record[held_out]]
     | record[tap_out])();

    // The auto-repeat is swallowed; the quick tap still re-emits press+release.
    ASSERT_EQ(tap_out.size(), 2U);
    EXPECT_EQ(tap_out.at(0).value(), 1);
    EXPECT_EQ(tap_out.at(1).value(), 0);
    EXPECT_TRUE(held_out.empty());
}

TEST(HoldModTest, MovementWhileHeldConsumesRelease) {
    using namespace fs8;
    held_out.clear();
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 1},
       {.type = EV_REL, .code = REL_X, .value = 5},
       {.type = EV_KEY, .code = KEY_A, .value = 0},
    }]
     | mice_quantifier
     | on_held[KEY_A, mouse_to_scroll]
     | record[tap_out])();

    // The movement was turned into scroll (so the key was used as a modifier):
    // the release is swallowed and no KEY_A ever reaches the output.
    EXPECT_TRUE(std::ranges::none_of(tap_out, [](fs8::event_type const& e) {
        return e.type() == EV_KEY;
    }));
    EXPECT_FALSE(tap_out.empty());
    EXPECT_TRUE(std::ranges::all_of(tap_out, [](fs8::event_type const& e) {
        return e.type() == EV_REL;
    }));
}

TEST(HoldModTest, PassThroughWhenNotHeld) {
    using namespace fs8;
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_REL, .code = REL_X, .value = 5},
    }]
     | mice_quantifier
     | on_held[KEY_A, mouse_to_scroll]
     | record[tap_out])();

    // No modifier key held: mouse movement passes straight through.
    ASSERT_EQ(tap_out.size(), 1U);
    EXPECT_EQ(tap_out.at(0).type(), EV_REL);
    EXPECT_EQ(tap_out.at(0).code(), REL_X);
}

TEST(HoldModTest, ToggleModeTurnedBackOffAfterModifierUse) {
    using namespace fs8;
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 1},
       {.type = EV_LED,    .code = LED_CAPSL, .value = 1}, // the desktop toggled caps on
       {.type = EV_REL,        .code = REL_X, .value = 5},
       {.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 0},
    }]
     | mice_quantifier
     | on_held[KEY_CAPSLOCK, mouse_to_scroll]
     | record[tap_out])();

    // The press/release are swallowed, but because the desktop turned caps on,
    // on_held emits a synthetic press+release to restore it to off (mouse mode).
    // The physical events never reach the output.
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY && e.code() == KEY_CAPSLOCK;
                                    }),
              2U);
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY && e.code() == KEY_CAPSLOCK && e.value() == 1;
                                    }),
              1U);
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY && e.code() == KEY_CAPSLOCK && e.value() == 0;
                                    }),
              1U);
}

TEST(HoldModTest, PenModeRestoredAfterModifierUse) {
    using namespace fs8;
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_LED,    .code = LED_CAPSL, .value = 1}, // pen mode: caps is on
       {.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 1},
       {.type = EV_LED,    .code = LED_CAPSL, .value = 0}, // the desktop toggled caps off
       {.type = EV_REL,        .code = REL_X, .value = 5},
       {.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 0},
    }]
     | mice_quantifier
     | on_held[KEY_CAPSLOCK, mouse_to_scroll]
     | record[tap_out])();

    // Caps was on (pen mode) before the press; the desktop flipped it off, so
    // on release a synthetic press+release restores it to on (pen mode).
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY && e.code() == KEY_CAPSLOCK;
                                    }),
              2U);
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY && e.code() == KEY_CAPSLOCK && e.value() == 1;
                                    }),
              1U);
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY && e.code() == KEY_CAPSLOCK && e.value() == 0;
                                    }),
              1U);
}

TEST(HoldModTest, ToggleModeLeftAloneWhenDesktopDidNotToggle) {
    using namespace fs8;
    tap_out.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 1},
       {.type = EV_REL,        .code = REL_X, .value = 5},
       {.type = EV_KEY, .code = KEY_CAPSLOCK, .value = 0},
    }]
     | mice_quantifier
     | on_held[KEY_CAPSLOCK, mouse_to_scroll]
     | record[tap_out])();

    // The desktop never saw the swallowed press, so the mode was never toggled:
    // nothing synthetic is emitted and no KEY_CAPSLOCK reaches the output.
    EXPECT_EQ(std::ranges::count_if(tap_out,
                                    [](fs8::event_type const& e) {
                                        return e.type() == EV_KEY;
                                    }),
              0U);
    EXPECT_FALSE(tap_out.empty()); // only the scroll events
    EXPECT_TRUE(std::ranges::all_of(tap_out, [](fs8::event_type const& e) {
        return e.type() == EV_REL;
    }));
}
