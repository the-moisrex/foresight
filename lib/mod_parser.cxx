// Created by moisrex on 11/4/25.

module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <ranges>
#include <stdexcept>
module foresight.lib.mod_parser;
import foresight.mods.event;
import foresight.devices.key_codes;
import foresight.utils.hash;
import foresight.lib.xkb.event2unicode;
import foresight.main.log;

using fs8::user_event;

// NOLINTBEGIN(*-magic-numbers)
namespace {
    constexpr std::size_t max_simultaneous_key_presses = 32;

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
    template <typename CharT, typename CharT2>
    [[nodiscard]] bool iequals(std::basic_string_view<CharT> const  lhs,
                               std::basic_string_view<CharT2> const rhs) noexcept {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        static constexpr auto to_lower = []<typename CC>(CC const ch32) constexpr noexcept -> CC {
            if (ch32 >= static_cast<CC>('A') && ch32 <= static_cast<CC>('Z')) {
                return ch32 + static_cast<CC>(U'a' - U'A');
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
    constexpr std::array<mod_entry, 45> mod_table = []() consteval {
        // NOLINTBEGIN(*-use-designated-initializers)
        std::array<mod_entry, 45> data{
          {
           {U"alt", KEY_LEFTALT},
           {U"alt_gr", KEY_RIGHTALT},
           {U"altgr", KEY_RIGHTALT},
           {U"altgr", KEY_RIGHTALT},
           {U"caps", KEY_CAPSLOCK},
           {U"capslock", KEY_CAPSLOCK},
           {U"cmd", KEY_LEFTMETA},
           {U"command", KEY_LEFTMETA},
           {U"control", KEY_LEFTCTRL},
           {U"controlkey", KEY_LEFTCTRL},
           {U"ctl", KEY_LEFTCTRL},
           {U"ctrl", KEY_LEFTCTRL},
           {U"lctrl", KEY_LEFTCTRL},
           {U"leftalt", KEY_LEFTALT},
           {U"leftctrl", KEY_LEFTCTRL},
           {U"leftmeta", KEY_LEFTMETA},
           {U"leftshift", KEY_LEFTSHIFT},
           {U"lshift", KEY_LEFTSHIFT},
           {U"meta", KEY_LEFTMETA},
           {U"mod1", KEY_LEFTALT},
           {U"mod2", KEY_LEFTALT},
           {U"mod3", KEY_LEFTALT},
           {U"mod4", KEY_LEFTMETA},
           {U"mod5", KEY_LEFTALT},
           {U"num", KEY_NUMLOCK},
           {U"numlock", KEY_NUMLOCK},
           {U"opt", KEY_LEFTALT},
           {U"option", KEY_LEFTALT},
           {U"optionkey", KEY_LEFTALT},
           {U"rctrl", KEY_RIGHTCTRL},
           {U"rightalt", KEY_RIGHTALT},
           {U"rightctrl", KEY_RIGHTCTRL},
           {U"rightmeta", KEY_RIGHTMETA},
           {U"rightshift", KEY_RIGHTSHIFT},
           {U"rshift", KEY_RIGHTSHIFT},
           {U"scroll", KEY_SCROLLLOCK},
           {U"scrolllock", KEY_SCROLLLOCK},
           {U"shift", KEY_LEFTSHIFT},
           {U"super", KEY_LEFTMETA},
           {U"win", KEY_LEFTMETA},
           {U"windows", KEY_LEFTMETA},
           {U"⇧", KEY_LEFTSHIFT},
           {U"⊞", KEY_LEFTMETA},
           {U"⌘", KEY_LEFTMETA},
           {U"⌥", KEY_LEFTALT},
           }
        };
        for (auto &field : data) {
            field.hash = fs8::ci_hash(field.key);
        }

        // keep these sorted to use bubble sort
        std::ranges::sort(data, {}, &mod_entry::hash);
        return data;
        // NOLINTEND(*-use-designated-initializers)
    }();

    /**
     * Alternative keys
     * This function finds alternative representations of common keys.
     */
    template <typename CharT>
    std::uint16_t alternative_modifier(std::basic_string_view<CharT> const str) noexcept {
        auto const hid = fs8::ci_hash(str);

        // probe over table entries, hash-first to avoid expensive compares
        auto const it = std::ranges::lower_bound(mod_table, hid, {}, [](auto const &entry) {
            return entry.hash;
        });
        if (it == std::end(mod_table)) [[unlikely]] {
            return fs8::invalid_user_event.code;
        }
        auto const &[key, code, _] = *it;
        if (!iequals(key, str)) [[unlikely]] {
            return fs8::invalid_user_event.code;
        }
        return code;
    }

    template <typename CharT>
    [[nodiscard]] std::uint16_t get_modifier_code(std::basic_string_view<CharT> const key) noexcept {
        auto const code = fs8::key_code_of(key);
        return code != 0 ? code : alternative_modifier(key);
    }

    [[nodiscard]] fs8::code32_t to_code(fs8::key_event::code_type const  code,
                                        fs8::key_event::value_type const value = 1) noexcept {
        return static_cast<fs8::code32_t>(
          fs8::event_encoded_code32_t | fs8::hashed(fs8::key_event{.code = code, .value = value}));
    }

    // [[nodiscard]] fs8::code32_t to_code(fs8::event_type const &event) noexcept {
    //     return to_code(event.code(), event.value());
    // }

    [[nodiscard]] fs8::code32_t to_code(fs8::key_event const event) noexcept {
        return to_code(event.code, event.value);
    }

    [[nodiscard]] fs8::key_event to_event(fs8::code32_t const code) noexcept {
        return fs8::unhashed(
          ~static_cast<std::uint32_t>(fs8::event_encoded_code32_t) & static_cast<std::uint32_t>(code));
    }

    /// Check if we've run it through `to_code` or is it a normal Unicode code point
    [[nodiscard]] bool is_encoded_event(fs8::code32_t const code) noexcept {
        return (fs8::event_encoded_code32_t & code) == fs8::event_encoded_code32_t;
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

fs8::code32_t fs8::unicode_encoded_event(xkb::basic_state const &state, key_event const event) noexcept {
    auto const code_point = xkb::event2unicode(state, event);
    if (code_point == U'\0') {
        return to_code(event);
    }
    return code_point;
}

/// Reads the next UTF-8 sequence in a string
char32_t fs8::utf8_next_code_point(std::string_view &src) noexcept {
    char32_t   code_point = 0;
    auto const len        = utf8_sequence_length(src.front());

    if (src.empty() || len > src.size()) [[unlikely]] {
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

char32_t fs8::parse_char_or_codepoint(std::string_view &src) noexcept {
    assert(!src.empty());

    // Try to parse the parameter as a UTF-8 encoded single character
    char32_t codepoint = utf8_next_code_point(src);

    if (codepoint == 'U' && src.starts_with("+")) [[unlikely]] {
        char *endp = nullptr; // NOLINT(*-const-correctness)

        // U+NNNN format standard Unicode code point format
        src.remove_prefix(2);

        // Use strtol with explicit bases instead of `0` in order to avoid unexpected parsing as octal.
        errno     = 0;
        // NOLINTNEXTLINE(*-suspicious-stringview-data-usage)
        codepoint = static_cast<char32_t>(strtol(src.data(), &endp, 16));
        if (errno != 0 || !is_empty(endp) || codepoint > 0x10'FFFFU) [[unlikely]] {
            log("ERROR: Failed to convert to UTF-32");
            return invalid_code_point;
        }
        src.remove_prefix(static_cast<std::size_t>(endp - src.data()));
    }

    return codepoint;
}

namespace {
    template <typename CharT>
    std::size_t
    find_delim_impl(std::basic_string_view<CharT> str, CharT const delim, std::size_t const pos) noexcept {
        auto lhsptr = str.find(delim, pos);
        if (lhsptr == 0) {
            return lhsptr;
        }
        while (lhsptr != std::basic_string_view<CharT>::npos) {
            bool escaped = false;
            for (auto escape_pos = lhsptr; str.at(escape_pos) == U'\\'; --escape_pos) [[unlikely]] {
                escaped = !escaped;
            }
            if (!escaped) {
                break;
            }
            // skip the escaped ones
            lhsptr = str.find(delim, lhsptr + 1);
        }
        return lhsptr;
    }
} // namespace

std::size_t fs8::find_delim(std::string_view const str, char const delim, std::size_t const pos) noexcept {
    return find_delim_impl(str, delim, pos);
}

std::size_t
fs8::find_delim(std::u32string_view const str, char32_t const delim, std::size_t const pos) noexcept {
    return find_delim_impl(str, delim, pos);
}

namespace {

    template <typename CharT>
    bool parse_modifier_impl(std::basic_string_view<CharT> mod_str, fs8::key_code_callback callback) {
        assert(mod_str.starts_with(U'<') && mod_str.ends_with(U'>'));
        mod_str.remove_prefix(1);
        mod_str.remove_suffix(1);
        bool const     is_release   = mod_str.starts_with(U'/');
        auto           dash_start   = mod_str.find(U'-');
        bool const     is_monotonic = !is_release && dash_start == std::u32string_view::npos;
        fs8::key_event event;

        if (is_monotonic) {
            // handling <shift> or <ctrl> types
            event = {.code = get_modifier_code(mod_str), .value = 1};
        } else if (is_release) {
            // handling </shift> or </ctrl>
            mod_str.remove_prefix(1);
            event = {.code = get_modifier_code(mod_str), .value = 0};
        } else {
            // handling <C-r> type of mods
            constexpr auto max_len = static_cast<std::uint32_t>(max_simultaneous_key_presses);
            std::array<std::uint16_t, max_simultaneous_key_presses> keys{};
            std::uint32_t                                           index = 0;
            while (index != max_len) {
                auto const sub_mod = mod_str.substr(0, dash_start);
                if (sub_mod.empty()) {
                    break;
                }
                auto const ev = fs8::key_event{.code = get_modifier_code(sub_mod), .value = 1};
                if (is_invalid(ev)) [[unlikely]] {
                    break;
                }
                keys.at(index++) = ev.code;
                callback(ev); // keydown
                if (dash_start == std::u32string_view::npos) {
                    break;
                }
                mod_str.remove_prefix(dash_start + 1);
                dash_start = mod_str.find(U'-');
            }

            // release the keys in reverse order:
            for (std::uint32_t cindex = 0; cindex != index; ++cindex) {
                // keyup:
                callback({
                  .code  = keys.at(cindex),
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

    template <typename CharT>
    bool parse_modifier_impl(std::basic_string_view<CharT> const mod_str, fs8::code32_callback callback) {
        return parse_modifier_impl(mod_str, [&](fs8::key_event const &key) {
            callback(to_code(key));
        });
    }

    template <typename CharT>
    bool parse_modifier_impl(std::basic_string_view<CharT> const mod_str, fs8::user_event_callback callback) {
        return parse_modifier_impl(mod_str, [&](fs8::key_event const &key) {
            callback(user_event{.type = EV_KEY, .code = key.code, .value = key.value});
            callback(fs8::syn_user_event);
        });
    }

    template <typename CharT>
    std::u32string parse_modifier_impl(std::basic_string_view<CharT> const mod_str) {
        std::u32string result;
        if (!parse_modifier_impl(mod_str,
                                 [&](fs8::key_event const &key) {
                                     result += to_code(key);
                                 })) [[unlikely]]
        {
            result.clear();
        }
        return result;
    }
} // namespace

bool fs8::parse_modifier(std::u32string_view const mod_str, key_code_callback callback) {
    return parse_modifier_impl(mod_str, callback);
}

bool fs8::parse_modifier(std::u32string_view const mod_str, code32_callback callback) {
    return parse_modifier_impl(mod_str, callback);
}

bool fs8::parse_modifier(std::u32string_view const mod_str, user_event_callback callback) {
    return parse_modifier_impl(mod_str, callback);
}

bool fs8::parse_modifier(std::string_view const mod_str, key_code_callback callback) {
    return parse_modifier_impl(mod_str, callback);
}

bool fs8::parse_modifier(std::string_view const mod_str, code32_callback callback) {
    return parse_modifier_impl(mod_str, callback);
}

bool fs8::parse_modifier(std::string_view const mod_str, user_event_callback callback) {
    return parse_modifier_impl(mod_str, callback);
}

std::u32string fs8::parse_modifier(std::string_view const mod_str) {
    return parse_modifier_impl(mod_str);
}

std::u32string fs8::parse_modifier(std::u32string_view const mod_str) {
    return parse_modifier_impl(mod_str);
}

void fs8::on_modifier_tags(std::u32string_view const                       str,
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

bool fs8::normalize_modifiers(std::u32string &str) noexcept {
    xkb::basic_state keyboard_state;
    keyboard_state.initialize(xkb::get_default_keymap());

    bool                  was_normalized = true;
    event_type::code_type code_to_remove = KEY_MAX;
    auto                  lhs            = str.begin();
    auto const            beg            = str.begin();
    for (char32_t const code : str) {
        // Ignore Unicode Code Points
        if (!is_encoded_event(code)) {
            code_to_remove = KEY_MAX;
            ++lhs;
            continue;
        }

        auto const event   = to_event(code);
        auto const recoded = unicode_encoded_event(keyboard_state, event);
        if (event.value != 1 || is_encoded_event(recoded)) {
            if (code_to_remove == event.code) {
                continue; // remove current code point
            }
            assert(recoded == code);
            ++lhs;
            continue;
        }

        was_normalized = false;

        // Replace the event with the Unicode Code Point equivalent
        *lhs = recoded;
        ++lhs;

        code_to_remove = event.code;
    }
    str.resize(static_cast<std::size_t>(std::distance(beg, lhs)));
    return was_normalized;
}

void fs8::replace_modifier_strings(std::u32string &str) noexcept {
    std::size_t index = 0;
    for (;;) {
        // find the first modifier:
        auto const lhsptr = find_delim(str, U'<', index);
        if (lhsptr == std::u32string_view::npos) {
            break;
        }
        auto const rhsptr = find_delim(str, U'>', lhsptr);
        auto const code   = str.substr(0, rhsptr + 1);

        auto encoded = parse_modifier(code);
        for (char32_t const code : encoded) {
            log("Encoded: {:x}", static_cast<std::uint32_t>(code));
        }
        normalize_modifiers(encoded);
        for (char32_t const code : encoded) {
            log("Normalized: {:x}", static_cast<std::uint32_t>(code));
        }
        str.replace(lhsptr, code.size(), encoded);

        index = lhsptr + encoded.size();
    }
}

// NOLINTEND(*-magic-numbers)
