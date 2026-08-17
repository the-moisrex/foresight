#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.lib.mod_parser;
import fs8.mods;

namespace {
    /// Events captured by the `on` mods under test.
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)

    /// Record the current event into `captured_events`.
    auto record = [](auto& ctx) noexcept {
        captured_events.push_back(ctx.event());
    };
} // namespace

TEST(HeldTest, StringForm) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 2},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | on[held["<f1>"], record])();

    // Only the initial press fires; the auto-repeat (value 2) and release are suppressed.
    ASSERT_EQ(captured_events.size(), 1U);
    EXPECT_EQ(captured_events.at(0).type(), EV_KEY);
    EXPECT_EQ(captured_events.at(0).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(0).value(), 1);
}

TEST(HeldTest, MultiStringForm) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTALT, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTALT, .value = 2},
       {.type = EV_KEY,      .code = KEY_F1, .value = 0},
    }]
     | on[held["[F1][Alt]"], record])();

    // Fires only once both [F1][Alt] are down; the Alt repeat is suppressed.
    ASSERT_EQ(captured_events.size(), 1U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_LEFTALT);
    EXPECT_EQ(captured_events.at(0).value(), 1);
}

TEST(HeldTest, CodeForm) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_F2, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 2},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | on[held[KEY_F1, KEY_F2], record])();

    // Fires only when both keys are held; repeats of either are suppressed.
    ASSERT_EQ(captured_events.size(), 1U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_F2);
    EXPECT_EQ(captured_events.at(0).value(), 1);
}

TEST(HeldTest, RelatedEventWhileHeld) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_REL,  .code = REL_X, .value = 5},
       {.type = EV_KEY, .code = KEY_F1, .value = 2},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | on[held["<f1>"], record])();

    // Unrelated events keep the condition active; only F1's own repeats are suppressed.
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).type(), EV_REL);
    EXPECT_EQ(captured_events.at(1).code(), REL_X);
}

TEST(HeldTest, PressedStillFiresOnRepeats) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 2},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | keys_status
     | on[pressed[KEY_F1], record])();

    // Contrast with held: `pressed` treats the auto-repeat as "still pressed".
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).value(), 2);
}

TEST(HeldTest, ParseKeyTags) {
    using code_type = fs8::event_type::code_type;
    std::array<code_type, 8> out{};

    auto const n1 = fs8::parse_key_tags("[F1][Alt]", out);
    ASSERT_EQ(n1, 2U);
    EXPECT_EQ(out[0], KEY_F1);
    EXPECT_EQ(out[1], KEY_LEFTALT);

    auto const n2 = fs8::parse_key_tags("<ctrl-r>", out);
    ASSERT_EQ(n2, 2U);
    EXPECT_EQ(out[0], KEY_LEFTCTRL);
    EXPECT_EQ(out[1], KEY_R);

    auto const n3 = fs8::parse_key_tags("f1", out);
    ASSERT_EQ(n3, 1U);
    EXPECT_EQ(out[0], KEY_F1);

    auto const n4 = fs8::parse_key_tags("ctrl-r", out);
    ASSERT_EQ(n4, 2U);
    EXPECT_EQ(out[0], KEY_LEFTCTRL);
    EXPECT_EQ(out[1], KEY_R);
}
