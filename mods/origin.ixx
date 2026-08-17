// Created by moisrex on 8/17/26.

module;
#include <array>
#include <concepts>
#include <utility>
export module fs8.mods.origin;
import fs8.context;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    /// True when the event was read from a real kernel input device.
    constexpr struct [[nodiscard]] basic_from_device {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.origin() == event_origin::device;
        }
    } from_device;

    /// True when the event was read from stdin (redirect mode).
    constexpr struct [[nodiscard]] basic_from_stdin {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.origin() == event_origin::stdin;
        }
    } from_stdin;

    /// True when the event was synthesized by this pipeline, or read back from
    /// a uinput device that this process created.
    constexpr struct [[nodiscard]] basic_self_emitted {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.origin() == event_origin::self;
        }
    } self_emitted;

    /// True when the event came from another process's foresight virtual
    /// device (its phys is "foresight:...").
    constexpr struct [[nodiscard]] basic_from_chained {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.origin() == event_origin::chained;
        }
    } from_chained;

    /// Ignore events whose origin is one of the listed origins.
    template <std::size_t N>
    struct [[nodiscard]] basic_ignore_origin : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<event_origin, N> origins{};

      public:
        explicit constexpr basic_ignore_origin(std::array<event_origin, N> const inp_origins) noexcept : origins{inp_origins} {}

        template <std::size_t NN>
        consteval basic_ignore_origin operator[](std::array<event_origin, NN> const inp_origins) const noexcept {
            return basic_ignore_origin<NN>{inp_origins};
        }

        template <std::size_t NN>
        consteval auto operator[](event_origin (&&inp_origins)[NN]) const noexcept {
            return basic_ignore_origin<NN>{std::to_array(std::move(inp_origins))};
        }

        template <typename... T>
            requires((std::convertible_to<T, event_origin> && ...))
        consteval auto operator[](T... inp_origins) const noexcept {
            return basic_ignore_origin<sizeof...(T)>{std::array<event_origin, sizeof...(T)>{static_cast<event_origin>(inp_origins)...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            using enum context_action;
            for (event_origin const origin : origins) {
                if (event.origin() == origin) {
                    return ignore_event;
                }
            }
            return next;
        }
    };

    constexpr basic_ignore_origin<0> ignore_origin;

    /// Convenience: drop events synthesized by this pipeline / read from our
    /// own uinput devices.
    constexpr basic_ignore_origin<1> ignore_self{std::array{event_origin::self}};

} // namespace fs8
