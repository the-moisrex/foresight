#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon-compose.h>

import foresight.lib.xkb;
import foresight.lib.xkb.compose;
import foresight.mods.event;

using foresight::user_event;
using foresight::xkb::how2type;

namespace {
    template <typename Obj, typename... Args>
    std::vector<user_event> to_vector(Obj& obj, Args&&... args) {
        std::vector<user_event> vec;
        obj.find_first_typing(std::forward<Args>(args)..., [&vec](user_event const& event) {
            return vec.emplace_back(event);
        });
        return vec;
    }
} // namespace

TEST(XKB, Basic) {
    how2type   manager{};
    auto const vec = to_vector(manager, U'A');
    EXPECT_EQ(vec.front().code, KEY_LEFTSHIFT);
    EXPECT_EQ(vec.at(2).code, KEY_A);
}
