#include "./common/tests_common_pch.hpp"
#include <array>
#include <concepts>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


import foresight.devices.classification;

using fs8::udev;
using fs8::udev_device;
using fs8::udev_enumerate;
using fs8::udev_list_entry;
using fs8::udev_monitor;

using fs8::classify::Classification;
using fs8::classify::match;
using fs8::classify::drawing_tablet;
using fs8::classify::keyboard;
using fs8::classify::matches;
using fs8::classify::mouse;
using fs8::classify::properties;
using fs8::classify::properties_storage;
using fs8::classify::properties_type;
using fs8::classify::property_matcher;
using fs8::classify::subsystem;
using fs8::classify::via_usb;
using fs8::classify::with_name;

namespace {

// ---------------------------------------------------------------------------
// Custom classifications used only in tests
// ---------------------------------------------------------------------------

struct empty_classification {
    // no subsystem / properties members → defaults
};

struct explicit_subsystem_only {
    static constexpr std::string_view subsystem = "hid";
};

struct single_flag_property {
    static constexpr std::string_view property_key = "ID_INPUT_TOUCHPAD";
};

struct key_and_value_property {
    static constexpr std::string_view property_key   = "ID_BUS";
    static constexpr std::string_view property_value = "bluetooth";
};

struct multi_property_classification {
    static constexpr std::string_view subsystem = "input";
    static constexpr properties_storage<2> properties{{
        {"ID_INPUT_KEYBOARD", "1"},
        {"ID_BUS", "usb"},
    }};
};

struct unknown_subsystem_type {
    // intentionally no members
};

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

class UdevEnvironment : public ::testing::Test {
  protected:
    void SetUp() override {
        ctx_ = udev::instance();
        ASSERT_TRUE(ctx_.is_valid()) << "libudev context could not be created";
    }

    [[nodiscard]] udev const& ctx() const noexcept { return ctx_; }

    /// Best-effort: first enumerated device matching subsystem (may be empty).
    [[nodiscard]] udev_device first_device_in_subsystem(char const* sub) const {
        udev_enumerate en(ctx_);
        if (!en) {
            return {};
        }
        en.match_subsystem(sub);
        en.scan_devices();
        for (auto const entry : en.list_entries()) {
            auto const path = entry.name();
            if (path.empty()) {
                continue;
            }
            // syspath must be a null-terminated C string for libudev
            std::string const syspath{path};
            udev_device dev(ctx_.native(), syspath.c_str());
            if (dev) {
                return dev;
            }
        }
        return {};
    }

    /// Enumerate devices after applying a classification filter.
    [[nodiscard]] std::vector<udev_device>
    enumerate_with(auto const& cls) const {
        udev_enumerate en(ctx_);
        EXPECT_TRUE(static_cast<bool>(en));
        match(en, cls);
        en.scan_devices();

        std::vector<udev_device> out;
        for (auto const entry : en.list_entries()) {
            auto const path = entry.name();
            if (path.empty()) {
                continue;
            }
            std::string const syspath{path};
            udev_device dev(ctx_.native(), syspath.c_str());
            if (dev) {
                out.push_back(std::move(dev));
            }
        }
        return out;
    }

  private:
    udev ctx_{};
};

class ClassificationMetaTest : public ::testing::Test {};

class PropertyMatcherTest : public UdevEnvironment {};

class MatchesIntegrationTest : public UdevEnvironment {};

class ApplyFilterEnumerateTest : public UdevEnvironment {};

class ApplyFilterMonitorTest : public UdevEnvironment {};

} // namespace

// =============================================================================
// Compile-time / concept checks
// =============================================================================

TEST(ClassificationConcept, BuiltinsSatisfyConcept) {
    static_assert(Classification<decltype(keyboard)>);
    static_assert(Classification<decltype(mouse)>);
    static_assert(Classification<decltype(drawing_tablet)>);
    static_assert(Classification<multi_property_classification>);
    static_assert(Classification<single_flag_property>);
    static_assert(Classification<key_and_value_property>);
    SUCCEED();
}

