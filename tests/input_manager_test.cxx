// Created by moisrex on 8/9/26.

#include "common/test_helpers.hpp"
#include "common/tests_common_pch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <span>
#include <string_view>

import fs8.mods;
import fs8.devices.udev;
import fs8.devices.queries;
import fs8.devices.evdev;
import dynamic_scoping;

using namespace fs8;

static constinit auto input_pipeline = context | io_manager | input_manager;

namespace {

    [[nodiscard]] auto& manager() noexcept {
        return input_pipeline.mod<basic_io_manager>();
    }

    [[nodiscard]] auto& input_mgr() noexcept {
        return input_pipeline.mod<basic_input_manager>();
    }

    /// A query provider backed by its own `owned_query`, exposing a span over
    /// a refreshed `device_query` view.
    struct test_query_provider {
        owned_query                 q;
        std::array<device_query, 1> views{};

        explicit test_query_provider(device_query const query) noexcept {
            q.set(query);
        }

        [[nodiscard]] std::span<device_query const> queries() noexcept {
            views[0] = q;
            return views;
        }
    };

    /// Counts device-list control events pushed by input_manager while a
    /// dynamic scope is bound (mirrors what intercept / keys_state consume).
    /// The three kinds share code 13; distinguish them by full event equality.
    inline int devices_changed_count  = 0;
    inline int device_connected_count = 0;

    /// True when `im` is tracking an event device with the given sysname
    /// (e.g. "event5"). Immune to unrelated devices that happen to match the
    /// query, which is what made bare count assertions flaky.
    [[nodiscard]] bool has_sysname(basic_input_manager const& im, std::string_view const sysname) noexcept {
        for (auto const& dev : im.devices()) {
            if (device_sysname(dev) == sysname) {
                return true;
            }
        }
        return false;
    }

    /// Pump `load_event` until `pred` holds or `timeout_ms` elapses.
    /// A short idle timeout keeps `load_event` from blocking forever when the
    /// monitor stays quiet (background udev traffic only wakes it briefly).
    /// Returns `pred()` on exit.
    template <typename Pred>
    [[nodiscard]] bool pump_until(basic_io_manager& io, int const timeout_ms, Pred pred) {
        if (pred()) {
            return true;
        }
        io.set_idle_timeout(std::chrono::milliseconds{50});
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
        bool       ok       = false;
        while (!ok && std::chrono::steady_clock::now() < deadline) {
            (void) io(load_event);
            ok = pred();
        }
        io.clear_idle_timeout();
        return ok;
    }

    /// Pump `load_event` for at least `duration_ms` so queued udev events are
    /// drained into the pipeline (used when the expected outcome is "nothing
    /// was added", so there is no count/sysname change to wait for).
    void pump_for(basic_io_manager& io, int const duration_ms) {
        io.set_idle_timeout(std::chrono::milliseconds{20});
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{duration_ms};
        do {
            (void) io(load_event);
        } while (std::chrono::steady_clock::now() < deadline);
        io.clear_idle_timeout();
    }

    /// Wait until the probe monitor delivers a udev event for `sysname`
    /// (any action). Drains unrelated background events without counting them.
    [[nodiscard]] bool wait_for_probe_sysname(udev_monitor& probe, std::string_view const sysname, int timeout_ms) {
        while (timeout_ms > 0) {
            if (test::wait_for_event(probe.file_descriptor(), std::min(timeout_ms, 50))) {
                while (auto dev = probe.next_device()) {
                    if (dev.sysname() == sysname) {
                        return true;
                    }
                }
            }
            timeout_ms -= 50;
        }
        return false;
    }

} // namespace

/// A mod that tallies the push-model notifications from input_manager.
constexpr struct [[nodiscard]] change_counter {
    constexpr context_action operator()(control_event const& tag) const noexcept {
        using enum context_action;
        if (tag == devices_changed) {
            ++devices_changed_count;
            return next;
        }
        if (tag == device_connected) {
            ++device_connected_count;
            return next;
        }
        return drop_event;
    }
} change_counter;

