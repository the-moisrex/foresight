#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.mods;

namespace {
    /// Pull the (type, code, value) triples, skipping SYN_REPORTs.
    [[nodiscard]] std::vector<std::array<int, 3>> key_events(std::vector<fs8::user_event> const &events) {
        std::vector<std::array<int, 3>> out;
        for (auto const &event : events) {
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                continue;
            }
            out.push_back({event.type, static_cast<int>(event.code), event.value});
        }
        return out;
    }

    /// Events captured by the `on` mod downstream of `autocomplete`.
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)

    /// Convert captured events to plain user_events for assertions.
    [[nodiscard]] std::vector<fs8::user_event> to_user_events(std::vector<fs8::event_type> const &events) {
        std::vector<fs8::user_event> out;
        out.reserve(events.size());
        for (auto const &event : events) {
            out.push_back(static_cast<fs8::user_event>(event));
        }
        return out;
    }
} // namespace

TEST(AutocompleteTest, TriggerSwallowsTriggerKey) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    // typing "test@" (Shift+2 on us) then pressing Tab fires "gmail.com", and Tab is swallowed.
    (context
     | emit_all[{
       {.type = EV_KEY,         .code = KEY_T, .value = 1},
       {.type = EV_KEY,         .code = KEY_E, .value = 1},
       {.type = EV_KEY,         .code = KEY_S, .value = 1},
       {.type = EV_KEY,         .code = KEY_T, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1},
       {.type = EV_KEY,         .code = KEY_2, .value = 1},
       {.type = EV_KEY,       .code = KEY_TAB, .value = 1},
    }]
     | autocomplete["test@<tab>gmail.com"]
     | record[captured_events])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        {EV_KEY,         KEY_T, 1},
        {EV_KEY,         KEY_E, 1},
        {EV_KEY,         KEY_S, 1},
        {EV_KEY,         KEY_T, 1},
        {EV_KEY, KEY_LEFTSHIFT, 1},
        {EV_KEY,         KEY_2, 1},
        // the completion is emitted after the trigger keypress:
        {EV_KEY,         KEY_G, 1},
        {EV_KEY,         KEY_G, 0},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_M, 0},
        {EV_KEY,         KEY_A, 1},
        {EV_KEY,         KEY_A, 0},
        {EV_KEY,         KEY_I, 1},
        {EV_KEY,         KEY_I, 0},
        {EV_KEY,         KEY_L, 1},
        {EV_KEY,         KEY_L, 0},
        {EV_KEY,       KEY_DOT, 1},
        {EV_KEY,       KEY_DOT, 0},
        {EV_KEY,         KEY_C, 1},
        {EV_KEY,         KEY_C, 0},
        {EV_KEY,         KEY_O, 1},
        {EV_KEY,         KEY_O, 0},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_M, 0},
        // the trigger key itself never reaches the app
    }));
}

TEST(AutocompleteTest, TriggerPassesTriggerKeyThrough) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    (context
     | emit_all[{
       {.type = EV_KEY,         .code = KEY_T, .value = 1},
       {.type = EV_KEY,         .code = KEY_E, .value = 1},
       {.type = EV_KEY,         .code = KEY_S, .value = 1},
       {.type = EV_KEY,         .code = KEY_T, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1},
       {.type = EV_KEY,         .code = KEY_2, .value = 1},
       {.type = EV_KEY,       .code = KEY_TAB, .value = 1},
    }]
     | autocomplete["test@<tab>gmail.com"][pass_trigger]
     | record[captured_events])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        {EV_KEY,         KEY_T, 1},
        {EV_KEY,         KEY_E, 1},
        {EV_KEY,         KEY_S, 1},
        {EV_KEY,         KEY_T, 1},
        {EV_KEY, KEY_LEFTSHIFT, 1},
        {EV_KEY,         KEY_2, 1},
        {EV_KEY,         KEY_G, 1},
        {EV_KEY,         KEY_G, 0},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_M, 0},
        {EV_KEY,         KEY_A, 1},
        {EV_KEY,         KEY_A, 0},
        {EV_KEY,         KEY_I, 1},
        {EV_KEY,         KEY_I, 0},
        {EV_KEY,         KEY_L, 1},
        {EV_KEY,         KEY_L, 0},
        {EV_KEY,       KEY_DOT, 1},
        {EV_KEY,       KEY_DOT, 0},
        {EV_KEY,         KEY_C, 1},
        {EV_KEY,         KEY_C, 0},
        {EV_KEY,         KEY_O, 1},
        {EV_KEY,         KEY_O, 0},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_M, 0},
        // the trigger key is passed through:
        {EV_KEY,       KEY_TAB, 1},
    }));
}

