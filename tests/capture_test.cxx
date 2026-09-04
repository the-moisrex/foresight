// Created by moisrex on 9/4/26.

#include "./common/tests_common_pch.hpp"

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/stat.h>
#include <unistd.h>

import fs8.mods;

using namespace fs8;

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    /// Write a binary capture file (magic header + raw input_events).
    void write_binary_capture(std::string const& path, std::vector<event_type> const& events) {
        int const fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        ASSERT_GE(fd, 0);

        struct __attribute__((packed)) {
            std::uint32_t magic;
            std::uint16_t version;
        } constexpr header{0x38534646u, 1};
        ASSERT_EQ(::write(fd, &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));

        for (auto const& ev : events) {
            auto const& native = ev.native();
            ASSERT_EQ(::write(fd, &native, sizeof(input_event)), static_cast<ssize_t>(sizeof(input_event)));
        }
        ::close(fd);
    }

    /// Write an evtest-format capture file.
    void write_evtest_capture(std::string const& path, std::vector<event_type> const& events) {
        int const fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        ASSERT_GE(fd, 0);

        constexpr std::string_view hdr = "# foresight capture evtest\n";
        ASSERT_EQ(::write(fd, hdr.data(), hdr.size()), static_cast<ssize_t>(hdr.size()));

        for (auto const& ev : events) {
            auto const  tv   = ev.native().time;
            auto const  sec  = static_cast<std::int64_t>(tv.tv_sec);
            auto const  usec = static_cast<std::int32_t>(tv.tv_usec);

            std::string line = "Event: time " + std::to_string(sec) + "." + std::to_string(usec) + ", type "
                             + std::to_string(ev.type()) + ", code " + std::to_string(ev.code()) + ", value "
                             + std::to_string(ev.value()) + "\n";
            ASSERT_EQ(::write(fd, line.data(), line.size()), static_cast<ssize_t>(line.size()));
        }
        ::close(fd);
    }

    /// Make an input_event with a specific timestamp.
    input_event make_native(int type, int code, int value, long sec, long usec) {
        input_event ev{};
        ev.type  = static_cast<__u16>(type);
        ev.code  = static_cast<__u16>(code);
        ev.value = value;
        ev.time.tv_sec  = sec;
        ev.time.tv_usec = usec;
        return ev;
    }

    /// Make an event_type from a native input_event.
    event_type make_event(input_event const& native) {
        event_type ev{};
        ev.native() = native;
        return ev;
    }

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Capture tests
// ══════════════════════════════════════════════════════════════════════════════

TEST(CaptureTest, InactiveByDefault) {
    constexpr basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};
    EXPECT_FALSE(cap.is_active());
    EXPECT_EQ(cap.buffer_size(), 0U);
    EXPECT_TRUE(cap.buffered().empty());
}

TEST(CaptureTest, ToggleOnActivates) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    context_action result = cap(special_event{.code = start.code});
    EXPECT_EQ(result, context_action::next);

    result = cap(special_event{.code = toggle_on.code, .value = 1});
    EXPECT_EQ(result, context_action::next);
    EXPECT_TRUE(cap.is_active());
}

TEST(CaptureTest, EventsBufferedWhenActive) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(special_event{.code = start.code});
    cap(special_event{.code = toggle_on.code, .value = 1});

    cap(event_type{EV_KEY, KEY_A, 1});
    cap(event_type{EV_SYN, SYN_REPORT, 0});
    cap(event_type{EV_KEY, KEY_A, 0});

    EXPECT_EQ(cap.buffer_size(), 3U);
    EXPECT_EQ(cap.buffered()[0].code(), KEY_A);
    EXPECT_EQ(cap.buffered()[0].value(), 1);
    EXPECT_EQ(cap.buffered()[1].code(), SYN_REPORT);
    EXPECT_EQ(cap.buffered()[2].value(), 0);
}

TEST(CaptureTest, EventsNotBufferedWhenInactive) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(event_type{EV_KEY, KEY_A, 1});
    EXPECT_EQ(cap.buffer_size(), 0U);
}

TEST(CaptureTest, ToggleOffDeactivates) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(special_event{.code = start.code});
    cap(special_event{.code = toggle_on.code, .value = 1});
    EXPECT_TRUE(cap.is_active());

    cap(special_event{.code = toggle_on.code, .value = 0});
    EXPECT_FALSE(cap.is_active());

    cap(event_type{EV_KEY, KEY_B, 1});
    EXPECT_EQ(cap.buffer_size(), 0U);
}

