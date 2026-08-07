// Created by moisrex on 7/10/26.
module;
#include <span>
export module fs8.mods.device_registry;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.nullable_indirect;
import fs8.pimpl;

export namespace fs8 {


    /**
     * Monitor and manage input devices.
     */
    constexpr struct [[nodiscard]] basic_device_registry : pimpl_idiom<basic_device_registry> {
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
    } device_registry;


} // namespace fs8