TEST(AutocompleteTest, AutoModeCompletesOnPrefix) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    // typing "example@" (Shift+2 on us) completes to "example@email.com" automatically.
    (context
     | emit_all[{
       {.type = EV_KEY,         .code = KEY_E, .value = 1},
       {.type = EV_KEY,         .code = KEY_X, .value = 1},
       {.type = EV_KEY,         .code = KEY_A, .value = 1},
       {.type = EV_KEY,         .code = KEY_M, .value = 1},
       {.type = EV_KEY,         .code = KEY_P, .value = 1},
       {.type = EV_KEY,         .code = KEY_L, .value = 1},
       {.type = EV_KEY,         .code = KEY_E, .value = 1},
       {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1},
       {.type = EV_KEY,         .code = KEY_2, .value = 1},
    }]
     | autocomplete["example@<tab>email.com"][auto_mode]
     | record[captured_events])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(
      keys,
      (std::vector<std::array<int, 3>>{
        {EV_KEY,         KEY_E, 1},
        {EV_KEY,         KEY_X, 1},
        {EV_KEY,         KEY_A, 1},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_P, 1},
        {EV_KEY,         KEY_L, 1},
        {EV_KEY,         KEY_E, 1},
        {EV_KEY, KEY_LEFTSHIFT, 1},
        // the completion forks right after the prefix is complete (@ = Shift+2):
        {EV_KEY,         KEY_E, 1},
        {EV_KEY,         KEY_E, 0},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_M, 0},
        {EV_KEY,         KEY_A, 1},
        {EV_KEY,         KEY_A, 0},
        {EV_KEY,         KEY_I, 1},
        {EV_KEY,         KEY_I, 0},
        {EV_KEY,         KEY_L, 1},
        {EV_KEY,         KEY_L, 0},
        {EV_KEY,       KEY_DOT, 1},
        {EV_KEY,       KEY_DOT, 0},
        {EV_KEY,         KEY_C, 1},
        {EV_KEY,         KEY_C, 0},
        {EV_KEY,         KEY_O, 1},
        {EV_KEY,         KEY_O, 0},
        {EV_KEY,         KEY_M, 1},
        {EV_KEY,         KEY_M, 0},
        // then the key that produced '@' itself reaches the app:
        {EV_KEY,         KEY_2, 1},
    }));
}

TEST(AutocompleteTest, NoTriggerTagIsANoop) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    captured_events.clear();
    // a pattern without a trigger tag is not a valid autocomplete; it does nothing.
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_H, .value = 1},
       {.type = EV_KEY, .code = KEY_E, .value = 1},
       {.type = EV_KEY, .code = KEY_L, .value = 1},
       {.type = EV_KEY, .code = KEY_L, .value = 1},
       {.type = EV_KEY, .code = KEY_O, .value = 1},
    }]
     | autocomplete["hello"]
     | record[captured_events])();

    auto const keys = key_events(to_user_events(captured_events));
    EXPECT_EQ(keys,
              (std::vector<std::array<int, 3>>{
                {EV_KEY, KEY_H, 1},
                {EV_KEY, KEY_E, 1},
                {EV_KEY, KEY_L, 1},
                {EV_KEY, KEY_L, 1},
                {EV_KEY, KEY_O, 1},
    }));
}
