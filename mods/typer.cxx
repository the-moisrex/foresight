// Created by moisrex on 10/11/25.

module;
#include <functional>
#include <string_view>
module foresight.mods.typer;
import foresight.lib.mod_parser;
import foresight.lib.xkb;

namespace {

    template <typename CharT>
    void emit_impl(std::basic_string_view<CharT> str, fs8::user_event_callback callback) {
        // first initialize the how2type object
        auto const &map = fs8::xkb::get_default_keymap();

        constexpr auto delim_start = static_cast<CharT>('<');
        constexpr auto delim_end   = static_cast<CharT>('>');

        for (;;) {
            // 1. find the first modifier:
            auto const lhsptr = fs8::find_delim(str, delim_start);
            if (lhsptr == std::u32string_view::npos) {
                break;
            }
            auto const rhsptr = fs8::find_delim(str, delim_end, lhsptr);
            auto const lhs    = str.substr(0, lhsptr);
            auto const rhs    = str.substr(lhsptr, rhsptr - lhsptr);

            // 2. emit the strings before the <...> mod
            fs8::xkb::how2type::emit(map, lhs, callback);

            // 3. parse the modifier string if any:
            if (!fs8::parse_modifier(rhs, callback)) [[unlikely]] {
                // send the <...> string as is:
                fs8::xkb::how2type::emit(map, rhs, callback);
            }

            // 4. remove the already processed string:
            str.remove_prefix(rhsptr);
        }
    }
} // namespace

void fs8::emit(std::u32string_view const str, user_event_callback callback) {
    emit_impl(str, callback);
}

void fs8::emit(std::string_view const str, user_event_callback callback) {
    emit_impl(str, callback);
}
