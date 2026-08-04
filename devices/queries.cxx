
module;
#include <algorithm>
#include <cassert>
#include <format>
#include <generator>
#include <ranges>
#include <string>
#include <utility>
module fs8.devices.queries;
import fs8.devices.capabilities;

namespace {

    [[nodiscard]] bool matches(std::string_view const pattern, std::string_view const text) noexcept {
        std::size_t i         = 0;
        std::size_t j         = 0;
        std::size_t star_idx  = std::string_view::npos;
        std::size_t match_idx = 0;

        while (i < text.size()) {
            // If characters match
            if (j < pattern.size() && pattern[j] == text[i]) {
                i++;
                j++;
            }
            // If pattern has a wildcard, mark the position for backtracking
            else if (j < pattern.size() && pattern[j] == '*')
            {
                star_idx  = j++;
                match_idx = i;
            }
            // If mismatch occurs, backtrack to the last '*'
            else if (star_idx != std::string_view::npos)
            {
                j = star_idx + 1;
                i = ++match_idx;
            } else {
                return false;
            }
        }

        // Check for trailing wildcards
        while (j < pattern.size() && pattern[j] == '*') {
            j++;
        }

        return j == pattern.size();
    }

} // namespace

bool fs8::is_matched(query_term const& field, std::string_view query_term::* key_val, std::string_view const val) noexcept {
    using enum query_target;
    // todo: handle percentage
    if ((+field.target & +nomatch_flag) != 0) {
        return field.*key_val != val;
    }
    if ((field.*key_val).contains('*')) {
        return ::matches(field.*key_val, val);
    }
    return field.*key_val == val;
}

std::string_view fs8::to_string(query_target const action) noexcept {
    using enum query_target;

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

// 1. Check if an existing device belongs to this query
bool fs8::matches(udev_device const& dev, device_query const& inp_query) noexcept {
    using enum query_target;

    bool res = true;
    for (auto const& field : inp_query.fields) {
        if (is_property(field)) {
            res &= is_matched(field, &query_term::value, dev.property(field.key.data()));
        } else if (is_subsystem(field)) {
            res &= is_matched(field, &query_term::key, dev.subsystem());
            res &= is_matched(field, &query_term::value, dev.devtype());
        } else if (is_sysattr(field)) {
            res &= is_matched(field, &query_term::value, dev.sysattr(field.key.data()));
        } else if (field.target == tag) {
            res &= dev.has_tag(field.value.data());
            assert(field.key.empty());
        } else if (field.target == syspath) {
            res &= is_matched(field, &query_term::value, dev.syspath());
            assert(field.key.empty());
        } else if (field.target == sysname) {
            res &= is_matched(field, &query_term::value, dev.sysname());
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

bool fs8::has_subsystem(device_query const& inp_query) noexcept {
    return !subsystems(inp_query).empty();
}

bool fs8::has_subsystem(device_query const& inp_query, std::string_view const subsystem) noexcept {
    return std::ranges::contains(subsystems(inp_query), subsystem, &query_term::key);
}

bool fs8::has_property(device_query const& inp_query, std::string_view const key) noexcept {
    return std::ranges::contains(properties(inp_query), key, &query_term::key);
}

std::generator<fs8::udev_device> fs8::filter_devices(udev_enumerate const& enumerate, device_query const& query) noexcept {
    std::uint8_t limit = query.matches_limit;

    for (auto const& entry : enumerate.list_entries()) {
        auto dev = udev_device{entry};
        if (!dev) [[unlikely]] {
            continue;
        }
        if (matches(dev, query) && limit-- != 0) {
            co_yield std::move(dev);
        }
    }
}

fs8::query_term fs8::property(device_query const& inp_query, std::string_view const key) noexcept {
    for (auto const& field : inp_query.fields) {
        if (field.target == query_target::match_property && field.key == key) {
            return field;
        }
    }
    return invalid_field;
}

fs8::query_term fs8::sysattr(device_query const& inp_query, std::string_view const key) noexcept {
    for (query_term const& field : sysattrs(inp_query)) {
        if (is_matched(field, &query_term::key, key)) {
            return field;
        }
    }
    return invalid_field;
}

std::string_view fs8::name(device_query const& inp_query) noexcept {
    auto const field = sysattr(inp_query, "device/name");
    return field ? "" : field.value;
}


////////////////////////////////////////////////////////////////////////////////////

fs8::query_term fs8::parse_query_term(std::string_view str) noexcept {
    bool invert = false;
    if (str.starts_with('!')) {
        invert = true;
        str.remove_prefix(1);
    }

    auto const eq_pos = str.find('=');
    if (eq_pos == std::string_view::npos) {
        return invalid_field; // Or handle error
    }

    auto const target_key_part = str.substr(0, eq_pos);
    auto const value           = str.substr(eq_pos + 1);

    auto const colon_pos  = target_key_part.find(':');
    auto const target_str = target_key_part.substr(0, colon_pos);
    auto const key        = (colon_pos != std::string_view::npos) ? target_key_part.substr(colon_pos + 1) : std::string_view{};

    query_target target{};
    if (target_str == "sub") {
        target = query_target::match_subsystem;
    } else if (target_str == "attr") {
        target = query_target::match_sysattr;
    } else if (target_str == "prop") {
        target = query_target::match_property;
    } else if (target_str == "tag") {
        target = query_target::tag;
    } else if (target_str == "path") {
        target = query_target::syspath;
    } else if (target_str == "name") {
        target = query_target::sysname;
    } else {
        return invalid_field;
    }

    if (invert) {
        target = static_cast<query_target>(+target | +query_target::nomatch_flag);
    }

    return query_term{.key = key, .value = value, .target = target, .percentage = globe_search};
}

fs8::device_query fs8::parse_device_query(int const argc, char const* const* argv, std::vector<query_term>& fields) {
    if (argc <= 1) {
        return query; // Empty query
    }
    assert(fields.empty());

    fields.reserve(static_cast<std::size_t>(argc - 1));

    for (int i = 1; i < argc; ++i) {
        auto cur_query = parse_query_term(std::string_view{argv[i]});
        if (!cur_query) [[unlikely]] {
            continue;
        }
        fields.emplace_back(std::move(cur_query));
    }

    device_query res{};
    res.fields = std::span<query_term const>{fields.data(), fields.size()};
    return res;
}

