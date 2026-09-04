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

    /**
     * Debounce events: drop events that arrive too soon after a previous event of
     * the same code. Used to clean up faulty hardware — double-clicking mice,
     * bouncing keyboard switches, noisy axes or scroll wheels.
     *
     * For `EV_KEY` codes a press that lands within `time_threshold` of the
     * previous press of the same code is dropped together with its matching
     * release, so the key/button state stays consistent.
     *
     * For any other event type, every event arriving within the window is
     * dropped, regardless of value.
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

        constexpr explicit basic_debounce(event_code const inp_code, msec_type const inp_time_threshold = default_time_threshold) noexcept
          : codes{
              event_code{.type = inp_code.type, .code = inp_code.code}
        },
            time_threshold{inp_time_threshold} {}

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

        /// Change the debounce window at runtime (e.g. from app args).
        void set_time_threshold(msec_type const inp_time_threshold) noexcept {
            time_threshold = inp_time_threshold;
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

            // For EV_KEY codes we pair press/release so that a spurious
            // double-click becomes a clean single click+release.  For anything
            // else there is no press/release pair, so we just drop repeats.
            bool const click_mode = event.type() == EV_KEY;
            if (click_mode) {
                switch (event.value()) {
                    case 0: // release
                        if (std::exchange(swallow_release[found], false)) [[unlikely]] {
                            return drop_event;
                        }
                        break;
                    case 1: { // press
                        if (has_event[found] && now - last_event[found] < time_threshold) [[unlikely]] {
                            swallow_release[found] = true;
                            return drop_event;
                        }
                        has_event[found]  = true;
                        last_event[found] = now;
                        break;
                    }
                    default: break; // repeats (value 2) and other values pass through
                }
            } else {
                if (has_event[found] && now - last_event[found] < time_threshold) [[unlikely]] {
                    return drop_event;
                }
                has_event[found]  = true;
                last_event[found] = now;
            }

            return next;
        }
    };

    constexpr basic_debounce<0> debounce;

} // namespace fs8
