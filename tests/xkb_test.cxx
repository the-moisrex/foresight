#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon-compose.h>

import foresight.lib.xkb;
import foresight.lib.xkb.compose;

using foresight::xkb::compose_manager;
using foresight::xkb::context;
using foresight::xkb::keymap;

TEST(XKB, Basic) {
    compose_manager manager;
    manager.load_from_locale();
    manager.find_first_typing(U'A', [](input_event const& event) {
        EXPECT_EQ(event.code, KEY_LEFTSHIFT);
        EXPECT_EQ(event.code, KEY_A);
    });
}
