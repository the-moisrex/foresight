module;
#include <chrono>
#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <tuple>
#include <utility>
module foresight.mods.on;

using fs8::basic_swipe_detector;

void basic_swipe_detector::reset() noexcept {
    cur_x = 0;
    cur_y = 0;
}

bool basic_swipe_detector::is_active(value_type const x_axis, value_type const y_axis) const noexcept {
    using std::abs;
    using std::signbit;
    return (abs(cur_x) >= abs(x_axis))
           && (signbit(cur_x) == signbit(x_axis) || x_axis == 0)
           &&                                                     // X
           (abs(cur_y) >= abs(y_axis))
           && (signbit(cur_y) == signbit(y_axis) || y_axis == 0); // Y
}

std::pair<std::uint16_t, std::uint16_t> basic_swipe_detector::passed_threshold_count(
  value_type const x_axis,
  value_type const y_axis) const noexcept {
    using std::abs;
    using std::signbit; // Required for checking signs

    std::uint16_t x_multiples = 0;
    std::uint16_t y_multiples = 0;

    // Calculate multiples for X
    if (x_axis != 0) {
        // Check if cur_x and x_axis have the same sign (or if cur_x is zero)
        // If they do, then calculate multiples. Otherwise, the count is 0.
        if (signbit(cur_x) == signbit(x_axis)) {
            x_multiples = static_cast<std::uint16_t>(abs(cur_x) / abs(x_axis));
        }
    }
    // If x_axis is 0, x_multiples remains 0, which is correct.

    // Calculate multiples for Y
    if (y_axis != 0) {
        // Check if cur_y and y_axis have the same sign (or if cur_y is zero)
        if (signbit(cur_y) == signbit(y_axis)) {
            y_multiples = static_cast<std::uint16_t>(abs(cur_y) / abs(y_axis));
        }
    }
    // If y_axis is 0, y_multiples remains 0, which is correct.

    return {x_multiples, y_multiples};
}

void basic_swipe_detector::operator()(event_type const& event) noexcept {
    if (event.type() == EV_KEY && event.code() == BTN_LEFT) {
        reset();
        return;
    }
    if (is_mouse_movement(event)) {
        switch (event.code()) {
            case REL_X: cur_x += event.value(); break;
            case REL_Y: cur_y += event.value(); break;
            default: break;
        }
    }
}

bool fs8::multi_click::operator()(event_type const& event) noexcept {
    auto const now = event.micro_time();
    if (event != usr) {
        return false;
    }
    auto const dur = now - std::exchange(last_click, now);
    if (dur <= std::chrono::milliseconds(1)) {
        return false; // ignore less than 1 millisecond clicks
    }
    if (dur > duration_threshold) {
        cur_count = 1;
        return false;
    }
    bool const res = ++cur_count >= count;
    if (res) {
        cur_count = 0;
    }
    return res;
}
