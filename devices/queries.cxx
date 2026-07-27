
module;
#include <format>
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
        case match_tag: return {"match_tag"};
        case syspath: return {"syspath"};
        case match_sysname: return {"match_sysname"};
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
