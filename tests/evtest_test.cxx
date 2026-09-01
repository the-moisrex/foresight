#include "./common/tests_common_pch.hpp"

#include <charconv>
#include <linux/input-event-codes.h>
#include <span>

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

// ── evtest_output tests ─────────────────────────────────────────────────────

TEST(EvtestOutput, FormatKeyEvent) {
    fs8::default_evtest_format fmt;
    // Ctrl press: type=1 (EV_KEY), code=29 (KEY_LEFTCTRL), value=1
    event_type event{EV_KEY, KEY_LEFTCTRL, 1};
    event.native().time = {.tv_sec = 1'787'439'635, .tv_usec = 318'229};

    char       buf[fs8::evtest_format_buf_size];
    auto const text = fmt.format(event, buf);
    EXPECT_FALSE(text.empty());
    // format() includes a trailing newline for the line-based reader.
    EXPECT_TRUE(text.ends_with('\n'));
    EXPECT_NE(text.find("Event: time 1787439635.318229"), std::string_view::npos);
    EXPECT_NE(text.find("type 1"), std::string_view::npos);
    EXPECT_NE(text.find("code 29"), std::string_view::npos);
    EXPECT_NE(text.find("KEY_LEFTCTRL"), std::string_view::npos);
    EXPECT_NE(text.find("value 1"), std::string_view::npos);
}

TEST(EvtestOutput, FormatSynReport) {
    fs8::default_evtest_format fmt;
    event_type                 event{EV_SYN, SYN_REPORT, 0};
    event.native().time = {.tv_sec = 1'787'439'635, .tv_usec = 318'229};

    char       buf[fs8::evtest_format_buf_size];
    auto const text = fmt.format(event, buf);
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("SYN_REPORT"), std::string_view::npos);
}

TEST(EvtestOutput, FormatNegativeValue) {
    fs8::default_evtest_format fmt;
    event_type                 event{EV_REL, REL_X, -5};

    char       buf[fs8::evtest_format_buf_size];
    auto const text = fmt.format(event, buf);
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("value -5"), std::string_view::npos);
}

// ── from_evtest tests ───────────────────────────────────────────────────────

namespace {
    /// Write `data` to a pipe and return the read fd.
    int make_pipe_reader(std::string_view const data) {
        int fds[2];
        EXPECT_EQ(pipe(fds), 0);
        auto const n = write(fds[1], data.data(), data.size());
        EXPECT_EQ(n, static_cast<ssize_t>(data.size()));
        close(fds[1]);
        return fds[0];
    }
} // namespace

TEST(FromEvtest, ReadsEventLines) {
    constexpr std::string_view input =
      "Event: time 1.000000, type 1 (EV_KEY), code 30 (KEY_A), value 1\n"
      "Event: time 1.001000, type 1 (EV_KEY), code 30 (KEY_A), value 0\n";

    int const              fd = make_pipe_reader(input);
    fs8::basic_from_evtest reader{fd};

    event_type event;
    auto const action1 = reader(event, fs8::load_event);
    EXPECT_EQ(action1, fs8::context_action::next);
    EXPECT_EQ(event.type(), EV_KEY);
    EXPECT_EQ(event.code(), KEY_A);
    EXPECT_EQ(event.value(), 1);
    EXPECT_EQ(event.source(), fs8::sid(fs8::from_input));

    auto const action2 = reader(event, fs8::load_event);
    EXPECT_EQ(action2, fs8::context_action::next);
    EXPECT_EQ(event.value(), 0);

    auto const action3 = reader(event, fs8::load_event);
    EXPECT_EQ(action3, fs8::context_action::exit);

    close(fd);
}

TEST(FromEvtest, SkipsHeadersAndJunk) {
    constexpr std::string_view input =
      "No device specified, trying to scan all of /dev/input/event*\n"
      "Not running as root, no devices may be available.\n"
      "Available devices:\n"
      "/dev/input/event0:	Sleep Button\n"
      "Select the device event number [0-28]: 27\n"
      "Input driver version is 1.0.1\n"
      "Input device ID: bus 0x6 vendor 0x1c4f product 0x2 version 0x110\n"
      "Input device name: \"USB USB Keykoard (Virtual)\"\n"
      "Supported events:\n"
      "  Event type 0 (EV_SYN)\n"
      "Event: time 2.000000, type 1 (EV_KEY), code 28 (KEY_ENTER), value 1\n"
      "Event: time 2.001000, type 1 (EV_KEY), code 28 (KEY_ENTER), value 0\n";

    int const              fd = make_pipe_reader(input);
    fs8::basic_from_evtest reader{fd};

    event_type event;
    auto const action1 = reader(event, fs8::load_event);
    EXPECT_EQ(action1, fs8::context_action::next);
    EXPECT_EQ(event.type(), EV_KEY);
    EXPECT_EQ(event.code(), KEY_ENTER);
    EXPECT_EQ(event.value(), 1);

    auto const action2 = reader(event, fs8::load_event);
    EXPECT_EQ(action2, fs8::context_action::next);
    EXPECT_EQ(event.value(), 0);

    close(fd);
}

