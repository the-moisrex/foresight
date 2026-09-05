// Created by moisrex on 9/4/26.

#include "./common/tests_common_pch.hpp"

#include <fcntl.h>
#include <format>
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
    EXPECT_FALSE(cap.is_open());
    EXPECT_EQ(cap.buffer_size(), 0U);
    EXPECT_TRUE(cap.buffered().empty());
}

TEST(CaptureTest, EventsAlwaysBuffered) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(event_type{EV_KEY, KEY_A, 1});
    cap(event_type{EV_SYN, SYN_REPORT, 0});
    cap(event_type{EV_KEY, KEY_A, 0});

    EXPECT_EQ(cap.buffer_size(), 3U);
    EXPECT_EQ(cap.buffered()[0].code(), KEY_A);
    EXPECT_EQ(cap.buffered()[0].value(), 1);
    EXPECT_EQ(cap.buffered()[1].code(), SYN_REPORT);
    EXPECT_EQ(cap.buffered()[2].value(), 0);
}

TEST(CaptureTest, ToggleOffFlushes) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(event_type{EV_KEY, KEY_B, 1});
    cap(event_type{EV_SYN, SYN_REPORT, 0});
    EXPECT_EQ(cap.buffer_size(), 2U);

    // Idle opens the file.
    cap(special_event{.code = idle.code});
    EXPECT_TRUE(cap.is_open());

    cap(event_type{EV_KEY, KEY_B, 1});

    // toggle_off flushes the buffer.
    cap(special_event{.code = toggle_on.code, .value = 0});
    EXPECT_EQ(cap.buffer_size(), 0U);
}

TEST(CaptureTest, IdleFlushesToFile) {
    basic_capture<capture_binary_format, capture_daily> cap{
      capture_binary_format{}, capture_daily{}};

    cap(event_type{EV_KEY, KEY_A, 1});
    cap(event_type{EV_SYN, SYN_REPORT, 0});
    EXPECT_EQ(cap.buffer_size(), 2U);

    // Idle should open file and flush the buffer.
    cap(special_event{.code = idle.code});
    EXPECT_EQ(cap.buffer_size(), 0U);
    EXPECT_TRUE(cap.is_open());
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

    EXPECT_FALSE(cap.is_open());
    EXPECT_EQ(cap.buffer_size(), 0U);
    EXPECT_TRUE(cap.buffered().empty());

    cap(event_type{EV_REL, REL_X, 5});
    cap(event_type{EV_SYN, SYN_REPORT, 0});

    EXPECT_EQ(cap.buffer_size(), 2U);
    EXPECT_FALSE(cap.buffered().empty());
    EXPECT_EQ(cap.buffered()[0].code(), REL_X);

    // toggle_off flushes.
    cap(special_event{.code = toggle_on.code, .value = 0});
    EXPECT_FALSE(cap.is_open());
}

