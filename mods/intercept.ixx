// Created by moisrex on 6/22/24.

module;
#include <array>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
export module fs8.mods.intercept;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.context;
import fs8.mods.io_manager;
import fs8.mods.input_manager;
import fs8.pimpl;

export namespace fs8 {

    /**
     * Query-driven event provider.
     *
     * Owns no devices; it forwards queries/devices to `input_manager` and reads
     * ready devices through `io_manager`. Blocking readiness is provided by
     * `io_manager` (the `load_event` provider) and events are delivered as a
     * `next_event` provider.
     */
    constexpr struct [[nodiscard]] basic_interceptor : pimpl_idiom<basic_interceptor> {
        using pimpl_idiom::pimpl_idiom;

        /// Pipeline form: intercept[keyboard, mouse]
        template <typename... Qs>
            requires(sizeof...(Qs) >= 1 && (std::convertible_to<Qs, device_query> && ...))
        consteval basic_interceptor operator[](Qs... qs) const noexcept {
            return basic_interceptor{qs...};
        }

        /// Runtime additions
        void add(device_query const& q) noexcept; // udev-query based
        void add(owned_query const& q) noexcept;  // udev-query based
        void add(evdev&& dev) noexcept;           // manual (find_devices output)

        /// Range of evdev and/or device_query
        template <std::ranges::range R>
        void add(R&& rng) noexcept {
            for (auto&& e : std::forward<R>(rng)) {
                add(std::forward<decltype(e)>(e));
            }
        }

        /// query_provider: hand every live query to the manager as a span over
        /// owned storage (refreshed on each call). The queries stay owned here
        /// so `input_manager` can pull them again via `requery()`.
        [[nodiscard]] std::span<device_query const> queries() noexcept;

        /// Forward queries/devices to input_manager; ensure it started.
        template <Context ContextT>
        context_action operator()(ContextT ctx, start_tag) noexcept {
            return do_start(ctx.mod(input_manager), ctx.mod(io_manager));
        }

        /// io_manager handler: drain a readable device fd into the pending queue.
        context_action operator()(io_fd const& fd) noexcept;

        /// next_event provider: reconcile watches, pop one event, else ignore_event.
        template <Context ContextT>
        context_action operator()(ContextT ctx, next_event_tag) noexcept {
            using enum context_action;
            if (auto const ev = do_pop(ctx.mod(input_manager), ctx.mod(io_manager)); ev.has_value()) [[unlikely]] {
                ctx.event(*ev);
                return next;
            }
            return ignore_event;
        }

      private:
        template <typename... Qs>
            requires(sizeof...(Qs) >= 1 && (std::convertible_to<Qs, device_query> && ...))
        explicit consteval basic_interceptor(Qs... qs) noexcept {
            std::size_t index = 0;
            ((owned_queries[index++].set(qs)), ...);
            queries_count = index;
        }

        context_action            do_start(basic_input_manager& im, basic_io_manager& io) noexcept;
        std::optional<event_type> do_pop(basic_input_manager& im, basic_io_manager& io) noexcept;

        std::array<owned_query, 16>  owned_queries{}; // consteval-copyable part
        std::size_t                  queries_count = 0;
        std::array<device_query, 16> query_cache{};   // span source for queries()
    } intercept;

    static_assert(Modifier<basic_interceptor>);

} // namespace fs8