TEST(ClassificationConcept, EmptyAndDefaultsStillFormValidCallables) {
    // Defaults: subsystem → "unknown", properties → empty span
    static_assert(std::same_as<decltype(subsystem(empty_classification{})), std::string_view>);
    static_assert(std::same_as<decltype(properties(empty_classification{})), properties_type>);
    EXPECT_EQ(subsystem(empty_classification{}), "unknown");
    EXPECT_TRUE(properties(empty_classification{}).empty());
}

// =============================================================================
// subsystem() extraction
// =============================================================================

TEST_F(ClassificationMetaTest, KeyboardSubsystemIsInput) {
    EXPECT_EQ(subsystem(keyboard), "input");
}

TEST_F(ClassificationMetaTest, MouseSubsystemIsInput) {
    EXPECT_EQ(subsystem(mouse), "input");
}

TEST_F(ClassificationMetaTest, DrawingTabletSubsystemIsInput) {
    EXPECT_EQ(subsystem(drawing_tablet), "input");
}

TEST_F(ClassificationMetaTest, ExplicitSubsystemOnly) {
    EXPECT_EQ(subsystem(explicit_subsystem_only{}), "hid");
}

TEST_F(ClassificationMetaTest, DefaultSubsystemIsUnknown) {
    EXPECT_EQ(subsystem(unknown_subsystem_type{}), "unknown");
}

TEST_F(ClassificationMetaTest, PropertyMatcherStaticSubsystemIsInput) {
    property_matcher const m{.key = "ID_BUS", .value = "usb"};
    EXPECT_EQ(property_matcher::subsystem, "input");
    // free-function subsystem via static member path
    EXPECT_EQ(subsystem(m), "input");
}

// =============================================================================
// properties() extraction
// =============================================================================

TEST_F(ClassificationMetaTest, KeyboardHasIdInputKeyboardFlag) {
    properties_type const props = properties(keyboard);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].first, "ID_INPUT_KEYBOARD");
    EXPECT_EQ(props[0].second, "1");
}

TEST_F(ClassificationMetaTest, MouseHasIdInputMouseFlag) {
    properties_type const props = properties(mouse);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].first, "ID_INPUT_MOUSE");
    EXPECT_EQ(props[0].second, "1");
}

TEST_F(ClassificationMetaTest, DrawingTabletHasIdInputTabletFlag) {
    properties_type const props = properties(drawing_tablet);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].first, "ID_INPUT_TABLET");
    EXPECT_EQ(props[0].second, "1");
}

TEST_F(ClassificationMetaTest, SingleFlagPropertyDefaultsValueToOne) {
    properties_type const props = properties(single_flag_property{});
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].first, "ID_INPUT_TOUCHPAD");
    EXPECT_EQ(props[0].second, "1");
}

TEST_F(ClassificationMetaTest, KeyAndValuePropertyPreservesValue) {
    properties_type const props = properties(key_and_value_property{});
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].first, "ID_BUS");
    EXPECT_EQ(props[0].second, "bluetooth");
}

TEST_F(ClassificationMetaTest, MultiPropertyClassificationExposesAllPairs) {
    properties_type const props = properties(multi_property_classification{});
    ASSERT_EQ(props.size(), 2u);
    EXPECT_EQ(props[0].first, "ID_INPUT_KEYBOARD");
    EXPECT_EQ(props[0].second, "1");
    EXPECT_EQ(props[1].first, "ID_BUS");
    EXPECT_EQ(props[1].second, "usb");
}

TEST_F(ClassificationMetaTest, EmptyClassificationHasNoProperties) {
    EXPECT_TRUE(properties(empty_classification{}).empty());
}

// =============================================================================
// via_usb / with_name helpers
// =============================================================================

TEST_F(ClassificationMetaTest, ViaUsbMatcherFields) {
    EXPECT_EQ(via_usb.key, "ID_BUS");
    EXPECT_EQ(via_usb.value, "usb");
    EXPECT_EQ(via_usb.subsystem, "input");
}

