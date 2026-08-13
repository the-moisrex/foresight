
module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
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
            if (j < pattern.size() && pattern.at(j) == text.at(i)) {
                i++;
                j++;
            }
            // If pattern has a wildcard, mark the position for backtracking
            else if (j < pattern.size() && pattern.at(j) == '*')
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
        while (j < pattern.size() && pattern.at(j) == '*') {
            j++;
        }

        return j == pattern.size();
    }

    [[nodiscard]] std::uint8_t calculate_similarity(std::string_view const s1, std::string_view const s2) noexcept {
        // NOLINTBEGIN(*-magic-numbers)
        if (s1.empty() && s2.empty()) {
            return 100;
        }
        if (s1.empty() || s2.empty()) {
            return 0;
        }

        std::vector<std::size_t> dist(s2.size() + 1);
        for (std::size_t i = 0; i <= s2.size(); ++i) {
            dist[i] = i;
        }

        // Compare case-insensitively: device names are typically Title Case
        // ("USB USB Keykoard") while queries are written lowercase.
        auto const eq = [](unsigned char const l, unsigned char const r) noexcept {
            return std::tolower(l) == std::tolower(r);
        };

        for (std::size_t i = 1; i <= s1.size(); ++i) {
            std::size_t prev = dist[0];
            dist[0]          = i;
            for (std::size_t j = 1; j <= s2.size(); ++j) {
                std::size_t temp = dist[j];
                if (eq(static_cast<unsigned char>(s1.at(i - 1)), static_cast<unsigned char>(s2.at(j - 1)))) {
                    dist[j] = prev;
                } else {
                    dist[j] = 1 + std::min({dist[j - 1], dist[j], prev});
                }
                prev = temp;
            }
        }

        std::size_t const max_len = std::max(s1.size(), s2.size());
        return static_cast<std::uint8_t>(100 - (dist.back() * 100 / max_len));
        // NOLINTEND(*-magic-numbers)
    }

} // namespace

bool fs8::is_matched(query_term const& field, std::string_view query_term::* key_val, std::string_view const val) noexcept {
    using enum query_target;

    std::string_view const target_val = field.*key_val;
    bool                   is_match   = false;

    if (field.percentage == 100) {
        // 100% -> Exact match or Wildcard/Globe match
        is_match = target_val.contains('*') ? ::matches(target_val, val) : (target_val == val);
    } else {
        // < 100% -> Fuzzy search based on Levenshtein distance
        is_match = calculate_similarity(target_val, val) >= field.percentage;
    }

    // Handle inversion flag
    if ((+field.target & +nomatch_flag) != 0) {
        return !is_match;
    }

    return is_match;
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
    using enum query_target;

    if (!dev.is_ok()) {
        return false;
    }

    // 1. Check capability match threshold if capabilities are specified
    if (!inp_query.caps.empty()) {
        auto const score = dev.match_caps(inp_query.caps);
        if (score < inp_query.caps_support_percentage) {
            return false;
        }
    }

    // 2. Evaluate query fields against evdev attributes.
    // The device is assumed to have already gone through udev-level filtering,
    // so only fields expressible through evdev are checked here. Fields that
    // require udev metadata (properties, tags, arbitrary sysattrs) were already
    // verified against the udev device and are therefore not re-checked; use
    // `matches_full` when that assumption does not hold.
    for (auto const& field : inp_query.fields) {
        if (positive(field.target) == sysname) {
            if (!is_matched(field, &query_term::value, device_sysname(dev))) {
                return false;
            }
        } else if (positive(field.target) == syspath) {
            if (!is_matched(field, &query_term::value, dev.physical_location())) {
                return false;
            }
        } else if (is_sysattr(field)) {
            if (field.key == "name" || field.key == "device/name") {
                if (!is_matched(field, &query_term::value, dev.device_name())) {
                    return false;
                }
            } else if (field.key == "phys" || field.key == "device/phys") {
                if (!is_matched(field, &query_term::value, dev.physical_location())) {
                    return false;
                }
            } else if (field.key == "uniq" || field.key == "device/uniq") {
                if (!is_matched(field, &query_term::value, dev.unique_identifier())) {
                    return false;
                }
            }
            // Other sysattrs are not exposed by evdev; can't verify, so we don't exclude.
        } else if (is_subsystem(field)) {
            // evdev devices belong strictly to the "input" subsystem
            if (!is_matched(field, &query_term::key, "input")) {
                return false;
            }
        }
        // Properties and tags require udev metadata; can't verify from evdev, so we don't exclude.
    }

    return true;
}

