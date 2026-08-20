// Created by moisrex on 8/16/26.

module;
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <string>
#include <string_view>
module fs8.mods;
import fs8.lib.mod_parser;
import fs8.log;

namespace {
    /// Should the current word be reset when this code point was produced?
    [[nodiscard]] constexpr bool is_reset_code(fs8::code32_t const code) noexcept {
        if ((code & fs8::event_encoded_code32_t) == fs8::event_encoded_code32_t) {
            return true; // special keys (F-keys, arrows, combos, ...)
        }
        if (code < 0x20U || code == 0x7FU) {
            return true; // control characters
        }
        switch (code) {
            case U' ':
            case U'\t':
            case U'\n':
            case U'\r':
            case U'\v':
            case U'\f': return true;
            default: return false;
        }
    }

    /// Decode a UTF-8 string into a UTF-32 string (used for the buffer bookkeeping).
    [[nodiscard]] std::u32string to_u32(std::string_view str) {
        std::u32string out;
        out.reserve(str.size());
        while (!str.empty()) {
            auto const cp = fs8::utf8_next_code_point(str);
            if (cp == fs8::invalid_code_point) [[unlikely]] {
                str.remove_prefix(1);
                continue;
            }
            out += cp;
        }
        return out;
    }
} // namespace

template <>
struct fs8::pimpl_idiom<fs8::basic_autocomplete>::impl {
    std::u32string        buffer;     // the current word being typed
    std::u32string        prefix;     // decoded PREFIX of the pattern
    std::string           completion; // raw COMPLETION of the pattern (tags intact)
    event_type::code_type trigger_code = KEY_MAX;
    bool                  valid        = false;
};

fs8::context_action fs8::basic_autocomplete::on_start() noexcept try {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->valid = false;

    // the first modifier tag in the pattern is the trigger/separator
    std::size_t begin = 0;
    std::size_t end   = 0;
    if (!find_modifier_tag(pattern, 0, begin, end)) {
        log("autocomplete: pattern '{}' has no trigger tag (e.g. <tab>).", pattern);
        return context_action::next;
    }

    auto const prefix_sv     = pattern.substr(0, begin);
    auto const tag_sv        = pattern.substr(begin, end - begin);
    auto const completion_sv = pattern.substr(end);

    event_type::code_type trigger = KEY_MAX;

    std::ignore = parse_modifier(tag_sv, [&](key_event const& key) noexcept {
        if (trigger == KEY_MAX) {
            trigger = key.code;
        }
    });

    if (trigger == KEY_MAX || is_invalid(key_event{.code = trigger})) {
        log("autocomplete: invalid trigger tag '{}'.", tag_sv);
        return context_action::next;
    }

    pimpl->trigger_code = trigger;
    pimpl->prefix       = to_u32(prefix_sv);
    pimpl->completion   = completion_sv;

    if (auto_mode && pimpl->prefix.empty()) {
        log("autocomplete: auto mode requires a non-empty prefix in '{}'.", pattern);
        return context_action::next;
    }

    pimpl->valid = true;
    return context_action::next;
} catch (...) {
    // keep the mod disabled instead of terminating the whole pipeline
    return context_action::next;
}

fs8::context_action fs8::basic_autocomplete::on_event(event_type const&                               event,
                                                      std::function_ref<void(std::string_view)> const inp_emit) noexcept {
    using enum context_action;
    if (event.type() != EV_KEY) {
        return next;
    }
    if (pimpl.get() == nullptr || !pimpl->valid) [[unlikely]] {
        return next;
    }
    auto const key = static_cast<key_event>(event);

    // feed the keyboard state and get the code point for this key
    auto const code = unicode_encoded_event(keyboard_state, key);

    if (key.value != 1) {
        return next; // only track keydowns
    }

    if (key.code == KEY_BACKSPACE) {
        if (!pimpl->buffer.empty()) {
            pimpl->buffer.pop_back();
        }
        return next;
    }

    // trigger mode: complete when the trigger key is pressed after the prefix
    if (!auto_mode && key.code == pimpl->trigger_code) {
        if (pimpl->prefix.empty() || pimpl->buffer.ends_with(pimpl->prefix)) {
            inp_emit(pimpl->completion);
            pimpl->buffer  = pimpl->prefix;
            pimpl->buffer += to_u32(pimpl->completion);
            return pass_trigger ? next : ignore_event;
        }
    }

    if (is_modifier_key(key.code)) {
        return next; // don't disturb the current word
    }

    if (is_reset_code(code)) {
        pimpl->buffer.clear();
        return next;
    }

    // printable character → extend the current word
    pimpl->buffer += code;

    // auto mode: complete as soon as the prefix is fully typed
    if (auto_mode && !pimpl->prefix.empty() && pimpl->buffer.ends_with(pimpl->prefix)) {
        inp_emit(pimpl->completion);
        pimpl->buffer  = pimpl->prefix;
        pimpl->buffer += to_u32(pimpl->completion);
    }
    return next;
}
