#include <cmath>
#include <linux/input.h>
#include <unistd.h>

using code_type  = decltype(input_event::code);
using ev_type    = decltype(input_event::type);
using value_type = decltype(input_event::value);

namespace {
    void emit(input_event event, ev_type const type, code_type const code, value_type const value) noexcept {
        event.type  = type;
        event.code  = code;
        event.value = value;
        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    void emit(ev_type const type, code_type const code, value_type const value) noexcept {
        input_event event{};
        gettimeofday(&event.time, nullptr);
        event.type  = type;
        event.code  = code;
        event.value = value;
        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    void emit_syn() noexcept {
        emit(EV_SYN, SYN_REPORT, 0);
    }
} // anonymous namespace

int main() {
    input_event event{};

    // Global variables to store the last absolute position received from the tablet.
    // Initialized to a value that signifies "not yet set".
    value_type last_abs_x = std::numeric_limits<int>::min();
    value_type last_abs_y = std::numeric_limits<int>::min();

    for (;;) {
        if (read(STDIN_FILENO, &event, sizeof(event)) == 0) { // EOF
            break;
        }

        // Process the event based on its type
        switch (event.type) {
            case EV_ABS: {
                // Absolute position event from tablet
                if (event.code == ABS_X) {
                    if (last_abs_x != std::numeric_limits<int>::min()) {
                        auto const delta_x = event.value - last_abs_x;
                        if (delta_x > 0) {
                            emit(event, EV_REL, REL_X, delta_x);
                        }
                    }
                    last_abs_x = event.value; // Update last X position
                } else if (event.code == ABS_Y) {
                    if (last_abs_y != std::numeric_limits<int>::min()) {
                        auto const delta_y = event.value - last_abs_y;
                        if (delta_y > 0) {
                            emit(event, EV_REL, REL_Y, delta_y);
                        }
                    }
                    last_abs_y = event.value; // Update last Y position
                }
                // Handle other absolute events if needed (e.g., ABS_PRESSURE, ABS_TILT)
                // For a basic mouse mode, we might ignore them or map pressure to button.
                continue;
            }
            case EV_KEY: {
                // Button event from tablet
                if (event.code == BTN_TOUCH) {
                    // Map pen tip contact to left mouse button
                    event.code = BTN_LEFT;
                }
                // Pass through other stylus buttons directly (e.g., BTN_STYLUS, BTN_STYLUS2)
                // These might be mapped to middle/right click by desktop environment.
                break;
            }
            case EV_SYN:
                // Synchronization event: always pass through
                // This is crucial for the kernel to process the events.
                break;
            default:
                continue;
                // Other event types (e.g., EV_SW, EV_MSC) are ignored for this mouse mode.
        }
        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    return 0;
}