TEST(FromEvtest, SkipsSynReport) {
    constexpr std::string_view input =
      "Event: time 1.000000, type 1 (EV_KEY), code 30 (KEY_A), value 1\n"
      "Event: time 1.000000, -------------- SYN_REPORT ------------\n"
      "Event: time 1.001000, type 1 (EV_KEY), code 30 (KEY_A), value 0\n"
      "Event: time 1.001000, -------------- SYN_REPORT ------------\n";

    int const              fd = make_pipe_reader(input);
    fs8::basic_from_evtest reader{fd};

    event_type event;
    // First event
    auto const a1 = reader(event, fs8::load_event);
    EXPECT_EQ(a1, fs8::context_action::next);
    EXPECT_EQ(event.code(), KEY_A);
    EXPECT_EQ(event.value(), 1);

    // SYN_REPORT is skipped, so next read gets the release event
    auto const a2 = reader(event, fs8::load_event);
    EXPECT_EQ(a2, fs8::context_action::next);
    EXPECT_EQ(event.value(), 0);

    // Then EOF
    auto const a3 = reader(event, fs8::load_event);
    EXPECT_EQ(a3, fs8::context_action::exit);

    close(fd);
}

// ── Round-trip: output → input ──────────────────────────────────────────────

TEST(Evtest, ModRoundTrip) {
    // Write events through evtest_output to a pipe, read them back with from_evtest.
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    fs8::basic_evtest_output writer{pipe_fds[1]};
    fs8::basic_from_evtest   reader{pipe_fds[0]};

    // Write a key press and release
    event_type press{EV_KEY, KEY_A, 1};
    event_type release{EV_KEY, KEY_A, 0};

    EXPECT_TRUE(writer(press));
    EXPECT_TRUE(writer(release));
    close(pipe_fds[1]); // close write end so reader gets EOF

    event_type event;
    auto const a1 = reader(event, fs8::load_event);
    EXPECT_EQ(a1, fs8::context_action::next);
    EXPECT_EQ(event.type(), EV_KEY);
    EXPECT_EQ(event.code(), KEY_A);
    EXPECT_EQ(event.value(), 1);

    auto const a2 = reader(event, fs8::load_event);
    EXPECT_EQ(a2, fs8::context_action::next);
    EXPECT_EQ(event.value(), 0);

    auto const a3 = reader(event, fs8::load_event);
    EXPECT_EQ(a3, fs8::context_action::exit);

    close(pipe_fds[0]);
}

// ── Custom format ───────────────────────────────────────────────────────────

struct minimal_evtest_format {
    [[nodiscard]] bool parse(std::string_view line, parsed_evtest_event& out) const noexcept {
        // Minimal format: "type T code C value V\n"
        // Find "type " prefix.
        auto const type_pos = line.find("type ");
        if (type_pos == std::string_view::npos) {
            return false;
        }
        auto rest = line.substr(type_pos + 5);

        // Parse type number.
        std::uint16_t type_val = 0;
        auto [p1, ec1]         = std::from_chars(rest.data(), rest.data() + rest.size(), type_val);
        if (ec1 != std::errc{}) {
            return false;
        }
        rest.remove_prefix(static_cast<std::size_t>(p1 - rest.data()));

        // Skip " code ".
        if (!rest.starts_with(" code ")) {
            return false;
        }
        rest.remove_prefix(6);

        // Parse code number.
        std::uint16_t code_val = 0;
        auto [p2, ec2]         = std::from_chars(rest.data(), rest.data() + rest.size(), code_val);
        if (ec2 != std::errc{}) {
            return false;
        }
        rest.remove_prefix(static_cast<std::size_t>(p2 - rest.data()));

        // Skip " value ".
        if (!rest.starts_with(" value ")) {
            return false;
        }
        rest.remove_prefix(7);

        // Parse value number.
        std::int32_t val = 0;
        auto [p3, ec3]   = std::from_chars(rest.data(), rest.data() + rest.size(), val);
        if (ec3 != std::errc{}) {
            return false;
        }

        out.event = fs8::user_event{.type = type_val, .code = code_val, .value = val};
        return true;
    }

    [[nodiscard]] std::string_view format(event_type const& event, std::span<char> buf) const noexcept {
        // Minimal: "type code value\n"
        auto*      pos       = buf.data();
        auto const write_str = [&](std::string_view const s) {
            for (auto const c : s) {
                *pos++ = c;
            }
        };
        auto const write_int = [&](auto const val) {
            auto const [ptr, ec] = std::to_chars(pos, buf.data() + buf.size(), val);
            pos                  = ptr;
        };

        write_str("type ");
        write_int(event.type());
        write_str(" code ");
        write_int(event.code());
        write_str(" value ");
        write_int(event.value());
        write_str("\n");
        return std::string_view{buf.data(), static_cast<std::size_t>(pos - buf.data())};
    }
};

static_assert(fs8::EvtestFormat<minimal_evtest_format>);

TEST(Evtest, CustomFormat) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    fs8::basic_evtest_output<minimal_evtest_format> writer{pipe_fds[1]};
    fs8::basic_from_evtest<minimal_evtest_format>   reader{pipe_fds[0]};

    event_type press{EV_KEY, KEY_SPACE, 1};
    EXPECT_TRUE(writer(press));
    close(pipe_fds[1]);

    event_type event;
    auto const a1 = reader(event, fs8::load_event);
    EXPECT_EQ(a1, fs8::context_action::next);
    EXPECT_EQ(event.type(), EV_KEY);
    EXPECT_EQ(event.code(), KEY_SPACE);
    EXPECT_EQ(event.value(), 1);

    auto const a2 = reader(event, fs8::load_event);
    EXPECT_EQ(a2, fs8::context_action::exit);

    close(pipe_fds[0]);
}
