#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-compose.h>

import foresight.lib.xkb;
import foresight.lib.xkb.compose;

using foresight::xkb::compose_manager;
using foresight::xkb::context;
using foresight::xkb::keymap;

TEST(XKB, Basic) {
    context const   ctx;
    keymap const    map{ctx};
    compose_manager manager{ctx.get()};
    manager.load_from_locale();
    auto const vec           = manager.find_first_typing(map.get(), U'A');
    EXPECT_EQ(vec.size(), 8);
    EXPECT_EQ(vec.front().code, KEY_LEFTSHIFT);
    EXPECT_EQ(vec.at(2).code, KEY_A);
}
