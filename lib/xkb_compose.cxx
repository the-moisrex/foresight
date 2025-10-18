// Created by moisrex on 10/13/25.

module;
#include <algorithm>
#include <linux/input.h>
#include <memory>
#include <ranges>
#include <vector>
#include <xkbcommon/xkbcommon-compose.h>
module foresight.lib.xkb.compose;
import foresight.main.log;
import foresight.mods.event;

using foresight::xkb::compose_manager;
using foresight::xkb::key_position;

constexpr std::size_t COMPOSE_MAX_LHS_LEN  = 8;
constexpr std::size_t MAX_TYPE_MAP_ENTRIES = 32;

compose_manager::compose_manager(keymap::pointer inp_map) : map{std::move(inp_map)} {}

compose_manager::compose_manager() : compose_manager{keymap::create()} {}

namespace {
    struct ModMapEntry {
        xkb_mod_index_t index;
        std::uint16_t   keycode;
        xkb_mod_mask_t  mask;
    };

    /**
     * Thread-safe singleton accessor for a fixed modifier map.
     *
     * Lazily builds the map once for the given keymap. No dynamic allocation,
     * no global state mutation, and safe across threads (per C++11 guarantees).
     */
    std::array<ModMapEntry, 5> const &get_modmap(xkb_keymap *keymap) {
        static std::array<ModMapEntry, 5> map{};
        static bool                       initialized = false;

        if (!initialized) {
            map[0].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
            map[0].keycode = KEY_LEFTSHIFT;
            map[0].mask =
              map[0].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[0].index) : 0;

            map[1].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
            map[1].keycode = KEY_LEFTCTRL;
            map[1].mask =
              map[1].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[1].index) : 0;

            map[2].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
            map[2].keycode = KEY_LEFTALT;
            map[2].mask =
              map[2].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[2].index) : 0;

            map[3].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
            map[3].keycode = KEY_LEFTMETA;
            map[3].mask =
              map[3].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[3].index) : 0;

            map[4].index   = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
            map[4].keycode = KEY_CAPSLOCK;
            map[4].mask =
              map[4].index != XKB_MOD_INVALID ? xkb_keymap_mod_get_mask2(keymap, map[4].index) : 0;

            initialized = true;
        }

        return map;
    }

    /**
     * Invoke a callable for each modifier event based on a xkb_mod_mask_t.
     *
     * - modmap: automatically fetched from get_modmap().
     * - mask:   active modifier mask.
     * - pressed: true for key press, false for key release.
     * - emit:   callable taking (const struct user_event&).
     */
    template <typename EmitFunc>
    bool
    invoke_mod_events(xkb_keymap *keymap, xkb_mod_mask_t const mask, bool const pressed, EmitFunc &&emit) {
        auto const &modmap = get_modmap(keymap);

        bool        mod_found = false;
        foresight::user_event ev{};
        // struct timeval     now{};
        // gettimeofday(&now, nullptr);

        for (auto const &m : modmap) {
            if (!m.mask) {
                continue;
            }

            if ((mask & m.mask) != m.mask) {
                continue;
            }

            // ev.time  = now;
            ev.type  = EV_KEY;
            ev.code  = m.keycode;
            ev.value = pressed ? 1 : 0;
            emit(ev);
            mod_found = true;
        }
        return mod_found;
    }


} // namespace

bool compose_manager::load_from_locale() {
    char const *locale = std::setlocale(LC_CTYPE, nullptr);
    if (locale == nullptr) {
        locale = "";
    }

    // unique_ptr with custom deleter
    auto const ctx = map->get_context();
    compose_table.reset(xkb_compose_table_new_from_locale(ctx->get(), locale, XKB_COMPOSE_COMPILE_NO_FLAGS));
    if (!compose_table) {
        log("Couldn't create compose table from locale");
        return false;
    }
    return true;
}

