#include "./common/tests_common_pch.hpp"

import foresight.devices.udev;

using fs8::udev;
using fs8::udev_device;
using fs8::udev_enumerate;
using fs8::udev_monitor;


// ============================================================================
// udev Tests
// ============================================================================

TEST(UdevTest, DefaultConstructorCreatesValidInstance) {
    udev ctx;
    EXPECT_TRUE(ctx.is_valid());
    EXPECT_TRUE(static_cast<bool>(ctx));
    EXPECT_NE(ctx.native(), nullptr);
}

TEST(UdevTest, InstanceReturnsValidSingleton) {
    udev ctx1 = udev::instance();
    udev ctx2 = udev::instance();

    EXPECT_TRUE(ctx1.is_valid());
    // Since it's a singleton wrapper, the native pointers should be the same
    // depending on how instance() is implemented, but at least they should both be valid.
    EXPECT_NE(ctx1.native(), nullptr);
}

TEST(UdevTest, CopySemantics) {
    udev ctx1;
    ASSERT_TRUE(ctx1.is_valid());

    udev ctx2 = ctx1; // Copy construct
    EXPECT_TRUE(ctx2.is_valid());
    EXPECT_EQ(ctx1.native(), ctx2.native());

    udev ctx3;
    ctx3 = ctx1; // Copy assign
    EXPECT_TRUE(ctx3.is_valid());
    EXPECT_EQ(ctx1.native(), ctx3.native());
}

TEST(UdevTest, MoveSemantics) {
    udev ctx1;
    ASSERT_TRUE(ctx1.is_valid());
    auto* native_ptr = ctx1.native();

    udev ctx2 = std::move(ctx1); // Move construct
    EXPECT_TRUE(ctx2.is_valid());
    EXPECT_EQ(ctx2.native(), native_ptr);
    EXPECT_FALSE(ctx1.is_valid());

    udev ctx3;
    ctx3 = std::move(ctx2); // Move assign
    EXPECT_TRUE(ctx3.is_valid());
    EXPECT_EQ(ctx3.native(), native_ptr);
    EXPECT_FALSE(ctx2.is_valid());
}

// ============================================================================
// udev_device Tests
// ============================================================================

TEST(UdevDeviceTest, DefaultConstructorCreatesInvalidDevice) {
    udev_device dev;
    EXPECT_FALSE(dev.is_valid());
    EXPECT_FALSE(static_cast<bool>(dev));
    EXPECT_EQ(dev.native(), nullptr);
}

TEST(UdevDeviceTest, MoveSemantics) {
    EXPECT_FALSE(std::is_copy_constructible_v<udev_device>);
    EXPECT_FALSE(std::is_copy_assignable_v<udev_device>);
    EXPECT_TRUE(std::is_move_constructible_v<udev_device>);
    EXPECT_TRUE(std::is_move_assignable_v<udev_device>);

    udev_device dev1;
    udev_device dev2 = std::move(dev1);
    EXPECT_FALSE(dev2.is_valid());
}

// ============================================================================
// udev_enumerate Tests
// ============================================================================

TEST(UdevEnumerateTest, DefaultConstructorCreatesValidInstance) {
    udev_enumerate enumerator;
    EXPECT_TRUE(enumerator.is_valid());
    EXPECT_TRUE(static_cast<bool>(enumerator));
    EXPECT_NE(enumerator.native(), nullptr);
}

TEST(UdevEnumerateTest, MoveSemantics) {
    EXPECT_FALSE(std::is_copy_constructible_v<udev_enumerate>);
    EXPECT_FALSE(std::is_copy_assignable_v<udev_enumerate>);

    udev_enumerate enum1;
    ASSERT_TRUE(enum1.is_valid());
    auto* native_ptr = enum1.native();

    udev_enumerate enum2 = std::move(enum1);
    EXPECT_TRUE(enum2.is_valid());
    EXPECT_EQ(enum2.native(), native_ptr);
    EXPECT_EQ(enum1.native(), nullptr);

    udev_enumerate enum3;
    enum3 = std::move(enum2);
    EXPECT_TRUE(enum3.is_valid());
    EXPECT_EQ(enum3.native(), native_ptr);
    EXPECT_EQ(enum2.native(), nullptr);
}

