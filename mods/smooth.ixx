
module;
#include <algorithm> // For std::min
#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
export module foresight.mods.smooth;
import foresight.mods.mouse_status;
import foresight.mods.event;
import foresight.mods.context;
import foresight.utils.easings;
import foresight.main.log;

export namespace foresight {

    /**
     * Linear Interpolation
     *   lerp(start, end, t) = start × (1 − t) + end × t
     */
    constexpr struct [[nodiscard]] basic_lerp {
        using value_type = event_type::value_type;

      private:
        std::array<value_type, 2> cur_vals = {0, 0};
        bool                      cached   = false;

        constexpr value_type next_step(value_type const  step,
                                       value_type const  total_steps,
                                       std::size_t const axis = REL_X) const noexcept {
            static_assert(REL_X == 0x0 && REL_Y == 0x1,
                          "We need REL_X and REL_Y events' values to be 0 and 1.");

            float const t_normalized  = static_cast<float>(step) / (static_cast<float>(total_steps) - 1);
            float       interpolated  = t_normalized; // easeInQuad(t_normalized);
            interpolated             *= static_cast<float>(cur_vals[axis]);
            return static_cast<value_type>(interpolated);
        }

      public:
        constexpr basic_lerp() noexcept                        = default;
        consteval basic_lerp(basic_lerp const&)                = default;
        constexpr basic_lerp(basic_lerp&&) noexcept            = default;
        consteval basic_lerp& operator=(basic_lerp const&)     = default;
        constexpr basic_lerp& operator=(basic_lerp&&) noexcept = default;
        constexpr ~basic_lerp() noexcept                       = default;

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;

            auto& event = ctx.event();
            if (is_mouse_movement(event)) {
                cur_vals[event.code()] = event.value();
                cached                 = true;
                return ignore_event;
            }
            if (!is_syn(event) || !cached) {
                return next;
            }
            cached = false;

            auto const total_steps = std::max(std::abs(cur_vals[REL_X]), std::abs(cur_vals[REL_Y]));
            if (total_steps <= 2) {
                return next;
            }
            value_type all_x = 0;
            value_type all_y = 0;
            for (value_type step = 1; step < total_steps; ++step) {
                auto const cur_x  = next_step(step, total_steps, REL_X);
                auto const cur_y  = next_step(step, total_steps, REL_Y);
                auto const rel_x  = cur_x - all_x;
                auto const rel_y  = cur_y - all_y;
                all_x            += rel_x;
                all_y            += rel_y;
                if (cur_x == 0 && cur_y == 0) {
                    log("{}/{} {} {} ({}, {})", step, total_steps, cur_x, cur_y, all_x, all_y);
                    continue;
                }
                log("{}/{} {} {} ({}, {}) ||||", step, total_steps, cur_x, cur_y, all_x, all_y);
                std::ignore =
                  ctx.fork_emit(event | user_event{.type = EV_REL, .code = REL_X, .value = rel_x});
                std::ignore =
                  ctx.fork_emit(event | user_event{.type = EV_REL, .code = REL_Y, .value = rel_y});
                std::ignore = ctx.fork_emit(syn());
            }

            return ignore_event;
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

            auto&       event = ctx.event();
            auto const& mhist = ctx.mod(mouse_history);
            auto const  cur   = mhist.cur();

            if (is_mouse_movement(event)) {
                return ignore_event;
            }
            if (!is_syn(event)) {
                return next;
            }

            // Apply the low-pass filter
            float const smoothed_x = (alpha * static_cast<float>(cur.x)) + ((1.f - alpha) * prev_x);
            float const smoothed_y = (alpha * static_cast<float>(cur.y)) + ((1.f - alpha) * prev_y);

            // Update previous values
            prev_x = smoothed_x;
            prev_y = smoothed_y;

            // Emit the smoothed values
            std::ignore = ctx.fork_emit(EV_REL, REL_X, static_cast<value_type>(std::round(smoothed_x)));
            std::ignore = ctx.fork_emit(EV_REL, REL_Y, static_cast<value_type>(std::round(smoothed_y)));

            // Send Syn
            event.reset_time();
            return next;
        }
    } low_pass_filter;

    /**
     * Kalman Filter
     */
    constexpr struct [[nodiscard]] basic_kalman_filter {
        using value_type = event_type::value_type;

      private:
        float prev_x = 0.f;
        float prev_y = 0.f;

        float q = 0.1f;  // Process noise covariance
        float r = 0.5f;  // Measurement noise covariance

        float k_x = 0.f; // Kalman gain for x
        float k_y = 0.f; // Kalman gain for y

      public:
        constexpr explicit basic_kalman_filter(float const process_noise,
                                               float const measurement_noise = 0.5f) noexcept
          : q(process_noise),
            r(measurement_noise) {}

        constexpr basic_kalman_filter() noexcept                                 = default;
        consteval basic_kalman_filter(basic_kalman_filter const&)                = default;
        constexpr basic_kalman_filter(basic_kalman_filter&&) noexcept            = default;
        consteval basic_kalman_filter& operator=(basic_kalman_filter const&)     = default;
        constexpr basic_kalman_filter& operator=(basic_kalman_filter&&) noexcept = default;
        constexpr ~basic_kalman_filter() noexcept                                = default;

        consteval basic_kalman_filter operator()(float const process_noise,
                                                 float const measurement_noise = 0.5f) const noexcept {
            return basic_kalman_filter{process_noise, measurement_noise};
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;

            auto&       event = ctx.event();
            auto const& mhist = ctx.mod(mouse_history);
            auto const  cur   = mhist.cur();

            if (is_mouse_movement(event)) {
                return ignore_event;
            }
            if (!is_syn(event)) {
                return next;
            }

            // Predict step
            float const predicted_x = prev_x; // The predicted position
            float const predicted_y = prev_y; // The predicted position

            // Update uncertainty
            float const uncertainty_x = k_x + q;
            float const uncertainty_y = k_y + q;

            // Kalman Gain
            k_x = uncertainty_x / (uncertainty_x + r);
            k_y = uncertainty_y / (uncertainty_y + r);

            // Update step with new measurement
            float const smoothed_x = predicted_x + (k_x * (static_cast<float>(cur.x) - predicted_x));
            float const smoothed_y = predicted_y + (k_y * (static_cast<float>(cur.y) - predicted_y));

            // Update previous values
            prev_x = smoothed_x;
            prev_y = smoothed_y;

            // Emit the smoothed values
            auto const x_val = static_cast<value_type>(std::round(smoothed_x));
            auto const y_val = static_cast<value_type>(std::round(smoothed_y));
            std::ignore      = ctx.fork_emit(EV_REL, REL_X, x_val);
            std::ignore      = ctx.fork_emit(EV_REL, REL_Y, y_val);

            // Send Syn
            event.reset_time();
            return next;
        }
    } kalman_filter;

} // namespace foresight