TEST(CaptureTest, IdleFlushesToFile) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(special_event{.code = start.code});
    cap(special_event{.code = toggle_on.code, .value = 1});

    cap(event_type{EV_KEY, KEY_A, 1});
    cap(event_type{EV_SYN, SYN_REPORT, 0});
    EXPECT_EQ(cap.buffer_size(), 2U);

    // Idle should flush the buffer.
    cap(special_event{.code = idle.code});
    EXPECT_EQ(cap.buffer_size(), 0U);
}

TEST(CaptureTest, BinaryFormatRoundtrip) {
    std::vector const original = {
      make_event(make_native(EV_KEY, KEY_A, 1, 1000, 0)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 1000, 0)),
      make_event(make_native(EV_KEY, KEY_A, 0, 1000, 1000)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 1000, 1000)),
    };

    char const* const tmp = "/tmp/capture_test_rt.bin";
    write_binary_capture(tmp, original);

    basic_replay<capture_binary_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    std::vector<event_type> replayed;
    for (int i = 0; i < 10; ++i) {
        event_type ev{};
        auto const result = rep(ev, special_event{.code = load_event.code});
        if (result == context_action::exit) {
            break;
        }
        if (result == context_action::next) {
            replayed.push_back(ev);
        }
    }

    ASSERT_EQ(replayed.size(), original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(replayed[i].type(), original[i].type()) << "event " << i << " type mismatch";
        EXPECT_EQ(replayed[i].code(), original[i].code()) << "event " << i << " code mismatch";
        EXPECT_EQ(replayed[i].value(), original[i].value()) << "event " << i << " value mismatch";
    }

    unlink(tmp);
}

TEST(CaptureTest, EvtestFormatRoundtrip) {
    std::vector const original = {
      make_event(make_native(EV_KEY, KEY_B, 1, 2000, 500)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 2000, 500)),
      make_event(make_native(EV_KEY, KEY_B, 0, 2000, 600)),
    };

    char const* const tmp = "/tmp/capture_test_rt.txt";
    write_evtest_capture(tmp, original);

    basic_replay<capture_evtest_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    std::vector<event_type> replayed;
    for (int i = 0; i < 10; ++i) {
        event_type ev{};
        auto const result = rep(ev, special_event{.code = load_event.code});
        if (result == context_action::exit) {
            break;
        }
        if (result == context_action::next) {
            replayed.push_back(ev);
        }
    }

    ASSERT_EQ(replayed.size(), original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(replayed[i].type(), original[i].type()) << "event " << i << " type mismatch";
        EXPECT_EQ(replayed[i].code(), original[i].code()) << "event " << i << " code mismatch";
        EXPECT_EQ(replayed[i].value(), original[i].value()) << "event " << i << " value mismatch";
    }

    unlink(tmp);
}

TEST(CaptureTest, AccessorsReportCorrectState) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    EXPECT_FALSE(cap.is_active());
    EXPECT_EQ(cap.buffer_size(), 0U);
    EXPECT_TRUE(cap.buffered().empty());

    cap(special_event{.code = start.code});
    cap(special_event{.code = toggle_on.code, .value = 1});

    EXPECT_TRUE(cap.is_active());

    cap(event_type{EV_REL, REL_X, 5});
    cap(event_type{EV_SYN, SYN_REPORT, 0});

    EXPECT_EQ(cap.buffer_size(), 2U);
    EXPECT_FALSE(cap.buffered().empty());
    EXPECT_EQ(cap.buffered()[0].code(), REL_X);

    cap(special_event{.code = toggle_on.code, .value = 0});
    EXPECT_FALSE(cap.is_active());
}

// ══════════════════════════════════════════════════════════════════════════════
// Replay tests
// ══════════════════════════════════════════════════════════════════════════════

TEST(ReplayTest, BinaryFormatDetection) {
    std::vector const events = {
      make_event(make_native(EV_KEY, KEY_A, 1, 100, 0)),
    };

    char const* const tmp = "/tmp/replay_test_detect.bin";
    write_binary_capture(tmp, events);

    basic_replay<capture_binary_format> rep{};
    rep.set_file(tmp);

    auto const result = rep(special_event{.code = start.code});
    EXPECT_EQ(result, context_action::next);

    event_type ev{};
    auto const load_result = rep(ev, special_event{.code = load_event.code});
    EXPECT_EQ(load_result, context_action::next);
    EXPECT_EQ(ev.type(), EV_KEY);
    EXPECT_EQ(ev.code(), KEY_A);

    unlink(tmp);
}

TEST(ReplayTest, EvtestFormatDetection) {
    std::vector const events = {
      make_event(make_native(EV_KEY, KEY_B, 1, 200, 0)),
    };

    char const* const tmp = "/tmp/replay_test_detect.txt";
    write_evtest_capture(tmp, events);

    basic_replay<capture_evtest_format> rep{};
    rep.set_file(tmp);

    auto const result = rep(special_event{.code = start.code});
    EXPECT_EQ(result, context_action::next);

    event_type ev{};
    auto const load_result = rep(ev, special_event{.code = load_event.code});
    EXPECT_EQ(load_result, context_action::next);
    EXPECT_EQ(ev.type(), EV_KEY);
    EXPECT_EQ(ev.code(), KEY_B);

    unlink(tmp);
}

