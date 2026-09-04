// Created by moisrex on 9/3/26.

module;
#include <libevdev/libevdev.h>
#include <optional>
#include <string>
#include <string_view>
module fs8.parsing;

import fs8.event;

[[nodiscard]] std::optional<fs8::event_code> fs8::parse_code(std::string_view const str) noexcept {
    if (auto const colon = str.find(':'); colon != std::string_view::npos) {
        std::string const type_name{str.substr(0, colon)};
        std::string const code_name{str.substr(colon + 1)};
        int const         type = libevdev_event_type_from_name(type_name.c_str());
        if (type == -1) {
            return std::nullopt;
        }
        int const code = libevdev_event_code_from_name(static_cast<unsigned>(type), code_name.c_str());
        if (code == -1) {
            return std::nullopt;
        }
        return event_code{
          .type = static_cast<decltype(event_code::type)>(type),
          .code = static_cast<decltype(event_code::code)>(code),
        };
    }

    std::string const code_name{str};
    int const         code = libevdev_event_code_from_name(EV_KEY, code_name.c_str());
    if (code == -1) {
        return std::nullopt;
    }
    return event_code{
      .type = EV_KEY,
      .code = static_cast<decltype(event_code::code)>(code),
    };
}