TEST_F(ClassificationMetaTest, WithNameBuildsNameMatcher) {
    constexpr auto m = with_name("AT Translated Set 2 keyboard");
    EXPECT_EQ(m.key, "NAME");
    EXPECT_EQ(m.value, "AT Translated Set 2 keyboard");
}

TEST_F(ClassificationMetaTest, WithNameEmptyStringIsAllowed) {
    constexpr auto m = with_name("");
    EXPECT_EQ(m.key, "NAME");
    EXPECT_TRUE(m.value.empty());
}

TEST_F(ClassificationMetaTest, WithNameUnusualCharactersPreserved) {
    constexpr auto m = with_name(R"(Vendor "X" / Model #1)");
    EXPECT_EQ(m.value, R"(Vendor "X" / Model #1)");
}

// =============================================================================
// matches() — invalid / edge devices
// =============================================================================

TEST_F(MatchesIntegrationTest, InvalidDeviceDoesNotMatchKeyboard) {
    udev_device const invalid{};
    ASSERT_FALSE(invalid.is_valid());
    // subsystem()/property() on null device should yield empty views → no match
    EXPECT_FALSE(matches(invalid, keyboard));
    EXPECT_FALSE(matches(invalid, mouse));
    EXPECT_FALSE(matches(invalid, drawing_tablet));
}

TEST_F(MatchesIntegrationTest, InvalidDeviceDoesNotMatchPropertyMatcher) {
    udev_device const invalid{};
    EXPECT_FALSE(matches(invalid, via_usb));
    EXPECT_FALSE(matches(invalid, with_name("anything")));
}

TEST_F(MatchesIntegrationTest, EmptyClassificationMatchesOnlyUnknownSubsystem) {
    // Real devices use real subsystems; empty_classification wants "unknown"
    auto const input = first_device_in_subsystem("input");
    if (!input) {
        GTEST_SKIP() << "No input devices present on this host";
    }
    EXPECT_FALSE(matches(input, empty_classification{}));
}

// =============================================================================
// matches() — real devices (integration; skipped when hardware absent)
// =============================================================================

TEST_F(MatchesIntegrationTest, KeyboardMatchesAreConsistentWithProperties) {
    auto const devices = enumerate_with(keyboard);
    if (devices.empty()) {
        GTEST_SKIP() << "No keyboard-class devices enumerated";
    }

    for (auto const& dev : devices) {
        ASSERT_TRUE(dev.is_valid());
        EXPECT_EQ(dev.subsystem(), "input");
        EXPECT_EQ(dev.property("ID_INPUT_KEYBOARD"), "1");
        EXPECT_TRUE(matches(dev, keyboard));
    }
}

TEST_F(MatchesIntegrationTest, MouseMatchesAreConsistentWithProperties) {
    auto const devices = enumerate_with(mouse);
    if (devices.empty()) {
        GTEST_SKIP() << "No mouse-class devices enumerated";
    }

    for (auto const& dev : devices) {
        ASSERT_TRUE(dev.is_valid());
        EXPECT_EQ(dev.subsystem(), "input");
        EXPECT_EQ(dev.property("ID_INPUT_MOUSE"), "1");
        EXPECT_TRUE(matches(dev, mouse));
    }
}

TEST_F(MatchesIntegrationTest, DrawingTabletMatchesAreConsistentWithProperties) {
    auto const devices = enumerate_with(drawing_tablet);
    if (devices.empty()) {
        GTEST_SKIP() << "No drawing-tablet devices enumerated";
    }

    for (auto const& dev : devices) {
        ASSERT_TRUE(dev.is_valid());
        EXPECT_EQ(dev.subsystem(), "input");
        EXPECT_EQ(dev.property("ID_INPUT_TABLET"), "1");
        EXPECT_TRUE(matches(dev, drawing_tablet));
    }
}

TEST_F(MatchesIntegrationTest, KeyboardDoesNotMatchMouseOnlyDevice) {
    auto const mice = enumerate_with(mouse);
    if (mice.empty()) {
        GTEST_SKIP() << "No mouse devices to cross-check";
    }

    for (auto const& dev : mice) {
        // Pure mice often lack ID_INPUT_KEYBOARD=1; if a combo device has both,
        // matches(keyboard) may still be true — only assert the inverse when
        // the keyboard flag is absent.
        if (dev.property("ID_INPUT_KEYBOARD") != "1") {
            EXPECT_FALSE(matches(dev, keyboard));
        }
    }
}

