// Created by moisrex on 10/13/25.

module;
#include <linux/input.h>
#include <memory>
#include <vector>
#include <xkbcommon/xkbcommon-compose.h>
export module foresight.lib.xkb.compose;

namespace foresight::xkb {


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

        explicit compose_manager(xkb_context *inp_ctx);

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
        bool gather_keysym_positions(xkb_keymap *keymap);

        [[nodiscard]] std::vector<compose_lhs> const  &composeEntries() const noexcept;
        [[nodiscard]] std::vector<keysym_entry> const &keysymEntries() const noexcept;

        /**
         * Find first possible typing for a given keysym.
         * an ordered vector of key_position (each entry corresponds to
         * a physical key press to produce the final symbol).
         */
        std::vector<key_position> find_first_typing(xkb_keymap *keymap, xkb_keysym_t target_keysym);

        /**
         *  Given a Unicode code point, return a best-effort sequence of Linux input_event
         *  records that type it. The returned vector contains press/release pairs for each key
         *  and intervening SYN_REPORTs.
         *
         *  NOTE: This function does a best-effort mapping of xkb keycodes -> evdev codes using
         *        the conventional X11 offset (evdev = xkb_keycode - 8). If your environment
         *        uses a different mapping, replace the mapping function below.
         *
         *        Modifier masks (key_position::mask) are NOT fully synthesized here. If a
         *        key_position has non-zero .mask, we emit the main key press/release but
         *        do NOT press the modifier keys. You should extend this function to:
         *          - map modifier bit indices -> modifier keycodes (e.g., Shift, AltGr)
         *          - emit press events for modifiers before the main key,
         *          - and release them afterward.
         */
        std::vector<input_event> find_first_typing(xkb_keymap *keymap, char32_t ucs32);

      private:
        // Helper to ensure a keysym entry exists in keysym_entries and return pointer
        keysym_entries_iterator ensure_keysym_entry_exists(xkb_keysym_t keysym);

        // Find keysym entries (non-const pointer)
        keysym_entries_iterator find_keysym(xkb_keysym_t keysym) noexcept;

        // Add positions for a given keysym by scanning the keymap. This mirrors original
        // add_compose_keysym_entry semantics: for each keycode/layout/level with single
        // keysym equal to keysym, append a key_position (one per mask returned).
        void add_positions(xkb_keymap *keymap, xkb_keysym_t keysym);

        struct compose_table_deleter {
            void operator()(xkb_compose_table *ptr) const noexcept;
        };

        xkb_context                                              *ctx{nullptr};
        std::unique_ptr<xkb_compose_table, compose_table_deleter> compose_table;
        std::vector<compose_lhs>                                  compose_entries;
        std::vector<keysym_entry>                                 keysym_entries;
    };

} // namespace foresight::xkb