TEST(InputManager, StartupRegistersOnlyTheUdevMonitorFd) {
    auto& io = manager();
    io.clear();

    EXPECT_EQ(input_pipeline(start), context_action::next);
    // Even when devices are discovered, only the udev monitor FD is watched.
    EXPECT_EQ(io.size(), 1);

    // No event queue: `start`/`enumerate_devices` reach the mod as control
    // events, and readiness is driven through the io_manager handler — it
    // never consumes input events.
    static_assert(std::is_invocable_v<basic_input_manager, control_event const&>);
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
    auto&                 io                 = enumerate_pipeline.mod<basic_io_manager>();
    auto&                 im                 = enumerate_pipeline.mod<basic_input_manager>();

    test_query_provider provider(keyboard);
    im.add_query_provider(provider_handle(provider));
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

    auto&               im = fail_pipeline.mod<basic_input_manager>();
    auto                q  = (query + match_sysname("foresight_device_that_never_exists")) | required;
    test_query_provider provider(q);
    im.add_query_provider(provider_handle(provider));

    EXPECT_EQ(fail_pipeline(start), context_action::exit);
}

TEST(InputManager, ProviderQueriesArePulledOnStartupAndRequery) {
    static constinit auto pipeline = context | io_manager | input_manager;
    auto&                 io       = pipeline.mod<basic_io_manager>();
    auto&                 im       = pipeline.mod<basic_input_manager>();
    io.clear();

    // A provider that hands its query over on demand and counts pulls.
    struct counting_provider {
        owned_query                 q;
        mutable int                 calls = 0;
        std::array<device_query, 1> views{};

        counting_provider() noexcept {
            q.set(query + attr::input_subsystem + attr::event_sysname);
        }

        [[nodiscard]] std::span<device_query const> queries() noexcept {
            ++calls;
            views[0] = q;
            return views;
        }
    };

    counting_provider provider;

    // Registering the same provider twice must be a no-op (dedupe by address).
    im.add_query_provider(provider_handle(provider));
    im.add_query_provider(provider_handle(provider));

    EXPECT_EQ(pipeline(start), context_action::next);
    // Each startup phase pulls every provider once: monitor match + the two
    // enumerate passes (match-all, then per-query device filtering).
    EXPECT_EQ(provider.calls, 3) << "Each startup pass must pull every registered provider.";

    // Re-asking pulls the fresh queries again (monitor filter + two passes).
    im.requery();
    EXPECT_EQ(provider.calls, 6) << "requery() must re-pull every registered provider.";

    if (im.devices().empty()) {
        GTEST_SKIP() << "No matching input devices are present on this system.";
    }
    for (auto const& dev : im.devices()) {
        EXPECT_TRUE(dev.is_ok());
    }
}

