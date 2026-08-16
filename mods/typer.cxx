// Created by moisrex on 10/11/25.

module;
#include <functional>
#include <string_view>
module fs8.mods.typer;
import fs8.lib.mod_parser;
import fs8.lib.xkb;

namespace {

    template <typename CharT>
    void emit_impl(std::basic_string_view<CharT> str, fs8::user_event_callback callback) {
        // first initialize the how2type object
        auto const &map = fs8::xkb::get_default_keymap();

        std::size_t index = 0;
        for (;;) {
            // 1. find the next modifier tag (`<...>`, `[...]`, `<<...>>`, `[[...]]`):
            std::size_t lhsptr = 0;
            std::size_t rhsptr = 0;
            if (!fs8::find_modifier_tag(str, index, lhsptr, rhsptr)) {
                break;
            }
            auto const lhs = str.substr(index, lhsptr - index);
            auto const rhs = str.substr(lhsptr, rhsptr - lhsptr);

            // 2. emit the strings before the tag
            fs8::xkb::how2type::emit(map, lhs, callback);

            // 3. parse the modifier tag if any (press the keys down then release them):
            if (!fs8::parse_modifier(rhs, callback)) [[unlikely]] {
                // send the tag as is:
                fs8::xkb::how2type::emit(map, rhs, callback);
            }

            // 4. remove the already processed string:
            index = rhsptr;
        }

        // 5. emit whatever is left:
        fs8::xkb::how2type::emit(map, str.substr(index), callback);
    }
} // namespace

void fs8::emit(std::u32string_view const str, user_event_callback callback) {
    emit_impl(str, callback);
}

void fs8::emit(std::string_view const str, user_event_callback callback) {
    emit_impl(str, callback);
}
