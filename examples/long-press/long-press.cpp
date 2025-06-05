#include <cassert>
#include <chrono>
#include <linux/input.h>
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
        event.type  = type;
        event.code  = code;
        event.value = value;
        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    void emit_syn() noexcept {
        emit(EV_SYN, SYN_REPORT, 0);
    }
} // anonymous namespace

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

static constexpr value_type threshold       = 100; // pixels to resistence to move
static constexpr auto       long_press_time = std::chrono::milliseconds(500);

int main() {
    input_event event{};

    key_status left_click{BTN_LEFT};
    key_status right_click{BTN_RIGHT};
    auto       start = std::chrono::steady_clock::now();

    value_type dx = 0;
    value_type dy = 0;

    timeval timeout{};

    fd_set set{};

    bool lock = false;

    for (;;) {
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        timeout.tv_sec  = 0;                              // seconds part of timeout
        timeout.tv_usec = 1000 * long_press_time.count(); // microseconds part (500,000 µs = 500 ms)

        int const rv = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
        if (rv > 0 && FD_ISSET(STDIN_FILENO, &set)) {
            auto const len = read(STDIN_FILENO, &event, sizeof(event));
            if (len == 0) {
                break;
            }

            left_click.process(event);
            right_click.process(event);

            if (left_click.is_pressed()) {
                start = std::chrono::steady_clock::now();
                dx    = 0;
                dy    = 0;
            } else {
                lock = false;
            }

            if (event.type == EV_REL) {
                switch (event.code) {
                    case REL_X: dx += event.value; break;
                    case REL_Y: dy += event.value; break;
                    default: break;
                }
            }

            if (!lock) {
                std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
            }
        }

        auto const diff = std::chrono::steady_clock::now() - start;
        if (!lock && !right_click.is_pressed() && left_click.is_pressed() && diff >= long_press_time) {
            emit(EV_KEY, BTN_LEFT, 0);
            emit_syn();
            emit(EV_KEY, BTN_RIGHT, 1);
            emit_syn();
            emit(EV_KEY, BTN_RIGHT, 0);
            emit_syn();

            lock = true;
        }
    }

    return 0;
}
