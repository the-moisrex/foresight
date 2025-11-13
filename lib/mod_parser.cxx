// Created by moisrex on 11/4/25.

module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <stdexcept>
module foresight.lib.mod_parser;
import foresight.mods.event;
import foresight.devices.key_codes;
import foresight.utils.hash;
import foresight.lib.xkb.event2unicode;
import foresight.main.log;

using foresight::user_event;

// NOLINTBEGIN(*-magic-numbers)
namespace foresight {
    constexpr std::size_t max_simultaneous_key_presses = 32;
} // namespace foresight

namespace {

    /**
     * Array mapping the leading byte to the length of a UTF-8 sequence.
     * A value of zero indicates that the byte can not begin a UTF-8 sequence.
     */
    constexpr std::array<uint8_t, 256> utf8_sequence_length_by_leading_byte{
      {
       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x00-0x0F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x10-0x1F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x20-0x2F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x30-0x3F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x40-0x4F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x50-0x5F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x60-0x6F */
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x70-0x7F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x80-0x8F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x90-0x9F */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xA0-0xAF */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xB0-0xBF */
        0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xC0-0xCF */
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xD0-0xDF */
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0xE0-0xEF */
        4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xF0-0xFF */
      }
    };

    /// Length of next utf-8 sequence
    std::uint8_t utf8_sequence_length(char const src) noexcept {
        return utf8_sequence_length_by_leading_byte.at(static_cast<unsigned char>(src));
    }

    /**
     * Check if a Unicode code point is a surrogate.
     * Those code points are used only in UTF-16 encodings.
     */
    [[nodiscard]] bool is_surrogate(char32_t const cp) noexcept {
        return cp >= 0xd800 && cp <= 0xDFFFU;
    }

    [[nodiscard]] bool is_empty(char const *src) noexcept {
        return src == nullptr || *src == '\0';
    }

    // ASCII-only case-insensitive equality (keeps your original semantics)
    [[nodiscard]] bool iequals(std::u32string_view const lhs, std::u32string_view const rhs) noexcept {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        static constexpr auto to_lower = [](char32_t const ch32) constexpr noexcept -> char32_t {
            if (ch32 >= U'A' && ch32 <= U'Z') {
                return ch32 + (U'a' - U'A');
            }
            return ch32;
        };

        return std::ranges::equal(lhs, rhs, {}, to_lower, to_lower);
    }

    struct mod_entry {
        std::u32string_view key;      // must refer to a static literal (we use U"...")
        std::uint16_t       code;
        std::uint32_t       hash = 0; // precomputed ci_hash(key)
    };

    // Table of known names/synonyms. Add entries here as needed.
    // NOTE: keep keys lowercase where possible for readability — hash is case-insensitive anyway.
    constexpr std::array<mod_entry, 53> mod_table = []() consteval {
        // NOLINTBEGIN(*-use-designated-initializers)
        std::array<mod_entry, 53> data{
          {// SHIFT
           {U"shift", KEY_LEFTSHIFT},
           {U"sh", KEY_LEFTSHIFT},
           {U"s", KEY_LEFTSHIFT},
           {U"+", KEY_LEFTSHIFT},
           {U"⇧", KEY_LEFTSHIFT},
           {U"leftshift", KEY_LEFTSHIFT},
           {U"rightshift", KEY_RIGHTSHIFT},
           {U"rshift", KEY_RIGHTSHIFT},
           {U"lshift", KEY_LEFTSHIFT},

           // CTRL
           {U"ctrl", KEY_LEFTCTRL},
           {U"control", KEY_LEFTCTRL},
           {U"ctl", KEY_LEFTCTRL},
           {U"c", KEY_LEFTCTRL},
           {U"^", KEY_LEFTCTRL},
           {U"⌃", KEY_LEFTCTRL},
           {U"leftctrl", KEY_LEFTCTRL},
           {U"rightctrl", KEY_RIGHTCTRL},
           {U"rctrl", KEY_RIGHTCTRL},
           {U"lctrl", KEY_LEFTCTRL},

           // META / CMD / SUPER / WIN
           {U"meta", KEY_LEFTMETA},
           {U"cmd", KEY_LEFTMETA},
           {U"command", KEY_LEFTMETA},
           {U"super", KEY_LEFTMETA},
           {U"win", KEY_LEFTMETA},
           {U"windows", KEY_LEFTMETA},
           {U"⊞", KEY_LEFTMETA},
           {U"⌘", KEY_LEFTMETA},
           {U"leftmeta", KEY_LEFTMETA},
           {U"rightmeta", KEY_RIGHTMETA},

           // ALT / OPTION / ALTGR
           {U"alt", KEY_LEFTALT},
           {U"option", KEY_LEFTALT},
           {U"opt", KEY_LEFTALT},
           {U"a", KEY_LEFTALT},
           {U"⌥", KEY_LEFTALT},
           {U"altgr", KEY_RIGHTALT},
           {U"alt_gr", KEY_RIGHTALT},
           {U"leftalt", KEY_LEFTALT},
           {U"rightalt", KEY_RIGHTALT},

           // LOCKS
           {U"caps", KEY_CAPSLOCK},
           {U"capslock", KEY_CAPSLOCK},
           {U"num", KEY_NUMLOCK},
           {U"numlock", KEY_NUMLOCK},
           {U"scroll", KEY_SCROLLLOCK},
           {U"scrolllock", KEY_SCROLLLOCK},

           // XKB-style mod names
           {U"mod1", KEY_LEFTALT},
           {U"mod2", KEY_LEFTALT},
           {U"mod3", KEY_LEFTALT},
           {U"mod4", KEY_LEFTMETA},
           {U"mod5", KEY_LEFTALT},

           // some combined/alternate spellings
           {U"altgr", KEY_RIGHTALT},
           {U"optionkey", KEY_LEFTALT},
           {U"controlkey", KEY_LEFTCTRL}}
        };
        for (auto &field : data) {
            field.hash = foresight::ci_hash(field.key);
        }
        return data;
        // NOLINTEND(*-use-designated-initializers)
    }();

