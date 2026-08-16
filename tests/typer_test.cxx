#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.lib.xkb;
import fs8.lib.xkb.how2type;
import fs8.mods;

namespace {
    /// Collect emitted events for assertions.
    struct recorder {
        std::vector<fs8::user_event> events;

        void operator()(fs8::user_event const &event) noexcept {
            events.push_back(event);
        }
    };

    /// Pull the (type, code, value) triples, skipping SYN_REPORTs.
    [[nodiscard]] std::vector<std::array<int, 3>> key_events(std::vector<fs8::user_event> const &events) {
        std::vector<std::array<int, 3>> out;
        for (auto const &event : events) {
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                continue;
            }
            out.push_back({event.type, static_cast<int>(event.code), event.value});
        }
        return out;
    }

    /// Events captured by the `on` mod downstream of `type_string`.
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)

    /// Convert captured events to plain user_events for assertions.
    [[nodiscard]] std::vector<fs8::user_event> to_user_events(std::vector<fs8::event_type> const &events) {
        std::vector<fs8::user_event> out;
        out.reserve(events.size());
        for (auto const &event : events) {
            out.push_back(static_cast<fs8::user_event>(event));
        }
        return out;
    }
} // namespace

TEST(TyperTest, EmitPlainTextStringView) {
    fs8::xkb::keymap const &map = fs8::xkb::get_default_keymap();
    recorder                rec;

    fs8::xkb::how2type::emit(map, std::string_view{"ab"}, rec);

    auto const keys = key_events(rec.events);
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_A, 1},
                {EV_KEY, KEY_A, 0},
                {EV_KEY, KEY_B, 1},
                {EV_KEY, KEY_B, 0},
    }));
    // every key event is followed by a SYN_REPORT
    EXPECT_EQ(rec.events.size(), 8U);
    for (std::size_t i = 1; i < rec.events.size(); i += 2) {
        EXPECT_TRUE(rec.events.at(i).type == EV_SYN && rec.events.at(i).code == SYN_REPORT);
    }
}

TEST(TyperTest, EmitPlainTextU8StringView) {
    fs8::xkb::keymap const &map = fs8::xkb::get_default_keymap();
    recorder                rec;

    fs8::xkb::how2type::emit(map, std::u8string_view{u8"hi"}, rec);

    auto const keys = key_events(rec.events);
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_H, 1},
                {EV_KEY, KEY_H, 0},
                {EV_KEY, KEY_I, 1},
                {EV_KEY, KEY_I, 0},
    }));
}

TEST(TyperTest, EmitUppercaseRequiresShift) {
    fs8::xkb::keymap const &map = fs8::xkb::get_default_keymap();
    recorder                rec;

    fs8::xkb::how2type::emit(map, U'A', rec);

    // 'A' needs LeftShift held: shift-down, A-down, A-up, shift-up (with SYN_REPORTs between)
    auto const keys = key_events(rec.events);
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_LEFTSHIFT, 1},
                {EV_KEY,         KEY_A, 1},
                {EV_KEY,         KEY_A, 0},
                {EV_KEY, KEY_LEFTSHIFT, 0},
    }));
}

TEST(TyperTest, EmitComposedCharacter) {
    // us-intl has no direct key for 'ÿ' (U+00FF), so it's typed via a composed sequence:
    // dead_diaeresis (Shift + apostrophe) followed by 'y'.
    fs8::xkb::keymap const map{fs8::xkb::get_default_context(), nullptr, nullptr, "us", "intl"};
    recorder               rec;

    fs8::xkb::how2type::emit(map, U'ÿ', rec);

    auto const keys = key_events(rec.events);
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        {EV_KEY,  KEY_LEFTSHIFT, 1},
        {EV_KEY, KEY_APOSTROPHE, 1},
        {EV_KEY, KEY_APOSTROPHE, 0},
        {EV_KEY,  KEY_LEFTSHIFT, 0},
        {EV_KEY,          KEY_Y, 1},
        {EV_KEY,          KEY_Y, 0},
    }));
}

TEST(TyperTest, EmitAltGrCharacter) {
    // us-intl types 'é' (U+00E9) directly with AltGr (Mod5) + E
    fs8::xkb::keymap const map{fs8::xkb::get_default_context(), nullptr, nullptr, "us", "intl"};
    recorder               rec;

    fs8::xkb::how2type::emit(map, U'é', rec);

    auto const keys = key_events(rec.events);
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_RIGHTALT, 1},
                {EV_KEY,        KEY_E, 1},
                {EV_KEY,        KEY_E, 0},
                {EV_KEY, KEY_RIGHTALT, 0},
    }));
}

TEST(TyperTest, EmitUnsupportedCharIsNoop) {
    // plain US layout has no way to produce U+00E9 without dead keys / Multi_key
    fs8::xkb::keymap const &map = fs8::xkb::get_default_keymap();
    recorder                rec;

    fs8::xkb::how2type::emit(map, U'é', rec);

    EXPECT_TRUE(rec.events.empty());
}

TEST(TyperTest, TypeStringPipelineEmitsText) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_0, .value = 1},
    }]
     | type_string["hi"]
     | on[always_enable, [](auto &ctx) noexcept {
           captured_events.push_back(ctx.event());
       }])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        // injected KEY_0 drives the pipeline once
        {EV_KEY, KEY_H, 1},
        {EV_KEY, KEY_H, 0},
        {EV_KEY, KEY_I, 1},
        {EV_KEY, KEY_I, 0},
        {EV_KEY, KEY_0, 1},
    }));
}

TEST(TyperTest, TypeStringPipelineEmitsModifierTag) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_0, .value = 1},
    }]
     | type_string["<ctrl-r>"]
     | on[always_enable, [](auto &ctx) noexcept {
           captured_events.push_back(ctx.event());
       }])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_LEFTCTRL, 1},
                {EV_KEY,        KEY_R, 1},
                {EV_KEY,        KEY_R, 0},
                {EV_KEY, KEY_LEFTCTRL, 0},
                {EV_KEY,        KEY_0, 1},
    }));
}

TEST(TyperTest, TypeStringPipelineEmitsMixedTextAndTags) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_0, .value = 1},
    }]
     | type_string["go <ctrl-s>"]
     | on[always_enable, [](auto &ctx) noexcept {
           captured_events.push_back(ctx.event());
       }])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        {EV_KEY,        KEY_G, 1},
        {EV_KEY,        KEY_G, 0},
        {EV_KEY,        KEY_O, 1},
        {EV_KEY,        KEY_O, 0},
        {EV_KEY,    KEY_SPACE, 1},
        {EV_KEY,    KEY_SPACE, 0},
        {EV_KEY, KEY_LEFTCTRL, 1},
        {EV_KEY,        KEY_S, 1},
        {EV_KEY,        KEY_S, 0},
        {EV_KEY, KEY_LEFTCTRL, 0},
        {EV_KEY,        KEY_0, 1},
    }));
}

TEST(TyperTest, TypeStringPipelineEmitsU8Text) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_0, .value = 1},
    }]
     | type_string[u8"ok"]
     | on[always_enable, [](auto &ctx) noexcept {
           captured_events.push_back(ctx.event());
       }])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_O, 1},
                {EV_KEY, KEY_O, 0},
                {EV_KEY, KEY_K, 1},
                {EV_KEY, KEY_K, 0},
                {EV_KEY, KEY_0, 1},
    }));
}
