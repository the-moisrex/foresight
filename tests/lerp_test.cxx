#include "common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
import fs8.mods;
import fs8.log;

using namespace fs8;

// this is just to see if testing works or not
TEST(SmoothTest, LERPTest) {
    (context
     | emit_all({
       {EV_REL,      REL_X, 3},
       {EV_REL,      REL_Y, 3},
       {EV_SYN, SYN_REPORT, 0},
       {EV_REL,      REL_X, 9},
       {EV_REL,      REL_Y, 9},
       {EV_SYN, SYN_REPORT, 0}
    })
     | lerp
     | log
     | ([](event_type &event) {
           EXPECT_LE(event.value(), 10);
       }))();
}
