#include <cmath>
#include <linux/input.h>
#include <unistd.h>

using code_type  = decltype(input_event::code);
using ev_type    = decltype(input_event::type);
using value_type = decltype(input_event::value);


// Threshold for mouse movement speed (absolute delta value).
// Movements with `abs(delta)` less than or equal to this threshold will not be accelerated.
static constexpr value_type SPEED_THRESHOLD = 5; // Pixels/units. Adjust this value to your preference.

// Acceleration exponent for fast movements.
// A value > 1.0 means faster movements are accelerated more aggressively.
// E.g., 1.5 means a 10-unit movement becomes 10^1.5 = ~31.6 units (if delta > SPEED_THRESHOLD).
static constexpr double ACCELERATION_EXPONENT = 1.2; // Adjust this value to control acceleration intensity.

int main() {
    input_event event{};

    // Example events:
    // Event: type 2 (EV_REL), code 0 (REL_X), value 1
    // Event: type 2 (EV_REL), code 1 (REL_Y), value 1
    // Event: type 2 (EV_REL), code 0 (REL_X), value 1
    // Event: type 2 (EV_REL), code 1 (REL_Y), value -1

    for (;;) {
        if (read(STDIN_FILENO, &event, sizeof(event)) == 0) { // EOF
            break;
        }

        if (event.type == EV_REL && (event.code == REL_X || event.code == REL_Y)) {
            auto const delta = std::abs(static_cast<double>(event.value));

            if (delta > SPEED_THRESHOLD) {
                // Apply non-linear acceleration for fast movements.
                // The power function makes larger movements accelerate more aggressively.
                double const accelerated_delta = std::pow(delta, ACCELERATION_EXPONENT);

                // Preserve the original sign of the movement.
                event.value =
                  static_cast<value_type>(std::round(std::copysign(accelerated_delta, event.value)));
            }
            // If absolute_delta <= speed_threshold, ev.value remains unchanged (no acceleration).
        }

        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    return 0;
}
