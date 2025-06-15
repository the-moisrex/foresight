
module;
#include <cmath>
#include <linux/input-event-codes.h>
export module foresight.mods.smooth;
import foresight.mods.mouse_status;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight::mods {

    /**
     * Linear Interpolation
     *   lerp(start, end, t) = start × (1 − t) + end × t
     */
    constexpr struct [[nodiscard]] basic_lerp {
      private:
        float t_val = 0.95;

      public:
        constexpr explicit basic_lerp(float const inp_t) noexcept : t_val{inp_t} {}

        constexpr basic_lerp() noexcept                        = default;
        consteval basic_lerp(basic_lerp const&)                = default;
        constexpr basic_lerp(basic_lerp&&) noexcept            = default;
        consteval basic_lerp& operator=(basic_lerp const&)     = default;
        constexpr basic_lerp& operator=(basic_lerp&&) noexcept = default;
        constexpr ~basic_lerp() noexcept                       = default;

        consteval basic_lerp operator()(float const inp_t) const noexcept {
            return basic_lerp{inp_t};
        }

        context_action operator()(Context auto& ctx) const noexcept {
            using enum context_action;
            using value_type = event_type::value_type;

            auto&       out   = ctx.mod(output_mod);
            auto&       event = ctx.event();
            auto const& mhist = ctx.mod(mouse_history_mod);

            if (is_mouse_movement(event)) {
                return ignore_event;
            } else if (!is_syn(event)) {
                return next;
            }

            auto const  cur            = mhist.cur();
            auto const  prev           = mhist.prev();
            float const x_interpolated = prev.x + t_val * (cur.x - prev.x);
            float const y_interpolated = prev.y + t_val * (cur.y - prev.y);
            auto const  x_lerped       = static_cast<value_type>(std::round(x_interpolated));
            auto const  y_lerped       = static_cast<value_type>(std::round(y_interpolated));

            out.emit(EV_REL, REL_X, x_lerped);
            out.emit(EV_REL, REL_Y, y_lerped);

            // Send Syn
            event.reset_time();
            return next;
        }
    } lerp;

    /**
     * Low-pass filtering is a common technique used to smooth out the variations in sensor data, including
     * mouse movements. The idea is to allow only low-frequency signals to pass through while attenuating the
     * high-frequency signals (rapid changes in mouse position).
     *
     * Notes on the Code:
     *   1. Smoothing Factor (alpha): You can experiment with different values for alpha. A smaller value will
     *      result in more smoothing (more storage of past values), whereas a larger value will make the
     *      output closer to the raw input.
     *   2. State Management: The variables prev_x and prev_y are used to keep track of the smoothed values
     *      from the last event. In a real-world implementation, you might want to ensure thread-safety if
     *      this function is called from multiple threads.
     *   3. Event Emission: Adjust the event emission as necessary to match the expected event types in your
     *      OS or input handling code.
     *
     * You could also look into more advanced filtering algorithms such as Kalman filters.
     *
     * smoothed = α × newInput + (1 − α) × previousSmoothed
     */
    constexpr struct [[nodiscard]] basic_low_pass_filter {
        using value_type = event_type::value_type;

      private:
        float prev_x = 0.f;
        float prev_y = 0.f;

        float alpha = 0.9f; // Adjust as needed (0 < alpha < 1)

      public:
        constexpr explicit basic_low_pass_filter(float const inp_alpha) noexcept : alpha{inp_alpha} {}

        constexpr basic_low_pass_filter() noexcept                                   = default;
        consteval basic_low_pass_filter(basic_low_pass_filter const&)                = default;
        constexpr basic_low_pass_filter(basic_low_pass_filter&&) noexcept            = default;
        consteval basic_low_pass_filter& operator=(basic_low_pass_filter const&)     = default;
        constexpr basic_low_pass_filter& operator=(basic_low_pass_filter&&) noexcept = default;
        constexpr ~basic_low_pass_filter() noexcept                                  = default;

        consteval basic_low_pass_filter operator()(float const inp_alpha) const noexcept {
            return basic_low_pass_filter{inp_alpha};
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;

            auto&       out   = ctx.mod(output_mod);
            auto&       event = ctx.event();
            auto const& mhist = ctx.mod(mouse_history_mod);
            auto const  cur   = mhist.cur();

            if (is_mouse_movement(event)) {
                return ignore_event;
            } else if (!is_syn(event)) {
                return next;
            }

            // Apply the low-pass filter
            float const smoothed_x = alpha * static_cast<float>(cur.x) + (1.f - alpha) * prev_x;
            float const smoothed_y = alpha * static_cast<float>(cur.y) + (1.f - alpha) * prev_y;

            // Update previous values
            prev_x = smoothed_x;
            prev_y = smoothed_y;

            // Emit the smoothed values
            out.emit(EV_REL, REL_X, static_cast<value_type>(std::round(smoothed_x)));
            out.emit(EV_REL, REL_Y, static_cast<value_type>(std::round(smoothed_y)));

            // Send Syn
            event.reset_time();
            return next;
        }
    } low_pass_filter;

} // namespace foresight::mods
