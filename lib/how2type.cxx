// Created by moisrex on 10/13/25.

module;
#include <algorithm>
#include <linux/input.h>
#include <memory>
#include <ranges>
#include <vector>
#include <xkbcommon/xkbcommon.h>
module foresight.lib.xkb.how2type;
import foresight.main.log;
import foresight.mods.event;

using foresight::xkb::how2type;
using foresight::xkb::key_position;

constexpr std::size_t COMPOSE_MAX_LHS_LEN  = 8;
constexpr std::size_t MAX_TYPE_MAP_ENTRIES = 32;

how2type::how2type(keymap::pointer inp_map) : map{std::move(inp_map)} {}

namespace {
    struct modifier_map_entry {
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
    std::array<modifier_map_entry, 5> const &get_modmap(xkb_keymap *keymap) {
        static std::array<modifier_map_entry, 5> map{};
        static bool                              initialized = false;

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

        bool                  mod_found = false;
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

void how2type::on_keysym(xkb_keysym_t const target_keysym, handle_keysym_callback callback) noexcept {
    bool done = false;
    on_keypos(target_keysym, [&](key_position const &position) {
        // Return the first direct position (first-found strategy)
        if (done) {
            return;
        }
        callback(position);
        done = true;
    });
}

void how2type::emit(char32_t const ucs32, handle_event_callback callback) {
    // Convert Unicode -> keysym (uses libxkbcommon helper)
    xkb_keysym_t const ks = xkb_utf32_to_keysym(static_cast<uint32_t>(ucs32));
    if (ks == XKB_KEY_NoSymbol) {
        return;
    }

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
    on_keysym(ks, [&](key_position const &pos) {
        int const evcode = keycode_to_evdev(pos.keycode);
        if (evcode < 0) {
            // can't map this key; abort
            log("Warning: can't map xkb keycode {} to evdev code", static_cast<unsigned>(pos.keycode));
            return;
        }

        bool const requires_mods = pos.mask != 0;

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
              pos.keycode,
              pos.layout,
              pos.level,
              masks.data(),
              masks.size());

            if (num_masks > 0) {
                if (invoke_mod_events(map->get(),
                                      masks.at(0),
                                      true,
                                      [&](user_event const &event) {
                                          callback(event);
                                      }))
                {
                    callback(ev_syn);
                }
            }
        }


        callback(ev_press);
        callback(ev_syn);
        callback(ev_release);
        callback(ev_syn);

        // release the evs
        if (requires_mods) {
            if (num_masks > 0) {
                if (invoke_mod_events(map->get(),
                                      masks.at(0),
                                      false,
                                      [&](user_event const &event) {
                                          callback(event);
                                      }))
                {
                    callback(ev_syn);
                }
            }
        }
    });
}

void how2type::emit(std::u32string_view const str, handle_event_callback callback) {
    for (char32_t const ucs32 : str) {
        emit(ucs32, callback);
    }
}

bool how2type::on_keypos(xkb_keysym_t const keysym, handle_keysym_callback callback) {
    xkb_keycode_t const       min_keycode = xkb_keymap_min_keycode(map->get());
    xkb_keycode_t const       max_keycode = xkb_keymap_max_keycode(map->get());
    std::vector<xkb_keysym_t> seen_syms;
    bool                      found_positions = false;

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
            seen_syms.clear();
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
                    callback({.keycode = keycode, .layout = layout, .level = level, .mask = 0});
                    found_positions = true;
                    // entry->positions.emplace_back();
                } else {
                    found_positions |= n_masks > 0;
                    for (std::size_t mi = 0; mi < n_masks; ++mi) {
                        callback(
                          {.keycode = keycode, .layout = layout, .level = level, .mask = masks.at(mi)});
                        // entry->positions.emplace_back();
                    }
                }
            }
        }
    }
    return found_positions;
}
