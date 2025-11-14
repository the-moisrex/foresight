#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import foresight.mods;

TEST(SearchTest, Basic) {
    using namespace foresight;
    (context
     | emit_all({
       {EV_KEY,      KEY_T, 1},
       {EV_SYN, SYN_REPORT, 0},
       {EV_KEY,      KEY_T, 0},
       {EV_SYN, SYN_REPORT, 0},

       {EV_KEY,      KEY_E, 1},
       {EV_SYN, SYN_REPORT, 0},
       {EV_KEY,      KEY_E, 0},
       {EV_SYN, SYN_REPORT, 0},

       {EV_KEY,      KEY_S, 1},
       {EV_SYN, SYN_REPORT, 0},
       {EV_KEY,      KEY_S, 0},
       {EV_SYN, SYN_REPORT, 0},

       {EV_KEY,      KEY_T, 1},
       {EV_SYN, SYN_REPORT, 0},
       {EV_KEY,      KEY_T, 0},
       {EV_SYN, SYN_REPORT, 0},
    })
     | search_engine
     | on(typed("test"), [] {
           EXPECT_TRUE(true);
       }))();
}
