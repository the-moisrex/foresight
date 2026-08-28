// Created by moisrex on 8/28/26.

module;
#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
export module fs8.mods:key_sync_lock;
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
    constexpr struct [[nodiscard]] basic_key_sync_lock : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        /// Release all held keys on a device by sending EV_KEY release events.
        static void release_all_keys(evdev& dev) noexcept {
            if (!dev.has_event_type(EV_KEY)) {
                return;
            }
            std::array<std::uint8_t, key_bitmap_bytes> bitmap{};
            if (!query_key_state(dev, bitmap)) [[unlikely]] {
                return;
            }
            for (std::size_t i = 0; i < key_bitmap_bytes; ++i) {
                if (bitmap[i] == 0) [[likely]] {
                    continue;
                }
                for (int bit = 0; bit < 8; ++bit) {
                    if (bitmap[i] & (1u << bit)) {
                        auto const       code = static_cast<std::uint16_t>(i * 8 + bit);
                        event_type const event{EV_KEY, code, 0};
                        log("Releasing key {} for device: {}", event.code_name(), dev.device_name());
                        auto const state = dev.grab();
                        if (state == grab_state::grabbing) {
                            log("  ungrabbing it.");
                            dev.grab_input(false);
                        }
                        if (!dev.send_event(event.native()) || !dev.send_event(syn().native())) [[unlikely]] {
                            log("  Failed to release the key.");
                        }
                        if (state == grab_state::grabbing) {
                            log("  re-grabbing it.");
                            dev.grab_input(true);
                        }
                    }
                }
            }
        }

        /// Register a device-change listener and check already-enumerated devices.
        template <ContextWith<basic_input_manager> CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            using enum context_action;
            basic_input_manager& mgr = ctx.mod(input_manager);

            for (evdev& dev : mgr.devices()) {
                if (dev.has_event_type(EV_KEY)) {
                    release_all_keys(dev);
                }
            }

            mgr.add_device_change_listener({
              .identity = this,
              .invoke =
                [&mgr](device_id const id, device_change const change) noexcept {
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
    } key_sync_lock;

} // namespace fs8