TEST(ReplayTest, ReplaysBinaryEvents) {
    std::vector const original = {
      make_event(make_native(EV_KEY, KEY_A, 1, 500, 0)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 500, 0)),
      make_event(make_native(EV_KEY, KEY_A, 0, 500, 1000)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 500, 1000)),
      make_event(make_native(EV_REL, REL_X, 10, 500, 2000)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 500, 2000)),
    };

    char const* const tmp = "/tmp/replay_test_events.bin";
    write_binary_capture(tmp, original);

    basic_replay<capture_binary_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    std::vector<event_type> replayed;
    for (int i = 0; i < 20; ++i) {
        event_type ev{};
        auto const result = rep(ev, special_event{.code = load_event.code});
        if (result == context_action::exit) {
            break;
        }
        if (result == context_action::next) {
            replayed.push_back(ev);
        }
    }

    ASSERT_EQ(replayed.size(), original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(replayed[i].type(), original[i].type()) << "event " << i;
        EXPECT_EQ(replayed[i].code(), original[i].code()) << "event " << i;
        EXPECT_EQ(replayed[i].value(), original[i].value()) << "event " << i;
    }

    unlink(tmp);
}

TEST(ReplayTest, ReplaysEvtestEvents) {
    std::vector const original = {
      make_event(make_native(EV_KEY, KEY_C, 1, 300, 0)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 300, 0)),
      make_event(make_native(EV_KEY, KEY_C, 0, 300, 500)),
    };

    char const* const tmp = "/tmp/replay_test_events.txt";
    write_evtest_capture(tmp, original);

    basic_replay<capture_evtest_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    std::vector<event_type> replayed;
    for (int i = 0; i < 10; ++i) {
        event_type ev{};
        auto const result = rep(ev, special_event{.code = load_event.code});
        if (result == context_action::exit) {
            break;
        }
        if (result == context_action::next) {
            replayed.push_back(ev);
        }
    }

    ASSERT_EQ(replayed.size(), original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(replayed[i].type(), original[i].type()) << "event " << i;
        EXPECT_EQ(replayed[i].code(), original[i].code()) << "event " << i;
        EXPECT_EQ(replayed[i].value(), original[i].value()) << "event " << i;
    }

    unlink(tmp);
}

TEST(ReplayTest, NoFileSetReturnsExit) {
    basic_replay<capture_binary_format> rep{};
    auto const result = rep(special_event{.code = start.code});
    EXPECT_EQ(result, context_action::exit);
}

TEST(ReplayTest, MissingFileReturnsExit) {
    basic_replay<capture_binary_format> rep{};
    rep.set_file("/tmp/nonexistent_replay_file.bin");
    auto const result = rep(special_event{.code = start.code});
    EXPECT_EQ(result, context_action::exit);
}

TEST(ReplayTest, HeaderOnlyFileReturnsExitOnLoad) {
    char const* const tmp = "/tmp/replay_test_short.bin";
    {
        int const fd = ::open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        ASSERT_GE(fd, 0);
        struct __attribute__((packed)) {
            std::uint32_t magic;
            std::uint16_t version;
        } constexpr hdr{0x38534646u, 1};
        ASSERT_EQ(::write(fd, &hdr, sizeof(hdr)), static_cast<ssize_t>(sizeof(hdr)));
        ::close(fd);
    }

    basic_replay<capture_binary_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    event_type ev{};
    auto const result = rep(ev, special_event{.code = load_event.code});
    EXPECT_EQ(result, context_action::exit);

    unlink(tmp);
}

TEST(ReplayTest, NonStartTagReturnsDrop) {
    basic_replay<capture_binary_format> rep{};
    rep.set_file("/tmp/anything.bin");

    auto const result = rep(special_event{.code = toggle_on.code, .value = 1});
    EXPECT_EQ(result, context_action::drop_event);
}

TEST(ReplayTest, NonLoadTagReturnsDrop) {
    std::vector const events = {
      make_event(make_native(EV_KEY, KEY_A, 1, 100, 0)),
    };

    char const* const tmp = "/tmp/replay_test_droptag.bin";
    write_binary_capture(tmp, events);

    basic_replay<capture_binary_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    event_type ev{};
    auto const result = rep(ev, special_event{.code = toggle_on.code, .value = 1});
    EXPECT_EQ(result, context_action::drop_event);

    unlink(tmp);
}
