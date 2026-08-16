#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.mods;
import fs8.lib.mod_parser;

int happened = 0;        // NOLINT

TEST(SearchTest, Basic) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    happened = 0;

    (context
     | emit_all[{
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
    }]
     | search_engine
     | on[typed["test"], [] noexcept {
           happened = 1;
       }])();
    EXPECT_TRUE(happened == 1);
}

TEST(SearchTest, BasicStateful) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    happened = 0;

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,         .code = KEY_2, .value = 1}, // at-sight @
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,         .code = KEY_2, .value = 0}, // at-sight @
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 0},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,         .code = KEY_T, .value = 1},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,         .code = KEY_T, .value = 0},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,         .code = KEY_E, .value = 1},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,         .code = KEY_E, .value = 0},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,         .code = KEY_S, .value = 1},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,         .code = KEY_S, .value = 0},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,         .code = KEY_T, .value = 1},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,         .code = KEY_T, .value = 0},
       {.type = EV_SYN,    .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["@test"], [] noexcept {
           happened = 1;
       }])();
    EXPECT_TRUE(happened == 1);
}

TEST(SearchTest, Multi) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    (context
     | emit_all[{
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
    }]
     | search_engine
     | on[typed["test"],
          [] noexcept {
              ++happened;
              EXPECT_EQ(happened, 2);
          }]
     | on[typed["es"], [] noexcept {
           ++happened;
           EXPECT_EQ(happened, 1);
       }])();
    EXPECT_EQ(happened, 2);
}

TEST(SearchTest, ModiferTest) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,        .code = KEY_R, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_R, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["<ctrl-r>"], [] noexcept {
           ++happened;
           EXPECT_EQ(happened, 1);
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(SearchTest, CanonicalOrder) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // `<r-ctrl>` must behave exactly like `<ctrl-r>` (order doesn't matter in keydown mode)
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,        .code = KEY_R, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_R, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["<r-ctrl>"], [] noexcept {
           ++happened;
           EXPECT_EQ(happened, 1);
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(SearchTest, EncodedCanonical) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    EXPECT_EQ(encoded_modifiers("<ctrl-shift-x>"), encoded_modifiers("<shift-ctrl-x>"));
}

TEST(SearchTest, KeyUp) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // `[x]` must fire on the key-up of `x`
    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_X, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_X, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["[x]"], [] noexcept {
           ++happened;
           EXPECT_EQ(happened, 1);
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(SearchTest, OrderedKeydown) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // `<<ctrl-r>>` must match ctrl-then-r
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,        .code = KEY_R, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_R, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["<<ctrl-r>>"], [] noexcept {
           ++happened;
           EXPECT_EQ(happened, 1);
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(SearchTest, OrderedKeydownNegative) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // `<<r-ctrl>>` must NOT match ctrl-then-r (order matters in ordered mode)
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY,        .code = KEY_R, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_R, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["<<r-ctrl>>"], [] noexcept {
           ++happened;
       }])();
    EXPECT_EQ(happened, 0);
}
