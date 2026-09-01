// Created by moisrex on 8/17/26.

#include "./common/tests_common_pch.hpp"

#include <chrono>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <poll.h>
#include <thread>
#include <unistd.h>

import fs8.mods;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.devices.evdev;

using namespace fs8;

namespace {

    /// External sink for `record[captured_events]`.
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)

    // NOLINTBEGIN(*-global-variables)
    bool saw_self      = false;
    bool saw_dev       = false;
    bool saw_stdin     = false;
    bool saw_chained   = false;
    bool saw_device_is = false;

    // NOLINTEND(*-global-variables)

    [[nodiscard]] bool input_available() noexcept {
        if (verify_access_to_uinput() != uinput_access_result::available) {
            return false;
        }
        udev_queue queue(udev::instance().native());
        return queue.is_active();
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

    /// Inject a KEY_A down + SYN into the given device node.
    void inject_key_down(std::string_view const devnode) {
        int const fd = ::open(devnode.data(), O_WRONLY | O_NONBLOCK);
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
    }

} // namespace

TEST(DeviceTest, EmitterEventsAreSelf) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col.at(0).source(), source_id_none);
    EXPECT_EQ(col.at(1).source(), source_id_none);
}

TEST(DeviceTest, EmitForksAreSelf) {
    auto  pipeline = context | emit_all[{syn_user_event}] | emit[down(KEY_B)] | record;
    auto& col      = pipeline.mod<basic_record>();

    pipeline();

    // The provider's SYN plus the emitted key down + SYN.
    ASSERT_EQ(col.size(), 3U);
    EXPECT_EQ(col.at(0).source(), source_id_none);
    EXPECT_EQ(col.at(1).source(), source_id_none);
    EXPECT_EQ(col.at(2).source(), source_id_none);
}

TEST(DeviceTest, ForkEmitPreservesSource) {
    auto pipeline =
      context
      | emit_all[{syn_user_event}]
      | run{[](auto& ctx) noexcept -> void {
            event_type ev{EV_KEY, KEY_C, 1};
            ev.source(sid(intercept, 42)); // pretend it came from a device
            std::ignore = ctx.fork_emit(ev);
        }}
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // The forked event runs first (record captures it inside the fork), then
    // the provider's SYN reaches record. The forked event must keep its source.
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col.at(0).code(), KEY_C);
    EXPECT_EQ(col.at(0).source(), sid(intercept, 42));
    EXPECT_EQ(col.at(1).type(), EV_SYN);
    EXPECT_EQ(col.at(1).source(), source_id_none);
}

TEST(DeviceTest, IgnoreOriginDropsSelf) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | drop_origin[source_id_none]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    EXPECT_TRUE(col.empty());
}

TEST(DeviceTest, IgnoreOriginKeepsOthers) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | drop_origin[sid(from_input)]
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col.at(0).source(), source_id_none);
}

TEST(DeviceTest, Conditions) {
    saw_self  = false;
    saw_dev   = false;
    saw_stdin = false;
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | run{[](auto& ctx) noexcept {
            saw_self  = saw_self || self_emitted(ctx.event());
            saw_dev   = saw_dev || from_device(ctx.event());
            saw_stdin = saw_stdin || from_stdin(ctx.event());
        }}
      | record;

    pipeline();

    EXPECT_TRUE(saw_self);
    EXPECT_FALSE(saw_dev);
    EXPECT_FALSE(saw_stdin);
}

TEST(DeviceTest, DeviceIsPredicate) {
    saw_device_is = false;
    auto pipeline =
      context
      | emit_all[{syn_user_event}]
      | run{[](auto& ctx) noexcept {
            saw_device_is = saw_device_is || device_is(source_id_none)(ctx.event());
        }}
      | record;

    pipeline();

    EXPECT_TRUE(saw_device_is);
}

TEST(DeviceTest, OnlyDeviceAndIgnoreDevice) {
    // only_device[source_id_none] lets synthesized events through.
    auto  keep = context | emit_all[{syn_user_event}] | only_device[source_id_none] | record;
    auto& col  = keep.mod<basic_record>();
    keep();
    ASSERT_EQ(col.size(), 1U);
    EXPECT_EQ(col.front().source(), source_id_none);

    // only_device for a different device drops them.
    auto  drop = context | emit_all[{syn_user_event}] | only_device[sid(intercept, 123)] | record;
    auto& cold = drop.mod<basic_record>();
    drop();
    EXPECT_TRUE(cold.empty());

    // drop_device[source_id_none] drops them.
    auto  drop2 = context | emit_all[{syn_user_event}] | drop_device[source_id_none] | record;
    auto& cold2 = drop2.mod<basic_record>();
    drop2();
    EXPECT_TRUE(cold2.empty());
}

