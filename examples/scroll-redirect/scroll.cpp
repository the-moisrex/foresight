#include <algorithm>
#include <array>
#include <concepts>
#include <cstdio>
#include <iostream>
#include <linux/input.h>
#include <span>
#include <unistd.h>
#include <utility>

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
        event.type  = EV_SYN;
        event.code  = SYN_REPORT;
        event.value = 0;
        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    void emit_syn() noexcept {
        emit(EV_SYN, SYN_REPORT, 0);
    }
} // anonymous namespace

enum struct gesture_type {
    none,
    swipe_left,
    swipe_right,
    swipe_up,
    swipe_down,
};

static constexpr std::array<code_type, 3> mouse_buttons{BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};
static constexpr std::array<code_type, 4> all_buttons{BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_TOUCH};

struct gesture_detector {
  private:
    std::int32_t               start_x = 0;
    std::int32_t               start_y = 0;
    std::int32_t               x       = 0;
    std::int32_t               y       = 0;
    std::span<code_type const> btns;
    bool                       is_pressed = false;

    std::int32_t min_swipe_distance         = 50; // Pixels: Min movement on primary axis for a swipe
    std::int32_t max_perpendicular_movement = 30; // Pixels: Max movement on secondary axis for a swap

  public:
    explicit gesture_detector(std::span<code_type const> const inp_btns) noexcept : btns(inp_btns) {}

    gesture_detector(gesture_detector const&) noexcept            = default;
    gesture_detector(gesture_detector&&) noexcept                 = default;
    gesture_detector& operator=(gesture_detector const&) noexcept = default;
    gesture_detector& operator=(gesture_detector&&) noexcept      = default;
    ~gesture_detector() noexcept                                  = default;

    gesture_type detect(input_event const& event) noexcept {
        using enum gesture_type;
        if (!btns.empty() && !std::ranges::contains(btns, event.code)) {
            return none;
        }
        is_pressed = event.value == 1;
        if (!is_pressed) {
            start_x = x;
            start_y = y;
        }
        switch (event.type) {
            case EV_SYN: break;
            case EV_REL:
                switch (event.code) {
                    case REL_X: x += event.value; break;
                    case REL_Y: y += event.value; break;
                    default: break;
                }
                break;
            case EV_ABS: break;
            case EV_KEY: break;
            default: break;
        }
        return detect_swap();
    }

    [[nodiscard]] gesture_type detect_swap() const noexcept {
        using enum gesture_type;
        if (std::abs(x) > std::abs(y) && std::abs(x) > min_swipe_distance) {
            // Horizontal swipe (movement primarily on X-axis)
            if (std::abs(y) < max_perpendicular_movement) { // Ensure minimal vertical deviation
                if (x < 0) {
                    return swipe_left;
                }
                return swipe_right;
            }
        } else if (std::abs(y) > std::abs(x) && std::abs(y) > min_swipe_distance) {
            // Vertical swipe (movement primarily on Y-axis)
            if (std::abs(x) < max_perpendicular_movement) { // Ensure minimal horizontal deviation
                if (y < 0) {
                    return swipe_up;
                }
                return swipe_down;
            }
        }
        return none;
    }

    [[nodiscard]] std::uint32_t distance_x() const noexcept {
        return static_cast<std::uint32_t>(std::abs(x));
    }

    [[nodiscard]] std::uint32_t distance_y() const noexcept {
        return static_cast<std::uint32_t>(std::abs(y));
    }

    [[nodiscard]] std::uint32_t distance() const noexcept {
        return std::max(distance_x(), distance_y());
    }
};

/**
 * This class helps to check the last value of a key
 */
struct key_status {
  private:
    code_type  btn    = 0;
    value_type status = 0;

  public:
    explicit key_status(code_type const btn_code) noexcept : btn{btn_code} {}

    key_status(key_status const&)                = default;
    key_status(key_status&&) noexcept            = default;
    key_status& operator=(key_status const&)     = default;
    key_status& operator=(key_status&&) noexcept = default;
    ~key_status() noexcept                       = default;

    void process(input_event const& event) noexcept {
        if (event.type != EV_KEY) {
            return;
        }
        if (event.code == btn) {
            status = event.value;
        }
    }

    [[nodiscard]] bool is_pressed() const noexcept {
        return status == 1;
    }
};

struct keys_status {
  private:
    std::array<value_type, KEY_MAX> btns{};

  public:
    explicit keys_status() noexcept                = default;
    keys_status(keys_status const&)                = default;
    keys_status(keys_status&&) noexcept            = default;
    keys_status& operator=(keys_status const&)     = default;
    keys_status& operator=(keys_status&&) noexcept = default;
    ~keys_status() noexcept                        = default;

    void process(input_event const& event) noexcept {
        if (event.type != EV_KEY) {
            return;
        }
        btns.at(event.code) = event.value;
    }

    template <std::integral... T>
    [[nodiscard]] bool is_pressed(T const... key_codes) const noexcept {
        return ((btns.at(key_codes) != 0) && ...);
    }
};

int main() {
    input_event event{};

    // Example events:
    // Event: type 2 (EV_REL), code 0 (REL_X), value 1
    // Event: type 2 (EV_REL), code 1 (REL_Y), value 1
    // Event: type 2 (EV_REL), code 0 (REL_X), value 1
    // Event: type 2 (EV_REL), code 1 (REL_Y), value -1
    // Event: type 1 (EV_KEY), code 272 (BTN_LEFT), value 1
    // Event: type 1 (EV_KEY), code 272 (BTN_LEFT), value 0
    // Event: type 1 (EV_KEY), code 273 (BTN_RIGHT), value 1
    //
    // Event: type 2 (EV_REL), code 8 (REL_WHEEL), value -1
    // Event: type 2 (EV_REL), code 11 (REL_WHEEL_HI_RES), value -120
    // Event: type 2 (EV_REL), code 8 (REL_WHEEL), value 1
    // Event: type 2 (EV_REL), code 11 (REL_WHEEL_HI_RES), value 120



    keys_status keys;

    for (;;) {
        std::ignore = read(STDIN_FILENO, &event, sizeof(event));
        keys.process(event);

        if (event.type == EV_REL) {
            if (event.code == REL_X || event.code == REL_Y) {
                if (keys.is_pressed(BTN_LEFT, BTN_MIDDLE)) {
                    auto const val = event.value;
                    // std::cerr << "Type: " << event.type << " Code: " << event.code
                    //           << " Value: " << event.value << std::endl;

                    emit(event, EV_KEY, BTN_LEFT, 0);
                    emit(event, EV_KEY, BTN_MIDDLE, 0);
                    // emit_syn();

                    switch (event.code) {
                        case REL_X:
                            emit(event, EV_REL, REL_HWHEEL, val > 0 ? 1 : val < 0 ? -1 : 0);
                            emit(event, EV_REL, REL_HWHEEL_HI_RES, val > 0 ? 120 : val < 0 ? -120 : 0);
                            emit_syn();
                            break;
                        case REL_Y:
                            emit(event, EV_REL, REL_WHEEL, val > 0 ? 1 : val < 0 ? -1 : 0);
                            emit(event, EV_REL, REL_WHEEL_HI_RES, val > 0 ? 120 : val < 0 ? -120 : 0);
                            emit_syn();
                            break;
                        default: break;
                    }

                    // emit(event, EV_KEY, BTN_LEFT, 1);
                    // emit(event, EV_KEY, BTN_RIGHT, 1);
                    // emit_syn();
                    continue;
                }
            }
        }


        // std::this_thread::yield();
        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
        // ignore errors
    }

    return 0;
}
