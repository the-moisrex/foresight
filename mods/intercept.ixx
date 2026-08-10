// Created by moisrex on 6/22/24.

module;
#include <array>
#include <concepts>
#include <filesystem>
#include <optional>
#include <ranges>
export module fs8.mods.intercept;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.context;
import fs8.mods.io_manager;
import fs8.mods.input_manager;
import fs8.pimpl;

export namespace fs8 {

    struct input_file_type {
        std::filesystem::path file;
        bool                  grab = false;
    };

    /// Owned copy of a device query: the fields are copied into inline storage so
    /// the consteval pipeline form never holds dangling spans.
    struct [[nodiscard]] owned_device_query {
        std::array<query_term, 16> fields{};
        std::uint8_t               field_count = 0;
        dev_caps_view              caps = +caps::nothing;
        std::uint8_t               caps_support_percentage = 80;
        std::uint8_t               matches_limit = 1;
        bool                       grab = false;
        bool                       fail_on_no_match = false;

        constexpr void set(device_query const& inp_query) noexcept {
            field_count = static_cast<std::uint8_t>(inp_query.fields.size());
            for (std::size_t i = 0; i < field_count; ++i) {
                fields[i] = inp_query.fields[i];
            }
            caps                    = inp_query.caps;
            caps_support_percentage = inp_query.caps_support_percentage;
            matches_limit           = inp_query.matches_limit;
            grab                    = inp_query.grab;
            fail_on_no_match        = inp_query.fail_on_no_match;
        }

        constexpr explicit operator device_query() const noexcept {
            return device_query{
              .fields                  = std::span<query_term const>{fields.data(), field_count},
              .caps                    = caps,
              .caps_support_percentage = caps_support_percentage,
              .matches_limit           = matches_limit,
              .grab                    = grab,
              .fail_on_no_match        = fail_on_no_match};
        }
    };

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

        /// Pipeline form: intercept(keyboard, mouse)
        template <typename... Qs>
            requires(sizeof...(Qs) >= 1 && (std::convertible_to<Qs, device_query> && ...))
        consteval basic_interceptor operator()(Qs... qs) const noexcept {
            std::array<owned_device_query, 16> arr{};
            std::size_t                        index = 0;
            ((arr[index++].set(qs)), ...);
            return basic_interceptor{arr, index};
        }

        /// Runtime additions
        void add(device_query const& q) noexcept; // udev-query based
        void add(evdev&& dev) noexcept;           // manual (find_devices output)

        /// Range of evdev and/or device_query
        template <std::ranges::range R>
        void add(R&& rng) noexcept {
            for (auto&& e : std::forward<R>(rng)) {
                add(std::forward<decltype(e)>(e));
            }
        }

        /// Forward queries/devices to input_manager; ensure it started.
        template <ContextWith<basic_io_manager, basic_input_manager> ContextT>
        context_action operator()(ContextT ctx, start_tag) noexcept {
            return do_start(ctx.mod(input_manager), ctx.mod(io_manager));
        }

        /// io_manager handler: drain a readable device fd into the pending queue.
        context_action operator()(io_fd const& fd) noexcept;

        /// next_event provider: reconcile watches, pop one event, else ignore_event.
        template <ContextWith<basic_io_manager, basic_input_manager> ContextT>
        context_action operator()(ContextT ctx, next_event_tag) noexcept {
            using enum context_action;
            if (auto const ev = do_pop(ctx.mod(input_manager), ctx.mod(io_manager)); ev.has_value()) [[unlikely]] {
                ctx.event(*ev);
                return next;
            }
            return ignore_event;
        }

      private:
        explicit consteval basic_interceptor(std::array<owned_device_query, 16> qs, std::size_t const count) noexcept
          : queries{qs},
            queries_count{count} {}

        context_action            do_start(basic_input_manager& im, basic_io_manager& io) noexcept;
        std::optional<event_type> do_pop(basic_input_manager& im, basic_io_manager& io) noexcept;

        std::array<owned_device_query, 16> queries{}; // consteval-copyable part
        std::size_t                        queries_count = 0;
    } intercept;

    static_assert(Modifier<basic_interceptor>);

} // namespace fs8
