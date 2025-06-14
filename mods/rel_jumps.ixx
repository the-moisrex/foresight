// Created by moisrex on 6/8/25.

module;
#include <cmath>
export module foresight.mods.rel_jumps;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight::mods {

    constexpr struct [[nodiscard]] ignore_big_jumps_type {
      private:
        event_type::value_type threshold = 30; // pixels to resistence to move
      public:
        constexpr ignore_big_jumps_type() noexcept = default;

        constexpr explicit ignore_big_jumps_type(event_type::value_type const inp_threshold) noexcept
          : threshold{inp_threshold} {}

        constexpr ignore_big_jumps_type(ignore_big_jumps_type const&) noexcept            = default;
        constexpr ignore_big_jumps_type(ignore_big_jumps_type&&) noexcept                 = default;
        constexpr ignore_big_jumps_type& operator=(ignore_big_jumps_type const&) noexcept = default;
        constexpr ignore_big_jumps_type& operator=(ignore_big_jumps_type&&) noexcept      = default;
        constexpr ~ignore_big_jumps_type() noexcept                                       = default;

        [[nodiscard]] constexpr context_action operator()(Context auto& ctx) const noexcept {
            using enum context_action;
            if (is_mouse_movement(ctx.event()) && std::abs(value(ctx)) > threshold) {
                return ignore_event;
            }
            return next;
        }
    } ignore_big_jumps;

} // namespace foresight::mods
