#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon-compose.h>

import fs8.lib.xkb;
import fs8.lib.xkb.how2type;
import fs8.event;
import fs8.mods.lambda;

using fs8::user_event;
using fs8::xkb::get_default_keymap;

namespace {
    template <typename... Args>
    std::vector<user_event> to_vector(Args&&... args) {
        std::vector<user_event> vec;
        fs8::run                rec{[&vec](user_event const& event) noexcept {
            return vec.emplace_back(event);
        }};
        fs8::xkb::how2type::emit(std::forward<Args>(args)..., rec);
        return vec;
    }
} // namespace

TEST(XKB, Basic) {
    auto const vec = to_vector(get_default_keymap(), U'A');
    EXPECT_EQ(vec.front().code, KEY_LEFTSHIFT);
    EXPECT_EQ(vec.at(2).code, KEY_A);
}

TEST(XKB, BasicStringU32) {
    constexpr std::array<std::uint16_t, 6> codes{KEY_A, KEY_A, KEY_B, KEY_B, KEY_C, KEY_C};
    fs8::xkb::how2type::emit(get_default_keymap(), U"ABC", [&, index = 0U](user_event const& event) mutable {
        if (is_syn(event) || event.code == KEY_LEFTSHIFT) {
            return;
        }
        EXPECT_EQ(event.code, codes.at(index++));
    });
}

TEST(XKB, EmitPersianMultiLayout) {
    // A keymap with both US and Persian (ir) layouts. Persian letters are only
    // reachable through the second layout group: AC02 -> Arabic_seen, AC07 -> Arabic_teh.
    fs8::xkb::keymap const map{fs8::xkb::get_default_context(), nullptr, nullptr, "us,ir", ","};
    auto const             vec = to_vector(map, U"\x062A\x0633\x062A"); // "تست"

    std::vector<std::array<int, 3>> keys;
    for (auto const& event : vec) {
        if (is_syn(event)) {
            continue;
        }
        keys.push_back({event.type, event.code, event.value});
    }

    // Persian letters are plain base-level presses; no modifier keys are needed.
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        {EV_KEY, KEY_J, 1},
        {EV_KEY, KEY_J, 0},
        {EV_KEY, KEY_S, 1},
        {EV_KEY, KEY_S, 0},
        {EV_KEY, KEY_J, 1},
        {EV_KEY, KEY_J, 0},
    }));
}

TEST(XKB, EmitPersianStringIsTypable) {
    // The whole Persian word must be typable (non-empty event stream) rather than
    // falling through to the "no way to type" path.
    fs8::xkb::keymap const map{fs8::xkb::get_default_context(), nullptr, nullptr, "us,ir", ","};
    auto const             vec = to_vector(map, U"\x062A\x0633\x062A");
    EXPECT_FALSE(vec.empty());

    std::size_t presses = 0;
    for (auto const& event : vec) {
        if (event.type == EV_KEY && event.value == 1) {
            ++presses;
        }
    }
    EXPECT_EQ(presses, 3U);
}
