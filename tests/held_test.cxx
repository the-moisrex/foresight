#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.lib.mod_parser;
import fs8.mods;

namespace {
    /// Events captured by the `on` mods under test.
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)
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
     | on[held["<f1>"], record[captured_events]])();

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
     | on[held["[F1][Alt]"], record[captured_events]])();

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
     | on[held[KEY_F1, KEY_F2], record[captured_events]])();

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
     | on[held["<f1>"], record[captured_events]])();

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
     | on[pressed[KEY_F1], record[captured_events]])();

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

TEST(HeldTest, GateTapEmitsNormally) {
    using namespace fs8;
    captured_events.clear();
    auto const never = [](auto&) noexcept {
        return false;
    };

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 2},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | held["<f1>", never]
     | on[always_enable, record[captured_events]])();

    // Emitted normally on release; the repeat is suppressed.
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(1).value(), 0);
}

TEST(HeldTest, GateAlwaysConsumes) {
    using namespace fs8;
    captured_events.clear();
    auto const always = [](auto&) noexcept {
        return true;
    };

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 2},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
       {.type = EV_KEY,  .code = KEY_G, .value = 1},
       {.type = EV_KEY,  .code = KEY_G, .value = 0},
    }]
     | held["<f1>", always]
     | on[always_enable, record[captured_events]])();

    // F1 is fully swallowed; other keys pass through.
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_G);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_G);
    EXPECT_EQ(captured_events.at(1).value(), 0);
}

TEST(HeldTest, GateComboIgnores) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY,  .code = KEY_G, .value = 1},
       {.type = EV_KEY,  .code = KEY_G, .value = 0},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | keys_status
     | held["<f1>", pressed[KEY_G]]
     | on[always_enable, record[captured_events]])();

    // Holding F1 and pressing G consumes F1; only G is emitted.
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_G);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_G);
    EXPECT_EQ(captured_events.at(1).value(), 0);
}

TEST(HeldTest, GateOrderIndependent) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,  .code = KEY_G, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
       {.type = EV_KEY,  .code = KEY_G, .value = 0},
    }]
     | keys_status
     | held["<f1>", pressed[KEY_G]]
     | on[always_enable, record[captured_events]])();

    // G held before F1: the decision fires when F1 is released, still consuming it.
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_G);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_G);
    EXPECT_EQ(captured_events.at(1).value(), 0);
}

TEST(HeldTest, GateFlushDeferredToRelease) {
    using namespace fs8;
    captured_events.clear();
    auto const never = [](auto&) noexcept {
        return false;
    };

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_REL,  .code = REL_X, .value = 5},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | held["<f1>", never]
     | on[always_enable, record[captured_events]])();

    // The mouse move passes immediately; F1 is deferred until release.
    ASSERT_EQ(captured_events.size(), 3U);
    EXPECT_EQ(captured_events.at(0).type(), EV_REL);
    EXPECT_EQ(captured_events.at(0).code(), REL_X);
    EXPECT_EQ(captured_events.at(1).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(1).value(), 1);
    EXPECT_EQ(captured_events.at(2).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(2).value(), 0);
}

TEST(HeldTest, GateChordEmitsNormally) {
    using namespace fs8;
    captured_events.clear();
    auto const never = [](auto&) noexcept {
        return false;
    };

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTALT, .value = 1},
       {.type = EV_KEY,      .code = KEY_F1, .value = 0},
       {.type = EV_KEY, .code = KEY_LEFTALT, .value = 0},
    }]
     | held["[F1][Alt]", never]
     | on[always_enable, record[captured_events]])();

    // The whole chord is replayed in order once fully released.
    ASSERT_EQ(captured_events.size(), 4U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_LEFTALT);
    EXPECT_EQ(captured_events.at(1).value(), 1);
    EXPECT_EQ(captured_events.at(2).code(), KEY_F1);
    EXPECT_EQ(captured_events.at(2).value(), 0);
    EXPECT_EQ(captured_events.at(3).code(), KEY_LEFTALT);
    EXPECT_EQ(captured_events.at(3).value(), 0);
}

TEST(HeldTest, GateChordConsumesOnCombo) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_F1, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTALT, .value = 1},
       {.type = EV_KEY,       .code = KEY_G, .value = 1},
       {.type = EV_KEY,       .code = KEY_G, .value = 0},
       {.type = EV_KEY,      .code = KEY_F1, .value = 0},
       {.type = EV_KEY, .code = KEY_LEFTALT, .value = 0},
    }]
     | keys_status
     | held["[F1][Alt]", pressed[KEY_G]]
     | on[always_enable, record[captured_events]])();

    // The chord is dropped entirely; only G passes through.
    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_G);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_G);
    EXPECT_EQ(captured_events.at(1).value(), 0);
}

TEST(HeldTest, ParseKeyTagsRepeatedTag) {
    using code_type = fs8::event_type::code_type;
    std::array<code_type, 8> out{};

    // [Ctrl+A][Ctrl+A] — two separate keyup tags in sequence
    auto const n = fs8::parse_key_tags("[Ctrl+A][Ctrl+A]", out);
    ASSERT_EQ(n, 4U);
    EXPECT_EQ(out[0], KEY_LEFTCTRL);
    EXPECT_EQ(out[1], KEY_A);
    EXPECT_EQ(out[2], KEY_LEFTCTRL);
    EXPECT_EQ(out[3], KEY_A);
}

TEST(HeldTest, ParseKeyTagsDuplicateInTag) {
    using code_type = fs8::event_type::code_type;
    std::array<code_type, 8> out{};

    // [Ctrl+A+A] — one keyup tag with duplicate key
    auto const n = fs8::parse_key_tags("[Ctrl+A+A]", out);
    ASSERT_EQ(n, 3U);
    EXPECT_EQ(out[0], KEY_LEFTCTRL);
    EXPECT_EQ(out[1], KEY_A);
    EXPECT_EQ(out[2], KEY_A);
}

TEST(HeldTest, GateDeciderEmitsAction) {
    using namespace fs8;
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_F1, .value = 1},
       {.type = EV_KEY,  .code = KEY_G, .value = 1},
       {.type = EV_KEY,  .code = KEY_G, .value = 0},
       {.type = EV_KEY, .code = KEY_F1, .value = 0},
    }]
     | held["<f1>",
            [](auto& ctx) noexcept {
                if (ctx.event().is(EV_KEY, KEY_G, 1)) {
                    std::ignore = ctx.fork_emit(fs8::user_event{.type = EV_KEY, .code = KEY_H, .value = 1});
                    return true;
                }
                return false;
            }]
     | on[always_enable, record[captured_events]])();

    // The decider itself fires the action (KEY_H) and swallows F1.
    ASSERT_EQ(captured_events.size(), 3U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_H);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(1).code(), KEY_G);
    EXPECT_EQ(captured_events.at(1).value(), 1);
    EXPECT_EQ(captured_events.at(2).code(), KEY_G);
    EXPECT_EQ(captured_events.at(2).value(), 0);
}
