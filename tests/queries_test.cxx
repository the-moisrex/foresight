
#include "common/tests_common_pch.hpp"

#include <coroutine>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

import fs8.devices.queries;
import fs8.devices.evdev;
import fs8.devices.udev;

using namespace fs8;

// ===========================================================================
// to_string(matching_action_type) Tests
// ===========================================================================

TEST(MatchingActionTypeToString, AllKnownActionsReturnNonEmpty) {
    using enum query_target;
    for (auto const action :
         {match_subsystem, match_sysattr, match_property, tag, syspath, sysname, nomatch_subsystem, nomatch_sysattr, nomatch_property})
    {
        SCOPED_TRACE("action = " + std::to_string(static_cast<unsigned>(action)));
        EXPECT_FALSE(to_string(action).empty());
    }
}

TEST(MatchingActionTypeToString, EachActionProducesUniqueString) {
    using enum query_target;
    std::vector<std::string_view> results;
    for (auto const action :
         {match_subsystem, match_sysattr, match_property, tag, syspath, sysname, nomatch_subsystem, nomatch_sysattr, nomatch_property})
    {
        results.push_back(to_string(action));
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
        for (std::size_t j = i + 1; j < results.size(); ++j) {
            SCOPED_TRACE("i = " + std::to_string(i) + ", j = " + std::to_string(j));
            EXPECT_NE(results[i], results[j]);
        }
    }
}

TEST(MatchingActionTypeToString, MatchAndNomatchDiffer) {
    using enum query_target;
    EXPECT_NE(to_string(match_subsystem), to_string(nomatch_subsystem));
    EXPECT_NE(to_string(match_sysattr), to_string(nomatch_sysattr));
    EXPECT_NE(to_string(match_property), to_string(nomatch_property));
}

TEST(MatchingActionTypeToString, UnknownValueReturnsNonEmpty) {
    auto const unknown = static_cast<query_target>(0xFF);
    EXPECT_FALSE(to_string(unknown).empty());
}

TEST(DeviceList, Basic) {
    int count = 0;
    for (auto dev : filter_devices(keyboard)) {
        EXPECT_TRUE(dev.device.is_valid());
        EXPECT_TRUE(matches(dev.device, keyboard));
        EXPECT_FALSE(matches(dev.device, mouse));
        // std::println("|||| {}", attr::name(dev.device));
        ++count;
    }
    EXPECT_NE(count, 0);
}

TEST(DeviceList, MultiQuery) {
    int count = 0;
    for (auto dev : filter_devices(keyboard, mouse)) {
        EXPECT_TRUE(dev.device.is_valid());
        EXPECT_TRUE(matches(dev.device, keyboard) || matches(dev.device, mouse));
        // std::println("Name {} {}", attr::name(dev.device), dev.device.sysname());
        ++count;
    }
    auto const vec = filter_devices(keyboard) | std::ranges::to<std::vector>();
    EXPECT_NE(count, vec.size());
}


TEST(ParseQueryTermTest, ValidTargetsNoKey) {
    auto q1 = parse_query_term("sub=input");
    EXPECT_EQ(q1.target, query_target::match_subsystem);
    EXPECT_TRUE(q1.key.empty());
    EXPECT_EQ(q1.value, "input");
    EXPECT_EQ(q1.percentage, 100);

    auto q2 = parse_query_term("name=event0");
    EXPECT_EQ(q2.target, query_target::sysname);
    EXPECT_TRUE(q2.key.empty());
    EXPECT_EQ(q2.value, "event0");
}

TEST(ParseQueryTermTest, ValidTargetsWithKey) {
    auto q1 = parse_query_term("attr:device/name=my_mouse");
    EXPECT_EQ(q1.target, query_target::match_sysattr);
    EXPECT_EQ(q1.key, "device/name");
    EXPECT_EQ(q1.value, "my_mouse");

    auto q2 = parse_query_term("prop:ID_INPUT_KEYBOARD=1");
    EXPECT_EQ(q2.target, query_target::match_property);
    EXPECT_EQ(q2.key, "ID_INPUT_KEYBOARD");
    EXPECT_EQ(q2.value, "1");
}

TEST(ParseQueryTermTest, InvertedMatches) {
    auto q1 = parse_query_term("!sub=input");
    EXPECT_EQ(q1.target, static_cast<query_target>(+query_target::match_subsystem | +query_target::nomatch_flag));
    EXPECT_TRUE(q1.key.empty());
    EXPECT_EQ(q1.value, "input");

    auto q2 = parse_query_term("!prop:ID_INPUT=1");
    EXPECT_EQ(q2.target, static_cast<query_target>(+query_target::match_property | +query_target::nomatch_flag));
    EXPECT_EQ(q2.key, "ID_INPUT");
    EXPECT_EQ(q2.value, "1");
}

