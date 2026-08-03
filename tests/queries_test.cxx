
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
        // std::println("{}", attr::name(dev.device));
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
