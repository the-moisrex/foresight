// Created by moisrex on 12/13/25.

module;
#include <string_view>
#include <utility>
module foresight.mods.context;

[[nodiscard]] std::string_view fs8::to_string(context_action const action) noexcept {
    using enum context_action;
    switch (action) {
        case next: return {"Next"};
        case ignore_event: return {"Ignore Event"};
        case idle: return {"Idle"};
        case exit: return {"Exit"};
        default: return {"<unknown>"};
    }
    std::unreachable();
}
