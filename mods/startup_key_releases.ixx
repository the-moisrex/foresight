// Created by moisrex on 8/28/26.

module;
#include <linux/input-event-codes.h>
export module fs8.mods:startup_key_releases;
import fs8.event;
import fs8.context;
import fs8.traits;
import fs8.devices.evdev;
import :input_manager;

export namespace fs8 {

    /// Sync the pipeline with the physical keyboard state at startup.
    /// On device connect, queries the device's EVIOCGKEY bitmap and
    /// releases any held keys by sending EV_KEY release events.
    ///
    /// This is useful when the pipeline is launched by a key press (e.g.
    /// holding Enter to start pen2mice): the OS has already seen the press
    /// from the physical keyboard, so the pipeline must synthetically
    /// release them before it can correctly track key state.
    constexpr struct [[nodiscard]] basic_startup_key_releases : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        /// Seed already-enumerated devices at start and release keys on every
        /// subsequent connect.
        template <Context CtxT>
        context_action operator()(CtxT& ctx, control_event const& event) noexcept {
            using enum context_action;
            switch (event.code) {
                case start.code: {
                    auto snap = tracked_devices(ctx);
                    if (!snap) [[unlikely]] {
                        return snap.action();
                    }
                    for (evdev* dev : snap) {
                        if (dev->has_event_type(EV_KEY)) {
                            release_all_keys(*dev);
                        }
                    }
                    return next;
                }
                case devices_changed.code:
                    if (event == device_connected) {
                        if (auto& dev = payload<device_connected>(event); dev.has_event_type(EV_KEY)) {
                            release_all_keys(dev);
                        }
                        return next;
                    }
                    return drop_event;
                default: return drop_event;
            }
        }

        constexpr void operator()() const noexcept {
            // do nothing
        }
    } startup_key_releases;

} // namespace fs8
