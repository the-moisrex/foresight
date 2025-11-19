// Created by moisrex on 10/11/25.

module;
#include <functional>
#include <string_view>
module foresight.mods.typer;
import foresight.lib.mod_parser;
import foresight.lib.xkb;

namespace {

    template <typename CharT>
    void emit_impl(std::basic_string_view<CharT> str, foresight::user_event_callback callback) {
        // first initialize the how2type object
        auto const &map = foresight::xkb::get_default_keymap();

        constexpr CharT delim_start = static_cast<CharT>('<');
        constexpr CharT delim_end   = static_cast<CharT>('>');

        for (;;) {
            // 1. find the first modifier:
            auto const lhsptr = foresight::find_delim(str, delim_start);
            if (lhsptr == std::u32string_view::npos) {
                break;
            }
            auto const rhsptr = foresight::find_delim(str, delim_end, lhsptr);
            auto const lhs    = str.substr(0, lhsptr);
            auto const rhs    = str.substr(lhsptr, rhsptr - lhsptr);

            // 2. emit the strings before the <...> mod
            foresight::xkb::how2type::emit(map, lhs, callback);

            // 3. parse the modifier string if any:
            if (!foresight::parse_modifier(rhs, callback)) [[unlikely]] {
                // send the <...> string as is:
                foresight::xkb::how2type::emit(map, rhs, callback);
            }

            // 4. remove the already processed string:
            str.remove_prefix(rhsptr);
        }
    }
} // namespace

void foresight::emit(std::u32string_view const str, user_event_callback callback) {
    emit_impl(str, callback);
}

void foresight::emit(std::string_view const str, user_event_callback callback) {
    emit_impl(str, callback);
}
