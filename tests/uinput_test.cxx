#include "./common/tests_common_pch.hpp"

#include <array>
#include <span>

import fs8.devices.uinput;
import fs8.devices.queries;

TEST(Uinput, CheckAvailablity) {
    auto const res = fs8::verify_access_to_uinput();
    EXPECT_EQ(res, fs8::uinput_access_result::available) << to_string(res);
}

TEST(Uinput, SetDeviceFromQuery) {
    auto const res = fs8::verify_access_to_uinput();
    if (res != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(res);
    }
    fs8::basic_uinput vdev;
    EXPECT_TRUE(vdev.init(fs8::keyboard));
    EXPECT_TRUE(vdev.is_ok());
    EXPECT_FALSE(vdev.devnode().empty());
    vdev.close();
}

TEST(Uinput, SetDeviceFromQueryEmptyFallback) {
    auto const res = fs8::verify_access_to_uinput();
    if (res != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(res);
    }
    fs8::basic_uinput vdev;
    EXPECT_TRUE(vdev.set_device_from(fs8::query));
    EXPECT_TRUE(vdev.is_ok());
    vdev.close();
}

TEST(Uinput, SetDeviceFromQueryFailOnNoMatch) {
    std::array<fs8::query_term, 1> fields = {fs8::match_sysname("nonexistent-device-xyz")};
    fs8::device_query              q{.fields = std::span<fs8::query_term const>{fields}, .fail_on_no_match = true};
    fs8::basic_uinput              vdev;
    EXPECT_FALSE(vdev.set_device_from(q));
    vdev.close();
}

