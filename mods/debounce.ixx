// Created by moisrex on 8/18/26.

module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <utility>
export module fs8.mods:debounce;
import fs8.context;
import fs8.traits;

export namespace fs8 {

    /// How a debounce drops events.
    enum struct [[nodiscard]] debounce_mode : std::uint8_t {
        click, ///< buttons/keys: drop a second press within the window and its matching release.
        event, ///< any code: drop every event that lands within the window of the previous one.
    };

    /**
     * Debounce events: drop events that arrive too soon after a previous event of
     * the same code. Used to clean up faulty hardware — double-clicking mice,
     * bouncing keyboard switches, noisy axes or scroll wheels.
     *
     * In `click` mode (default) a press that lands within `time_threshold` of the
     * previous press of the same code is dropped together with its matching
     * release, so the key/button state stays consistent. For non-EV_KEY codes
     * `click` mode degrades to `event` mode (there is no press/release pair).
     *
     * In `event` mode every event of a tracked code arriving within the window of
     * the previous one is dropped, regardless of value.
     */
    template <std::size_t N>
    struct [[nodiscard]] basic_debounce : consteval_copyable {
        using consteval_copyable::consteval_copyable;
        using msec_type = std::chrono::microseconds;

        static constexpr msec_type default_time_threshold = std::chrono::milliseconds(30);

      private:
        std::array<event_code, N> codes{};
        std::array<msec_type, N>  last_event{};
        std::array<bool, N>       swallow_release{};
        std::array<bool, N>       has_event{};
        msec_type                 time_threshold{default_time_threshold};
        debounce_mode             mode{debounce_mode::click};

        template <typename T>
        static constexpr event_code to_code(T const inp_code) noexcept {
            if constexpr (std::convertible_to<T, event_code>) {
                return inp_code;
            } else {
                return event_code{.type = EV_KEY, .code = static_cast<event_type::code_type>(inp_code)};
            }
        }

      public:
        constexpr explicit basic_debounce(std::array<event_code, N> const inp_codes,
                                          msec_type const                 inp_time_threshold = default_time_threshold) noexcept
          : codes{inp_codes},
            time_threshold{inp_time_threshold} {}

        constexpr explicit basic_debounce(std::array<event_code, N> const inp_codes, debounce_mode const inp_mode) noexcept
          : codes{inp_codes},
            mode{inp_mode} {}

        constexpr explicit basic_debounce(event_code const inp_code, msec_type const inp_time_threshold = default_time_threshold) noexcept
          : codes{
              event_code{.type = inp_code.type, .code = inp_code.code}
        },
            time_threshold{inp_time_threshold} {}

        constexpr explicit basic_debounce(event_code const inp_code, debounce_mode const inp_mode) noexcept
          : codes{
              event_code{.type = inp_code.type, .code = inp_code.code}
        },
            mode{inp_mode} {}

        template <typename CodeT>
            requires std::convertible_to<CodeT, event_type::code_type>
        consteval basic_debounce<1> operator[](CodeT const     inp_code,
                                               msec_type const inp_time_threshold = default_time_threshold) const noexcept {
            return basic_debounce<1>{
              event_code{.type = EV_KEY, .code = static_cast<event_type::code_type>(inp_code)},
              inp_time_threshold,
            };
        }

        consteval basic_debounce<1> operator[](event_code const inp_code,
                                               msec_type const  inp_time_threshold = default_time_threshold) const noexcept {
            return basic_debounce<1>{inp_code, inp_time_threshold};
        }

        template <typename... T>
            requires(sizeof...(T) > 1 && (... && (std::convertible_to<T, event_code> || std::convertible_to<T, event_type::code_type>) ))
        consteval auto operator[](T... inp_codes) const noexcept {
            return basic_debounce<sizeof...(T)>{std::array<event_code, sizeof...(T)>{to_code(inp_codes)...}};
        }

        /// Compile-time switch to `click` mode (the default).
        consteval auto click() const noexcept {
            auto result = *this;
            result.mode = debounce_mode::click;
            return result;
        }

        /// Compile-time switch to `event` mode: drop any event within the window.
        consteval auto event() const noexcept {
            auto result = *this;
            result.mode = debounce_mode::event;
            return result;
        }

        /// Change the debounce window at runtime (e.g. from app args).
        void set_time_threshold(msec_type const inp_time_threshold) noexcept {
            time_threshold = inp_time_threshold;
        }

        /// Change the debounce mode at runtime (e.g. from app args).
        void set_mode(debounce_mode const inp_mode) noexcept {
            mode = inp_mode;
        }

        /// Replace the tracked codes at runtime (e.g. from app args). Up to the
        /// first `N` codes are used; remaining slots stay unmatchable. Tracking
        /// state is reset so a changed code list starts fresh.
        void set_codes(std::span<event_code const> const inp_codes) noexcept {
            codes           = {};
            last_event      = {};
            swallow_release = {};
            has_event       = {};
            std::copy_n(inp_codes.begin(), std::min(inp_codes.size(), codes.size()), codes.begin());
        }

        context_action operator()(event_type const& event) noexcept {
            using enum context_action;

            std::size_t found = N;
            for (std::size_t i = 0; i < N; ++i) {
                if (event.is_of(codes[i])) {
                    found = i;
                    break;
                }
            }
            if (found == N) {
                return next;
            }

            auto const now = event.micro_time();

            // `click` mode only has a press/release pair for EV_KEY codes; for
            // anything else it degrades to `event` mode (no release to swallow).
            bool const click_mode = mode == debounce_mode::click && event.type() == EV_KEY;
            if (click_mode) {
                switch (event.value()) {
                    case 0: // release
                        if (std::exchange(swallow_release[found], false)) [[unlikely]] {
                            return ignore_event;
                        }
                        break;
                    case 1: { // press
                        if (has_event[found] && now - last_event[found] < time_threshold) [[unlikely]] {
                            swallow_release[found] = true;
                            return ignore_event;
                        }
                        has_event[found]  = true;
                        last_event[found] = now;
                        break;
                    }
                    default: break; // repeats (value 2) and other values pass through
                }
            } else {
                if (has_event[found] && now - last_event[found] < time_threshold) [[unlikely]] {
                    return ignore_event;
                }
                has_event[found]  = true;
                last_event[found] = now;
            }

            return next;
        }
    };

    constexpr basic_debounce<0> debounce;

} // namespace fs8
