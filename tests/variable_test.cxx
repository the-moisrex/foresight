#include "common/tests_common_pch.hpp"

import foresight.mods;
import foresight.main.log;
import foresight.utils.hash;

using namespace fs8;

// static_assert(has_variables<var_type<>>, "Variable Mod should have a variable");

TEST(VariableTest, Basic) {
    static constinit auto pipeline =
      context
      | var_type{"one", 1} // variable one
      | var_type{"two", 2};

    EXPECT_EQ(get<int>(pipeline["one"]), 1);
}

TEST(VariableTest, DifferentTypes) {
    static constinit auto pipeline =
      context
      | var_type{"one", 1} // variable one
      | var_type{"two", std::string_view{"two"}};

    EXPECT_EQ(get<int>(pipeline["one"]), 1);
    EXPECT_EQ(get<std::string_view>(pipeline["two"]), "two");
}
