
module;
#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include <string>
#include <utility>
module fs8.devices.queries;
import fs8.devices.capabilities;

std::string_view fs8::to_string(matching_action_type const action) noexcept {
    using enum matching_action_type;

    switch (action) {
        case match_subsystem: return {"match_subsystem"};
        case match_sysattr: return {"match_sysattr"};
        case match_property: return {"match_property"};
        case tag: return {"match_tag"};
        case syspath: return {"syspath"};
        case sysname: return {"match_sysname"};
        case nomatch_subsystem: return {"nomatch_subsystem"};
        case nomatch_sysattr: return {"nomatch_sysattr"};
        case nomatch_property:
            return {"nomatch_property"};
            // case nomatch_flag: return {"nomatch_flag"};
    }

    return {"unknown"};
}

[[nodiscard]] std::string fs8::to_string(device_query const& inp_query) {
    std::string result;
    result += "device_query {";
    result += std::format(
      "caps_support_percentage={}, matches_limit={}, grab={}, fail_on_no_match={}, fields=[",
      inp_query.caps_support_percentage,
      inp_query.matches_limit,
      inp_query.grab,
      inp_query.fail_on_no_match);

    bool first = true;
    for (auto const& [key, value, matching_action, percentage] : inp_query.fields) {
        if (!std::exchange(first, false)) {
            result += ", ";
        }

        result += std::format(
          "{{key=\"{}\", value=\"{}\", matching_action={}, percentage={}}}",
          key,
          value,
          to_string(matching_action),
          percentage);
    }

    result += "], caps=[";

    first = true;
    for (auto const cap : inp_query.caps) {
        if (!std::exchange(first, false)) {
            result += ", ";
        }

        // Assumes capabilities has a to_string overload for its element type.
        result += to_string(cap);
    }

    result += "]}";
    return result;
}

bool fs8::matches(evdev const& dev, device_query const& inp_query) noexcept {
    // todo
}

// 1. Check if an existing device belongs to this classification
bool fs8::matches(udev_device const& dev, device_query const& inp_query) noexcept {
    using enum matching_action_type;

    bool res = has_subsystem(inp_query, dev.subsystem());
    for (auto const& field : inp_query.fields) {
        if (is_property(field)) {
            res &= is_matched(field, &field_type::value, dev.property(field.key.data()));
        } else if (is_subsystem(field)) {
            res &= is_matched(field, &field_type::key, dev.subsystem());
            res &= is_matched(field, &field_type::value, dev.devtype());
        } else if (is_sysattr(field)) {
            res &= is_matched(field, &field_type::value, dev.sysattr(field.key.data()));
        } else if (field.matching_action == tag) {
            res &= dev.has_tag(field.value.data());
            assert(field.key.empty());
        } else if (field.matching_action == syspath) {
            res &= is_matched(field, &field_type::value, dev.syspath());
            assert(field.key.empty());
        } else if (field.matching_action == sysname) {
            res &= is_matched(field, &field_type::value, dev.sysname());
            assert(field.key.empty());
        } else {
            // Have not yet implemented.
            assert(false);
        }

        if (!res) {
            break;
        }
    }
    return res;
}

// 2. Apply rules to find these devices
void fs8::match(udev_enumerate& enumerate, device_query const& inp_query) noexcept {
    for (auto const& field : inp_query.fields) {
        if (is_subsystem(field)) {
            enumerate.match_subsystem(field.key.data());
            if (!field.value.empty()) {
                // Property-level filter (The equivalent of matching devtype)
                enumerate.match_property("DEVTYPE", field.value.data());
            }
        } else if (is_property(field)) {
            enumerate.match_property(field.key.data(), field.value.data());
        }
    }
}

// 3. Apply rules to monitor these devices
// Note: udev_monitor cannot filter by property directly, only by subsystem/devtype/tag.
// We filter by subsystem here, and use `matches()` on the received event later.
void fs8::match(udev_monitor& monitor, device_query const& inp_query) noexcept {
    for (auto const& field : subsystems(inp_query)) {
        monitor.match_device(field.key.data(), field.value.empty() ? nullptr : field.value.data());
    }
}

bool fs8::has_subsystem(device_query const& inp_query, std::string_view const subsystem) noexcept {
    return std::ranges::contains(subsystems(inp_query), subsystem, &field_type::key);
}

bool fs8::has_property(device_query const& inp_query, std::string_view const key) noexcept {
    return std::ranges::contains(properties(inp_query), key, &field_type::key);
}

fs8::field_type fs8::property(device_query const& inp_query, std::string_view const key) noexcept {
    for (auto const& field : inp_query.fields) {
        if (field.matching_action == matching_action_type::match_property && field.key == key) {
            return field;
        }
    }
    return invalid_field;
}

fs8::field_type fs8::sysattr(device_query const& inp_query, std::string_view const key) noexcept {
    for (field_type const& field : sysattrs(inp_query)) {
        if (is_matched(field, &field_type::key, key)) {
            return field;
        }
    }
    return invalid_field;
}

std::string_view fs8::name(device_query const& inp_query) noexcept {
    auto const field = sysattr(inp_query, "DEVICE/NAME");
    return field ? "" : field.value;
}
