// Created by moisrex on 10/13/25.

module;
#include <functional>
#include <memory>
#include <vector>
#include <xkbcommon/xkbcommon-compose.h>
export module foresight.lib.xkb.compose;
import foresight.lib.xkb;
import foresight.mods.event;

export namespace foresight::xkb {


    using compose_lhs = std::vector<xkb_keysym_t>; // left-hand side sequence

    struct key_position {
        xkb_keycode_t      keycode{};
        xkb_layout_index_t layout{};
        xkb_level_index_t  level{};
        xkb_mod_mask_t     mask{};
    };

    struct keysym_entry {
        xkb_keysym_t              keysym{};
        std::vector<key_position> positions;
    };

    /**
     * XKB Compose Manager
     */
    struct compose_manager {
        using keysym_entries_iterator = std::vector<keysym_entry>::iterator;
        using handle_event_callback   = std::function<void(user_event const &event)> const &;
        // todo: use std::function_ref instead of std::function

        explicit compose_manager(keymap::pointer);
        compose_manager();

        /// Load compose table from current locale. Returns false on failure.
        bool load_from_locale();

        /**
         * Gather Compose sequences that produce 'target_keysym'.
         * Returns true on success.
         */
        bool gather_compose_sequences(xkb_keysym_t target_keysym);

        /**
         * For each keysym used in the compose sequences, find all key positions
         * in the provided keymap that produce that keysym.
         *
         * The function fills keysym entries vector, and returns true if any positions were found.
         */
        bool gather_keysym_positions();

        [[nodiscard]] std::vector<compose_lhs> const  &composeEntries() const noexcept;
        [[nodiscard]] std::vector<keysym_entry> const &keysymEntries() const noexcept;

        /**
         * Find first possible typing for a given keysym.
         * an ordered vector of key_position (each entry corresponds to
         * a physical key press to produce the final symbol).
         */
        std::vector<key_position> find_first_typing(xkb_keysym_t target_keysym);

        /**
         *  Given a Unicode code point, return a best-effort sequence of events
         *  records that type it. The returned vector contains press/release pairs for each key
         *  and intervening SYN_REPORTs.
         */
        void find_first_typing(char32_t ucs32, handle_event_callback callback);

      private:
        // Helper to ensure a keysym entry exists in keysym_entries and return pointer
        keysym_entries_iterator ensure_keysym_entry_exists(xkb_keysym_t keysym);

        // Find keysym entries (non-const pointer)
        keysym_entries_iterator find_keysym(xkb_keysym_t keysym) noexcept;

        // Add positions for a given keysym by scanning the keymap. This mirrors original
        // add_compose_keysym_entry semantics: for each keycode/layout/level with single
        // keysym equal to keysym, append a key_position (one per mask returned).
        void add_positions(xkb_keysym_t keysym);

        struct compose_table_deleter {
            void operator()(xkb_compose_table *ptr) const noexcept;
        };

        keymap::pointer                                           map;
        std::unique_ptr<xkb_compose_table, compose_table_deleter> compose_table;
        std::vector<compose_lhs>                                  compose_entries;
        std::vector<keysym_entry>                                 keysym_entries;
    };

} // namespace foresight::xkb
