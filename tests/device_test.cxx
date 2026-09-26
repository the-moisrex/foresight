// Created by moisrex on 8/17/26.

#include "./common/test_helpers.hpp"
#include "./common/tests_common_pch.hpp"

#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <unistd.h>

import fs8.mods;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.devices.evdev;
import dynamic_scoping;

#include "./common/fake_keyboard.hpp"

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
    auto  drop_pipe = context | emit_all[{syn_user_event}] | only_device[sid(intercept, 123)] | record;
    auto& cold      = drop_pipe.mod<basic_record>();
    drop_pipe();
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
    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto fake = test::make_fake_keyboard();
    ASSERT_TRUE(fake.dev.is_ok());
    int const expected_fd = fake.dev.native_handle();
    im.add(std::move(fake.dev));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    fake.inject_key_down(KEY_A);
    EXPECT_EQ(io(load_event), context_action::drop_event);
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
    EXPECT_FALSE(is_owned_source(source));
    EXPECT_FALSE(is_chained_source(source));
}

TEST(DeviceTest, DropOwnedDropsOwnedDeviceEvents) {
    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | drop_owned | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto fake = test::make_fake_keyboard();
    ASSERT_TRUE(fake.dev.is_ok());
    im.own_device(device_sysname(fake.dev));
    im.add(std::move(fake.dev));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    fake.inject_key_down(KEY_A);
    EXPECT_EQ(io(load_event), context_action::drop_event);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    // `drop_owned` drops the event (it came back from our own device).
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::drop_event);

    EXPECT_TRUE(col.empty());
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
    // Synthetic: drop_emitted only drops source_id_none; any non-zero source passes.
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | run{[](auto& ctx) noexcept {
            // Stamp a non-zero source to simulate a device-sourced event.
            ctx.event().source(sid(intercept, 0));
        }}
      | drop_emitted
      | record;
    auto& col = pipeline.mod<basic_record>();

    pipeline();

    // drop_emitted only drops source_id_none, not device-sourced events.
    ASSERT_FALSE(col.empty());
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_NE(col.front().source(), source_id_none);
}

TEST(DeviceTest, OwnedDeviceIsResolvableAndOwned) {
    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto fake = test::make_fake_keyboard();
    ASSERT_TRUE(fake.dev.is_ok());
    im.own_device(device_sysname(fake.dev));
    int const expected_fd = fake.dev.native_handle();
    im.add(std::move(fake.dev));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    fake.inject_key_down(KEY_A);
    EXPECT_EQ(io(load_event), context_action::drop_event);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    auto const source = col.front().source();
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_NE(source, source_id_none); // it's the device id, not the synthesized marker
    EXPECT_EQ(im.fd_of(source), expected_fd);
    EXPECT_TRUE(im.is_owned(source));
    EXPECT_TRUE(is_owned_source(source));
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
    if (!test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "Chained virtual keyboard did not become openable.";
    }

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[keyboard]
      | input_manager
      | run{[](auto& ctx) noexcept {
            saw_chained = saw_chained || from_chained(ctx.event());
        }}
      | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    dynamic_scope scope{dynamic_context, pipeline};
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

    test::inject_key_down(uin.devnode());
    EXPECT_EQ(io(load_event), context_action::drop_event);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::next);

    ASSERT_FALSE(col.empty());
    auto const source = col.front().source();
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_NE(source, source_id_none);
    EXPECT_NE(im.device_of(source), nullptr);
    EXPECT_FALSE(im.is_owned(source));
    EXPECT_TRUE(im.is_chained(source));
    EXPECT_FALSE(is_owned_source(source));
    EXPECT_TRUE(is_chained_source(source));
    EXPECT_TRUE(saw_chained);

    uin.close();
}

TEST(DeviceTest, DropSelfDropsOwnedDeviceEvents) {
    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | drop_self | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto fake = test::make_fake_keyboard();
    ASSERT_TRUE(fake.dev.is_ok());
    im.own_device(device_sysname(fake.dev));
    im.add(std::move(fake.dev));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    fake.inject_key_down(KEY_A);
    EXPECT_EQ(io(load_event), context_action::drop_event);
    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::next);
    // `drop_self` drops the last event (it came back from our own device).
    EXPECT_EQ(invoke_mods(pipeline, pipeline.get_mods()), context_action::drop_event);

    // All events came back from our own device, so `drop_self` dropped them.
    EXPECT_TRUE(col.empty());
}