bool compose_manager::gather_compose_sequences(xkb_keysym_t const target_keysym) {
    compose_entries.clear();

    if (!compose_table) {
        if (!load_from_locale()) {
            return false;
        }
    }

    using iterator_ptr_t =
      std::unique_ptr<xkb_compose_table_iterator, decltype(&xkb_compose_table_iterator_free)>;
    iterator_ptr_t const iter(xkb_compose_table_iterator_new(compose_table.get()),
                              &xkb_compose_table_iterator_free);
    if (!iter) {
        log("ERROR: cannot iterate Compose table");
        return false;
    }

    while (xkb_compose_table_entry *entry = xkb_compose_table_iterator_next(iter.get())) {
        if (target_keysym != xkb_compose_table_entry_keysym(entry)) {
            continue;
        }

        std::size_t         count = 0;
        xkb_keysym_t const *seq   = xkb_compose_table_entry_sequence(entry, &count);
        compose_lhs         lhs;
        lhs.assign(seq, std::next(seq, static_cast<std::ptrdiff_t>(count)));
        compose_entries.push_back(std::move(lhs));
    }

    return true;
}

bool compose_manager::gather_keysym_positions() {
    keysym_entries.clear();

    // For each compose LHS sequence we collected earlier, collect positions
    for (auto const &lhs : compose_entries) {
        for (xkb_keysym_t const ksym : lhs) {
            ensure_keysym_entry_exists(ksym);

            // Now find positions in keymap for this keysym
            add_positions(ksym);
        }
    }

    // Return whether we have any positions
    return std::ranges::any_of(keysym_entries, [](keysym_entry const &keysym) noexcept -> bool {
        return !keysym.positions.empty();
    });
}

std::vector<foresight::xkb::compose_lhs> const &compose_manager::composeEntries() const noexcept {
    return compose_entries;
}

std::vector<foresight::xkb::keysym_entry> const &compose_manager::keysymEntries() const noexcept {
    return keysym_entries;
}

std::vector<key_position> compose_manager::find_first_typing(xkb_keysym_t const target_keysym) {
    // 1) Try direct typing first
    // Ensure positions for the target keysym exist
    keysym_entries.clear(); // keep local state consistent
    add_positions(target_keysym);
    if (auto const direct = find_keysym(target_keysym); direct != keysym_entries.end()) {
        if (!direct->positions.empty()) {
            // Return the first direct position (first-found strategy)
            return std::vector<key_position>{direct->positions.front()};
        }
    }

    // 2) No direct typing → try to compose sequences producing the target keysym.
    // Gather compose sequence LHSs that produce the given keysym
    if (!gather_compose_sequences(target_keysym)) {
        return {}; // error or no compose table
    }

    // Collect positions for all keysyms used in the compose sequences
    // This fills keysym_entries_ (used by the cartesian-product search below).
    gather_keysym_positions();
    // If no entries at all, fail
    bool any_positions = false;
    for (auto const &e : keysym_entries) {
        if (!e.positions.empty()) {
            any_positions = true;
            break;
        }
    }
    if (!any_positions) {
        return {};
    }

    // For each compose LHS entry, try to find the first reachable product combination
    for (auto const &lhs : compose_entries) {
        // prepare indexes as in the original algorithm
        struct IndexWrap {
            std::size_t             pos_index;
            keysym_entries_iterator entry;
        };

        std::array<IndexWrap, COMPOSE_MAX_LHS_LEN> indexes{};
        bool                                       enabled = true;

        // Link each LHS keysym to a KeysymEntries pointer
        for (std::size_t k = 0; k < lhs.size(); ++k) {
            auto const ent = find_keysym(lhs[k]);
            if (ent == keysym_entries.end() || ent->positions.empty()) {
                enabled = false;
                break;
            }
            indexes.at(k).pos_index = 0;
            indexes.at(k).entry     = ent;
        }
        if (!enabled) {
            continue;
        }

        bool more_sequences = true;
        while (more_sequences) {
            // Check layout-mixing constraint
            xkb_layout_index_t fixed_layout    = XKB_LAYOUT_INVALID;
            bool               layout_mismatch = false;
            for (std::size_t k = 0; k < lhs.size(); ++k) {
                key_position const &kp = indexes.at(k).entry->positions[indexes.at(k).pos_index];
                if (fixed_layout
                    == XKB_LAYOUT_INVALID
                    && xkb_keymap_num_layouts_for_key(map->get(), kp.keycode)
                    > 1)
                {
                    fixed_layout = kp.layout;
                }
                if (fixed_layout
                    != XKB_LAYOUT_INVALID
                    && xkb_keymap_num_layouts_for_key(map->get(), kp.keycode)
                    > 1
                    && kp.layout
                    != fixed_layout)
                {
                    layout_mismatch = true;
                    break;
                }
            }
            if (!layout_mismatch) {
                // Found a valid combination: build the vector and return it
                std::vector<key_position> result;
                result.reserve(lhs.size());
                for (std::size_t k = 0; k < lhs.size(); ++k) {
                    key_position const &kp = indexes.at(k).entry->positions[indexes.at(k).pos_index];
                    result.push_back(kp);
                }
                return result;
            }

            // Advance the cartesian product (rightmost index that can advance)
            {
                std::size_t k = lhs.size();
                while (k > 0) {
                    k--;
                    indexes.at(k).pos_index++;
                    if (indexes.at(k).pos_index < indexes.at(k).entry->positions.size()) {
                        k++; // advanced successfully, continue outer loop
                        break;
                    }
                    indexes.at(k).pos_index = 0;
                }
                if (k == 0) {
                    more_sequences = false;
                }
            }
        }
    }

    // nothing found
    return {};
}