TEST_F(MatchesIntegrationTest, MultiPropertyRequiresAllPairs) {
    auto const keyboards = enumerate_with(keyboard);
    if (keyboards.empty()) {
        GTEST_SKIP() << "No keyboards enumerated";
    }

    multi_property_classification const cls{};
    for (auto const& dev : keyboards) {
        bool const expect =
            dev.subsystem() == "input" &&
            dev.property("ID_INPUT_KEYBOARD") == "1" &&
            dev.property("ID_BUS") == "usb";
        EXPECT_EQ(matches(dev, cls), expect);
    }
}

// =============================================================================
// property_matcher specialized matches / apply_filter
// =============================================================================

TEST_F(PropertyMatcherTest, MatchesViaUsbWhenBusIsUsb) {
    auto const keyboards = enumerate_with(keyboard);
    if (keyboards.empty()) {
        GTEST_SKIP() << "No keyboards enumerated";
    }

    bool saw_usb     = false;
    bool saw_non_usb = false;

    for (auto const& dev : keyboards) {
        auto const bus = dev.property("ID_BUS");
        if (bus == "usb") {
            saw_usb = true;
            EXPECT_TRUE(matches(dev, via_usb));
        } else if (!bus.empty()) {
            saw_non_usb = true;
            EXPECT_FALSE(matches(dev, via_usb));
        }
    }

    if (!saw_usb && !saw_non_usb) {
        GTEST_SKIP() << "No ID_BUS properties on enumerated keyboards";
    }
}

TEST_F(PropertyMatcherTest, MatchesWithNameExact) {
    auto const keyboards = enumerate_with(keyboard);
    if (keyboards.empty()) {
        GTEST_SKIP() << "No keyboards enumerated";
    }

    // Prefer NAME; fall back to empty skip
    std::string_view observed_name;
    for (auto const& dev : keyboards) {
        auto const n = dev.property("NAME");
        if (!n.empty()) {
            observed_name = n;
            break;
        }
    }
    if (observed_name.empty()) {
        GTEST_SKIP() << "No NAME property on keyboard devices";
    }

    // property_matcher compares against property(key); ensure key is used.
    // Note: production code uses matcher.key.data() — values must be
    // backed by null-terminated storage for libudev.
    std::string const name_storage{observed_name};
    property_matcher const exact{.key = "NAME", .value = name_storage};

    bool any = false;
    for (auto const& dev : keyboards) {
        if (dev.property("NAME") == observed_name) {
            any = true;
            EXPECT_TRUE(matches(dev, exact));
        } else if (!dev.property("NAME").empty()) {
            EXPECT_FALSE(matches(dev, exact));
        }
    }
    EXPECT_TRUE(any);
}

TEST_F(PropertyMatcherTest, MatchesWithNameWrongValueIsFalse) {
    auto const input = first_device_in_subsystem("input");
    if (!input) {
        GTEST_SKIP() << "No input devices";
    }
    property_matcher const bogus{.key = "NAME", .value = "__no_such_device_name__"};
    EXPECT_FALSE(matches(input, bogus));
}

TEST_F(PropertyMatcherTest, MatchesMissingPropertyIsFalse) {
    auto const input = first_device_in_subsystem("input");
    if (!input) {
        GTEST_SKIP() << "No input devices";
    }
    property_matcher const missing{
        .key   = "__FS8_TEST_PROPERTY_THAT_SHOULD_NOT_EXIST__",
        .value = "1",
    };
    EXPECT_FALSE(matches(input, missing));
}

