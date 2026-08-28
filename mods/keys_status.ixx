// Created by moisrex on 6/9/25.

module;
#include <array>
#include <bitset>
#include <cassert>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <span>
export module fs8.mods:keys_status;
import fs8.event;
import fs8.context;
import fs8.traits;
import fs8.devices.evdev;
import :input_manager;

export namespace fs8 {

    /**
     * If you need to check if a key is pressed or not, this is what you need to use.
     */
    constexpr struct [[nodiscard]] basic_keys_status : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        std::bitset<KEY_MAX> btns{};

      public:
        [[nodiscard]] bool is_pressed(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_pressed_any(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_released(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_released_any(std::span<code_type const> key_codes) const noexcept;

        template <std::integral... T>
        [[nodiscard]] bool is_pressed(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((btns.test(static_cast<std::size_t>(key_codes)) && ...));
        }

        template <std::integral... T>
        [[nodiscard]] bool is_pressed_any(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((btns.test(static_cast<std::size_t>(key_codes)) || ...));
        }

        template <std::integral... T>
        [[nodiscard]] code_type first_pressed(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            code_type pressed = KEY_MAX;
            std::ignore =
              ((btns.test(static_cast<std::size_t>(key_codes)) && (pressed = static_cast<code_type>(key_codes), true)) && ...);
            return pressed;
        }

        template <std::integral... T>
        [[nodiscard]] bool is_released(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((!btns.test(static_cast<std::size_t>(key_codes)) && ...));
        }

        template <std::integral... T>
        [[nodiscard]] bool is_released_any(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((!btns.test(static_cast<std::size_t>(key_codes)) || ...));
        }

        void release_all(Context auto& ctx) noexcept {
            bool is_any_pressed = false;
            for (code_type code = 0; code < KEY_MAX; ++code) {
                if (btns.test(static_cast<std::size_t>(code))) {
                    ctx.fork_emit(event_type{EV_KEY, code, 0});
                    is_any_pressed = true;
                    btns.reset(static_cast<std::size_t>(code));
                }
            }
            if (is_any_pressed) {
                ctx.fork_emit(syn());
            }
        }

        void operator()(event_type const& event) noexcept;

        /// Seed key state from a device's EVIOCGKEY bitmap.
        void seed_from_device(evdev const& dev) noexcept;

        /// Register a device-change listener to seed key state on connect.
        template <ContextWith<basic_input_manager> CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            using enum context_action;
            ctx.mod(input_manager).add_device_change_listener({
              .identity = this,
              .invoke   = [this, &input_manager = ctx.mod(input_manager)](device_id const id, device_change const change) noexcept {
                  if (change != device_change::connected) {
                      return;
                  }
                  if (auto* dev = input_manager.device_of(id); dev != nullptr) {
                      seed_from_device(*dev);
                  }
              },
            });
            return next;
        }
    } keys_status;

    template <typename ModT = void>
    struct [[nodiscard]] basic_mod_updater {
        void operator()(auto&&, Tag auto) = delete;
        void operator()(Tag auto)         = delete;

        template <typename InpModT>
            requires(!Context<std::remove_cvref_t<InpModT>> && !Tag<std::remove_cvref_t<InpModT>>)
        consteval auto operator[]([[maybe_unused]] InpModT&&) const noexcept {
            return basic_mod_updater<InpModT>{};
        }

        context_action operator()(Context auto& ctx) const noexcept {
            return invoke_mod(ctx.template mod<ModT>(), ctx);
        }
    };

    constexpr basic_mod_updater<> update_mod;

    /**
     * Keeps the track of LEDs
     */
    constexpr struct [[nodiscard]] basic_led_status : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type = event_type::code_type;

      private:
        // this is not wasteful, it's only 11 of them:
        std::array<event_type::value_type, LED_MAX> leds{};

      public:
        [[nodiscard]] bool is_on(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_off(std::span<code_type const> key_codes) const noexcept;

        template <std::integral... T>
        [[nodiscard]] bool is_on(T const... key_codes) const noexcept {
            return ((key_codes < LED_MAX && leds.at(key_codes) != 0) && ...);
        }

        template <std::integral... T>
        [[nodiscard]] bool is_off(T const... key_codes) const noexcept {
            return ((key_codes < LED_MAX && leds.at(static_cast<std::size_t>(key_codes)) == 0) && ...);
        }

        /// Seed the LED state from the hardware device (e.g. the keyboard's
        /// current CapsLock LED) so the mode indicator is correct at startup.
        /// Must run after the devices are open (i.e. after `input_manager`).
        template <ContextWith<basic_input_manager> CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            return seed(ctx);
        }

        /// Find the hardware keyboard (the device with LED_CAPSL) and copy the
        /// current LED values into our local LED state.
        template <Context CtxT>
        context_action seed(CtxT& ctx) noexcept {
            using enum context_action;
            for (evdev const& dev : ctx.mod(input_manager).devices()) {
                if (!dev.has_event_code(EV_LED, LED_CAPSL)) {
                    continue;
                }
                int  value = 0;
                bool found = false;
                for (code_type led = 0; led < LED_MAX; ++led) {
                    if (libevdev_fetch_event_value(dev.device_ptr(), EV_LED, led, &value) == 0) {
                        this->leds.at(led) = static_cast<event_type::value_type>(value);
                        found              = true;
                    }
                }
                if (found) [[likely]] {
                    break;
                }
            }
            return next;
        }

        /// Flip a toggle-key mode by emitting the key's press/release through
        /// the pipeline so the desktop handles the mode change, and mirror it
        /// in the tracked state.
        template <Context CtxT>
        context_action toggle_led(CtxT& ctx, code_type const led_code, code_type const key_code) noexcept {
            using enum context_action;
            this->leds.at(led_code) = this->leds.at(led_code) == 0 ? 1 : 0;
            std::ignore             = ctx.fork_emit(EV_KEY, key_code, 1);
            std::ignore             = ctx.fork_emit(EV_KEY, key_code, 0);
            std::ignore             = ctx.fork_emit(EV_SYN, SYN_REPORT, 0);
            return next;
        }

        /// Set a toggle-key mode: when the requested state differs from the
        /// current one, emit the key's press/release through the pipeline so
        /// the desktop toggles the mode; mirror it in the tracked state.
        template <Context CtxT>
        context_action set_led(CtxT& ctx, code_type const led_code, code_type const key_code, bool const on) noexcept {
            using enum context_action;
            auto const value = on ? 1 : 0;
            if (this->leds.at(led_code) != value) {
                this->leds.at(led_code) = value;
                std::ignore             = ctx.fork_emit(EV_KEY, key_code, 1);
                std::ignore             = ctx.fork_emit(EV_KEY, key_code, 0);
                std::ignore             = ctx.fork_emit(EV_SYN, SYN_REPORT, 0);
            }
            return next;
        }

        /// Flip the CapsLock mode by toggling the (physical) CapsLock LED.
        template <Context CtxT>
        context_action toggle_capslock(CtxT& ctx) noexcept {
            return toggle_led(ctx, LED_CAPSL, KEY_CAPSLOCK);
        }

        /// Set the CapsLock mode.
        template <Context CtxT>
        context_action set_capslock(CtxT& ctx, bool const on) noexcept {
            return set_led(ctx, LED_CAPSL, KEY_CAPSLOCK, on);
        }

        void operator()(event_type const& event) noexcept;
    } led_status;

    /// Flip the CapsLock mode by toggling the (physical) CapsLock LED.
    constexpr struct [[nodiscard]] basic_led_toggle : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        void operator()(Context auto& ctx) const noexcept {
            std::ignore = ctx.mod(led_status).toggle_capslock(ctx);
        }
    } led_toggle;

    /// Turn off a toggle-based mode key (CapsLock, NumLock, ScrollLock): when
    /// the mode is currently on, emit the key's press/release through the
    /// pipeline so the desktop turns it off.
    template <event_type::code_type LedCode, event_type::code_type KeyCode>
    struct [[nodiscard]] basic_toggle_off : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        void operator()(Context auto& ctx) const noexcept {
            std::ignore = ctx.mod(led_status).set_led(ctx, LedCode, KeyCode, false);
        }
    };

    /// Turn the CapsLock mode off.
    constexpr basic_toggle_off<LED_CAPSL, KEY_CAPSLOCK> capslock_off;

    /// Turn the NumLock mode off.
    constexpr basic_toggle_off<LED_NUML, KEY_NUMLOCK> numlock_off;

    /// Turn the ScrollLock mode off.
    constexpr basic_toggle_off<LED_SCROLLL, KEY_SCROLLLOCK> scrolllock_off;

} // namespace fs8
