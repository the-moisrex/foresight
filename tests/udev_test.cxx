#include "./common/tests_common_pch.hpp"

import fs8.devices.udev;

using fs8::udev;
using fs8::udev_device;
using fs8::udev_enumerate;
using fs8::udev_monitor;
using fs8::udev_hwdb;
using fs8::udev_queue;
using fs8::udev_list_entry;

// ============================================================================
// udev Context Tests
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
    EXPECT_NE(ctx1.native(), nullptr);
}

TEST(UdevTest, CopySemantics) {
    udev ctx1;
    ASSERT_TRUE(ctx1.is_valid());

    udev ctx2 = ctx1;
    EXPECT_TRUE(ctx2.is_valid());
    EXPECT_EQ(ctx1.native(), ctx2.native());

    udev ctx3;
    ctx3 = ctx1;
    EXPECT_TRUE(ctx3.is_valid());
    EXPECT_EQ(ctx1.native(), ctx3.native());
}

TEST(UdevTest, MoveSemantics) {
    udev ctx1;
    ASSERT_TRUE(ctx1.is_valid());
    auto* native_ptr = ctx1.native();

    udev ctx2 = std::move(ctx1);
    EXPECT_TRUE(ctx2.is_valid());
    EXPECT_EQ(ctx2.native(), native_ptr);
    EXPECT_FALSE(ctx1.is_valid());

    udev ctx3;
    ctx3 = std::move(ctx2);
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
    EXPECT_GT(monitor.file_descriptor(), 0);
}

TEST(UdevMonitorTest, MoveSemantics) {
    udev_monitor mon1;
    ASSERT_TRUE(mon1.is_valid());
    int fd = mon1.file_descriptor();

    udev_monitor mon2 = std::move(mon1);
    EXPECT_TRUE(mon2.is_valid());
    EXPECT_EQ(mon2.file_descriptor(), fd);
    EXPECT_EQ(mon1.file_descriptor(), -1);
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
// HWDB and Queue Tests
// ============================================================================

TEST(UdevHwdbTest, CreationAndQuery) {
    udev ctx;
    udev_hwdb hwdb(ctx.native());

    // Testing hwdb might return empty entries on some systems,
    // but the query itself shouldn't crash.
    auto entries = hwdb.get_properties("usb:v046DpC52B*", 0);
    EXPECT_NO_THROW({
        for (auto entry : entries) {
            (void)entry.name();
            (void)entry.value();
        }
    });
}

TEST(UdevQueueTest, QueueStateCheck) {
    udev ctx;
    udev_queue queue(ctx.native());

    // The daemon state varies, we just ensure the wrappers return booleans without crashing
    EXPECT_NO_THROW({
        bool active = queue.is_active();
        bool empty = queue.is_empty();
        (void)active;
        (void)empty;
    });
}

// ============================================================================
// Comprehensive Integration and Use Case Tests
// ============================================================================

class UdevIntegrationTest : public ::testing::Test {
protected:
    udev ctx;

    void SetUp() override {
        ASSERT_TRUE(ctx.is_valid());
    }

    udev_device get_loopback_device() {
        udev_enumerate enumerator(ctx);
        enumerator.match_subsystem("net").match_sysname("lo");
        enumerator.scan_devices();

        auto entries = enumerator.list_entries();
        auto it = entries.begin();
        if (it != entries.end()) {
            return udev_device(ctx.native(), (*it).name().data());
        }
        return udev_device();
    }
};

TEST_F(UdevIntegrationTest, RetrieveAndInspectLoopbackDevice) {
    udev_device lo_dev = get_loopback_device();

    if (!lo_dev.is_valid()) {
        GTEST_SKIP() << "Loopback device not found. Skipping.";
    }

    EXPECT_EQ(lo_dev.subsystem(), "net");
    EXPECT_EQ(lo_dev.sysname(), "lo");
    EXPECT_EQ(lo_dev.driver(), "");

    std::string_view operstate = lo_dev.sysattr("operstate");
    EXPECT_TRUE(operstate == "unknown" || operstate == "down" || operstate == "up");
}

TEST_F(UdevIntegrationTest, ParentDeviceNavigation) {
    udev_device lo_dev = get_loopback_device();
    if (!lo_dev.is_valid()) GTEST_SKIP();

    udev_device parent = lo_dev.parent();
    if (parent) {
        EXPECT_TRUE(parent.is_valid());
        EXPECT_FALSE(parent.syspath().empty());
    }
}

TEST_F(UdevIntegrationTest, FindAllKeyboards) {
    udev_enumerate enumerator(ctx);
    enumerator.match_subsystem("input")
              .match_property("ID_INPUT_KEYBOARD", "1");
    enumerator.scan_devices();

    auto entries = enumerator.list_entries();
    int keyboard_count = 0;

    for (auto entry : entries) {
        udev_device dev(ctx.native(), entry.name().data());
        if (dev.is_valid()) {
            keyboard_count++;
            EXPECT_EQ(dev.subsystem(), "input");
            EXPECT_EQ(dev.property("ID_INPUT_KEYBOARD"), "1");
        }
    }

    // CI environments might not have a keyboard, so we don't assert count > 0,
    // but we log it to show it works when present.
    RecordProperty("KeyboardCount", std::to_string(keyboard_count));
}

TEST_F(UdevIntegrationTest, FindAllMice) {
    udev_enumerate enumerator(ctx);
    enumerator.match_subsystem("input")
              .match_property("ID_INPUT_MOUSE", "1");
    enumerator.scan_devices();

    auto entries = enumerator.list_entries();
    int mouse_count = 0;

    for (auto entry : entries) {
        udev_device dev(ctx.native(), entry.name().data());
        if (dev.is_valid()) {
            mouse_count++;
            EXPECT_EQ(dev.subsystem(), "input");
            EXPECT_EQ(dev.property("ID_INPUT_MOUSE"), "1");
        }
    }

    RecordProperty("MouseCount", std::to_string(mouse_count));
}

TEST_F(UdevIntegrationTest, ListEntryIteratorSanity) {
    udev_enumerate enumerator(ctx);
    enumerator.match_subsystem("block");
    enumerator.scan_devices();

    auto entries = enumerator.list_entries();

    // Test the forward iterator
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        auto entry = *it;
        EXPECT_FALSE(entry.name().empty());
    }
}

TEST_F(UdevIntegrationTest, DevicePropertiesAndTagsList) {
    udev_device lo_dev = get_loopback_device();
    if (!lo_dev.is_valid()) GTEST_SKIP();

    auto props = lo_dev.properties();
    bool found_subsystem_prop = false;
    for (auto prop : props) {
        if (prop.name() == "SUBSYSTEM" && prop.value() == "net") {
            found_subsystem_prop = true;
        }
    }
    EXPECT_TRUE(found_subsystem_prop);

    // Just ensure the tags function doesn't crash on retrieval and iteration
    auto tags = lo_dev.tags();
    EXPECT_NO_THROW({
        for (auto tag : tags) {
            (void)tag.name();
        }
    });
}
