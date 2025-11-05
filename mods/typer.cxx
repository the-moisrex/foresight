// Created by moisrex on 10/11/25.

module;
#include <optional>
#include <string_view>
module foresight.mods.typer;
import foresight.lib.mod_parser;

void foresight::basic_typist::emit(std::u32string_view str, user_event_callback callback) {
    // first initialize the how2type object
    if (!typer.has_value()) {
        typer = std::make_optional<xkb::how2type>();
    }

    while (!str.empty()) {
        // 1. find the first modifier:
        auto const lhsptr = find_delim(str, U'<');
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const lhs    = str.substr(0, lhsptr);
        auto const rhs    = str.substr(lhsptr, rhsptr - lhsptr);

        // 2. emit the strings before the <...> mod
        this->typer->emit(lhs, callback);

        // 3. parse the modifier string if any:
        if (!parse_modifier(rhs, callback)) [[unlikely]] {
            // send the <...> string as is:
            this->typer->emit(rhs, callback);
        }

        // 4. remove the already processed string:
        str.remove_prefix(rhsptr);
    }
}

void foresight::basic_typist::emit(std::u32string_view const str) {
    auto const emit_event = [&](user_event const &event) {
        events.emplace_back(event);
    };
    emit(str, emit_event);
}
