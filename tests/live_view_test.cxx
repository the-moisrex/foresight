#include "./common/tests_common_pch.hpp"

#include <charconv>
#include <linux/input-event-codes.h>
#include <span>

import fs8.event;
import fs8.lib.evtest;
import fs8.lib.xkb;
import fs8.mods;

using fs8::event_type;
using fs8::parsed_evtest_event;
using fs8::user_event;

// ── live_view_format tests ──────────────────────────────────────────────────

TEST(LiveViewFormat, FormatKeyEvent) {
    fs8::live_view_format fmt;
    event_type event{EV_KEY, KEY_A, 1};
    event.native().time = {.tv_sec = 1'787'439'635, .tv_usec = 318'229};

    char       buf[fs8::live_view_format_buf_size];
    auto const text = fmt.format(event, buf);
    EXPECT_FALSE(text.empty());
    EXPECT_TRUE(text.ends_with('\n'));
    EXPECT_NE(text.find("Event: time 1787439635.318229"), std::string_view::npos);
    EXPECT_NE(text.find("type 1"), std::string_view::npos);
    EXPECT_NE(text.find("code 30"), std::string_view::npos);
    EXPECT_NE(text.find("KEY_A"), std::string_view::npos);
    EXPECT_NE(text.find("value 1"), std::string_view::npos);
}

TEST(LiveViewFormat, FormatSynReport) {
    fs8::live_view_format fmt;
    event_type event{EV_SYN, SYN_REPORT, 0};
    event.native().time = {.tv_sec = 1'787'439'635, .tv_usec = 318'229};

    char       buf[fs8::live_view_format_buf_size];
    auto const text = fmt.format(event, buf);
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("SYN_REPORT"), std::string_view::npos);
}

TEST(LiveViewFormat, FormatNegativeValue) {
    fs8::live_view_format fmt;
    event_type event{EV_REL, REL_X, -5};

    char       buf[fs8::live_view_format_buf_size];
    auto const text = fmt.format(event, buf);
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("value -5"), std::string_view::npos);
}

TEST(LiveViewFormat, ParseKeyEvent) {
    fs8::live_view_format fmt;
    constexpr std::string_view line = "Event: time 1787439635.318229, type 1 (EV_KEY), code 30 (KEY_A), value 1";
    parsed_evtest_event out;
    EXPECT_TRUE(fmt.parse(line, out));
    EXPECT_EQ(out.event, (user_event{.type = EV_KEY, .code = KEY_A, .value = 1}));
    EXPECT_DOUBLE_EQ(out.time, 1787439635.318229);
}

TEST(LiveViewFormat, ParseSynReport) {
    fs8::live_view_format fmt;
    parsed_evtest_event out;
    EXPECT_FALSE(fmt.parse("Event: time 0.000000, -------------- SYN_REPORT ------------", out));
}

TEST(LiveViewFormat, ParseNegativeValue) {
    fs8::live_view_format fmt;
    constexpr std::string_view line = "Event: time 0.000000, type 2 (EV_REL), code 0 (REL_X), value -5";
    parsed_evtest_event out;
    EXPECT_TRUE(fmt.parse(line, out));
    EXPECT_EQ(out.event, (user_event{.type = EV_REL, .code = REL_X, .value = -5}));
}

TEST(LiveViewFormat, ParseJunkLines) {
    fs8::live_view_format fmt;
    parsed_evtest_event out;
    EXPECT_FALSE(fmt.parse("", out));
    EXPECT_FALSE(fmt.parse("some random garbage", out));
    EXPECT_FALSE(fmt.parse("Input device name: \"Logitech\"", out));
}

// ── Round-trip tests ────────────────────────────────────────────────────────