bool fs8::matches_full(evdev const& dev, device_query const& inp_query) noexcept {
    if (!dev.is_ok()) {
        return false;
    }

    // Reconstruct the udev device this evdev was opened from; evdev devices
    // belong to the "input" subsystem and their sysname is derived from the fd.
    auto const sysname = device_sysname(dev);
    if (sysname.empty()) [[unlikely]] {
        return false;
    }
    udev_device const udev_dev{udev::instance().native(), "input", sysname.data()};
    if (!udev_dev) [[unlikely]] {
        return false;
    }

    // Verify every query field against udev (properties, tags, arbitrary
    // sysattrs, subsystem/devtype, sysname/syspath), then the fields
    // expressible through evdev plus the caps threshold.
    return matches(udev_dev, inp_query) && matches(dev, inp_query);
}

fs8::evdev fs8::device(device_query const& inp_query) noexcept {
    udev_enumerate enumerator{};
    if (!enumerator) [[unlikely]] {
        log("Failed init udev_enumerate");
        return {};
    }
    match(enumerator, inp_query);
    enumerator.scan_devices();

    evdev        best{};
    std::uint8_t best_score = 0;

    for (auto const& entry : enumerator.list_entries()) {
        auto dev = udev_device{entry};
        if (!dev) [[unlikely]] {
            continue;
        }
        auto edev = initialize(inp_query, dev);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        if (!matches(edev, inp_query)) {
            continue;
        }
        if (inp_query.caps.empty()) {
            return edev; // first fully-matching device wins when no caps are requested
        }
        auto const score = edev.match_caps(inp_query.caps);
        if (score >= best_score) {
            best       = std::move(edev);
            best_score = score;
        }
    }
    return best;
}

fs8::evdev fs8::device(dev_caps_view const caps) noexcept {
    return device(device_query{.caps = caps});
}

fs8::evdev fs8::device(std::string_view const str) noexcept {
    // 1. A device path is opened directly
    if (str.starts_with('/')) {
        return evdev{std::filesystem::path{str}};
    }

    // 2. Everything else is converted to a query and resolved normally
    return device(static_cast<device_query>(query_from(str)));
}

fs8::owned_query::owned_query(std::string_view const str) noexcept {
    *this = {}; // reset to defaults
    count = 0;

    if (str.empty()) {
        return;
    }

    // 1. A device path
    if (str.starts_with('/')) {
        auto const pos  = str.find_last_of('/');
        auto const base = (pos == std::string_view::npos) ? str : str.substr(pos + 1);
        storage[0]      = subsystem("input");
        storage[1]      = match_sysname(base);
        count           = 2;
    }
    // 2. A known capabilities name (e.g. "keyboard", "mouse", "pen")
    else if (auto const caps_value = caps_of(str); !caps_value.empty())
    {
        this->caps = caps_value;
        return;
    }
    // 3. A query term (e.g. "name=event0", "attr:device/name=my_mouse")
    else if (auto const term = parse_query_term(str); term)
    {
        storage[0] = term;
        count      = 1;
    }
    // 4. Fall back to a fuzzy device-name match
    else {
        query_term name_field = match_sysattr("device/name", str);
        name_field.percentage = 60; // NOLINT(*-magic-numbers)
        storage[0]            = name_field;
        count                 = 1;
    }
}

fs8::owned_query fs8::query_from(std::string_view const str) noexcept {
    return owned_query{str};
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
        if (is_positive_subsystem(field)) {
            enumerate.match_subsystem(field.key.data());
            if (!field.value.empty()) {
                // Property-level filter (The equivalent of matching devtype)
                enumerate.match_property("DEVTYPE", field.value.data());
            }
        } else if (is_positive_property(field)) {
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
        if (limit == 0) {
            break;
        }
        if (!dev || dev.devnode().empty()) [[unlikely]] {
            // Deviceless nodes (e.g. `inputX` controllers) can never be opened,
            // so they must not consume the match limit or count as a match.
            continue;
        }
        if (matches(dev, query)) {
            co_yield std::move(dev);
            --limit;
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
    return field ? field.value : "";
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

    return query_term{.key = key, .value = value, .target = target};
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

fs8::evdev fs8::initialize(device_query const& inp_query, udev_device const& dev) noexcept {
    using enum evdev_status;

    if (dev.devnode().empty()) [[unlikely]] {
        // Controller/parent nodes have no /dev node to open; they can never be
        // an input source, so treat them as "not matched" instead of attempting
        // an empty-path open (which would only fail with ENOENT).
        return evdev::invalid(not_matched);
    }

    if (!matches(dev, inp_query)) {
        return evdev::invalid(not_matched);
    }

    auto edev = to_evdev(dev);
    if (!edev.is_ok()) [[unlikely]] {
        return edev;
    }

    // Re-verify against the opened device so caps-only queries (e.g. "pen")
    // don't accept whatever happens to be first in the enumeration.
    if (!matches(edev, inp_query)) {
        return evdev::invalid(not_matched);
    }

    // Honour the grab flag from the query.
    if (inp_query.grab) {
        edev.grab_input(true);
        if (edev.grab() != grab_state::grabbing) [[unlikely]] {
            log("Grabbing failed for device: {}", edev.device_name());
            return edev;
        }
    }

    return edev;
}
