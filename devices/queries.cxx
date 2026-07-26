
module;
#include <format>
#include <string>
#include <utility>
module fs8.devices.queries;
import fs8.devices.capabilities;

std::string_view fs8::to_string(matching_action_type action) noexcept {
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
        case nomatch_property: return {"nomatch_property"};
        // case nomatch_flag: return {"nomatch_flag"};
    }

    return {"unknown"};
}

[[nodiscard]] std::string fs8::to_string(device_query const& query) {
    std::string result;
    result += "device_query {";
    result += std::format(
      "caps_support_percentage={}, matches_limit={}, grab={}, fail_on_no_match={}, fields=[",
      query.caps_support_percentage,
      query.matches_limit,
      query.grab,
      query.fail_on_no_match);

    bool first = true;
    for (field_type const& field : query.fields) {
        if (!std::exchange(first, false)) {
            result += ", ";
        }

        result += std::format(
          "{{key=\"{}\", value=\"{}\", matching_action={}, percentage={}}}",
          field.key,
          field.value,
          to_string(field.matching_action),
          field.percentage);
    }

    result += "], caps=[";

    first = true;
    for (auto const cap : query.caps) {
        if (!std::exchange(first, false)) {
            result += ", ";
        }

        // Assumes capabilities has a to_string overload for its element type.
        result += to_string(cap);
    }

    result += "]}";
    return result;
}
