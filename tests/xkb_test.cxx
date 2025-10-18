#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <xkbcommon/xkbcommon-compose.h>

import foresight.lib.xkb;
import foresight.lib.xkb.compose;

using foresight::xkb::compose_manager;

namespace {
    template <typename Obj, typename... Args>
    std::vector<input_event> to_vector(Obj& obj, Args&&... args) {
        std::vector<input_event> vec;
        obj.find_first_typing(std::forward<Args>(args)..., [&vec](input_event const& event) {
            return vec.emplace_back(event);
        });
        return vec;
    }
} // namespace

TEST(XKB, Basic) {
    compose_manager manager;
    manager.load_from_locale();
    auto const vec = to_vector(manager, U'A');
    EXPECT_EQ(vec.front().code, KEY_LEFTSHIFT);
    EXPECT_EQ(vec.at(2).code, KEY_A);
}