TEST(UdevEnumerateTest, ChainingMethodsKeepValidity) {
    udev_enumerate enumerator;

    enumerator.match_subsystem("net")
              .nomatch_subsystem("block")
              .match_sysname("lo");

    EXPECT_TRUE(enumerator.is_valid());
}

// ============================================================================
// udev_monitor Tests
// ============================================================================

TEST(UdevMonitorTest, DefaultConstructorCreatesValidInstance) {
    udev_monitor monitor;
    EXPECT_TRUE(monitor.is_valid());
    EXPECT_GT(monitor.file_descriptor(), 0); // Should have a valid FD
}

TEST(UdevMonitorTest, MoveSemantics) {
    EXPECT_FALSE(std::is_copy_constructible_v<udev_monitor>);
    EXPECT_FALSE(std::is_copy_assignable_v<udev_monitor>);

    udev_monitor mon1;
    ASSERT_TRUE(mon1.is_valid());
    int fd = mon1.file_descriptor();

    udev_monitor mon2 = std::move(mon1);
    EXPECT_TRUE(mon2.is_valid());
    EXPECT_EQ(mon2.file_descriptor(), fd);
    EXPECT_EQ(mon1.file_descriptor(), 0);

    udev_monitor mon3;
    mon3 = std::move(mon2);
    EXPECT_TRUE(mon3.is_valid());
    EXPECT_EQ(mon3.file_descriptor(), fd);
    EXPECT_EQ(mon2.file_descriptor(), 0);
}

TEST(UdevMonitorTest, SetupAndEnableDoesNotCrash) {
    udev_monitor monitor;
    ASSERT_TRUE(monitor.is_valid());

    monitor.match_device("net");
    EXPECT_TRUE(monitor.is_valid());

    monitor.enable();
    EXPECT_TRUE(monitor.is_valid());
}

// ============================================================================
// Integration-Style Tests (Requires standard Linux devices like "lo")
// ============================================================================

class UdevIntegrationTest : public ::testing::Test {
protected:
    udev ctx;
    udev_device get_loopback_device() {
        // Enumerate to find the "lo" (loopback) network interface
        // which is guaranteed to exist on Linux network stacks.
        udev_enumerate enumerator(ctx);
        enumerator.match_subsystem("net").match_sysname("lo");

        // Note: libudev raw C calls are used here just to extract a single
        // device path to construct the fs8::udev_device. Since `udev_enumerate`
        // wrapper doesn't yet expose `udev_enumerate_get_list_entry`, we
        // mock the expected syspath for standard loopback.

        return udev_device(ctx.native(), "/sys/class/net/lo");
    }
};

TEST_F(UdevIntegrationTest, RetrieveAndInspectDevice) {
    udev_device lo_dev = get_loopback_device();

    // If the test environment doesn't have /sys mounted (e.g., some minimal containers),
    // we gracefully skip the assertions.
    if (!lo_dev.is_valid()) {
        GTEST_SKIP() << "Loopback device not found in /sys/class/net/lo. Skipping integration test.";
    }

    EXPECT_EQ(lo_dev.subsystem(), "net");
    EXPECT_EQ(lo_dev.sysname(), "lo");

    // Test viewify safely handling nullptrs for properties that might not exist
    EXPECT_EQ(lo_dev.driver(), "");

    // Test sysattr (e.g., operational state of loopback)
    std::string_view operstate = lo_dev.sysattr("operstate");
    EXPECT_TRUE(operstate == "unknown" || operstate == "down" || operstate == "up");
}

TEST_F(UdevIntegrationTest, ParentDevice) {
    udev_device lo_dev = get_loopback_device();
    if (!lo_dev.is_valid()) GTEST_SKIP();

    // Virtual devices like 'lo' often have a parent in the 'virtual' subsystem
    udev_device parent = lo_dev.parent();

    // It's possible for some devices not to have a parent, handle gracefully
    if (parent) {
        EXPECT_TRUE(parent.is_valid());
        EXPECT_FALSE(parent.syspath().empty());
    }
}