TEST(InputManager, ManualAdditionsAreStoredButNotRediscovered) {
    basic_input_manager im;

    im.add(evdev::invalid(evdev_status::success));
    im.add(evdev::invalid(evdev_status::success));
    EXPECT_EQ(std::ranges::distance(im.devices()), 2);

    // Manual devices have no udev identity; a hotplug notification for an
    // unrelated device must leave them untouched.
    EXPECT_EQ(im(io_fd{.fd = 12'345}), context_action::next);
    EXPECT_EQ(std::ranges::distance(im.devices()), 2);
}

TEST(InputManager, AddBroadcastsDeviceConnectedOnce) {
    static constinit auto pipeline = context | io_manager | input_manager | change_counter;

    devices_changed_count  = 0;
    device_connected_count = 0;

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto&      im     = pipeline.mod<basic_input_manager>();
    auto const before = std::ranges::distance(im.devices());

    im.add(evdev::invalid(evdev_status::success));

    // Merged push events: one device_connected (value 1), no separate bulk
    // devices_changed for a single add.
    EXPECT_EQ(devices_changed_count, 0) << "add() must not broadcast a separate bulk devices_changed.";
    EXPECT_EQ(device_connected_count, 1) << "add() must broadcast device_connected once.";
    EXPECT_EQ(std::ranges::distance(im.devices()), before + 1);

    // Without a bound scope the same call is a silent list update.
    devices_changed_count  = 0;
    device_connected_count = 0;
    {
        auto const outer = dynamic_context.exchange(nullptr);
        im.add(evdev::invalid(evdev_status::success));
        dynamic_context.exchange(outer);
    }
    EXPECT_EQ(devices_changed_count, 0) << "Unbound add() must not broadcast.";
    EXPECT_EQ(device_connected_count, 0) << "Unbound add() must not broadcast.";
}

TEST(InputManager, TrackedDevicesReturnsSnapshot) {
    static constinit auto pipeline = context | io_manager | input_manager;

    dynamic_scope scope{dynamic_context, pipeline};
    EXPECT_EQ(pipeline(start), context_action::next);

    auto snap = tracked_devices(pipeline);
    EXPECT_TRUE(snap);
    EXPECT_EQ(snap.action(), context_action::next);
    EXPECT_EQ(snap.size(), static_cast<std::size_t>(std::ranges::distance(pipeline.mod<basic_input_manager>().devices())));
    for (evdev* dev : snap) {
        ASSERT_NE(dev, nullptr);
    }
}

TEST(InputManager, UnexpectedCallbackFdIsIgnoredSafely) {
    basic_input_manager im;

    EXPECT_EQ(im(io_fd{.fd = -1}), context_action::next);
    EXPECT_EQ(im(io_fd{.fd = 424'242}), context_action::next);
}

TEST(InputManager, UnknownFdOnStartedManagerIsIgnored) {
    auto& io = manager();
    auto& im = input_mgr();
    io.clear();

    EXPECT_EQ(input_pipeline(start), context_action::next);
    EXPECT_EQ(im(io_fd{.fd = 424'242}), context_action::next);
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
    auto&                 io               = hotplug_pipeline.mod<basic_io_manager>();
    auto&                 im               = hotplug_pipeline.mod<basic_input_manager>();

    // Scope by a unique name so background udev traffic (pen2mice,
    // key-sounds, leftover virtual devices) can never enter `im.devices()`
    // and shift the counts under us.
    test_query_provider provider(query + attr::input_subsystem + attr::event_sysname + attr::name["Foresight Hotplug AddRemove*"]);
    im.add_query_provider(provider_handle(provider));
    if (hotplug_pipeline(start) != context_action::next) {
        GTEST_SKIP() << "Cannot start the pipeline.";
    }

    // Drain any stale udev events left over from a prior run before sampling.
    pump_for(io, 100);
    auto const before = std::ranges::distance(im.devices());

    basic_uinput uin;
    uin.set_device(LIBEVDEV_UINPUT_OPEN_MANAGED, "Foresight Hotplug AddRemove");
    if (!uin.is_ok()) {
        GTEST_SKIP() << "Cannot create a virtual uinput device.";
    }
    if (!test::wait_for_openable(uin.devnode(), 3000)) {
        uin.close();
        GTEST_SKIP() << "The virtual device node was never openable.";
    }

    auto const our_sysname = test::sysname_of(uin.devnode());

    // Wait for the pipeline to observe *our* device, not merely for any udev
    // event (the old probe fired for unrelated input devices too).
    EXPECT_TRUE(pump_until(io,
                           5000,
                           [&] {
                               return has_sysname(im, our_sysname);
                           }))
      << "Hotplug add was not registered.";
    EXPECT_EQ(std::ranges::distance(im.devices()), before + 1) << "Exactly our device must be tracked after add.";

    uin.close();

    EXPECT_TRUE(pump_until(io,
                           5000,
                           [&] {
                               return !has_sysname(im, our_sysname);
                           }))
      << "Hotplug remove was not registered.";
    EXPECT_EQ(std::ranges::distance(im.devices()), before) << "Device list must return to its pre-add size.";
}

TEST(InputManager, OwnedDeviceIsNotReaddedByHotplug) {
    if (verify_access_to_uinput() != uinput_access_result::available) {
        GTEST_SKIP() << "No /dev/uinput access.";
    }
    udev_queue queue(udev::instance().native());
    if (!queue.is_active()) {
        GTEST_SKIP() << "udev daemon is not active.";
    }

    static constinit auto hotplug_pipeline = context | io_manager | input_manager;
    auto&                 io               = hotplug_pipeline.mod<basic_io_manager>();
    auto&                 im               = hotplug_pipeline.mod<basic_input_manager>();

    // Unique names isolate us from background udev traffic entirely.
    test_query_provider provider(query + attr::input_subsystem + attr::event_sysname + attr::name["Foresight Hotplug Owned*"]);
    im.add_query_provider(provider_handle(provider));
    if (hotplug_pipeline(start) != context_action::next) {
        GTEST_SKIP() << "Cannot start the pipeline.";
    }

    pump_for(io, 100);
    auto const before = std::ranges::distance(im.devices());

    // Probe filtered to our devices' sysnames so unrelated input events
    // (pen2mice / key-sounds churn) cannot satisfy the waits below.
    udev_monitor probe;
    probe.match_device("input");
    probe.enable();

    // A device we own: must never be re-enumerated even when udev reports it.
    basic_uinput owned_uin;
    owned_uin.set_device(LIBEVDEV_UINPUT_OPEN_MANAGED, "Foresight Hotplug Owned");
    if (!owned_uin.is_ok()) {
        GTEST_SKIP() << "Cannot create a virtual uinput device.";
    }
    if (!test::wait_for_openable(owned_uin.devnode(), 3000)) {
        owned_uin.close();
        GTEST_SKIP() << "The owned device node was never openable.";
    }
    auto const owned_sysname = test::sysname_of(owned_uin.devnode());
    im.own_device(owned_uin.devnode());

    if (!wait_for_probe_sysname(probe, owned_sysname, 5000)) {
        owned_uin.close();
        GTEST_SKIP() << "udev did not deliver the owned add event.";
    }
    // Give the pipeline a moment to drain the event it just received.
    pump_for(io, 200);
    EXPECT_FALSE(has_sysname(im, owned_sysname)) << "An owned (self-created) device must not be enumerated back in.";
    EXPECT_EQ(std::ranges::distance(im.devices()), before) << "Owned device must leave the tracked list unchanged.";

    // A foreign device must still be picked up by hotplug. Same name prefix
    // so it matches the scoped query above.
    basic_uinput foreign_uin;
    foreign_uin.set_device(LIBEVDEV_UINPUT_OPEN_MANAGED, "Foresight Hotplug Owned Foreign");
    if (!foreign_uin.is_ok()) {
        owned_uin.close();
        GTEST_SKIP() << "Cannot create a foreign virtual uinput device.";
    }
    if (!test::wait_for_openable(foreign_uin.devnode(), 3000)) {
        owned_uin.close();
        foreign_uin.close();
        GTEST_SKIP() << "The foreign device node was never openable.";
    }
    auto const foreign_sysname = test::sysname_of(foreign_uin.devnode());

    EXPECT_TRUE(pump_until(io,
                           5000,
                           [&] {
                               return has_sysname(im, foreign_sysname);
                           }))
      << "A foreign (unowned) device must still be enumerated by hotplug.";
    EXPECT_EQ(std::ranges::distance(im.devices()), before + 1) << "Only the foreign device may be added.";
    EXPECT_FALSE(has_sysname(im, owned_sysname)) << "Owned device must stay absent after foreign add.";

    owned_uin.close();
    foreign_uin.close();
}

TEST(InputManager, CapsQueryPrefersBestMatchingDevice) {
    if (verify_access_to_uinput() != uinput_access_result::available) {
        GTEST_SKIP() << "No /dev/uinput access.";
    }
    udev_queue queue(udev::instance().native());
    if (!queue.is_active()) {
        GTEST_SKIP() << "udev daemon is not active.";
    }

    // A base keyboard we can clone and rename. The clone names are unique so
    // the query below can never pick up unrelated devices.
    basic_uinput base;
    if (!base(caps::keyboard, start)) {
        GTEST_SKIP() << "Cannot create a base virtual uinput keyboard.";
    }
    if (!test::wait_for_openable(base.devnode(), 3000)) {
        base.close();
        GTEST_SKIP() << "The base device node was never openable.";
    }
    evdev base_src{std::filesystem::path{base.devnode()}};
    ASSERT_TRUE(base_src.is_ok());

    // Two keyboards with identical keys, differing only in keyboard LEDs. The
    // LED-less clone must lose to the full one under a caps query.
    auto full_src = clone_device(base_src);
    full_src.device_name("Foresight Test Full Keyboard");
    basic_uinput full_kbd;
    full_kbd.set_device(full_src);

    auto                              partial_src = clone_device(base_src);
    std::array<dev_cap_view, 1> const no_leds{
      dev_cap_view{.type = caps::keyboard_leds.type, .codes = caps::keyboard_leds.codes}
    };
    partial_src.disable_caps(no_leds);
    partial_src.device_name("Foresight Test Partial Keyboard");
    basic_uinput partial_kbd;
    partial_kbd.set_device(partial_src);

    if (!full_kbd.is_ok() || !partial_kbd.is_ok()) {
        base.close();
        GTEST_SKIP() << "Cannot create the virtual keyboards.";
    }
    if (!test::wait_for_openable(full_kbd.devnode(), 3000) || !test::wait_for_openable(partial_kbd.devnode(), 3000)) {
        base.close();
        full_kbd.close();
        partial_kbd.close();
        GTEST_SKIP() << "A virtual keyboard node was never openable.";
    }

    static constinit auto best_match_pipeline = context | io_manager | input_manager;
    auto&                 im                  = best_match_pipeline.mod<basic_input_manager>();

    test_query_provider provider((query + attr::input_subsystem + attr::event_sysname + attr::name["Foresight Test*"]) | caps::keyboard);
    im.add_query_provider(provider_handle(provider));
    if (best_match_pipeline(start) != context_action::next) {
        base.close();
        full_kbd.close();
        partial_kbd.close();
        GTEST_SKIP() << "Cannot start the pipeline.";
    }

    // Only one device may be taken, and it must be the one reporting the full
    // keyboard caps (100% match) rather than the LED-less clone.
    auto const opened = im.devices();
    ASSERT_EQ(std::ranges::distance(opened), 1) << "Only the best matching device should be enumerated.";
    evdev const full_node{std::filesystem::path{full_kbd.devnode()}};
    ASSERT_TRUE(full_node.is_ok());
    EXPECT_EQ(fs8::device_sysname(opened.front()), fs8::device_sysname(full_node))
      << "The device with the highest caps score must be chosen.";

    base.close();
    full_kbd.close();
    partial_kbd.close();
}

TEST(InputManager, CapsScoringPrefersFullKeyboardOverPartial) {
    // Synthetic: test match_caps scoring without real devices.
    // A device with all requested capabilities must score higher than one missing some.
    // Use a custom dev_caps_view for a small, self-contained query.
    static constexpr std::uint16_t led_codes[] = {LED_NUML, LED_CAPSL, LED_SCROLLL};
    dev_cap_view const             test_query{.type = EV_LED, .codes = led_codes};

    // Full device: has all keyboard LEDs.
    libevdev* const full_ptr = libevdev_new();
    ASSERT_NE(full_ptr, nullptr);
    libevdev_enable_event_type(full_ptr, EV_LED);
    libevdev_enable_event_code(full_ptr, EV_LED, LED_NUML, nullptr);
    libevdev_enable_event_code(full_ptr, EV_LED, LED_CAPSL, nullptr);
    libevdev_enable_event_code(full_ptr, EV_LED, LED_SCROLLL, nullptr);
    evdev full_dev(full_ptr, evdev_status::success);

    // Partial device: has only one LED.
    libevdev* const partial_ptr = libevdev_new();
    ASSERT_NE(partial_ptr, nullptr);
    libevdev_enable_event_type(partial_ptr, EV_LED);
    libevdev_enable_event_code(partial_ptr, EV_LED, LED_NUML, nullptr);
    evdev partial_dev(partial_ptr, evdev_status::success);

    std::span<dev_cap_view const, 1> const test_caps{&test_query, 1};
    auto const                             full_score    = full_dev.match_caps(test_caps);
    auto const                             partial_score = partial_dev.match_caps(test_caps);

    EXPECT_EQ(full_score, 100) << "A full device must match at 100%.";
    EXPECT_GT(full_score, partial_score) << "The full device must score higher than the partial one.";
}
