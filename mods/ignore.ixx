// Created by moisrex on 6/25/25.

module;
export module foresight.mods.ignore;
import foresight.mods.context;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_ignore_abs {
        context_action operator()(event_type const& event) const noexcept;
    } ignore_abs;

    // todo: ignore_types(EV_ABS)
    // todo: ignore_codes(EV_BTN_TOOL_RUBBER)

} // namespace foresight
