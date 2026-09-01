// Created by moisrex on 8/28/26.

module;
#include <cstdint>
#include <linux/input-event-codes.h>
export module fs8.mods:startup_key_releases;
import fs8.event;
import fs8.context;
import fs8.traits;
import fs8.log;
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

        /// Register a device-change listener and check already-enumerated devices.
        template <ContextWith<basic_input_manager> CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;
            if (tag.code != start.code) {
                return drop_event;
            }
            basic_input_manager& mgr = ctx.mod(input_manager);

            for (evdev& dev : mgr.devices()) {
                if (dev.has_event_type(EV_KEY)) {
                    release_all_keys(dev);
                }
            }

            mgr.add_device_change_listener({
              .identity = this,
              .invoke =
                [&mgr](std::uint32_t const id, device_change const change) noexcept {
                    if (change != device_change::connected) {
                        return;
                    }

                    evdev* dev = mgr.device_of(id);
                    if (dev == nullptr || !dev->has_event_type(EV_KEY)) {
                        return;
                    }

                    release_all_keys(*dev);
                },
            });
            return next;
        }

        constexpr void operator()() const noexcept {
            // do nothing
        }
    } startup_key_releases;

} // namespace fs8