TEST(LiveView, RoundTripKeyEvent) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    fs8::basic_live_view_output<fs8::live_view_format> writer{pipe_fds[1]};
    fs8::basic_from_live_view<fs8::live_view_format>   reader{pipe_fds[0]};

    event_type press{EV_KEY, KEY_A, 1};
    press.native().time = {.tv_sec = 100, .tv_usec = 500'000};
    EXPECT_TRUE(writer(press));

    event_type release{EV_KEY, KEY_A, 0};
    release.native().time = {.tv_sec = 100, .tv_usec = 650'000};
    EXPECT_TRUE(writer(release));
    close(pipe_fds[1]);

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

TEST(LiveView, RoundTripSynReport) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    fs8::basic_live_view_output<fs8::live_view_format> writer{pipe_fds[1]};
    fs8::basic_from_live_view<fs8::live_view_format>   reader{pipe_fds[0]};

    event_type syn{EV_SYN, SYN_REPORT, 0};
    EXPECT_TRUE(writer(syn));
    close(pipe_fds[1]);

    event_type event;
    // SYN_REPORT lines are skipped by the reader (same as evtest)
    auto const a1 = reader(event, fs8::load_event);
    EXPECT_EQ(a1, fs8::context_action::exit);

    close(pipe_fds[0]);
}

TEST(LiveView, RoundTripMultipleKeys) {
    // Simulate: press A, press B, release A, release B
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    fs8::basic_live_view_output<fs8::live_view_format> writer{pipe_fds[1]};
    fs8::basic_from_live_view<fs8::live_view_format>   reader{pipe_fds[0]};

    event_type press_a{EV_KEY, KEY_A, 1};
    press_a.native().time = {.tv_sec = 100, .tv_usec = 0};
    EXPECT_TRUE(writer(press_a));

    event_type press_b{EV_KEY, KEY_B, 1};
    press_b.native().time = {.tv_sec = 100, .tv_usec = 10'000};
    EXPECT_TRUE(writer(press_b));

    event_type release_a{EV_KEY, KEY_A, 0};
    release_a.native().time = {.tv_sec = 100, .tv_usec = 150'000};
    EXPECT_TRUE(writer(release_a));

    event_type release_b{EV_KEY, KEY_B, 0};
    release_b.native().time = {.tv_sec = 100, .tv_usec = 200'000};
    EXPECT_TRUE(writer(release_b));

    close(pipe_fds[1]);

    event_type event;

    auto const a1 = reader(event, fs8::load_event);
    EXPECT_EQ(a1, fs8::context_action::next);
    EXPECT_EQ(event.code(), KEY_A);
    EXPECT_EQ(event.value(), 1);

    auto const a2 = reader(event, fs8::load_event);
    EXPECT_EQ(a2, fs8::context_action::next);
    EXPECT_EQ(event.code(), KEY_B);
    EXPECT_EQ(event.value(), 1);

    auto const a3 = reader(event, fs8::load_event);
    EXPECT_EQ(a3, fs8::context_action::next);
    EXPECT_EQ(event.code(), KEY_A);
    EXPECT_EQ(event.value(), 0);

    auto const a4 = reader(event, fs8::load_event);
    EXPECT_EQ(a4, fs8::context_action::next);
    EXPECT_EQ(event.code(), KEY_B);
    EXPECT_EQ(event.value(), 0);

    auto const a5 = reader(event, fs8::load_event);
    EXPECT_EQ(a5, fs8::context_action::exit);

    close(pipe_fds[0]);
}

// ── live_view state tests ──────────────────────────────────────────────────

TEST(LiveView, MouseAccumulation) {
    fs8::live_view lv{false}; // non-terminal
    lv.set_ansi(false);

    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    // Feed several mouse events
    for (int i = 0; i < 5; ++i) {
        event_type ev{EV_REL, REL_X, 10};
        ev.source(fs8::hashed_device("event5"));
        lv.process_event(ev, pipe_fds[1]);
    }

    // Check accumulated state
    auto& st = lv.state_for(fs8::hashed_device("event5"));
    EXPECT_EQ(st.mouse.delta_x, 50);
    EXPECT_EQ(st.mouse.delta_y, 0);
    EXPECT_EQ(st.mouse.event_count, 5u);

    // Flush
    lv.flush(pipe_fds[1]);
    close(pipe_fds[1]);

    // After flush, state should be reset
    EXPECT_EQ(st.mouse.event_count, 0u);
    EXPECT_EQ(st.mouse.delta_x, 0);
}

TEST(LiveView, DirectionChangeFlushes) {
    fs8::live_view lv{false};
    lv.set_ansi(false);

    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    auto const dev = fs8::hashed_device("event5");

    // Move right
    event_type ev1{EV_REL, REL_X, 10};
    ev1.source(dev);
    lv.process_event(ev1, pipe_fds[1]);

    // Move up (direction change)
    event_type ev2{EV_REL, REL_Y, -10};
    ev2.source(dev);
    lv.process_event(ev2, pipe_fds[1]);

    auto& st = lv.state_for(dev);
    // After direction change, the first accumulation should have been flushed
    // and a new one started
    EXPECT_EQ(st.mouse.delta_y, -10);

    close(pipe_fds[1]);
}

TEST(LiveView, KeyTracking) {
    fs8::live_view lv{false};
    lv.set_ansi(false);

    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);

    auto const dev = fs8::hashed_device("event5");

    // Press A
    event_type press{EV_KEY, KEY_A, 1};
    press.source(dev);
    lv.process_event(press, pipe_fds[1]);

    auto& st = lv.state_for(dev);
    EXPECT_EQ(st.held_keys.size(), 1u);
    EXPECT_EQ(st.held_keys.count(KEY_A), 1u);

    // Release A
    event_type release{EV_KEY, KEY_A, 0};
    release.source(dev);
    lv.process_event(release, pipe_fds[1]);

    EXPECT_EQ(st.held_keys.size(), 0u);

    close(pipe_fds[1]);
}
