// Created by moisrex on 7/18/26.

module;
#include <cstdint>
#include <optional>
module foresight.mods.device_registry;
import foresight.mods.context;
import foresight.main.log;

using fs8::basic_device_registry;
using fs8::context_action;

void basic_device_registry::add(evdev&& inp_dev, device_query_snapshot query) {
    std::uint8_t query_index = 0;
    for (auto const& cur_query : queries) {
        if (query == cur_query) {
            break;
        }
        ++query_index;
    }
    if (query_index == queries.size()) {
        query_index = fs8::no_query;
    }

    devices.emplace_back(std::move(inp_dev), query_index);
    queries.emplace_back(std::move(query));
}

context_action basic_device_registry::operator()(start_tag) {
    using enum context_action;

    if (monitor.has_value()) {
        return next;
    }

    monitor = std::make_optional<udev_monitor>();

    std::uint8_t index = 0;
    for (auto const& query : queries) {
        if (!query.fail_on_no_match) {
            ++index;
            continue;
        }

        bool found = false;
        for (auto const& pick : devices) {
            if (pick.query_index == index) {
                found = true;
                break;
            }
        }
        if (!found) {
            log("Needed this query but didn't found it: {}", to_string(query));
            return exit;
        }
        ++index;
    }
    return next;
}
