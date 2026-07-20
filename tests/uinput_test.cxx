#include "./common/tests_common_pch.hpp"

import fs8.devices.uinput;

TEST(Uinput, CheckAvailablity) {
    auto const res = fs8::verify_access_to_uinput();
    EXPECT_EQ(res, fs8::uinput_access_result::available) << to_string(res);
}

