#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import foresight.mods;

int happened = 0;              // NOLINT

TEST(SearchTest, Basic) {
    using namespace foresight; // NOLINT(*-build-using-namespace)

    happened = 0;

    (context
     | emit_all({
       {.type = EV_KEY,      .code = KEY_T, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_T, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,      .code = KEY_E, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_E, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,      .code = KEY_S, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_S, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,      .code = KEY_T, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_T, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    })
     | search_engine
     | on(typed("test"), [] {
           happened = 1;
       }))();
    EXPECT_TRUE(happened == 1);
}

TEST(SearchTest, Multi) {
    using namespace foresight; // NOLINT(*-build-using-namespace)
    happened = 0;

    (context
     | emit_all({
       {.type = EV_KEY,      .code = KEY_T, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_T, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,      .code = KEY_E, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_E, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,      .code = KEY_S, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_S, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,      .code = KEY_T, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_T, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    })
     | search_engine
     | on(typed("test"),
          [] {
              ++happened;
          })
     | on(typed("es"), [] {
           ++happened;
       }))();
    EXPECT_TRUE(happened == 2);
}
