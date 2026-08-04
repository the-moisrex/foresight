
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
    EXPECT_EQ(q1.percentage, globe_search);

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
