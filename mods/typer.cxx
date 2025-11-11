// Created by moisrex on 10/11/25.

module;
#include <string_view>
module foresight.mods.typer;
import foresight.lib.mod_parser;
import foresight.lib.xkb;

void foresight::basic_typist::emit(std::u32string_view str, user_event_callback callback) {
    // first initialize the how2type object
    auto const &map = xkb::get_default_keymap();

    while (!str.empty()) {
        // 1. find the first modifier:
        auto const lhsptr = find_delim(str, U'<');
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const lhs    = str.substr(0, lhsptr);
        auto const rhs    = str.substr(lhsptr, rhsptr - lhsptr);

        // 2. emit the strings before the <...> mod
        xkb::how2type::emit(map, lhs, callback);

        // 3. parse the modifier string if any:
        if (!parse_modifier(rhs, callback)) [[unlikely]] {
            // send the <...> string as is:
            xkb::how2type::emit(map, rhs, callback);
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
