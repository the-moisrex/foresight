// Created by moisrex on 8/20/26.

module;
#include <cstdint>
#include <linux/input-event-codes.h>
export module fs8.mods:scale;
import fs8.context;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    /// Scale pen (absolute) movement events by a factor.
    ///
    /// Multiplies every `ABS_X` / `ABS_Y` value by the given `factor` with
    /// sub-pixel epsilon accumulation to prevent drift.  Place this *before*
    /// `abs2rel` so the scaled absolute positions are converted to relative
    /// deltas downstream.
    ///
    /// Epsilon is reset on tool-change events (`BTN_TOOL_*`) so a new stroke
    /// starts fresh.
    ///
    /// @par Example
    /// @code
    ///   | scale_pen[0.5f]   // halve pen movements
    ///   | abs2rel
    ///   | pen2mice
    /// @endcode
    constexpr struct [[nodiscard]] basic_scale_pen : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        float factor_    = 1.0f;
        float x_epsilon_ = 0.0f;
        float y_epsilon_ = 0.0f;

      public:
        constexpr explicit basic_scale_pen(float const factor) noexcept : factor_{factor} {}

        consteval basic_scale_pen operator[](float const factor) const noexcept {
            return basic_scale_pen{factor};
        }

        void operator()(auto&&, Tag auto) = delete;

        context_action operator()(event_type& event) noexcept;
    } scale_pen;

    /// Scale mouse (relative) movement events by a factor.
    ///
    /// Multiplies every `REL_X` / `REL_Y` value by the given `factor` with
    /// sub-pixel epsilon accumulation to prevent drift.  Place this *after*
    /// `abs2rel` (for pen) or directly in the pipeline (for mouse).
    ///
    /// @par Example
    /// @code
    ///   | scale_move[0.5f]  // halve mouse movements
    ///   | scale_move[2.0f]  // double mouse movements
    /// @endcode
    ///
    /// Gate with `on` / `on_held` to apply conditionally:
    /// @code
    ///   | on_held[KEY_LEFTSHIFT, scale_move[0.5f]]
    /// @endcode
    constexpr struct [[nodiscard]] basic_scale_move : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        float factor_    = 1.0f;
        float x_epsilon_ = 0.0f;
        float y_epsilon_ = 0.0f;

      public:
        constexpr explicit basic_scale_move(float const factor) noexcept : factor_{factor} {}

        consteval basic_scale_move operator[](float const factor) const noexcept {
            return basic_scale_move{factor};
        }

        void operator()(auto&&, Tag auto) = delete;

        context_action operator()(event_type& event) noexcept;
    } scale_move;

} // namespace fs8