TEST(DeviceTest, FromInputMarksStdin) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    int const saved_stdin = ::dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    ASSERT_EQ(::dup2(fds[0], STDIN_FILENO), STDIN_FILENO);

    input_event ev{};
    ev.type  = EV_KEY;
    ev.code  = KEY_A;
    ev.value = 1;
    ASSERT_EQ(::write(fds[1], &ev, sizeof(ev)), static_cast<ssize_t>(sizeof(ev)));
    ev.type  = EV_SYN;
    ev.code  = SYN_REPORT;
    ev.value = 0;
    ASSERT_EQ(::write(fds[1], &ev, sizeof(ev)), static_cast<ssize_t>(sizeof(ev)));
    ::close(fds[1]);

    captured_events.clear();
    auto pipeline = context | from_input | record[captured_events];
    pipeline();

    ASSERT_EQ(::dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    ::close(saved_stdin);
    ::close(fds[0]);

    ASSERT_EQ(captured_events.size(), 2U);
    EXPECT_EQ(captured_events.at(0).source(), sid(from_input));
    EXPECT_EQ(captured_events.at(1).source(), sid(from_input));
}

TEST(DeviceTest, InterceptMarksDeviceSource) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    // Build a uinput keyboard from an empty template whose phys is NOT the
    // foresight chain marker, simulating a plain (real) keyboard.
    libevdev* template_ptr = libevdev_new();
    ASSERT_NE(template_ptr, nullptr);
    libevdev_enable_event_type(template_ptr, EV_SYN);
    libevdev_enable_event_type(template_ptr, EV_KEY);
    for (event_type::code_type code = KEY_A; code <= KEY_C; ++code) {
        libevdev_enable_event_code(template_ptr, EV_KEY, code, nullptr);
    }
    libevdev_set_name(template_ptr, "plain test keyboard");
    libevdev_set_phys(template_ptr, "test:plain-keyboard");

    evdev        template_dev{template_ptr, evdev_status::success};
    basic_uinput uin;
    if (!finalize_device(uin, template_dev, {})) {
        GTEST_SKIP() << "Cannot create a plain virtual keyboard.";
    }
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Plain virtual keyboard did not become openable.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    // Bypass udev: add the virtual keyboard's node directly.
    fs8::evdev opened = fs8::evdev{uin.devnode()};
    ASSERT_TRUE(opened.is_ok());
    ASSERT_FALSE(opened.physical_location().starts_with("foresight:"));
    // Grab the virtual keyboard so the injected events reach only this process;
    // otherwise the test types a real 'a' into whatever app has focus.
    opened.grab_input(true);
    if (opened.get_status() == fs8::evdev_status::grab_failure) {
        uin.close();
        GTEST_SKIP() << "Cannot grab the virtual keyboard (a grab may be held by the display server).";
    }
    int const expected_fd = opened.native_handle();
    im.add(std::move(opened));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    auto const source = col.front().source();
    EXPECT_EQ(col.front().type(), EV_KEY);
    EXPECT_EQ(col.front().code(), KEY_A);
    // A plain device is neither stdin/self nor owned/chained.
    EXPECT_NE(source, source_id_none);
    EXPECT_NE(source, sid(from_input));
    EXPECT_NE(source, source_id_none);
    EXPECT_NE(im.device_of(source), nullptr);
    EXPECT_EQ(im.fd_of(source), expected_fd);
    EXPECT_FALSE(im.is_owned(source));
    EXPECT_FALSE(im.is_chained(source));

    uin.close();
}

TEST(DeviceTest, DropOwnedDropsOwnedDeviceEvents) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    basic_uinput uin;
    if (!uin(caps::keyboard, start)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual keyboard did not become openable.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | drop_owned | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    im.own_device(uin.devnode());

    fs8::evdev opened = fs8::evdev{uin.devnode()};
    ASSERT_TRUE(opened.is_ok());
    opened.grab_input(true);
    if (opened.get_status() == fs8::evdev_status::grab_failure) {
        uin.close();
        GTEST_SKIP() << "Cannot grab the virtual keyboard.";
    }
    im.add(std::move(opened));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    // `drop_owned` drops the event (it came back from our own device).
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::drop_event);

    EXPECT_TRUE(col.empty());

    uin.close();
}

TEST(DeviceTest, DropEmittedDropsSynthesizedEvents) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | drop_emitted
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // `drop_emitted` drops synthesized events.
    EXPECT_TRUE(col.empty());
}

TEST(DeviceTest, DropEmittedLetsOwnedThrough) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    basic_uinput uin;
    if (!uin(caps::keyboard, start)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual keyboard did not become openable.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | drop_emitted | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    im.own_device(uin.devnode());

    fs8::evdev opened = fs8::evdev{uin.devnode()};
    ASSERT_TRUE(opened.is_ok());
    opened.grab_input(true);
    if (opened.get_status() == fs8::evdev_status::grab_failure) {
        uin.close();
        GTEST_SKIP() << "Cannot grab the virtual keyboard.";
    }
    im.add(std::move(opened));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    // `drop_emitted` only drops source_id_none, not owned device events.
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    EXPECT_EQ(col.front().code(), KEY_A);
    // Owned device events are not source_id_none.
    EXPECT_NE(col.front().source(), source_id_none);

    uin.close();
}

