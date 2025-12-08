// Created by moisrex on 9/9/22.

module;
#include <algorithm>
#include <cstdio>
#include <linux/input.h>
#include <ranges>
#include <thread>
module foresight.main.keyboard;
import foresight.main.translate;
import foresight.main.log;

using fs8::keyboard;

keyboard::keyboard() {
    setbuf(stdin, nullptr);
    setbuf(stdout, nullptr);
}

void keyboard::to_string() {
    str.reserve(events.size());
    for ([[maybe_unused]] auto const &[time, type, code, value] : events) {
        char const cur = fs8::to_char(static_cast<std::uint8_t>(code));
        str.push_back(cur);
    }
}

void keyboard::replace(std::string_view const replace_it, std::string_view const replace_with) {
    backspace(replace_it.size());
    put(replace_with);
    events.clear();
}

void keyboard::backspace(std::size_t count) {
    for (; count != 0; count--) {
        put(input_event{.time = timeval{}, .type = EV_KEY, .code = KEY_BACKSPACE, .value = 0});
    }
}

void keyboard::put(std::string_view const text) {
    for (auto const cur_char : text) {
        put(input_event{.time  = timeval{},
                        .type  = EV_KEY,                              // key type
                        .code  = static_cast<std::uint8_t>(cur_char), // code
                        .value = KEY_PRESS});
    }
}

void keyboard::put(input_event ev) {
    ev.value = KEY_PRESS;
    if (auto const press_res = std::fwrite(&ev, sizeof(ev), 1, stdout); press_res == 0) {
        log("Failed to press {:d}", ev.code);
        return;
    }

    ev.value = KEY_RELEASE;

    // keep retrying
    for (;;) {
        if (auto const release_res = fwrite(&ev, sizeof(ev), 1, stdout); release_res == 1) {
            break;
        }
        std::this_thread::yield();
    }
}

void keyboard::buffer(input_event &ev) {
    switch (ev.value) {
        case KEY_PRESS: {
            break;
        }
        case KEY_RELEASE: {
            return;
        }
        case KEY_KEEPING_PRESSED: {
            return;
        }
        default: return;
    }
    if (events.size() > 500) {
        events.clear();
    }
    events.push_back(ev);
}

// true: try again
// false: give up
namespace {
    bool handle_errors() {
        static std::size_t tries = 1;
        ++tries;
        if (tries == fs8::give_up_limit + 1) {
            fs8::log("Tried {0:d} times and failed everytime to start the loop (or in the loop); giving up!", tries);
            return false;
        }
        return true;
    }
} // namespace

int keyboard::loop() noexcept {
    try {
        while (std::fread(&event, sizeof(event), 1, stdin) == 1) {
            if (event.type == EV_KEY) {
                buffer(event);
                // check();
            }

            if (auto const out_res = std::fwrite(&event, sizeof(event), 1, stdout); out_res == 0) {
                log("Can't send {}.", event.code);
            }
        }
    } catch (std::exception const &ex) {
        log("Error: \"{0:s}\" at the main loop.", ex.what());
        if (handle_errors()) {
            return loop();
        }
        return 1;
    } catch (...) {
        log("Unknown error occurred. Try reporting this at https://github.com/the-moisrex/foresight.");
        if (handle_errors()) {
            return loop();
        }
        return 1;
    }
    return 0;
}