    /**
     * Alternative keys
     * This function finds alternative representations of common keys.
     */
    std::uint16_t alternative_modifier(std::u32string_view const str) noexcept {
        auto const hid = foresight::ci_hash(str);

        // probe over table entries, hash-first to avoid expensive compares
        for (auto const &[key, code, hash] : mod_table) {
            if (hash != hid || !iequals(key, str)) {
                continue; // cheap filter
            }
            return code;  // confirm with case-insensitive exact match
        }

        [[unlikely]] { return foresight::invalid_user_event.code; }
    }

    [[nodiscard]] std::uint16_t get_modifier_code(std::u32string_view const key) noexcept {
        auto const code = foresight::key_code_of(key);
        if (code == 0) {
            return alternative_modifier(key);
        }
        return code;
    }

    [[nodiscard]] foresight::code32_t to_code(
      foresight::key_event_code::code_type const  code,
      foresight::key_event_code::value_type const value = 1) noexcept {
        return static_cast<foresight::code32_t>(
          foresight::event_encoded_code32_t
          | foresight::hashed(foresight::key_event_code{.code = code, .value = value}));
    }

    [[nodiscard]] foresight::code32_t to_code(foresight::event_type const &event) noexcept {
        return to_code(event.code(), event.value());
    }

    [[nodiscard]] foresight::code32_t to_code(foresight::key_event_code const event) noexcept {
        return to_code(event.code, event.value);
    }

    // [[nodiscard]] foresight::code32_t to_code(std::u32string_view const key) noexcept {
    //     return to_code(get_modifier_code(key));
    // }

    /// Convert key names or single characters to user_event (with value=1)
    // user_event to_event(std::u32string_view const key) noexcept {
    //     return user_event{
    //       .type  = EV_KEY,
    //       .code  = get_modifier_code(key),
    //       .value = 1,
    //     };
    // }

} // namespace

foresight::code32_t foresight::unicode_encoded_event(xkb::basic_state const &state,
                                                     event_type const       &event) noexcept {
    auto const code_point = xkb::event2unicode(state, event);
    if (code_point == U'\0') {
        return to_code(event);
    }
    return code_point;
}

/// Reads the next UTF-8 sequence in a string
char32_t foresight::utf8_next_code_point(std::string_view &src, std::size_t const max_size) noexcept {
    char32_t   code_point = 0;
    auto const len        = utf8_sequence_length(src.front());

    if (max_size == 0u || len > max_size) [[unlikely]] {
        return invalid_code_point;
    }

    // Handle leading byte
    auto const src0 = static_cast<char32_t>(src[0]);
    switch (len) {
        case 1: src.remove_prefix(1); return src0;
        case 2: code_point = src0 & 0x1FU; break;
        case 3: code_point = src0 & 0x0FU; break;
        case 4:
            code_point = src0 & 0x07U;
            break;
        [[unlikely]] default:
            return invalid_code_point;
    }

    // Process remaining bytes of the UTF-8 sequence
    for (std::size_t k = 1; k < len; k++) {
        if ((static_cast<char32_t>(src[k]) & 0xC0U) != 0x80U) [[unlikely]] {
            return invalid_code_point;
        }
        code_point <<= 6U;
        code_point  |= static_cast<char32_t>(src[k]) & 0x3FU;
    }

    src.remove_prefix(len);

    // Check surrogates
    if (is_surrogate(code_point)) [[unlikely]] {
        return invalid_code_point;
    }

    return code_point;
}

