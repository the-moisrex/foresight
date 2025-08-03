#include "common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
import foresight.mods;

using namespace foresight;

// this is just to see if testing works or not
TEST(SmoothTest, LERPTest) {
    (context
     | emit_all({
       {EV_REL,      REL_X, 10},
       {EV_REL,      REL_Y, 10},
       {EV_SYN, SYN_REPORT,  0}
    })
     | lerp
     | ([](event_type &event) {
           EXPECT_EQ(event.value(), 10);
       }))();
}
