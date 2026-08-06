// Created by moisrex on 7/10/26.
module;
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <poll.h>
#include <ranges>
#include <vector>
export module fs8.mods.device_registry;
import fs8.devices.udev;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;

namespace fs8 {


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
                assert(other.devs.empty());
                assert(queries.empty());
                assert(devs.empty());
            } else {
                std::abort();
            }
        }

        // Custom copy assignment
        constexpr basic_device_registry& operator=(basic_device_registry const& other) {
            if consteval {
                assert(other.queries.empty());
                assert(other.devs.empty());
                assert(queries.empty());
                assert(devs.empty());
            } else {
                std::abort();
            }
            return *this;
        }

        /// Add device manually
        void add(evdev&& inp_dev);
        void add(device_query const& inp_query);

        /// Append new queries and their devices
        template <typename... QueryT>
            requires((std::convertible_to<QueryT, device_query> && ...))
        void add(QueryT const&... inp_queries) {
            devs.append_range(filter_devices(inp_queries...) | to_evdev);
            (queries.emplace_back(inp_queries), ...);
        }

        [[nodiscard]] auto devices(this auto&& self) noexcept {
            assert(self.monitor.has_value());
            return std::span{self.devs};
        }

        /// Initialize monitoring
        context_action operator()(start_tag);

      private:
        std::optional<udev_monitor> monitor = std::nullopt;
        std::vector<evdev>          devs;
        std::vector<device_query>   queries;
        std::vector<pollfd>         fds;
    };

    export constexpr basic_device_registry device_registry;

} // namespace fs8
