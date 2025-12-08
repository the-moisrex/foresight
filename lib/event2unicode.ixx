// Created by moisrex on 10/29/25.

module;
#include <span>
#include <string>
export module foresight.lib.xkb.event2unicode;
import foresight.mods.event;
import foresight.lib.xkb;

export namespace fs8::xkb {

    constexpr struct [[nodiscard]] handle_modifiers_type {} handle_modifiers;

    /**
     * Convert an event into a Unicode Code Point
     * Don't handle control modifiers like `ctrl` and what not
     * https://www.x.org/releases/current/doc/kbproto/xkbproto.html#Interpreting_the_Control_Modifier
     */
    [[nodiscard]] char32_t       event2unicode(basic_state const&, key_event event, handle_modifiers_type) noexcept;

    /**
     * Convert an event into a Unicode Code Point
     */
    [[nodiscard]] char32_t       event2unicode(basic_state const&, key_event event) noexcept;

    /**
     * Convert a series of events into a UTF-32 string
     */
    [[nodiscard]] std::u32string event2unicode(basic_state const&, std::span<event_type const> events);

} // namespace fs8::xkb