char32_t foresight::parse_char_or_codepoint(std::string_view &src) noexcept {
    auto const raw_length = src.size();

    if (raw_length == 0U) [[unlikely]] {
        return invalid_code_point;
    }

    // Try to parse the parameter as a UTF-8 encoded single character
    char32_t   codepoint = utf8_next_code_point(src, raw_length);
    auto const length    = src.size() - raw_length;

    // If parsing failed or did not consume all the string, then try other formats
    if (codepoint == invalid_code_point || length == 0 || length != raw_length) {
        char    *endp = nullptr; // NOLINT(*-const-correctness)
        char32_t val  = 0;
        int      base = 10;
        // Detect U+NNNN format standard Unicode code point format
        if (src.starts_with("U+")) {
            base = 16;
            src.remove_prefix(2);
        }
        // Use strtol with explicit bases instead of `0` in order to avoid unexpected parsing as octal.
        for (; base <= 16; base += 6) {
            errno = 0;
            // NOLINTNEXTLINE(*-suspicious-stringview-data-usage)
            val   = static_cast<char32_t>(strtol(src.data(), &endp, base));
            if (errno != 0 || !is_empty(endp) || static_cast<std::int32_t>(val) < 0 || val > 0x10'FFFFU)
              [[unlikely]]
            {
                val = invalid_code_point;
            } else {
                break;
            }
        }
        if (val == invalid_code_point) [[unlikely]] {
            log("ERROR: Failed to convert to UTF-32");
            return invalid_code_point;
        }
        src.remove_prefix(endp - src.data());
        codepoint = val;
    }
    return codepoint;
}

std::size_t
foresight::find_delim(std::u32string_view str, char32_t const delim, std::size_t const pos) noexcept {
    auto lhsptr = str.find(delim, pos);
    while (lhsptr != 0 && str.at(lhsptr - 1) != U'\\') {
        // skip the escaped ones
        lhsptr = str.find(delim, lhsptr + 1);
    }
    return lhsptr;
}

bool foresight::parse_modifier(std::u32string_view mod_str, key_code_callback callback) {
    assert(mod_str.starts_with(U'<') && mod_str.ends_with(U'>'));
    mod_str.remove_prefix(1);
    mod_str.remove_suffix(1);
    bool const     is_release   = mod_str.starts_with(U'/');
    auto           dash_start   = mod_str.find(U'-');
    bool const     is_monotonic = !is_release && dash_start != std::u32string_view::npos;
    key_event_code event;

    if (is_monotonic) {
        // handling <shift> or <ctrl> types
        event = {.code = get_modifier_code(mod_str), .value = 1};
    } else if (is_release) {
        // handling </shift> or </ctrl>
        mod_str.remove_prefix(1);
        event = {.code = get_modifier_code(mod_str), .value = 0};
    } else {
        // handling <C-r> type of mods
        constexpr auto max_len = static_cast<std::int32_t>(max_simultaneous_key_presses);
        std::array<std::uint16_t, max_simultaneous_key_presses> keys{};
        std::int32_t                                            index = 0;
        for (; index != max_len; ++index) {
            auto const sub_mod = mod_str.substr(dash_start);
            if (sub_mod.empty()) {
                break;
            }
            auto const ev = key_event_code{.code = get_modifier_code(sub_mod), .value = 1};
            if (is_invalid(ev)) [[unlikely]] {
                break;
            }
            keys.at(index) = ev.code;
            callback(ev); // keydown
            mod_str.remove_prefix(dash_start + 1);
            dash_start = mod_str.find(U'-');
        }

        // release the keys in reverse order:
        for (; index >= 0; --index) {
            // keyup:
            callback({
              .code  = keys.at(index),
              .value = 0,
            });
        }
        return keys.front() != 0;
    }

    event.value = 1;
    callback(event);
    event.value = 0;
    callback(event);
    return true;
}

bool foresight::parse_modifier(std::u32string_view const mod_str, code32_callback callback) {
    return parse_modifier(mod_str, [&](key_event_code const &key) {
        callback(to_code(key));
    });
}

bool foresight::parse_modifier(std::u32string_view const mod_str, user_event_callback callback) {
    return parse_modifier(mod_str, [&](key_event_code const &key) {
        callback(user_event{.type = EV_KEY, .code = key.code, .value = key.value});
        callback(syn_user_event);
    });
}

std::u32string foresight::parse_modifier(std::u32string_view const mod_str) {
    std::u32string result;
    if (!parse_modifier(mod_str,
                        [&](key_event_code const &key) {
                            result += to_code(key);
                        })) [[unlikely]]
    {
        result.clear();
    };
    return result;
}

void foresight::on_modifier_tags(std::u32string_view const                       str,
                                 std::function<void(std::u32string_view)> const &callback) noexcept {
    std::size_t index = 0;
    for (;;) {
        // find the first modifier:
        auto const lhsptr = find_delim(str, U'<', index);
        if (lhsptr == std::u32string_view::npos) {
            break;
        }
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const code   = str.substr(0, rhsptr);

        callback(code);

        index = rhsptr;
    }
}

void foresight::replace_modifiers_and_actions(std::u32string &str) noexcept {
    std::size_t index = 0;
    for (;;) {
        // find the first modifier:
        auto const lhsptr = find_delim(str, U'<', index);
        if (lhsptr == std::u32string_view::npos) {
            break;
        }
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const code   = str.substr(0, rhsptr);

        auto const encoded = parse_modifier(code);
        str.replace(lhsptr, code.size(), encoded);

        index = lhsptr + encoded.size();
    }
}

// NOLINTEND(*-magic-numbers)
