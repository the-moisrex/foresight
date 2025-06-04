#include <cstdio>
#include <iostream>
#include <linux/input.h>
#include <thread>
#include <utility>

int main() {
    input_event event{};

    bool shift_pressed = false;
    bool ctrl_pressed  = false;

    while (std::fread(&event, sizeof(event), 1, stdin) == 1) {
        if (event.type == EV_KEY && event.value == 1) {
            shift_pressed |= event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT;
            ctrl_pressed  |= event.code == KEY_LEFTCTRL || event.code == KEY_RIGHTCTRL;
        } else if (event.type == EV_KEY && event.value == 0) {
            shift_pressed &= event.code != KEY_LEFTMETA && event.code != KEY_RIGHTSHIFT;
            ctrl_pressed  &= event.code != KEY_LEFTCTRL && event.code != KEY_RIGHTCTRL;
        }

        if (shift_pressed && ctrl_pressed && event.type == EV_REL) {
            switch (event.code) {
                case REL_X:
                    event.code   = REL_HWHEEL;
                    std::ignore  = std::fwrite(&event, sizeof(event), 1, stdout);
                    event.code   = REL_HWHEEL_HI_RES;
                    event.value = event.value > 0 ? 120 : -120;
                    // std::ignore  = std::fwrite(&event, sizeof(event), 1, stdout);
                    break;
                case REL_Y:
                    event.code   = REL_WHEEL;
                    std::ignore  = std::fwrite(&event, sizeof(event), 1, stdout);
                    event.code   = REL_WHEEL_HI_RES;
                    event.value = event.value > 0 ? 120 : -120;
                    // std::ignore  = std::fwrite(&event, sizeof(event), 1, stdout);
                    break;
                default: break;
            }
            // std::cerr << event.code << " " << event.value << "\n";
        }


        // std::this_thread::yield();
        std::ignore = std::fwrite(&event, sizeof(event), 1, stdout);
        // ignore errors
    }

    return 0;
}