TEST(ParseQueryTermTest, InvalidFormats) {
    auto expect_invalid = [](std::string_view str) {
        auto q = parse_query_term(str);
        EXPECT_EQ(q.target, invalid_field.target);
        EXPECT_EQ(q.key, invalid_field.key);
        EXPECT_EQ(q.value, invalid_field.value);
    };

    expect_invalid("sub_input");       // Missing '='
    expect_invalid("unknown=val");     // Unknown target
    expect_invalid("!badtarget=val");  // Unknown target with invert
    expect_invalid("");                // Empty string
}

TEST(ParseQueryTermTest, ValueContainsEquals) {
    // The first '=' should split target/key from value, allowing '=' inside the value itself
    auto q = parse_query_term("attr:test=a=b=c");
    EXPECT_EQ(q.target, query_target::match_sysattr);
    EXPECT_EQ(q.key, "test");
    EXPECT_EQ(q.value, "a=b=c");
}




class IsMatchedTest : public ::testing::Test {
protected:
    // Helper function to easily create a query_term for testing
    query_term create_term(std::string_view val, std::uint8_t percentage, query_target target = query_target::match_sysattr) {
        query_term term{};
        term.value = val; // Assuming 'value' is the target field string in query_term
        term.percentage = percentage;
        term.target = target;
        return term;
    }
};

// 1. Globe Search (101%)
TEST_F(IsMatchedTest, GlobeSearch) {
    auto term = create_term("usb*", 100);

    // Wildcard matches
    EXPECT_TRUE(is_matched(term, &query_term::value, "usb"));
    EXPECT_TRUE(is_matched(term, &query_term::value, "usb_device"));
    EXPECT_FALSE(is_matched(term, &query_term::value, "pci_usb"));

    // Multiple wildcards
    auto term2 = create_term("*usb*", 100);
    EXPECT_TRUE(is_matched(term2, &query_term::value, "pci_usb_device"));

    // No wildcards in target (fallback to exact match)
    auto term3 = create_term("usb_device", 100);
    EXPECT_TRUE(is_matched(term3, &query_term::value, "usb_device"));
    EXPECT_FALSE(is_matched(term3, &query_term::value, "usb_device_1"));
}

// 2. Exact Match (100%)
TEST_F(IsMatchedTest, ExactMatch) {
    auto term = create_term("exact_string", 100);

    EXPECT_TRUE(is_matched(term, &query_term::value, "exact_string"));
    EXPECT_FALSE(is_matched(term, &query_term::value, "exact_string_extra"));
    EXPECT_FALSE(is_matched(term, &query_term::value, "Exact_string")); // Case sensitive
}

// 3. Fuzzy Match (< 100%)
TEST_F(IsMatchedTest, FuzzyMatch) {
    // "hello" vs "helo" -> dist = 1, max_len = 5 -> sim = 100 - (1*100/5) = 80%
    auto term = create_term("hello", 80);

    EXPECT_TRUE(is_matched(term, &query_term::value, "helo"));   // Exactly 80%
    EXPECT_TRUE(is_matched(term, &query_term::value, "hello"));  // 100% (>= 80%)
    EXPECT_FALSE(is_matched(term, &query_term::value, "hero"));  // dist = 2 -> sim = 60% (< 80%)

    // "kitten" vs "sitting" -> dist = 3, max_len = 7 -> sim = 100 - 42 = 58%
    auto term2 = create_term("kitten", 50);
    EXPECT_TRUE(is_matched(term2, &query_term::value, "sitting")); // 58% >= 50%

    auto term3 = create_term("kitten", 60);
    EXPECT_FALSE(is_matched(term3, &query_term::value, "sitting")); // 58% < 60%

    // Edge cases (empty strings)
    auto empty_term = create_term("", 50);
    EXPECT_TRUE(is_matched(empty_term, &query_term::value, ""));    // Both empty = 100%
    EXPECT_FALSE(is_matched(empty_term, &query_term::value, "a"));  // One empty = 0%
}

// 4. Inversion (nomatch_flag)
TEST_F(IsMatchedTest, InvertedMatch) {
    // Globe match inverted
    auto term1 = create_term("usb*", 100, query_target::nomatch_flag);
    EXPECT_FALSE(is_matched(term1, &query_term::value, "usb_device")); // Match -> False
    EXPECT_TRUE(is_matched(term1, &query_term::value, "pci_device"));  // No match -> True

    // Exact match inverted
    auto term2 = create_term("exact", 100, query_target::nomatch_flag);
    EXPECT_FALSE(is_matched(term2, &query_term::value, "exact")); // Match -> False
    EXPECT_TRUE(is_matched(term2, &query_term::value, "other"));  // No match -> True

    // Fuzzy match inverted (threshold 80%)
    auto term3 = create_term("hello", 80, query_target::nomatch_flag);
    EXPECT_FALSE(is_matched(term3, &query_term::value, "helo")); // Match -> False
    EXPECT_TRUE(is_matched(term3, &query_term::value, "hero"));  // No match -> True
}