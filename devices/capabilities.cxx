module;
#include <format>
#include <string>
module fs8.devices.capabilities;

[[nodiscard]] std::string fs8::to_string(dev_cap_view caps) {
    std::string action_str;
    switch (caps.action) {
        case caps_action::append: action_str = "append"; break;
        case caps_action::remove_codes: action_str = "remove_codes"; break;
        case caps_action::remove_type: action_str = "remove_type"; break;
        default: action_str = "unknown"; break;
    }

    std::string codes_str = "[";
    for (std::size_t i = 0; i < caps.codes.size(); ++i) {
        codes_str += std::to_string(caps.codes[i]);
        if (i + 1 < caps.codes.size()) {
            codes_str += ", ";
        }
    }
    codes_str += "]";

    return std::format("{{type: {}, action: {}, codes: {}}}", static_cast<int>(caps.type), action_str, codes_str);
}

[[nodiscard]] std::string fs8::to_string(dev_caps_view caps) {
    std::string res = "[\n";
    for (std::size_t i = 0; i < caps.size(); ++i) {
        res += "  " + to_string(caps[i]);
        if (i + 1 < caps.size()) {
            res += ",";
        }
        res += "\n";
    }
    res += "]";
    return res;
}
