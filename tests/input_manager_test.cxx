// Created by moisrex on 8/9/26.

#include "common/tests_common_pch.hpp"

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <thread>

import fs8.mods;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.devices.evdev;
import fs8.devices.uinput;

using namespace fs8;

static constinit auto input_pipeline = context | io_manager | input_manager;

namespace {

    [[nodiscard]] auto& manager() noexcept {
        return input_pipeline.mod<basic_io_manager>();
    }

    [[nodiscard]] auto& input_mgr() noexcept {
        return input_pipeline.mod<basic_input_manager>();
    }

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

    /// Wait until `node` can be opened or `timeout_ms` elapses. A freshly
    /// created device node may be briefly missing or not yet world-openable
    /// (udev applying group permissions, or another process interfering).
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

} // namespace

TEST(InputManager, StartupRegistersOnlyTheUdevMonitorFd) {
    auto& io = manager();
    io.clear();

    EXPECT_EQ(input_pipeline(start), context_action::next);
    // Even when devices are discovered, only the udev monitor FD is watched.
    EXPECT_EQ(io.size(), 1);

    // No event queue, no next_event_tag support: the mod can only be started or
    // driven as an io_manager handler.
    static_assert(!std::is_invocable_v<basic_input_manager, next_event_tag const&>);
    static_assert(!std::is_invocable_v<basic_input_manager, event_type&>);
}

TEST(InputManager, RepeatedStartupRestoresMonitorRegistrationWithoutDuplicates) {
    auto& io = manager();
    io.clear();

    EXPECT_EQ(input_pipeline(start), context_action::next);
    EXPECT_EQ(io.size(), 1);

    // A pipeline restart clears io_manager and input_manager re-registers the
    // monitor; the registration must not accumulate duplicates.
    EXPECT_EQ(input_pipeline(start), context_action::next);
    EXPECT_EQ(io.size(), 1);
}

TEST(InputManager, StartupEnumeratesMatchingDevices) {
    static constinit auto enumerate_pipeline = context | io_manager | input_manager;
    auto& io = enumerate_pipeline.mod<basic_io_manager>();
    auto& im = enumerate_pipeline.mod<basic_input_manager>();

    im.add(keyboard);
    EXPECT_EQ(enumerate_pipeline(start), context_action::next);

    // Only the monitor FD is registered; devices are stored, not watched.
    EXPECT_EQ(io.size(), 1);

    if (im.devices().empty()) {
        GTEST_SKIP() << "No matching input devices are present on this system.";
    }
    for (auto const& dev : im.devices()) {
        EXPECT_TRUE(dev.is_ok());
    }
}

TEST(InputManager, RequiredQueryFailsStartupWhenNoDeviceMatches) {
    static constinit auto fail_pipeline = context | io_manager | input_manager;

    auto& im = fail_pipeline.mod<basic_input_manager>();
    auto q   = (query + match_sysname("foresight_device_that_never_exists")) | fail_on_no_match;
    im.add(q);

    EXPECT_EQ(fail_pipeline(start), context_action::exit);
}

TEST(InputManager, ManualAdditionsAreStoredButNotRediscovered) {
    basic_input_manager im;

    im.add(evdev::invalid(evdev_status::success));
    im.add(evdev::invalid(evdev_status::success));
    EXPECT_EQ(im.devices().size(), 2);

    // Manual devices have no udev identity; a hotplug notification for an
    // unrelated device must leave them untouched.
    EXPECT_EQ(im(io_fd{.fd = 12345}), context_action::next);
    EXPECT_EQ(im.devices().size(), 2);
}

TEST(InputManager, UnexpectedCallbackFdIsIgnoredSafely) {
    basic_input_manager im;

    EXPECT_EQ(im(io_fd{.fd = -1}), context_action::next);
    EXPECT_EQ(im(io_fd{.fd = 424242}), context_action::next);
}

TEST(InputManager, UnknownFdOnStartedManagerIsIgnored) {
    auto& io = manager();
    auto& im = input_mgr();
    io.clear();

    EXPECT_EQ(input_pipeline(start), context_action::next);
    EXPECT_EQ(im(io_fd{.fd = 424242}), context_action::next);
}

TEST(InputManager, HotplugAddsAndRemovesMatchingDevices) {
    if (verify_access_to_uinput() != uinput_access_result::available) {
        GTEST_SKIP() << "No /dev/uinput access.";
    }
    udev_queue queue(udev::instance().native());
    if (!queue.is_active()) {
        GTEST_SKIP() << "udev daemon is not active.";
    }

    static constinit auto hotplug_pipeline = context | io_manager | input_manager;
    auto& io = hotplug_pipeline.mod<basic_io_manager>();
    auto& im = hotplug_pipeline.mod<basic_input_manager>();

    im.add(query + attr::input_subsystem + attr::event_sysname);
    if (hotplug_pipeline(start) != context_action::next) {
        GTEST_SKIP() << "Cannot start the pipeline.";
    }

    auto const before = im.devices().size();

    // Probe monitor confirms udev delivers events to the netlink socket so the
    // blocking `load_event` dispatch below cannot hang the test.
    udev_monitor probe;
    probe.match_device("input");
    probe.enable();

    basic_uinput uin;
    if (!uin(caps::keyboard, start)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }
    // The device node may be missing or not yet openable if udev is still
    // applying permissions or another process (e.g. a competing instance)
    // is interfering; give it a moment and otherwise skip.
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "The virtual device node was never openable.";
    }

    bool const add_delivered = wait_for_event(probe.file_descriptor(), 5000);
    if (!add_delivered) {
        uin.close();
        GTEST_SKIP() << "udev did not deliver the add event.";
    }
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_GT(im.devices().size(), before) << "Hotplug add was not registered.";

    uin.close();

    bool const remove_delivered = wait_for_event(probe.file_descriptor(), 5000);
    if (!remove_delivered) {
        GTEST_SKIP() << "udev did not deliver the remove event.";
    }
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(im.devices().size(), before) << "Hotplug remove was not registered.";
}
