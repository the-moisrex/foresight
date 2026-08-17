// Created by moisrex on 8/9/26.

#include "common/tests_common_pch.hpp"

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <thread>
#include <unistd.h>

import fs8.mods;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.devices.evdev;
import fs8.devices.uinput;

using namespace fs8;

namespace {

    /// Pass-through mod that records whatever event the pipeline produces.
    struct collector {
        std::vector<event_type> events;

        context_action operator()(Context auto& ctx) noexcept {
            events.push_back(ctx.event());
            return context_action::next;
        }
    };

    static_assert(Modifier<collector>);

    /// Poll `fd` until it becomes readable or `timeout_ms` elapses.
    [[nodiscard]] bool wait_for_event(int const fd, int timeout_ms) noexcept {
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        while (timeout_ms > 0) {
            auto const res = ::poll(&pfd, 1, std::min(timeout_ms, 50));
            if (res > 0) {
                return true;
            }
            timeout_ms -= 50;
        }
        return false;
    }

    /// Wait until `node` can be opened or `timeout_ms` elapses.
    [[nodiscard]] bool wait_for_openable(std::string_view const node, int timeout_ms) noexcept {
        while (timeout_ms > 0) {
            int const fd = ::open(node.data(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                ::close(fd);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, timeout_ms)));
            timeout_ms -= 50;
        }
        return false;
    }

    [[nodiscard]] bool input_available() noexcept {
        if (verify_access_to_uinput() != uinput_access_result::available) {
            return false;
        }
        udev_queue queue(udev::instance().native());
        return queue.is_active();
    }

    /// Create a uinput keyboard and wait until udev delivers the add event.
    /// Returns false (and closes `uin`) if the environment cannot support it.
    [[nodiscard]] bool create_uinput_keyboard(basic_uinput& uin, udev_monitor& probe) noexcept {
        probe.match_device("input");
        probe.enable();

        if (!uin(caps::keyboard, start)) {
            return false;
        }
        if (!wait_for_openable(uin.devnode(), 3000)) {
            uin.close();
            return false;
        }
        if (!wait_for_event(probe.file_descriptor(), 5000)) {
            uin.close();
            return false;
        }
        return true;
    }

} // namespace

TEST(Interceptor, LoadEventThenNextEventDeliversToCollector) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | collector{};

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<collector>();

    EXPECT_EQ(pipeline(start), context_action::next);

    basic_uinput uin;
    udev_monitor probe;
    if (!create_uinput_keyboard(uin, probe)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }

    // The monitor FD is ready, so this drains the udev add without blocking.
    EXPECT_EQ(io(load_event), context_action::next);
    if (im.devices().empty()) {
        uin.close();
        GTEST_SKIP() << "The uinput keyboard was not enumerated.";
    }

    // A next_event with nothing pending sets up the watches and yields nothing.
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::ignore_event);
    EXPECT_TRUE(io.is_watched(im.devices().front().native_handle()));

    // Inject a key press into the device node.
    int const fd = ::open(uin.devnode().data(), O_WRONLY | O_NONBLOCK);
    ASSERT_GE(fd, 0);
    input_event ev{};
    ev.type  = EV_KEY;
    ev.code  = KEY_A;
    ev.value = 1;
    ASSERT_EQ(::write(fd, &ev, sizeof(ev)), static_cast<ssize_t>(sizeof(ev)));
    ev.type  = EV_SYN;
    ev.code  = SYN_REPORT;
    ev.value = 0;
    ASSERT_EQ(::write(fd, &ev, sizeof(ev)), static_cast<ssize_t>(sizeof(ev)));
    ::close(fd);

    // The device FD is readable now; io_manager dispatches it to the interceptor.
    EXPECT_EQ(io(load_event), context_action::next);

    // The next_event provider pops the queued event into the context.
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.events.empty());
    EXPECT_EQ(col.events.front().type(), EV_KEY);
    EXPECT_EQ(col.events.front().code(), KEY_A);
    EXPECT_EQ(col.events.front().value(), 1);

    uin.close();
}

TEST(Interceptor, HotpluggedDeviceGetsWatchedWithoutStaleEvent) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | collector{};

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<collector>();

    EXPECT_EQ(pipeline(start), context_action::next);

    basic_uinput uin;
    udev_monitor probe;
    if (!create_uinput_keyboard(uin, probe)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }

    EXPECT_EQ(io(load_event), context_action::next);
    if (im.devices().empty()) {
        uin.close();
        GTEST_SKIP() << "The uinput keyboard was not enumerated.";
    }

    // The machine may already have real keyboards matching `[keyboard]`, so the
    // uinput keyboard is only the *newest* device; track it by count delta.
    std::size_t const known    = std::ranges::distance(im.devices());
    int const         first_fd = std::ranges::next(im.devices().begin(), static_cast<std::ptrdiff_t>(known - 1))->native_handle();
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::ignore_event);
    EXPECT_TRUE(io.is_watched(first_fd));

    // Hotplug in a second keyboard.
    basic_uinput uin2;
    udev_monitor probe2;
    if (!create_uinput_keyboard(uin2, probe2)) {
        uin.close();
        GTEST_SKIP() << "Cannot create a second virtual uinput keyboard.";
    }

    EXPECT_EQ(io(load_event), context_action::next);
    if (std::ranges::distance(im.devices()) < static_cast<std::ptrdiff_t>(known + 1)) {
        uin.close();
        uin2.close();
        GTEST_SKIP() << "The second uinput keyboard was not enumerated.";
    }

    int const second_fd = std::ranges::next(im.devices().begin(), static_cast<std::ptrdiff_t>(known))->native_handle();

    // A udev-only wakeup must not fabricate an *input* event. The hotplugged
    // devices' initial state reports (LED/sync) are real events and may be
    // delivered, so drain them; the guarantee is that nothing delivered here is
    // an EV_KEY fabrication.
    col.events.clear();
    int drained = 0;
    while (invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event) == context_action::next) {
        ASSERT_LT(++drained, 100) << "the interceptor kept fabricating events";
        ASSERT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);
        ASSERT_NE(col.events.back().type(), EV_KEY);
    }

    // ... but the hotplugged device must now be watched.
    EXPECT_TRUE(io.is_watched(second_fd));

    uin.close();
    uin2.close();
}