TEST_F(PropertyMatcherTest, ApplyFilterOnEnumerateRestrictsByProperty) {
    udev_enumerate en(ctx());
    ASSERT_TRUE(static_cast<bool>(en));

    match(en, via_usb);
    en.scan_devices();

    std::size_t count = 0;
    for (auto const entry : en.list_entries()) {
        ++count;
        std::string const syspath{entry.name()};
        udev_device const dev(ctx().native(), syspath.c_str());
        if (!dev) {
            continue;
        }
        // enumerate match_property behaves as an OR with other property filters.
        // property matcher does not force subsystem in apply_filter — only property.
        EXPECT_EQ(dev.property("ID_BUS"), "usb")
            << "syspath=" << syspath;
    }

    // Host may legitimately have zero USB-tagged devices in the match set
    (void)count;
}

TEST_F(PropertyMatcherTest, ApplyFilterOnMonitorIsNoOpButCallable) {
    udev_monitor mon(ctx());
    if (!mon.is_valid()) {
        GTEST_SKIP() << "udev_monitor unavailable";
    }
    // Specialized overload intentionally does nothing (property filter post-event)
    EXPECT_NO_FATAL_FAILURE(match(mon, via_usb));
    EXPECT_NO_FATAL_FAILURE(match(mon, with_name("x")));
}

// =============================================================================
// apply_filter(udev_enumerate)
// =============================================================================

TEST_F(ApplyFilterEnumerateTest, KeyboardFilterOnlyYieldsInputSubsystem) {
    auto const devices = enumerate_with(keyboard);
    for (auto const& dev : devices) {
        EXPECT_EQ(dev.subsystem(), "input");
        EXPECT_EQ(dev.property("ID_INPUT_KEYBOARD"), "1");
    }
}

TEST_F(ApplyFilterEnumerateTest, MouseFilterOnlyYieldsInputSubsystem) {
    auto const devices = enumerate_with(mouse);
    for (auto const& dev : devices) {
        EXPECT_EQ(dev.subsystem(), "input");
        EXPECT_EQ(dev.property("ID_INPUT_MOUSE"), "1");
    }
}

TEST_F(ApplyFilterEnumerateTest, ChainedClassificationAndPropertyMatcherSemantics) {
    // apply_filter(keyboard) then additional property filter on a fresh enumerate
    udev_enumerate en(ctx());
    ASSERT_TRUE(static_cast<bool>(en));
    match(en, keyboard);
    match(en, via_usb);
    en.scan_devices();

    for (auto const entry : en.list_entries()) {
        std::string const syspath{entry.name()};
        udev_device const dev(ctx().native(), syspath.c_str());
        if (!dev) {
            continue;
        }
        EXPECT_EQ(dev.subsystem(), "input");

        // Multiple udev_enumerate_add_match_property calls act as a logical OR.
        // The device should match at least one of the applied property filters.
        bool const is_keyboard = dev.property("ID_INPUT_KEYBOARD") == "1";
        bool const is_usb      = dev.property("ID_BUS") == "usb";
        EXPECT_TRUE(is_keyboard || is_usb);
    }
}

struct subsystem_only {
    static constexpr std::string_view subsystem = "input";
};
TEST_F(ApplyFilterEnumerateTest, EmptyPropertiesStillAppliesSubsystem) {
    static_assert(Classification<subsystem_only>);

    udev_enumerate en(ctx());
    ASSERT_TRUE(static_cast<bool>(en));
    match(en, subsystem_only{});
    en.scan_devices();

    for (auto const entry : en.list_entries()) {
        std::string const syspath{entry.name()};
        udev_device const dev(ctx().native(), syspath.c_str());
        if (!dev) {
            continue;
        }
        EXPECT_EQ(dev.subsystem(), "input");
    }
}

struct bogus {
    static constexpr std::string_view subsystem    = "__fs8_no_such_subsystem__";
    static constexpr std::string_view property_key = "ID_INPUT_KEYBOARD";
};

TEST_F(ApplyFilterEnumerateTest, UnknownSubsystemYieldsNoDevicesTypically) {
    auto const devices = enumerate_with(bogus{});
    EXPECT_TRUE(devices.empty());
}

// =============================================================================
// apply_filter(udev_monitor)
// =============================================================================

