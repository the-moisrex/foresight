
#include "common/tests_common_pch.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import fs8.devices.queries;
import fs8.devices.capabilities;

using namespace fs8;

// ===========================================================================
// to_string(matching_action_type) Tests
// ===========================================================================

TEST(MatchingActionTypeToString, AllKnownActionsReturnNonEmpty) {
    using enum matching_action_type;
    for (auto const action :
         {match_subsystem,
          match_sysattr,
          match_property,
          match_tag,
          syspath,
          match_sysname,
          nomatch_subsystem,
          nomatch_sysattr,
          nomatch_property})
    {
        SCOPED_TRACE("action = " + std::to_string(static_cast<unsigned>(action)));
        EXPECT_FALSE(to_string(action).empty());
    }
}

TEST(MatchingActionTypeToString, EachActionProducesUniqueString) {
    using enum matching_action_type;
    std::vector<std::string_view> results;
    for (auto const action :
         {match_subsystem,
          match_sysattr,
          match_property,
          match_tag,
          syspath,
          match_sysname,
          nomatch_subsystem,
          nomatch_sysattr,
          nomatch_property})
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
    using enum matching_action_type;
    EXPECT_NE(to_string(match_subsystem), to_string(nomatch_subsystem));
    EXPECT_NE(to_string(match_sysattr), to_string(nomatch_sysattr));
    EXPECT_NE(to_string(match_property), to_string(nomatch_property));
}

TEST(MatchingActionTypeToString, UnknownValueReturnsNonEmpty) {
    auto const unknown = static_cast<matching_action_type>(0xFF);
    EXPECT_FALSE(to_string(unknown).empty());
}
