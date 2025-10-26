// Created by moisrex on 10/13/25.

module;
#include <functional>
#include <memory>
#include <xkbcommon/xkbcommon.h>
export module foresight.lib.xkb.how2type;
import foresight.lib.xkb;
import foresight.mods.event;

export namespace foresight::xkb {

    struct key_position {
        xkb_keycode_t      keycode{};
        xkb_layout_index_t layout{};
        xkb_level_index_t  level{};
        xkb_mod_mask_t     mask{};
    };

    /**
     * XKB How-To-Type (without composed sequences)
     */
    struct how2type {
        using handle_event_callback  = std::function<void(user_event const &event)> const &;
        using handle_keysym_callback = std::function<void(key_position const &)> const &;
        // todo: use std::function_ref instead of std::function

        explicit how2type(keymap::pointer);
        how2type(how2type const &)                = default;
        how2type(how2type &&)                     = default;
        how2type &operator=(how2type const &)     = default;
        how2type &operator=(how2type &&) noexcept = default;
        ~how2type()                               = default;

        how2type() : how2type{keymap::create()} {}

        /**
         * Find first possible typing for a given keysym.
         * an ordered vector of key_position (each entry corresponds to
         * a physical key press to produce the final symbol).
         */
        void on_keysym(xkb_keysym_t target_keysym, handle_keysym_callback callback) noexcept;

        /**
         *  Given a Unicode code point, return a best-effort sequence of events
         *  records that type it.
         */
        void emit(char32_t ucs32, handle_event_callback callback);

        /**
         * Call the callback with the series of events that will type that string.
         */
        void emit(std::u32string_view str, handle_event_callback callback);

      private:
        // For each keycode/layout/level with single keysym equal to keysym, call the callback with the
        // key_position (one per mask returned).
        bool on_keypos(xkb_keysym_t keysym, handle_keysym_callback callback);

        keymap::pointer map;
    };

} // namespace foresight::xkb