TEST_F(ApplyFilterMonitorTest, KeyboardFilterDoesNotThrowAndKeepsMonitorValid) {
    udev_monitor mon(ctx());
    if (!mon.is_valid()) {
        GTEST_SKIP() << "udev_monitor unavailable";
    }

    EXPECT_NO_FATAL_FAILURE(match(mon, keyboard));
    EXPECT_NO_FATAL_FAILURE(match(mon, mouse));
    EXPECT_NO_FATAL_FAILURE(match(mon, drawing_tablet));
    EXPECT_TRUE(mon.is_valid());
}

TEST_F(ApplyFilterMonitorTest, EnableAndPollNextDeviceIsSafeWhenIdle) {
    udev_monitor mon(ctx());
    if (!mon.is_valid()) {
        GTEST_SKIP() << "udev_monitor unavailable";
    }

    match(mon, keyboard);
    mon.enable();

    // Non-blocking receive: typically empty when no events are pending
    udev_device const ev = mon.next_device();
    if (ev) {
        // If an event raced in, classification must still be coherent
        if (ev.subsystem() == "input") {
            // Post-filter: only accept if matches() says so
            if (matches(ev, keyboard)) {
                EXPECT_EQ(ev.property("ID_INPUT_KEYBOARD"), "1");
            }
        }
    }
    SUCCEED();
}

// =============================================================================
// Boundary / malformed input style cases (classification side)
// =============================================================================

TEST_F(ClassificationMetaTest, PropertyMatcherEmptyKey) {
    property_matcher const m{.key = "", .value = "1"};
    EXPECT_TRUE(m.key.empty());
    EXPECT_EQ(m.value, "1");
}

TEST_F(ClassificationMetaTest, PropertyMatcherEmptyValue) {
    property_matcher const m{.key = "ID_BUS", .value = ""};
    EXPECT_TRUE(m.value.empty());
}

TEST_F(PropertyMatcherTest, EmptyKeyMatcherDoesNotMatchNormalDevices) {
    auto const input = first_device_in_subsystem("input");
    if (!input) {
        GTEST_SKIP() << "No input devices";
    }
    property_matcher const m{.key = "", .value = "1"};
    // libudev property lookup with empty / odd keys should not equal "1"
    EXPECT_FALSE(matches(input, m));
}

struct keyboard_and_impossible_bus {
    static constexpr std::string_view subsystem = "input";
    static constexpr properties_storage<2> properties{{
        {"ID_INPUT_KEYBOARD", "1"},
        {"ID_BUS", "__not_a_real_bus__"},
    }};
};

TEST_F(MatchesIntegrationTest, MatchesIsConjunctionOverProperties) {
    // Device must fail if any required property differs (manual iteration acts as AND)
    auto const devices = enumerate_with(keyboard);
    if (devices.empty()) {
        GTEST_SKIP() << "No keyboards";
    }

    for (auto const& dev : devices) {
        EXPECT_FALSE(matches(dev, keyboard_and_impossible_bus{}));
    }
}

struct wrong_subsystem {
    static constexpr std::string_view subsystem    = "block";
    static constexpr std::string_view property_key = "ID_INPUT_KEYBOARD";
};

TEST_F(MatchesIntegrationTest, SubsystemMismatchFailsEvenIfPropertiesHappenToMatch) {
    auto const devices = enumerate_with(keyboard);
    if (devices.empty()) {
        GTEST_SKIP() << "No keyboards";
    }
    for (auto const& dev : devices) {
        EXPECT_FALSE(matches(dev, wrong_subsystem{}));
    }
}

// =============================================================================
// Smoke: list_entries iteration stability after filter
// =============================================================================

struct impossible {
    static constexpr std::string_view subsystem    = "input";
    static constexpr std::string_view property_key   = "ID_INPUT_KEYBOARD";
    static constexpr std::string_view property_value = "__impossible__";
};

TEST_F(ApplyFilterEnumerateTest, ListEntriesBeginEndOnEmptyResult) {
    udev_enumerate en(ctx());
    ASSERT_TRUE(static_cast<bool>(en));
    match(en, impossible{});
    en.scan_devices();

    auto const list = en.list_entries();
    EXPECT_EQ(list.begin(), list.end());
}
