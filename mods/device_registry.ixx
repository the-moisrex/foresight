// Created by moisrex on 7/10/26.
module;
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>
export module foresight.mods.device_registry;
import foresight.devices.udev;
import foresight.mods.context;
import foresight.devices.evdev;
import foresight.devices.device_query;

namespace fs8 {

    export constexpr std::uint8_t no_query = std::numeric_limits<std::uint8_t>::max();

    struct [[nodiscard]] evdev_pick {
        evdev        device;
        std::uint8_t query_index = no_query;
    };

    /// Convert a `udev device` into a `evdev device`.
    export constexpr struct [[nodiscard]] basic_to_evdev_pick : std::ranges::range_adaptor_closure<basic_to_evdev_pick> {
        [[nodiscard]] constexpr auto operator()(udev_device_pick const& pick) const noexcept {
            return evdev_pick{.device = evdev{pick.device.subsystem()}, .query_index = pick.query_index};
        }

        template <std::ranges::range Range>
        [[nodiscard]] constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng) | std::views::transform(*this);
        }
    } to_evdev_pick;

    /**
     * Monitor and manage input devices.
     */
    export struct [[nodiscard]] basic_device_registry {
        consteval basic_device_registry() noexcept                                   = default;
        constexpr basic_device_registry(basic_device_registry&&) noexcept            = default;
        constexpr basic_device_registry& operator=(basic_device_registry&&) noexcept = default;
        constexpr ~basic_device_registry() noexcept                                  = default;

        // Custom copy constructor
        constexpr basic_device_registry(basic_device_registry const& other) {
            if consteval {
                assert(other.queries.empty());
                assert(other.devices.empty());
                assert(queries.empty());
                assert(devices.empty());
            } else {
                std::abort();
            }
        }

        // Custom copy assignment
        constexpr basic_device_registry& operator=(basic_device_registry const& other) {
            if consteval {
                assert(other.queries.empty());
                assert(other.devices.empty());
                assert(queries.empty());
                assert(devices.empty());
            } else {
                std::abort();
            }
            return *this;
        }

        /// Add device manually
        void add(evdev&& inp_dev, device_query_snapshot query);

        /// Append new queries and their devices
        template <classify::Classification... Cls>
        void add(device_query<Cls> const&... inp_queries) {
            devices.append_range(all_devices(inp_queries...) | to_evdev_pick);
            (queries.emplace_back(inp_queries), ...);
        }

        /// Initialize monitoring
        context_action operator()(start_tag);

      private:
        std::optional<udev_monitor>        monitor = std::nullopt;
        std::vector<evdev_pick>            devices;
        std::vector<device_query_snapshot> queries;
    };

    export constexpr basic_device_registry device_registry;

} // namespace fs8