TEST(DeviceTest, DevicePredicatesIgnoreOriginBits) {
    constexpr auto base    = make_source_id(0x42, 7);
    constexpr auto flagged = with_origin(base, source_id_owned | source_id_chained);
    constexpr auto other   = make_source_id(0x43, 1);

    event_type ev{};
    ev.source(flagged);

    // Identity-based predicates match despite the origin bits.
    EXPECT_TRUE(device_is(base)(ev));
    EXPECT_TRUE(device_is(flagged)(ev));
    EXPECT_FALSE(device_is(other)(ev));
    EXPECT_TRUE(from_device(ev));
    EXPECT_EQ(drop_origin[base](ev), context_action::drop_event);
    EXPECT_EQ(drop_origin[other](ev), context_action::next);
    EXPECT_EQ(drop_device[flagged](ev), context_action::drop_event);
    EXPECT_EQ(only_device[base](ev), context_action::next);
    EXPECT_EQ(only_device[other](ev), context_action::drop_event);

    // Origin-bit predicates see the flags.
    EXPECT_TRUE(from_chained(ev));
    EXPECT_FALSE(drop_owned(ev));
    EXPECT_FALSE(drop_self(ev));
    EXPECT_FALSE(self_emitted(ev));
    // drop_emitted returns true = keep: a device-sourced event is never
    // "synthesized", even with origin bits set.
    EXPECT_TRUE(drop_emitted(ev));
    EXPECT_FALSE(from_stdin(ev));

    // Unflagged device source: not owned, not chained.
    event_type plain_ev{};
    plain_ev.source(base);
    EXPECT_TRUE(drop_owned(plain_ev));
    EXPECT_TRUE(drop_self(plain_ev));
    EXPECT_FALSE(from_chained(plain_ev));
}

TEST(DeviceTest, LateOwnedDeviceGetsOriginBitStamped) {
    static constinit auto pipeline = context | io_manager | intercept[keyboard] | input_manager | record;

    auto& io  = pipeline.mod<basic_io_manager>();
    auto& im  = pipeline.mod<basic_input_manager>();
    auto& col = pipeline.mod<basic_record>();

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto fake = test::make_fake_keyboard();
    ASSERT_TRUE(fake.dev.is_ok());
    int const pipe_fd = fake.dev.native_handle();
    // Registered before this process ever tagged the device as its own.
    im.add(std::move(fake.dev));

    EXPECT_EQ(invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event), context_action::drop_event);

    // The pipeline also watches live system keyboards, which may emit their
    // own events while this test runs — drain until our KEY_A shows up.
    fake.inject_key_down(KEY_A);
    EXPECT_EQ(io(load_event), context_action::drop_event);
    event_type first_ev{};
    for (int i = 0; i < 8; ++i) {
        if (invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event) != context_action::next) {
            break;
        }
        if (invoke_mods(pipeline, pipeline.get_mods()) != context_action::next) {
            break;
        }
        if (!col.empty()) {
            first_ev = col.back();
        }
        if (first_ev.code() == KEY_A && im.fd_of(first_ev.source()) == pipe_fd) {
            break;
        }
    }
    ASSERT_EQ(first_ev.code(), KEY_A);
    auto const first = first_ev.source();
    EXPECT_FALSE(is_owned_source(first));
    EXPECT_FALSE(im.is_owned(first));

    // Tag it after registration: input_manager broadcasts `source_owned` and
    // intercept stamps the origin bit into its cached source id.
    ASSERT_EQ(im.fd_of(first), pipe_fd); // sanity: it really is our pipe device
    im.own_device(im.sysname_of(first));
    EXPECT_TRUE(im.is_owned(first));

    fake.inject_key_down(KEY_B);
    EXPECT_EQ(io(load_event), context_action::drop_event);
    // Drain events (the first inject's SYN_REPORT is still queued) until KEY_B
    // shows up.
    event_type last{};
    for (int i = 0; i < 8; ++i) {
        if (invoke_first_mod_of(pipeline, pipeline.get_mods(), next_event) != context_action::next) {
            break;
        }
        if (invoke_mods(pipeline, pipeline.get_mods()) != context_action::next) {
            break;
        }
        if (!col.empty()) {
            last = col.back();
        }
        // Only our pipe-backed keyboard's KEY_B ends the drain: the pipeline
        // also watches live system keyboards, which may emit their own events
        // while this test runs.
        if (last.code() == KEY_B && identity_of(last.source()) == identity_of(first)) {
            break;
        }
    }
    ASSERT_EQ(last.code(), KEY_B);
    auto const second = last.source();
    EXPECT_TRUE(is_owned_source(second));
    EXPECT_EQ(identity_of(second), identity_of(first));
    EXPECT_EQ(im.fd_of(second), im.fd_of(first));
}