TEST(DeviceTest, OwnedDeviceIsResolvableAndOwned) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    basic_uinput uin;
    if (!uin(caps::keyboard, start)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual keyboard did not become openable.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    // This process "owns" the device; events read back from it carry its real
    // device id, and `is_owned` reports it as ours.
    im.own_device(uin.devnode());

    fs8::evdev opened = fs8::evdev{uin.devnode()};
    ASSERT_TRUE(opened.is_ok());
    // Grab the virtual keyboard so the injected events reach only this process;
    // otherwise the test types a real 'a' into whatever app has focus.
    opened.grab_input(true);
    if (opened.get_status() == fs8::evdev_status::grab_failure) {
        uin.close();
        GTEST_SKIP() << "Cannot grab the virtual keyboard (a grab may be held by the display server).";
    }
    int const expected_fd = opened.native_handle();
    im.add(std::move(opened));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    auto const source = col.front().source();
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_NE(source, source_id_none); // it's the device id, not the synthesized marker
    EXPECT_EQ(im.fd_of(source), expected_fd);
    EXPECT_TRUE(im.is_owned(source));

    uin.close();
}

TEST(DeviceTest, ChainedDeviceIsChained) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    // Build a virtual keyboard from an empty template whose phys is stamped
    // with the foresight chain marker, as another foresight app would.
    libevdev* template_ptr = libevdev_new();
    ASSERT_NE(template_ptr, nullptr);
    libevdev_enable_event_type(template_ptr, EV_SYN);
    libevdev_enable_event_type(template_ptr, EV_KEY);
    for (event_type::code_type code = KEY_A; code <= KEY_C; ++code) {
        libevdev_enable_event_code(template_ptr, EV_KEY, code, nullptr);
    }
    libevdev_set_name(template_ptr, "foresight chained keyboard");
    libevdev_set_phys(template_ptr, "foresight:chain");

    evdev        template_dev{template_ptr, evdev_status::success};
    basic_uinput uin;
    if (!finalize_device(uin, template_dev, {})) {
        GTEST_SKIP() << "Cannot create a chained virtual keyboard.";
    }
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Chained virtual keyboard did not become openable.";
    }

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[keyboard]
      | input_manager
      | run{[](auto& ctx) noexcept {
            saw_chained = saw_chained || from_chained(ctx);
        }}
      | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    fs8::evdev opened = fs8::evdev{uin.devnode()};
    ASSERT_TRUE(opened.is_ok());
    ASSERT_TRUE(opened.physical_location().starts_with("foresight:"));
    // Grab the virtual keyboard so the injected events reach only this process;
    // otherwise the test types a real 'a' into whatever app has focus.
    opened.grab_input(true);
    if (opened.get_status() == fs8::evdev_status::grab_failure) {
        uin.close();
        GTEST_SKIP() << "Cannot grab the virtual keyboard (a grab may be held by the display server).";
    }
    im.add(std::move(opened));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    auto const source = col.front().source();
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_NE(source, source_id_none);
    EXPECT_NE(im.device_of(source), nullptr);
    EXPECT_FALSE(im.is_owned(source));
    EXPECT_TRUE(im.is_chained(source));
    EXPECT_TRUE(saw_chained);

    uin.close();
}

TEST(DeviceTest, DropSelfDropsOwnedDeviceEvents) {
    if (!input_available()) {
        GTEST_SKIP() << "No /dev/uinput access or udev daemon is not active.";
    }

    basic_uinput uin;
    if (!uin(caps::keyboard, start)) {
        GTEST_SKIP() << "Cannot create a virtual uinput keyboard.";
    }
    if (!wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Virtual keyboard did not become openable.";
    }

    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | drop_self | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    EXPECT_EQ(pipeline(start), context_action::next);

    im.own_device(uin.devnode());

    fs8::evdev opened = fs8::evdev{uin.devnode()};
    ASSERT_TRUE(opened.is_ok());
    // Grab the virtual keyboard so the injected events reach only this process;
    // otherwise the test types a real 'a' into whatever app has focus.
    opened.grab_input(true);
    if (opened.get_status() == fs8::evdev_status::grab_failure) {
        uin.close();
        GTEST_SKIP() << "Cannot grab the virtual keyboard (a grab may be held by the display server).";
    }
    im.add(std::move(opened));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::next);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    // `drop_self` drops the last event (it came back from our own device).
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::drop_event);

    // All events came back from our own device, so `drop_self` dropped them.
    EXPECT_TRUE(col.empty());

    uin.close();
}