void compose_manager::find_first_typing(char32_t const ucs32, handle_event_callback callback) {
    // Convert Unicode -> keysym (uses libxkbcommon helper)
    xkb_keysym_t const ks = xkb_utf32_to_keysym(static_cast<uint32_t>(ucs32));
    if (ks == XKB_KEY_NoSymbol) {
        return;
    }

    // Find first typing method for the keysym
    auto const positions = find_first_typing(ks);

    // Helper: naive xkb->evdev mapping using the historical +8 X11 offset
    auto keycode_to_evdev = [](xkb_keycode_t const xkb_k) -> int {
        // Historically for X11-compatible maps: xkb_keycode == evdev + 8
        if (xkb_k <= 8) {
            return -1;
        }
        return static_cast<int>(xkb_k) - 8;
    };

    std::array<xkb_mod_mask_t, MAX_TYPE_MAP_ENTRIES> masks{};
    std::size_t                                      num_masks = 0;
    for (key_position const &kp : positions) {
        int const evcode = keycode_to_evdev(kp.keycode);
        if (evcode < 0) {
            // can't map this key; abort
            log("Warning: can't map xkb keycode {} to evdev code", static_cast<unsigned>(kp.keycode));
            return;
        }

        bool const requires_mods = kp.mask != 0;

        // Prepare a press event
        user_event const ev_press{
          .type  = EV_KEY,
          .code  = static_cast<std::uint16_t>(evcode),
          .value = 1 // press
        };
        // time can be zeroed (caller can set real timestamps if desired)

        // Prepare a release event
        user_event const ev_release{
          .type  = EV_KEY,
          .code  = static_cast<std::uint16_t>(evcode),
          .value = 0 // release
        };

        // SYN_REPORT
        constexpr user_event ev_syn{.type = EV_SYN, .code = SYN_REPORT, .value = 0};

        if (requires_mods) {
            // todo: can we cache these results if this function is heavy?
            num_masks = xkb_keymap_key_get_mods_for_level(
              map->get(),
              kp.keycode,
              kp.layout,
              kp.level,
              masks.data(),
              masks.size());

            bool mod_found = false;
            if (num_masks > 0) {
                mod_found |= invoke_mod_events(map->get(), masks.at(0), true, [&](user_event const &event) {
                    callback(event);
                });
            }
            if (mod_found) {
                callback(ev_syn);
            }
        }


        callback(ev_press);
        callback(ev_syn);
        callback(ev_release);
        callback(ev_syn);

        // release the evs
        if (requires_mods) {
            bool mod_found = false;
            if (num_masks > 0) {
                mod_found |= invoke_mod_events(map->get(), masks.at(0), false, [&](user_event const &event) {
                    callback(event);
                });
            }
            if (mod_found) {
                callback(ev_syn);
            }
        }
    }
}

