// Created by moisrex on 8/17/26.

module;
#include <array>
#include <concepts>
#include <cstdint>
#include <utility>
export module fs8.mods:device;
import fs8.context;
import fs8.event;
import fs8.traits;
import :input_manager;
import :inout;
import :scheduler;
import :idle_detector;

export namespace fs8 {

    /// True when the event was read from any device node (a real device, our
    /// own uinput device, or another process's foresight virtual device).
    constexpr struct [[nodiscard]] basic_from_device {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            auto const src = event.source();
            // todo: this is not a clean implemenation:
            return src != source_id_none && src != sid(from_input) && src != sid(scheduler);
        }
    } from_device;

    /// True when the event was read from stdin (redirect mode).
    constexpr struct [[nodiscard]] basic_from_stdin {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.source() == sid(from_input);
        }
    } from_stdin;

    /// True when the event was synthesized by this pipeline (an emitter or a
    /// fork). Events read back from this process's own uinput devices carry a
    /// device id instead; check `input_manager::is_owned` for those.
    constexpr struct [[nodiscard]] basic_self_emitted {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.source() == source_id_none;
        }
    } self_emitted;

    /// True when the event came from another process's foresight virtual
    /// device (its phys is "foresight:...").
    constexpr struct [[nodiscard]] basic_from_chained {
        template <Context ContextT>
        [[nodiscard]] constexpr bool operator()(ContextT& ctx) const noexcept {
            return ctx.mod(input_manager).is_chained(ctx.event().source());
        }
    } from_chained;

    /// Predicate: `device_is(id)(event)` is true when `event` came from `id`.
    struct [[nodiscard]] basic_device_is {
        std::uint32_t id = source_id_none;

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.source() == id;
        }
    };

    constexpr struct [[nodiscard]] basic_device_id_of {
        [[nodiscard]] constexpr basic_device_is operator()(std::uint32_t const id) const noexcept {
            return basic_device_is{id};
        }
    } device_is;

    /// Ignore events whose source is one of the listed ids.
    template <std::size_t N>
    struct [[nodiscard]] basic_drop_origin : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<std::uint32_t, N> origins{};

      public:
        explicit constexpr basic_drop_origin(std::array<std::uint32_t, N> const inp_origins) noexcept : origins{inp_origins} {}

        template <std::size_t NN>
        consteval basic_drop_origin operator[](std::array<std::uint32_t, NN> const inp_origins) const noexcept {
            return basic_drop_origin<NN>{inp_origins};
        }

        template <std::size_t NN>
        consteval auto operator[](std::uint32_t (&&inp_origins)[NN]) const noexcept {
            return basic_drop_origin<NN>{std::to_array(std::move(inp_origins))};
        }

        template <typename... T>
            requires((std::convertible_to<T, std::uint32_t> && ...))
        consteval auto operator[](T... inp_origins) const noexcept {
            return basic_drop_origin<sizeof...(T)>{std::array<std::uint32_t, sizeof...(T)>{static_cast<std::uint32_t>(inp_origins)...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            using enum context_action;
            for (std::uint32_t const origin : origins) {
                if (event.source() == origin) {
                    return drop_event;
                }
            }
            return next;
        }
    };

    constexpr basic_drop_origin<0> drop_origin;

    /// Convenience: drop synthesized events (`source_id_none`) and events read
    /// back from this process's own uinput devices.
    constexpr struct [[nodiscard]] basic_drop_self {
        context_action operator()(Context auto& ctx) const noexcept {
            using enum context_action;
            auto const& event = ctx.event();
            if (event.source() == source_id_none) [[unlikely]] {
                return drop_event;
            }
            if (ctx.mod(input_manager).is_owned(event.source())) [[unlikely]] {
                return drop_event;
            }
            return next;
        }
    } drop_self;

    /// Drop events from devices created by this pipeline's own uinput mods.
    /// Unlike `drop_self`, this does NOT drop events synthesized by emit/fork
    /// (`source_id_none`).
    constexpr struct [[nodiscard]] basic_drop_owned {
        context_action operator()(Context auto& ctx) const noexcept {
            using enum context_action;
            if (ctx.mod(input_manager).is_owned(ctx.event().source())) [[unlikely]] {
                return drop_event;
            }
            return next;
        }
    } drop_owned;

    /// Drop events synthesized by this pipeline (emit, fork_emit, etc.).
    /// Unlike `drop_self`, this does NOT drop events from owned uinput
    /// devices.
    constexpr struct [[nodiscard]] basic_drop_emitted {
        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.source() != source_id_none;
        }
    } drop_emitted;

    /// Drop events coming from the listed devices.
    template <std::size_t N>
    struct [[nodiscard]] basic_drop_device : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<std::uint32_t, N> devices{};

      public:
        explicit constexpr basic_drop_device(std::array<std::uint32_t, N> const inp_devices) noexcept : devices{inp_devices} {}

        template <std::size_t NN>
        consteval auto operator[](std::uint32_t (&&inp_devices)[NN]) const noexcept {
            return basic_drop_device<NN>{std::to_array(std::move(inp_devices))};
        }

        template <typename... T>
            requires((std::convertible_to<T, std::uint32_t> && ...))
        consteval auto operator[](T... inp_devices) const noexcept {
            return basic_drop_device<sizeof...(T)>{std::array<std::uint32_t, sizeof...(T)>{static_cast<std::uint32_t>(inp_devices)...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            using enum context_action;
            for (std::uint32_t const device : devices) {
                if (event.source() == device) {
                    return drop_event;
                }
            }
            return next;
        }
    };

    constexpr basic_drop_device<0> drop_device;

    /// Only pass events coming from the listed devices.
    template <std::size_t N>
    struct [[nodiscard]] basic_only_device : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::array<std::uint32_t, N> devices{};

      public:
        explicit constexpr basic_only_device(std::array<std::uint32_t, N> const inp_devices) noexcept : devices{inp_devices} {}

        template <std::size_t NN>
        consteval auto operator[](std::uint32_t (&&inp_devices)[NN]) const noexcept {
            return basic_only_device<NN>{std::to_array(std::move(inp_devices))};
        }

        template <typename... T>
            requires((std::convertible_to<T, std::uint32_t> && ...))
        consteval auto operator[](T... inp_devices) const noexcept {
            return basic_only_device<sizeof...(T)>{std::array<std::uint32_t, sizeof...(T)>{static_cast<std::uint32_t>(inp_devices)...}};
        }

        context_action operator()(event_type const& event) const noexcept {
            using enum context_action;
            for (std::uint32_t const device : devices) {
                if (event.source() == device) {
                    return next;
                }
            }
            return drop_event;
        }
    };

    constexpr basic_only_device<0> only_device;

} // namespace fs8
