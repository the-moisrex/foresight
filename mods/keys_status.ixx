// Created by moisrex on 6/9/25.

module;
#include <array>
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
import fs8.log;

export namespace fs8 {

    /**
     * If you need to check if a key is pressed or not, this is what you need to use.
     */
    constexpr struct [[nodiscard]] basic_keys_status : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        // We know this is wasteful, but we don't care :)
        // It's about ~3KiB of storage
        std::array<value_type, KEY_MAX> btns{};

      public:
        [[nodiscard]] bool is_pressed(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_pressed_any(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_released(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_released_any(std::span<code_type const> key_codes) const noexcept;

        template <std::integral... T>
        [[nodiscard]] bool is_pressed(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((btns.at(static_cast<std::size_t>(key_codes)) != 0) && ...);
        }

        template <std::integral... T>
        [[nodiscard]] bool is_pressed_any(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((btns.at(static_cast<std::size_t>(key_codes)) != 0) || ...);
        }

        template <std::integral... T>
        [[nodiscard]] code_type first_pressed(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            code_type pressed = KEY_MAX;
            std::ignore =
              ((btns.at(static_cast<std::size_t>(key_codes)) != 0 && (pressed = static_cast<code_type>(key_codes), true)) && ...);
            return pressed;
        }

        template <std::integral... T>
        [[nodiscard]] bool is_released(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((btns.at(static_cast<std::size_t>(key_codes)) == 0) && ...);
        }

        template <std::integral... T>
        [[nodiscard]] bool is_released_any(T const... key_codes) const noexcept {
            assert(((key_codes < KEY_MAX) && ...));
            return ((btns.at(static_cast<std::size_t>(key_codes)) == 0) || ...);
        }

        void release_all(Context auto& ctx) noexcept {
            bool is_any_pressed = false;
            for (code_type code = 0; code < KEY_MAX; ++code) {
                if (is_pressed(code)) {
                    ctx.fork_emit(event_type{EV_KEY, code, 0});
                    is_any_pressed      = true;
                    this->btns.at(code) = 0;
                }
            }
            if (is_any_pressed) {
                ctx.fork_emit(syn());
            }
        }

        void operator()(event_type const& event) noexcept;
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

        /// Flip the CapsLock mode: emit a KEY_CAPSLOCK press/release through the
        /// pipeline so the desktop handles the mode change, and mirror it in the
        /// tracked state.
        template <Context CtxT>
        context_action toggle_capslock(CtxT& ctx) noexcept {
            using enum context_action;
            // log("LED toggle fired by {} tracked={}", ctx.event().code_name(), this->leds.at(LED_CAPSL) == 0 ? "off" : "on");
            this->leds.at(LED_CAPSL) = static_cast<event_type::value_type>(this->is_off(LED_CAPSL) ? 1 : 0);
            std::ignore              = ctx.fork_emit(EV_KEY, KEY_CAPSLOCK, 1);
            std::ignore              = ctx.fork_emit(EV_KEY, KEY_CAPSLOCK, 0);
            std::ignore              = ctx.fork_emit(EV_SYN, SYN_REPORT, 0);
            return next;
        }

        /// Set the CapsLock mode: when the requested state differs from the
        /// current one, emit a KEY_CAPSLOCK press/release through the pipeline so
        /// the desktop toggles the mode; mirror it in the tracked state.
        template <Context CtxT>
        context_action set_capslock(CtxT& ctx, bool const on) noexcept {
            using enum context_action;
            auto const value = static_cast<event_type::value_type>(on ? 1 : 0);
            if (this->leds.at(LED_CAPSL) != value) {
                this->leds.at(LED_CAPSL) = value;
                std::ignore              = ctx.fork_emit(EV_KEY, KEY_CAPSLOCK, 1);
                std::ignore              = ctx.fork_emit(EV_KEY, KEY_CAPSLOCK, 0);
                std::ignore              = ctx.fork_emit(EV_SYN, SYN_REPORT, 0);
            }
            return next;
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

    /// Turn off the CapsLock mode when the CapsLock key is held (e.g. it is
    /// being used as a scroll modifier); otherwise leave it alone.
    constexpr struct [[nodiscard]] basic_capslock_off : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        void operator()(Context auto& ctx) const noexcept {
            if (ctx.mod(keys_status).is_pressed(KEY_CAPSLOCK)) {
                std::ignore = ctx.mod(led_status).set_capslock(ctx, false);
            }
        }
    } capslock_off;


} // namespace fs8
