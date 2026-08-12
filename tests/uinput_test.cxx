#include "./common/tests_common_pch.hpp"

#include <array>
#include <chrono>
#include <coroutine>
#include <filesystem>
#include <span>
#include <thread>

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

namespace {

    std::string read_sysfs_file(std::filesystem::path const& path) {
        std::ifstream in{path};
        std::string   value;
        std::getline(in, value);
        return value;
    }

    fs8::evdev open_keyboard() {
        for (auto pick : fs8::filter_devices(fs8::keyboard)) {
            auto edev = fs8::initialize(fs8::query, pick.device);
            if (edev.is_ok()) {
                return edev;
            }
        }
        return {};
    }

    std::string sysname_of(std::string_view const devnode) {
        auto const pos = devnode.find_last_of('/');
        return std::string{pos == std::string_view::npos ? devnode : devnode.substr(pos + 1)};
    }

} // namespace

TEST(Uinput, VirtualDeviceHasStandardMarkers) {
    auto const res = fs8::verify_access_to_uinput();
    if (res != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(res);
    }
    auto src = open_keyboard();
    if (!src.is_ok()) {
        GTEST_SKIP() << "No keyboard device found.";
    }
    auto const src_name = std::string{src.device_name()};
    auto const src_uniq = std::string{src.unique_identifier()};

    fs8::basic_uinput vdev;
    EXPECT_TRUE(fs8::finalize_device(vdev, src, {}));
    ASSERT_TRUE(vdev.is_ok());

    auto const syspath = std::filesystem::path{vdev.syspath()};
    // Standard kernel marker: BUS_VIRTUAL
    EXPECT_EQ(read_sysfs_file(syspath / "id" / "bustype"), "0006");
    // Clean name.
    EXPECT_EQ(read_sysfs_file(syspath / "name"), src_name + " (Virtual)");
    // Origin chain marker in phys (uinput can't set the kernel uniq field).
    auto const phys = read_sysfs_file(syspath / "phys");
    EXPECT_TRUE(phys.starts_with("foresight:")) << phys;
    // The source device is deep-cloned: nothing it holds is modified.
    EXPECT_EQ(src.device_name(), src_name);
    EXPECT_EQ(src.unique_identifier(), src_uniq);
    EXPECT_EQ(src.get_status(), fs8::evdev_status::success);
    vdev.close();
}

TEST(Uinput, VirtualDeviceChainingAppendsOrigin) {
    auto const res = fs8::verify_access_to_uinput();
    if (res != fs8::uinput_access_result::available) {
        GTEST_SKIP() << "uinput is not available: " << to_string(res);
    }
    auto src = open_keyboard();
    if (!src.is_ok()) {
        GTEST_SKIP() << "No keyboard device found.";
    }

    // Stage 1: real device -> virtual device A
    fs8::basic_uinput vdev_a;
    ASSERT_TRUE(fs8::finalize_device(vdev_a, src, {}));
    ASSERT_TRUE(vdev_a.is_ok());
    auto const syspath_a = std::filesystem::path{vdev_a.syspath()};
    auto const phys_a    = read_sysfs_file(syspath_a / "phys");
    auto const name_a    = read_sysfs_file(syspath_a / "name");
    ASSERT_TRUE(phys_a.starts_with("foresight:")) << phys_a;

    // Stage 2: virtual device A -> virtual device B (chaining). udev may not
    // have finished chmod'ing the device node yet (it starts at 0600
    // root:root), so poll until it can be opened.
    fs8::evdev src_b;
    for (int attempt = 0; attempt < 100; ++attempt) {
        src_b = fs8::evdev{vdev_a.devnode()};
        if (src_b.is_ok()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    ASSERT_TRUE(src_b.is_ok());
    fs8::basic_uinput vdev_b;
    ASSERT_TRUE(fs8::finalize_device(vdev_b, src_b, {}));
    ASSERT_TRUE(vdev_b.is_ok());
    auto const syspath_b = std::filesystem::path{vdev_b.syspath()};

    // The chain is extended with the immediate parent's sysname.
    auto const expected_b = phys_a + "," + sysname_of(vdev_a.devnode());
    EXPECT_EQ(read_sysfs_file(syspath_b / "phys"), expected_b);
    // The name stays clean across hops (no "(Virtual) (Virtual)").
    EXPECT_EQ(read_sysfs_file(syspath_b / "name"), name_a);

    vdev_a.close();
    vdev_b.close();
}
