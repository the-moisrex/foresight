// Created by moisrex on 6/8/25.

module;
#include <chrono>
export module foresight.mods.rel_jumps;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight {

    constexpr struct [[nodiscard]] ignore_big_jumps_type {
        using value_type = event_type::value_type;

      private:
        value_type threshold = 20; // pixels to resistance to move
      public:
        constexpr ignore_big_jumps_type() noexcept = default;

        constexpr explicit ignore_big_jumps_type(value_type const inp_threshold) noexcept
          : threshold{inp_threshold} {}

        consteval ignore_big_jumps_type(ignore_big_jumps_type const&) noexcept            = default;
        constexpr ignore_big_jumps_type(ignore_big_jumps_type&&) noexcept                 = default;
        consteval ignore_big_jumps_type& operator=(ignore_big_jumps_type const&) noexcept = default;
        constexpr ignore_big_jumps_type& operator=(ignore_big_jumps_type&&) noexcept      = default;
        constexpr ~ignore_big_jumps_type() noexcept                                       = default;

        consteval ignore_big_jumps_type operator()(value_type const inp_threshold) const noexcept {
            return ignore_big_jumps_type{inp_threshold};
        }

        context_action operator()(event_type const& event) const noexcept;
    } ignore_big_jumps;

    constexpr struct [[nodiscard]] ignore_init_moves_type {
        using value_type = event_type::value_type;
        using msec_type  = std::chrono::microseconds;

      private:
        static constexpr msec_type default_time_threshold = std::chrono::milliseconds(1000);
        // user defined thresholds:
        value_type threshold                              = 7; // pixels to resistance to move
        msec_type  time_threshold{default_time_threshold};

        // active values:
        value_type init_distance    = 0;
        bool       is_left_btn_down = false;
        msec_type  last_moved{0};

      public:
        constexpr ignore_init_moves_type() noexcept = default;

        constexpr explicit ignore_init_moves_type(
          value_type const inp_threshold,
          msec_type const  inp_time_threshold = default_time_threshold) noexcept
          : threshold{inp_threshold},
            time_threshold{inp_time_threshold} {}

        constexpr ignore_init_moves_type(ignore_init_moves_type const&) noexcept            = default;
        constexpr ignore_init_moves_type(ignore_init_moves_type&&) noexcept                 = default;
        constexpr ignore_init_moves_type& operator=(ignore_init_moves_type const&) noexcept = default;
        constexpr ignore_init_moves_type& operator=(ignore_init_moves_type&&) noexcept      = default;
        constexpr ~ignore_init_moves_type() noexcept                                        = default;

        consteval ignore_init_moves_type operator()(
          value_type const inp_threshold,
          msec_type const  inp_time_threshold = default_time_threshold) const noexcept {
            return ignore_init_moves_type{inp_threshold, inp_time_threshold};
        }

        context_action operator()(event_type const& event) noexcept;
    } ignore_init_moves;


} // namespace foresight