compose_manager::keysym_entries_iterator compose_manager::ensure_keysym_entry_exists(
  xkb_keysym_t const keysym) {
    using diff_t          = std::iter_difference_t<keysym_entries_iterator>;
    auto const entry_iter = find_keysym(keysym);
    if (entry_iter != keysym_entries.end()) {
        return entry_iter;
    }
    keysym_entries.emplace_back(keysym, std::vector<key_position>{});
    return std::next(keysym_entries.begin(), static_cast<diff_t>(keysym_entries.size()) - 1);
}

compose_manager::keysym_entries_iterator compose_manager::find_keysym(xkb_keysym_t const keysym) noexcept {
    return std::ranges::find_if(keysym_entries, [keysym](keysym_entry const &entry) {
        return entry.keysym == keysym;
    });
}

void compose_manager::add_positions(xkb_keysym_t const keysym) {
    auto const entry = ensure_keysym_entry_exists(keysym);

    xkb_keycode_t const min_keycode = xkb_keymap_min_keycode(map->get());
    xkb_keycode_t const max_keycode = xkb_keymap_max_keycode(map->get());

    // We'll keep track of (keycode, layout) we already added for deduping of lower levels
    for (xkb_keycode_t keycode = min_keycode; keycode <= max_keycode; ++keycode) {
        char const *keyname = xkb_keymap_key_get_name(map->get(), keycode);
        if (keyname == nullptr) {
            continue;
        }

        xkb_layout_index_t const num_layouts = xkb_keymap_num_layouts_for_key(map->get(), keycode);
        for (xkb_layout_index_t layout = 0; layout < num_layouts; ++layout) {
            xkb_level_index_t const num_levels = xkb_keymap_num_levels_for_key(map->get(), keycode, layout);

            // keep track of which keysyms we have seen on lower levels of this key/layout
            std::vector<xkb_keysym_t> seen_syms;
            seen_syms.reserve(num_levels);

            for (xkb_level_index_t level = 0; level < num_levels; ++level) {
                xkb_keysym_t const *syms = nullptr;
                int const nsyms = xkb_keymap_key_get_syms_by_level(map->get(), keycode, layout, level, &syms);

                if (nsyms != 1) {
                    continue;              // only care about single keysym per level
                }

                auto const sym0 = syms[0]; // NOLINT(*-pro-bounds-pointer-arithmetic)
                if (sym0 != keysym) {
                    // If this keysym has already been seen in a lower level for the same key/layout,
                    // skip to avoid combinatorial explosion (original logic kept only lowest).
                    bool found = false;
                    for (auto const symbol : seen_syms) {
                        if (symbol == sym0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        seen_syms.push_back(sym0);
                        // Possibly this sym0 contributes to compose sequences; we don't add that here.
                    }
                    continue;
                }

                // Found keysym at this keycode/layout/level, collect masks
                std::array<xkb_mod_mask_t, MAX_TYPE_MAP_ENTRIES> masks{};
                auto const                                       n_masks = xkb_keymap_key_get_mods_for_level(
                  map->get(),
                  keycode,
                  layout,
                  level,
                  masks.data(),
                  masks.size());

                // If there are no masks reported, still push a default mask 0
                if (n_masks == 0) {
                    entry->positions.emplace_back(keycode, layout, level, 0);
                } else {
                    for (std::size_t mi = 0; mi < n_masks; ++mi) {
                        entry->positions.emplace_back(keycode, layout, level, masks.at(mi));
                    }
                }
            }
        }
    }
}

void compose_manager::compose_table_deleter::operator()(xkb_compose_table *ptr) const noexcept {
    if (ptr != nullptr) {
        xkb_compose_table_unref(ptr);
    }
}
