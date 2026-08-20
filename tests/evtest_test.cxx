#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.event;
import fs8.lib.evtest;
import fs8.lib.xkb;
import fs8.lib.xkb.how2type;
import fs8.mods;

using fs8::aho_state;
using fs8::basic_search_engine;
using fs8::event_type;
using fs8::parsed_evtest_event;
using fs8::user_event;
using fs8::xkb::basic_state;
namespace h2t = fs8::xkb::how2type;

TEST(Evtest, ParseEventLine) {
    parsed_evtest_event out;
    EXPECT_TRUE(fs8::parse_evtest_line("Event: time 1634220472.123456, type 1 (EV_KEY), code 29 (KEY_LEFTCTRL), value 1", out));
    EXPECT_EQ(out.event, (user_event{.type = EV_KEY, .code = KEY_LEFTCTRL, .value = 1}));
    EXPECT_DOUBLE_EQ(out.time, 1634220472.123456);
}

TEST(Evtest, ParseNegativeValue) {
    parsed_evtest_event out;
    EXPECT_TRUE(fs8::parse_evtest_line("Event: time 0.000000, type 2 (EV_REL), code 0 (REL_X), value -1", out));
    EXPECT_EQ(out.event, (user_event{.type = EV_REL, .code = REL_X, .value = -1}));
}

TEST(Evtest, ParseMscValue) {
    parsed_evtest_event out;
    EXPECT_TRUE(fs8::parse_evtest_line("Event: time 0.000000, type 4 (EV_MSC), code 4 (MSC_SCAN), value 458976", out));
    EXPECT_EQ(out.event, (user_event{.type = EV_MSC, .code = MSC_SCAN, .value = 458'976}));
}

TEST(Evtest, ParseSynReportLine) {
    parsed_evtest_event out;
    EXPECT_FALSE(fs8::parse_evtest_line("Event: time 0.000000, -------------- SYN_REPORT ------------", out));
}

TEST(Evtest, ParseHeaderLines) {
    parsed_evtest_event out;
    EXPECT_FALSE(fs8::parse_evtest_line("Input device ID: bus 0x3 (0x3) vendor 0x46d (0x46d) version 0x111", out));
    EXPECT_FALSE(fs8::parse_evtest_line("Input device name: \"Logitech\"", out));
    EXPECT_FALSE(fs8::parse_evtest_line("Properties:", out));
    EXPECT_FALSE(fs8::parse_evtest_line("Testing events (interrupt to exit):", out));
    EXPECT_FALSE(fs8::parse_evtest_line("", out));
    EXPECT_FALSE(fs8::parse_evtest_line("some random garbage", out));
}

/// Feed the evtest-formatted lines through the same search engine the CLI
/// uses, mirroring `foresight matches`.
bool round_trip_matches(std::string_view pattern, std::string const& stream) {
    basic_search_engine engine;
    auto const          trigger_id = engine.emplace_pattern(pattern);
    aho_state           state{0u};
    basic_state         keyboard_state;
    keyboard_state.initialize(fs8::xkb::get_default_keymap());

    std::istringstream iss{stream};
    std::string        line;
    while (std::getline(iss, line)) {
        parsed_evtest_event parsed;
        if (!fs8::parse_evtest_line(line, parsed)) {
            continue;
        }
        event_type const event{parsed.event};
        if (engine.search(event, trigger_id, keyboard_state, state)) {
            return true;
        }
    }
    return false;
}

TEST(Evtest, RoundTripKeydown) {
    testing::internal::CaptureStdout();
    h2t::print("<ctrl+shift+left>", h2t::output_syntax::evtest);
    auto const out = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(round_trip_matches("<ctrl+shift+left>", out));
    EXPECT_TRUE(round_trip_matches("<shift+ctrl+left>", out)); // canonical order
}

TEST(Evtest, RoundTripKeyup) {
    testing::internal::CaptureStdout();
    h2t::print("[ctrl+shift+left]", h2t::output_syntax::evtest);
    auto const out = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(round_trip_matches("[ctrl+shift+left]", out));
}

TEST(Evtest, NoMatch) {
    std::string const stream =
      "Event: time 0.000000, type 1 (EV_KEY), code 30 (KEY_A), value 1\n"
      "Event: time 0.000000, type 1 (EV_KEY), code 30 (KEY_A), value 0\n";
    EXPECT_FALSE(round_trip_matches("<ctrl+shift+left>", stream));
}

TEST(Evtest, MatchPlainText) {
    testing::internal::CaptureStdout();
    h2t::print("test", h2t::output_syntax::evtest);
    auto const out = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(round_trip_matches("test", out));
}
