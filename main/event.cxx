// Created by moisrex on 8/17/26.

module;
#include <string_view>
module fs8.event;

[[nodiscard]] std::string_view fs8::to_string(event_origin const origin) noexcept {
    using enum event_origin;
    switch (origin) {
        case none: return {"none"};
        case device: return {"device"};
        case event_origin::stdin: return {"stdin"};
        case self: return {"self"};
        case chained: return {"chained"};
        default: return {"<unknown>"};
    }
}
