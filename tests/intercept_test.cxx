// Created by moisrex on 8/9/26.

#include "common/test_helpers.hpp"
#include "common/tests_common_pch.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

import fs8.mods;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.devices.evdev;

#include "common/fake_keyboard.hpp"

using namespace fs8;

namespace {

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
        if (!test::wait_for_openable(uin.devnode(), 3000)) {
            uin.close();
            return false;
        }
        if (!test::wait_for_event(probe.file_descriptor(), 5000)) {
            uin.close();
            return false;
        }
        return true;
    }

} // namespace

TEST(Interceptor, LoadEventThenNextEventDeliversToCollector) {
    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    auto fake = test::make_fake_keyboard();
    ASSERT_TRUE(fake.dev.is_ok());
    im.add(std::move(fake.dev));

    // A next_event with nothing pending sets up the watches and yields nothing.
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    // Inject a key press into the pipe.
    fake.inject_key_down(KEY_A);

    // The device FD is readable now; io_manager dispatches it to the interceptor.
    EXPECT_EQ(io(load_event), context_action::drop_event);

    // The next_event provider pops the queued event into the context.
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    EXPECT_EQ(col.front().type(), EV_KEY);
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_EQ(col.front().value(), 1);
}

TEST(Interceptor, HotpluggedDeviceGetsWatchedWithoutStaleEvent) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    basic_uinput uin;
    udev_monitor probe;
    if (!create_uinput_keyboard(uin, probe)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }

    EXPECT_EQ(io(load_event), context_action::drop_event);
    if (im.devices().empty()) {
        uin.close();
        GTEST_SKIP() << "The uinput keyboard was not enumerated.";
    }

    // The machine may already have real keyboards matching `[keyboard]`, so the
    // uinput keyboard is only the *newest* device; track it by count delta.
    auto const known    = std::ranges::distance(im.devices());
    int const  first_fd = std::ranges::next(im.devices().begin(), static_cast<std::ptrdiff_t>(known - 1))->native_handle();
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);
    EXPECT_TRUE(io.is_watched(first_fd));

    // Hotplug in a second keyboard.
    basic_uinput uin2;
    udev_monitor probe2;
    if (!create_uinput_keyboard(uin2, probe2)) {
        uin.close();
        GTEST_SKIP() << "Cannot create a second virtual uinput keyboard.";
    }

    EXPECT_EQ(io(load_event), context_action::drop_event);
    if (std::ranges::distance(im.devices()) < static_cast<std::ptrdiff_t>(known + 1)) {
        uin.close();
        uin2.close();
        GTEST_SKIP() << "The second uinput keyboard was not enumerated.";
    }

    int const second_fd = std::ranges::next(im.devices().begin(), static_cast<std::ptrdiff_t>(known))->native_handle();

    // A udev-only wakeup must not fabricate an *input* event. The hotplugged
    // devices' initial state reports (LED/sync/initial-key-state) are real
    // events and may be delivered. Drain them; the guarantee is that the drain
    // terminates promptly (bounded below) and the device gets watched.
    col.clear();
    int drained = 0;
    while (invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event) == context_action::next) {
        ASSERT_LT(++drained, 100) << "the interceptor kept fabricating events";
        ASSERT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);
    }

    // ... but the hotplugged device must now be watched.
    EXPECT_TRUE(io.is_watched(second_fd));

    uin.close();
    uin2.close();
}

TEST(Interceptor, DestroyingWatchedDeviceIsSafe) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager;

    auto& io = pipeline.mod<basic_io_manager>();
    auto& im = pipeline.mod<basic_input_manager>();

    EXPECT_EQ(pipeline(start), context_action::next);

    auto const baseline = std::ranges::distance(im.devices());

    basic_uinput uin;
    udev_monitor probe;
    if (!create_uinput_keyboard(uin, probe)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }

    EXPECT_EQ(io(load_event), context_action::drop_event);
    if (std::ranges::distance(im.devices()) <= baseline) {
        uin.close();
        GTEST_SKIP() << "The uinput keyboard was not enumerated.";
    }

    int const watched_fd = std::ranges::next(im.devices().begin(), static_cast<std::ptrdiff_t>(baseline))->native_handle();

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);
    ASSERT_TRUE(io.is_watched(watched_fd));

    // Destroy the watched device.  The udev "remove" (which frees the evdev
    // the interceptor caches) and the device fd's POLLERR/POLLHUP arrive in
    // the same poll batch, and input_manager's handler is dispatched first —
    // so the interceptor must not dereference its cached pointer.
    uin.close();

    if (!test::wait_for_event(probe.file_descriptor(), 5000)) {
        GTEST_SKIP() << "udev did not deliver the remove event.";
    }
    EXPECT_EQ(io(load_event), context_action::drop_event);

    // Drain any events that were already queued; reconciliation happens in
    // next_event and must evict the dead watch instead of touching it.
    int drained = 0;
    while (invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event) == context_action::next) {
        ASSERT_LT(++drained, 100) << "the interceptor kept fabricating events";
        ASSERT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);
    }

    EXPECT_EQ(std::ranges::distance(im.devices()), baseline) << "The removed device is still tracked.";
    EXPECT_FALSE(io.is_watched(watched_fd)) << "The dead watch was not evicted.";

    // A replacement device must still get watched (the kernel reuses the
    // just-freed fd number, so the new device must not be skipped as dead).
    basic_uinput uin2;
    udev_monitor probe2;
    if (!create_uinput_keyboard(uin2, probe2)) {
        GTEST_SKIP() << "Cannot create a second virtual uinput keyboard.";
    }

    EXPECT_EQ(io(load_event), context_action::drop_event);
    if (std::ranges::distance(im.devices()) <= baseline) {
        GTEST_SKIP() << "The second uinput keyboard was not enumerated.";
    }
    int const second_fd = std::ranges::next(im.devices().begin(), static_cast<std::ptrdiff_t>(baseline))->native_handle();

    drained = 0;
    while (!io.is_watched(second_fd) && invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event) == context_action::next) {
        ASSERT_LT(++drained, 100) << "the interceptor kept fabricating events";
        ASSERT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);
    }
    EXPECT_TRUE(io.is_watched(second_fd)) << "A fresh device was skipped because of a stale dead-fd entry.";

    uin2.close();
}
