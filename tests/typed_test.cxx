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

TEST(SearchTest, EncodedRepeatedTag) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    // [Ctrl+A][Ctrl+A] encodes as two distinct keyup events
    auto const enc = encoded_modifiers("[Ctrl+A][Ctrl+A]");
    EXPECT_FALSE(enc.empty());
    // Must differ from a single [Ctrl+A]
    EXPECT_NE(enc, encoded_modifiers("[Ctrl+A]"));
}

TEST(SearchTest, EncodedDuplicateKeyTag) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    // [Ctrl+A+A] encodes differently from [Ctrl+A] (extra A key-up)
    EXPECT_NE(encoded_modifiers("[Ctrl+A+A]"), encoded_modifiers("[Ctrl+A]"));
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

TEST(SearchTest, RepeatedKeyUpTag) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // [Ctrl+A][Ctrl+A] — two separate keyup tags.
    // Each [Ctrl+A] encodes as enc_A_up, enc_CTRL_up.
    // So the full pattern is: A↑, Ctrl↑, A↑, Ctrl↑.
    // We need two full Ctrl+A press/release cycles.
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},

       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["[Ctrl+A][Ctrl+A]"], [] noexcept {
           ++happened;
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(SearchTest, DuplicateKeyInTag) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // [Ctrl+A+A] — one tag with three keys.
    // Canonical sort puts Ctrl first, then reverse for keyup: A↑, A↑, Ctrl↑.
    // We press Ctrl+A, release A twice, then release Ctrl.
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 1},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,        .code = KEY_A, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 0},
       {.type = EV_SYN,   .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["[Ctrl+A+A]"], [] noexcept {
           ++happened;
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

TEST(SearchTest, OverlappingKeys) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // Overlapping keys: T down, E down (T held), T up, S down (E held), E up, T down, S up, T up
    // This should still match "test".
    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_T, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_E, .value = 1}, // T still held
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_T, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_S, .value = 1}, // E still held
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_E, .value = 0},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_T, .value = 1}, // S still held
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_S, .value = 0},
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

TEST(SearchTest, OverlappingKeysWithShiftRepeat) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // Shift repeat events + overlapping keys: the exact scenario from the bug report.
    // Shift press with many repeats, then @ (Shift+2), then overlapping T/E/S/T.
    (context
     | emit_all[{
       // Shift down with many repeats
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 1},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 2},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},

       // @ (Shift+2)
       {.type = EV_KEY,          .code = KEY_2, .value = 1},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_2, .value = 0},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY, .code = KEY_RIGHTSHIFT, .value = 0},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},

       // Overlapping T/E/S/T
       {.type = EV_KEY,          .code = KEY_T, .value = 1},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_E, .value = 1},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_T, .value = 0},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_S, .value = 1},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_E, .value = 0},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_T, .value = 1},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_S, .value = 0},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,          .code = KEY_T, .value = 0},
       {.type = EV_SYN,     .code = SYN_REPORT, .value = 0},
    }]
     | search_engine
     | on[typed["@test"], [] noexcept {
           happened = 1;
       }])();
    EXPECT_TRUE(happened == 1);
}
