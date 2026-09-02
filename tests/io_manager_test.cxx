// Created by moisrex on 8/8/26.

#include "common/tests_common_pch.hpp"

import fs8.mods;

using namespace fs8;

static constinit auto io_pipeline = context | io_manager;

namespace {

    struct read_handler {
        io_fd                info{};
        std::array<char, 32> buf{};

        context_action operator()(io_fd& fd) noexcept {
            info = fd;
            if (auto const n = ::read(fd.fd, buf.data(), static_cast<std::size_t>(buf.size() - 1)); n > 0) {
                buf[static_cast<std::size_t>(n)] = '\0';
            }
            return context_action::next;
        }
    };

    struct self_unwatch_handler {
        basic_io_manager* mgr   = nullptr;
        int               self  = -1;
        int               other = -1;
        int               calls = 0;

        context_action operator()([[maybe_unused]] io_fd& fd) noexcept {
            ++calls;
            mgr->unwatch(self);
            mgr->unwatch(other);
            return context_action::next;
        }
    };

    struct not_a_handler {
        void operator()(io_fd&) noexcept {}
    };

    struct throwing_handler {
        context_action operator()(io_fd&) {
            return context_action::next;
        }
    };

    struct exit_handler {
        context_action operator()(io_fd&) noexcept {
            return context_action::exit;
        }
    };

    struct recovery_handler {
        context_action operator()(io_fd&) noexcept {
            return context_action::recovery;
        }
    };

    static_assert(!io_handler<not_a_handler>);
    static_assert(!io_handler<throwing_handler>);
    static_assert(io_handler<read_handler>);
    static_assert(io_handler<self_unwatch_handler>);
    static_assert(io_handler<exit_handler>);
    static_assert(io_handler<recovery_handler>);

    static_assert((io_event::in | io_event::out) != io_event::in);
    static_assert(has(io_event::in | io_event::err, io_event::err));
    static_assert((io_event::in & ~io_event::in) == static_cast<io_event>(0));
    static_assert(has(io_event::pri, io_event::pri));
    static_assert(has(io_event::nval, io_event::nval));

    [[nodiscard]] auto& manager() noexcept {
        return io_pipeline.mod<basic_io_manager>();
    }

} // namespace

TEST(IOManager, DispatchesReadyFd) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));
    ASSERT_TRUE(mgr.is_watched(fds[0]));
    ASSERT_FALSE(mgr.empty());

    char const byte = 'x';
    ASSERT_EQ(write(fds[1], &byte, 1), 1);

    ASSERT_EQ(mgr(load_event), context_action::next);
    EXPECT_EQ(handler.info.fd, fds[0]);
    EXPECT_TRUE(has(handler.info.revents, io_event::in));
    EXPECT_STREQ(handler.buf.data(), "x");

    mgr.unwatch(fds[0]);
    EXPECT_FALSE(mgr.is_watched(fds[0]));
    EXPECT_TRUE(mgr.empty());

    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, DuplicateWatchReplacesInPlace) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler first, second;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, first));
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, second));

    ASSERT_EQ(write(fds[1], "z", 1), 1);
    ASSERT_EQ(mgr(load_event), context_action::next);

    // The latest handler wins, and it's dispatched only once.
    EXPECT_EQ(second.buf[0], 'z');
    EXPECT_EQ(first.buf[0], 0);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, RestartClearsStaleRegistrations) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler first;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, first));

    // A pipeline restart runs `start`, which should drop the stale registration.
    ASSERT_EQ(mgr(start), context_action::next);
    ASSERT_TRUE(mgr.empty());

    // The mod re-registers its handler after the restart.
    read_handler second;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, second));
    ASSERT_TRUE(mgr.is_watched(fds[0]));

    ASSERT_EQ(write(fds[1], "y", 1), 1);
    ASSERT_EQ(mgr(load_event), context_action::next);
    EXPECT_EQ(second.buf[0], 'y');
    EXPECT_EQ(first.buf[0], 0);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, SnapshotDispatchesSafelyDespiteUnwatch) {
    auto& mgr = manager();
    mgr.clear();

    int a[2], b[2];
    ASSERT_EQ(pipe(a), 0);
    ASSERT_EQ(pipe(b), 0);

    self_unwatch_handler ha, hb;
    ha.mgr   = &mgr;
    ha.self  = a[0];
    ha.other = b[0];
    hb.mgr   = &mgr;
    hb.self  = b[0];
    hb.other = a[0];

    ASSERT_TRUE(mgr.watch(io_fd{.fd = a[0]}, ha));
    ASSERT_TRUE(mgr.watch(io_fd{.fd = b[0]}, hb));

    ASSERT_EQ(write(a[1], "1", 1), 1);
    ASSERT_EQ(write(b[1], "2", 1), 1);

    ASSERT_EQ(mgr(load_event), context_action::next);
    // `ha` runs first and unwatches both fds; `hb`'s fd is gone by the time we
    // get to it, so it must not be called (and nothing crashes).
    EXPECT_EQ(ha.calls, 1);
    EXPECT_EQ(hb.calls, 0);
    EXPECT_TRUE(mgr.empty());

    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
}

