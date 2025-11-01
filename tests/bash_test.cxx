#include "./common/tests_common_pch.hpp"

import foresight.bash;

using foresight::bash_runner;

TEST(BashTest, Basic) {
    bash_runner bash;
    bash.start();
    bash.set_variable("key", "value");
    EXPECT_EQ(bash.get_variable("key"), "value");
    bash.set_variable("key", "value 2");
    EXPECT_EQ(bash.get_variable("key"), "value 2");
}