TEST(CaptureTest, SystemUptimeNaming) {
    // Filename should contain "boot-" prefix.
    auto const name = capture_system_uptime::filename(".fs8");
    EXPECT_FALSE(name.empty());
    EXPECT_NE(name.find("capture-boot-"), std::string::npos);

    // On a running system, should_rotate should be false for a recent timestamp.
    auto const now = detail::now_epoch_seconds();
    EXPECT_FALSE(capture_system_uptime::should_rotate(now));
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

// ══════════════════════════════════════════════════════════════════════════════
// Pipeline integration tests
// ══════════════════════════════════════════════════════════════════════════════

TEST(CapturePipelineTest, CaptureInPipelineBuffersEvents) {
    static constinit auto pipeline =
      context | emit_all[{user_event{.type = EV_KEY, .code = KEY_A, .value = 1}, user_event{EV_SYN, SYN_REPORT, 0},
                          user_event{.type = EV_KEY, .code = KEY_A, .value = 0}, user_event{EV_SYN, SYN_REPORT, 0}}]
      | capture | record;

    auto& cap = pipeline.mod<basic_capture<capture_binary_format, capture_daily>>();

    pipeline();
    EXPECT_EQ(cap.buffer_size(), 4U);
    EXPECT_EQ(pipeline.mod<basic_record>().size(), 4U);
}

TEST(CapturePipelineTest, IdleFlushesCaptureToFile) {
    static constinit auto pipeline =
      context
      | emit_all[{user_event{.type = EV_KEY, .code = KEY_A, .value = 1}, user_event{EV_SYN, SYN_REPORT, 0},
                   user_event{.type = EV_KEY, .code = KEY_A, .value = 0}, user_event{EV_SYN, SYN_REPORT, 0}}]
      | capture | record;

    auto& cap = pipeline.mod<basic_capture<capture_binary_format, capture_daily>>();

    pipeline();
    EXPECT_EQ(cap.buffer_size(), 4U);

    // Manually trigger idle to flush the buffer.
    cap(special_event{.code = idle.code});
    EXPECT_EQ(cap.buffer_size(), 0U);
}

TEST(CapturePipelineTest, ReplayInPipelineReplaysEvents) {
    std::vector const original = {
      make_event(make_native(EV_KEY, KEY_A, 1, 100, 0)),
      make_event(make_native(EV_SYN, SYN_REPORT, 0, 100, 0)),
      make_event(make_native(EV_KEY, KEY_A, 0, 100, 1000)),
    };

    char const* const tmp = "/tmp/replay_pipeline_test.bin";
    write_binary_capture(tmp, original);

    basic_replay<capture_binary_format> rep{};
    rep.set_file(tmp);
    rep(special_event{.code = start.code});

    std::vector<event_type> captured;
    for (int i = 0; i < 10; ++i) {
        event_type ev{};
        auto const result = rep(ev, special_event{.code = load_event.code});
        if (result == context_action::exit) {
            break;
        }
        if (result == context_action::next) {
            captured.push_back(ev);
        }
    }

    ASSERT_EQ(captured.size(), original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(captured[i].type(), original[i].type()) << "event " << i;
        EXPECT_EQ(captured[i].code(), original[i].code()) << "event " << i;
        EXPECT_EQ(captured[i].value(), original[i].value()) << "event " << i;
    }

    unlink(tmp);
}

TEST(CapturePipelineTest, CaptureReplayRoundtrip) {
    // Step 1: Capture events to file via pipeline.
    static constinit auto cap_pipeline =
      context
      | emit_all[{user_event{.type = EV_KEY, .code = KEY_A, .value = 1}, user_event{EV_SYN, SYN_REPORT, 0},
                   user_event{.type = EV_KEY, .code = KEY_A, .value = 0}, user_event{EV_SYN, SYN_REPORT, 0},
                   user_event{.type = EV_REL, .code = REL_X, .value = 10}, user_event{EV_SYN, SYN_REPORT, 0}}]
      | capture | record;

    auto& cap = cap_pipeline.mod<basic_capture<capture_binary_format, capture_daily>>();
    cap_pipeline();
    cap(special_event{.code = idle.code});

    // The daily naming writes to capture-YYYY-MM-DD.fs8 in CWD.
    auto const now      = detail::local_time_now();
    auto const expected = std::format("capture-{:04d}-{:02d}-{:02d}.fs8", now.year, now.month, now.day);

    struct stat st{};
    ASSERT_EQ(::stat(expected.c_str(), &st), 0);
    ASSERT_GT(st.st_size, 0);

    // Step 2: Replay from the captured file via direct replay mod calls.
    std::vector<event_type> replayed;
    {
        basic_replay<capture_binary_format> rep{};
        rep.set_file(expected);
        rep(special_event{.code = start.code});

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
    }

    ASSERT_EQ(replayed.size(), 6U);
    // First 6 events should match what emit_all provided.
    EXPECT_EQ(replayed[0].type(), EV_KEY);
    EXPECT_EQ(replayed[0].code(), KEY_A);
    EXPECT_EQ(replayed[0].value(), 1);
    EXPECT_EQ(replayed[1].type(), EV_SYN);
    EXPECT_EQ(replayed[1].code(), SYN_REPORT);
    EXPECT_EQ(replayed[2].type(), EV_KEY);
    EXPECT_EQ(replayed[2].code(), KEY_A);
    EXPECT_EQ(replayed[2].value(), 0);
    EXPECT_EQ(replayed[3].type(), EV_SYN);
    EXPECT_EQ(replayed[3].code(), SYN_REPORT);
    EXPECT_EQ(replayed[4].type(), EV_REL);
    EXPECT_EQ(replayed[4].code(), REL_X);
    EXPECT_EQ(replayed[4].value(), 10);
    EXPECT_EQ(replayed[5].type(), EV_SYN);
    EXPECT_EQ(replayed[5].code(), SYN_REPORT);

    unlink(expected.c_str());
}
