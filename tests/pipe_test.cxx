// Created for testing the pipe mechanism between intercept's stdout and
// capture/replay's stdin (issue #299).

#include "common/tests_common_pch.hpp"

#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

import fs8.mods;

using namespace fs8;

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    std::vector<event_type> make_test_events() {
        return {
          event_type{EV_KEY, KEY_A, 1},
          event_type{EV_SYN, SYN_REPORT, 0},
          event_type{EV_KEY, KEY_A, 0},
          event_type{EV_SYN, SYN_REPORT, 0},
          event_type{EV_REL, REL_X, 10},
          event_type{EV_SYN, SYN_REPORT, 0},
        };
    }

    void write_raw_events(int const fd, std::vector<event_type> const& events) {
        for (auto const& ev : events) {
            auto const n = ::write(fd, &ev.native(), sizeof(input_event));
            ASSERT_EQ(n, static_cast<ssize_t>(sizeof(input_event)));
        }
    }

    void write_binary_capture_file(char const* path, std::vector<event_type> const& events) {
        int const fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        ASSERT_GE(fd, 0);

        struct __attribute__((packed)) {
            std::uint32_t magic;
            std::uint16_t version;
        } constexpr header{0x3853'4646u, 1};

        ASSERT_EQ(::write(fd, &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));
        for (auto const& ev : events) {
            auto const& native = ev.native();
            ASSERT_EQ(::write(fd, &native, sizeof(input_event)), static_cast<ssize_t>(sizeof(input_event)));
        }
        ::close(fd);
    }

    std::vector<char> drain_fd(int fd) {
        std::vector<char> buf;
        char              tmp[4096];
        for (;;) {
            auto const n = ::read(fd, tmp, sizeof(tmp));
            if (n <= 0) {
                break;
            }
            buf.insert(buf.end(), tmp, tmp + static_cast<std::size_t>(n));
        }
        return buf;
    }

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// 1. from_input reads raw binary events from a pipe (no io_manager).
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, FromInputReadsEventsFromPipe) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    auto const events = make_test_events();
    write_raw_events(fds[1], events);
    ::close(fds[1]);

    int const saved_stdin = ::dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
    ::close(fds[0]);

    auto pipeline = context | from_input | record;
    pipeline();

    ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    ::close(saved_stdin);

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(col[i].type(), events[i].type()) << "event " << i << " type";
        EXPECT_EQ(col[i].code(), events[i].code()) << "event " << i << " code";
        EXPECT_EQ(col[i].value(), events[i].value()) << "event " << i << " value";
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 2. std_output writes raw binary events (no file header).
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, StdOutputWritesRawBinary) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    {
        basic_std_output out{fds[1]};
        auto const events = make_test_events();
        for (auto const& ev : events) {
            EXPECT_TRUE(out.emit(ev));
        }
    }
    ::close(fds[1]);

    auto const raw_bytes = drain_fd(fds[0]);
    ::close(fds[0]);

    // Should be exactly N * sizeof(input_event) bytes — no file header.
    ASSERT_EQ(raw_bytes.size(), 6u * sizeof(input_event));

    // Verify first event.
    input_event native{};
    std::memcpy(&native, raw_bytes.data(), sizeof(input_event));
    EXPECT_EQ(native.type, static_cast<int>(EV_KEY));
    EXPECT_EQ(native.code, static_cast<int>(KEY_A));
    EXPECT_EQ(native.value, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// 3. std_output → pipe → from_input roundtrip.
//    Simulates `intercept keyboard | capture -` at the module level.
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, StdOutputToFromInputRoundtrip) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    // Step 1: Write events through std_output (simulates intercept's stdout).
    {
        basic_std_output out{fds[1]};
        std::vector const events = {
          event_type{EV_KEY, KEY_B, 1},
          event_type{EV_SYN, SYN_REPORT, 0},
          event_type{EV_KEY, KEY_B, 0},
          event_type{EV_SYN, SYN_REPORT, 0},
        };
        for (auto const& ev : events) {
            EXPECT_TRUE(out.emit(ev));
        }
    }
    ::close(fds[1]);

    // Step 2: Read events through from_input (simulates capture's stdin).
    int const saved_stdin = ::dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
    ::close(fds[0]);

    auto pipeline = context | from_input | record;
    pipeline();

    ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    ::close(saved_stdin);

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 4U);
    EXPECT_EQ(col[0].type(), EV_KEY);
    EXPECT_EQ(col[0].code(), KEY_B);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].type(), EV_SYN);
    EXPECT_EQ(col[1].code(), SYN_REPORT);
    EXPECT_EQ(col[2].type(), EV_KEY);
    EXPECT_EQ(col[2].code(), KEY_B);
    EXPECT_EQ(col[2].value(), 0);
    EXPECT_EQ(col[3].type(), EV_SYN);
    EXPECT_EQ(col[3].code(), SYN_REPORT);
}

