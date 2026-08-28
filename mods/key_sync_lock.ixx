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
    /// On device connect, queries the device's EVIOCGKEY bitmap.  If any
    /// key is held down, returns idle to restart the pipeline — repeats
    /// until all keys are released, then proceeds.
    ///
    /// This is useful when the pipeline is launched by a key press (e.g.
    /// holding Enter to start pen2mice): the OS has already seen the press
    /// from the physical keyboard, so the pipeline must wait for the user
    /// to release before it can correctly track key state.
    constexpr struct [[nodiscard]] basic_key_sync_lock : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        /// Check a single device for held keys.  Returns true if any key is pressed.
        static bool has_held_keys(evdev const& dev) noexcept {
            if (!dev.has_event_type(EV_KEY)) {
                return false;
            }
            std::array<std::uint8_t, key_bitmap_bytes> bitmap{};
            if (!query_key_state(dev, bitmap)) [[unlikely]] {
                return false;
            }
            for (auto const b : bitmap) {
                if (b != 0) [[unlikely]] {
                    return true;
                }
            }
            return false;
        }

        /// Register a device-change listener and check already-enumerated devices.
        template <ContextWith<basic_input_manager> CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            using enum context_action;
            basic_input_manager& mgr = ctx.mod(input_manager);
            // Register for future device connections (hotplug).

            mgr.add_device_change_listener({
              .identity = this,
              .invoke =
                [this, &mgr](device_id const id, device_change const change) noexcept {
                    if (change != device_change::connected) {
                        return;
                    }

                    auto const dev = mgr.device_of(id);
                    if (dev == nullptr) [[unlikely]] {
                        return;
                    }
                    if (has_held_keys(*dev)) {
                        log("Release all of the keys for device: {}", dev->device_name());
                        goto_idle = true;
                    }
                },
            });
            return next;
        }

        context_action operator()(event_type const&) const noexcept {
            using enum context_action;
            return !goto_idle ? next : idle;
        }

      private:
        bool goto_idle = false;
    } key_sync_lock;

} // namespace fs8
