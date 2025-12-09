// Created by moisrex on 10/13/25.

module;
#include <functional>
#include <memory>
#include <xkbcommon/xkbcommon.h>
export module foresight.lib.xkb.how2type;
import foresight.lib.xkb;
import foresight.mods.event;

export namespace fs8::xkb {

    struct key_position {
        xkb_keycode_t      keycode{};
        xkb_layout_index_t layout{};
        xkb_level_index_t  level{};
        xkb_mod_mask_t     mask{};
    };

    // todo: use std::function_ref instead of std::function
    using handle_keysym_callback = std::function<void(key_position const&)> const&;

    /**
     * XKB How-To-Type (without composed sequences)
     * Convert strings to events
     */
    namespace how2type {
        /**
         * Find first possible typing for a given keysym.
         * an ordered vector of key_position (each entry corresponds to
         * a physical key press to produce the final symbol).
         */
        void on_keysym(keymap const& map, xkb_keysym_t target_keysym, handle_keysym_callback callback) noexcept;

        /**
         *  Given a Unicode code point, return a best-effort sequence of events
         *  records that type it.
         */
        void emit(keymap const& map, char32_t ucs32, user_event_callback callback);

        /**
         * Call the callback with the series of events that will type that string.
         */
        void emit(keymap const& map, std::u32string_view str, user_event_callback callback);
        void emit(keymap const& map, std::string_view str, user_event_callback callback);

        enum struct output_syntax : std::uint8_t {
            evtest,
            cpp_code,
            // todo: add `libinput debug-events` syntax
        };

        void print(keymap const& map, std::string_view, output_syntax);

        /**
         * Print how to type the specified input in the specified syntax
         */
        void print(std::string_view, output_syntax syntax = output_syntax::evtest);
    }; // namespace how2type

} // namespace fs8::xkb
