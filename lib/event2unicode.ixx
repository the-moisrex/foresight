// Created by moisrex on 10/29/25.

module;
#include <span>
#include <string>
export module foresight.lib.xkb.event2unicode;
import foresight.mods.event;
import foresight.lib.xkb;

export namespace foresight::xkb {

    [[nodiscard]] char32_t       event2unicode(basic_state const&, event_type const& event) noexcept;
    [[nodiscard]] std::u32string event2unicode(basic_state const&, std::span<event_type const> events);

} // namespace foresight::xkb