// ══════════════════════════════════════════════════════════════════════════════
// 4. Replay reads raw binary events from a pipe (stdin mode).
//    Tests the non-seekable pipe path with header_buf stashing.
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, ReplayReadsRawBinaryFromStdin) {
    auto const events = make_test_events();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    // Write raw binary events (no file header) — simulates intercept's stdout.
    write_raw_events(fds[1], events);
    ::close(fds[1]);

    int const saved_stdin = ::dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
    ::close(fds[0]);

    auto pipeline = context | stopper | replay | record;
    auto& rep = pipeline.mod<basic_replay>();
    rep.set_file("-");  // stdin mode
    pipeline();

    ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    ::close(saved_stdin);

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(col[i].type(), events[i].type()) << "event " << i << " type";
        EXPECT_EQ(col[i].code(), events[i].code()) << "event " << i << " code";
        EXPECT_EQ(col[i].value(), events[i].value()) << "event " << i << " value";
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 5. std_output → pipe → replay stdin roundtrip.
//    Simulates `intercept keyboard | foresight replay -`.
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, StdOutputToReplayStdinRoundtrip) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    // Step 1: Write events through std_output.
    {
        basic_std_output out{fds[1]};
        std::vector const events = {
          event_type{EV_KEY, KEY_C, 1},
          event_type{EV_SYN, SYN_REPORT, 0},
          event_type{EV_KEY, KEY_C, 0},
          event_type{EV_SYN, SYN_REPORT, 0},
        };
        for (auto const& ev : events) {
            EXPECT_TRUE(out.emit(ev));
        }
    }
    ::close(fds[1]);

    // Step 2: Feed the pipe output into replay via stdin.
    int const saved_stdin = ::dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
    ::close(fds[0]);

    auto pipeline = context | stopper | replay | record;
    auto& rep = pipeline.mod<basic_replay>();
    rep.set_file("-");  // stdin
    pipeline();

    ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    ::close(saved_stdin);

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 4U);
    EXPECT_EQ(col[0].type(), EV_KEY);
    EXPECT_EQ(col[0].code(), KEY_C);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].type(), EV_SYN);
    EXPECT_EQ(col[1].code(), SYN_REPORT);
    EXPECT_EQ(col[2].type(), EV_KEY);
    EXPECT_EQ(col[2].code(), KEY_C);
    EXPECT_EQ(col[2].value(), 0);
    EXPECT_EQ(col[3].type(), EV_SYN);
    EXPECT_EQ(col[3].code(), SYN_REPORT);
}

// ══════════════════════════════════════════════════════════════════════════════
// 6. Full roundtrip: from_input pipe → binary capture file → replay.
//    Simulates `intercept keyboard | capture -` → file → `replay <file>`.
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, FromInputToCaptureFileToReplayRoundtrip) {
    auto const events = make_test_events();

    // Step 1: Read events from pipe via from_input.
    std::vector<event_type> read_events;
    {
        int fds[2];
        ASSERT_EQ(::pipe(fds), 0);
        write_raw_events(fds[1], events);
        ::close(fds[1]);

        int const saved_stdin = ::dup(STDIN_FILENO);
        ASSERT_GE(saved_stdin, 0);
        ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
        ::close(fds[0]);

        auto read_pipeline = context | from_input | record;
        read_pipeline();

        ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
        ::close(saved_stdin);

        auto const& col = read_pipeline.mod<basic_record>();
        ASSERT_EQ(col.size(), events.size());
        for (std::size_t i = 0; i < col.size(); ++i) {
            read_events.push_back(col[i]);
        }
    }

    // Step 2: Write the captured events to a binary file (same format as capture).
    char const* const tmp = "/tmp/pipe_test_capture.fs8";
    write_binary_capture_file(tmp, read_events);

    // Step 3: Replay from the file via pipeline.
    auto replay_pipeline = context | stopper | replay | record;
    auto& rep = replay_pipeline.mod<basic_replay>();
    rep.set_file(tmp);
    replay_pipeline();

    auto const& col2 = replay_pipeline.mod<basic_record>();
    ASSERT_EQ(col2.size(), events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(col2[i].type(), events[i].type()) << "event " << i << " type";
        EXPECT_EQ(col2[i].code(), events[i].code()) << "event " << i << " code";
        EXPECT_EQ(col2[i].value(), events[i].value()) << "event " << i << " value";
    }

    ::unlink(tmp);
}

// ══════════════════════════════════════════════════════════════════════════════
// 7. Full piped capture pipeline with io_manager.
//    Simulates `intercept keyboard | capture -` at the module level.
// ══════════════════════════════════════════════════════════════════════════════

TEST(PipeTest, FullPipedCaptureWithIoManager) {
    auto const events = make_test_events();

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    write_raw_events(fds[1], events);
    ::close(fds[1]);

    int const saved_stdin = ::dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
    ::close(fds[0]);

    auto pipeline = context | io_manager | idle_detector | from_input | stopper | capture;
    pipeline();

    ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    ::close(saved_stdin);

    auto const& cap = pipeline.mod<basic_capture<capture_binary_format, capture_daily>>();
    ASSERT_EQ(cap.buffered().size(), events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(cap.buffered()[i].type(), events[i].type()) << "event " << i << " type";
        EXPECT_EQ(cap.buffered()[i].code(), events[i].code()) << "event " << i << " code";
        EXPECT_EQ(cap.buffered()[i].value(), events[i].value()) << "event " << i << " value";
    }
}