TEST(IOManager, UnwatchUnknownFdIsNoop) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, handler));
    mgr.unwatch(fds[1]); // never watched
    ASSERT_TRUE(mgr.is_watched(fds[0]));

    mgr.clear();
    EXPECT_FALSE(mgr.is_watched(fds[0]));
    EXPECT_TRUE(mgr.empty());

    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, FreshManagerHandlesNoRegistrations) {
    basic_io_manager mgr;

    EXPECT_TRUE(mgr.empty());
    EXPECT_FALSE(mgr.is_watched(1));
    mgr.unwatch(1); // no-op, must not crash
    mgr.clear();    // no-op, must not crash
    EXPECT_EQ(mgr(start), context_action::next);

    // Nothing is watched, so load_event falls through instead of exiting.
    EXPECT_EQ(mgr(load_event), context_action::next);
}

TEST(IOManager, RejectsNegativeFd) {
    auto& mgr = manager();
    mgr.clear();

    read_handler handler;
    EXPECT_FALSE(mgr.watch(io_fd{.fd = -1}, handler));
    EXPECT_TRUE(mgr.empty());
    EXPECT_FALSE(mgr.is_watched(-1));
}

TEST(IOManager, DispatchesWritableFd) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[1], .events = io_event::out}, handler));

    ASSERT_EQ(mgr(load_event), context_action::next);
    EXPECT_EQ(handler.info.fd, fds[1]);
    EXPECT_TRUE(has(handler.info.revents, io_event::out));

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, ReportsHangup) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in | io_event::hup}, handler));

    close(fds[1]); // closing the write end hangs up the read end

    ASSERT_EQ(mgr(load_event), context_action::next);
    EXPECT_TRUE(has(handler.info.revents, io_event::hup));

    mgr.clear();
    close(fds[0]);
}

TEST(IOManager, HandlerExitPropagates) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    exit_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, handler));

    ASSERT_EQ(write(fds[1], "x", 1), 1);
    ASSERT_EQ(mgr(load_event), context_action::exit);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, HandlerIdlePropagates) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    recovery_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0]}, handler));

    ASSERT_EQ(write(fds[1], "x", 1), 1);
    ASSERT_EQ(mgr(load_event), context_action::recovery);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}

TEST(IOManager, DuplicateWatchReplacesEventsMask) {
    auto& mgr = manager();
    mgr.clear();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    read_handler handler;
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::out}, handler));
    ASSERT_TRUE(mgr.watch(io_fd{.fd = fds[0], .events = io_event::in}, handler));

    ASSERT_EQ(write(fds[1], "x", 1), 1);
    ASSERT_EQ(mgr(load_event), context_action::next);

    // The events mask is replaced, not OR'd with the earlier one.
    EXPECT_EQ(handler.info.events, io_event::in);

    mgr.clear();
    close(fds[0]);
    close(fds[1]);
}
