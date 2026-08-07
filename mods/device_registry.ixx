// Created by moisrex on 7/10/26.
module;
#include <memory>
#include <span>
export module fs8.mods.device_registry;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.utils.nullable_indirect;

namespace fs8 {


    /**
     * Monitor and manage input devices.
     */
    export struct [[nodiscard]] basic_device_registry {
        consteval basic_device_registry() noexcept                                     = default;
        constexpr basic_device_registry(basic_device_registry&&) noexcept              = default;
        constexpr basic_device_registry& operator=(basic_device_registry&&) noexcept   = default;
        consteval basic_device_registry(basic_device_registry const& other)            = default;
        consteval basic_device_registry& operator=(basic_device_registry const& other) = default;
        constexpr ~basic_device_registry() noexcept                                    = default;

        /// Add device manually
        void add(evdev&& inp_dev);
        void add(device_query const& inp_query);

        /// Append new queries and their devices
        // template <typename... QueryT>
        //     requires((std::convertible_to<QueryT, device_query> && ...))
        // void add(QueryT const&... inp_queries) {
        //     devs.append_range(filter_devices(inp_queries...) | to_evdev);
        //     (queries.emplace_back(inp_queries), ...);
        // }

        [[nodiscard]] std::span<evdev const> devices() const noexcept;
        [[nodiscard]] std::span<evdev>       devices() noexcept;

        /// Initialize monitoring
        context_action operator()(start_tag);

      private:
        struct [[nodiscard]] impl;
        nullable_indirect<impl> pimpl{};
    };

    export constexpr basic_device_registry device_registry;

} // namespace fs8
