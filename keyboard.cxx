// Created by moisrex on 9/9/22.

module;

#include <algorithm>
#include <cstdio>
#include <linux/input.h>
#include <ranges>
#include <spdlog/spdlog.h>
#include <thread>

module foresight.keyboard;

keyboard::keyboard() {
    setbuf(stdin, nullptr);
    setbuf(stdout, nullptr);
}

void keyboard::to_string() {
    str.reserve(events.size());
    for ([[maybe_unused]] auto const &[time, type, code, value] : events) {
        char const cur = to_char(code);
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
        put(input_event{.type = EV_KEY, .code = KEY_BACKSPACE});
    }
}

void keyboard::put(std::string_view const text) {
    for (auto const cur_char : text) {
        put(input_event{.type  = EV_KEY,                              // key type
                        .code  = static_cast<std::uint8_t>(cur_char), // code
                        .value = KEY_PRESS});
    }
}

void keyboard::put(input_event event) {
    event.value = KEY_PRESS;
    if (auto const press_res = std::fwrite(&event, sizeof(event), 1, stdout); press_res == 0) {
        spdlog::error("Failed to press {:d}", event.code);
        return;
    }

    event.value = KEY_RELEASE;
    if (auto const release_res = fwrite(&event, sizeof(event), 1, stdout); release_res == 0) {
        // todo: Oh, SH*T, should we put this in a retry loop until release?
        spdlog::error("Failed to release {:d}", event.code);
    }
}

void keyboard::buffer(input_event &event) {
    switch (event.value) {
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
    events.push_back(event);
}

// true: try again
// false: give up
bool handle_errors() {
    static std::size_t tries = 1;
    ++tries;
    if (tries == give_up_limit + 1) {
        spdlog::critical(
          "Tried {0:d} times and failed everytime to start the loop (or in the loop); giving up!",
          tries);
        return false;
    }
    return true;
}

int keyboard::loop() noexcept {
    try {
        while (std::fread(&event, sizeof(event), 1, stdin) == 1) {
            if (event.type == EV_KEY) {
                buffer(event);
                // check();
            }

            if (auto const out_res = std::fwrite(&event, sizeof(event), 1, stdout); out_res == 0) {
                spdlog::error("Can't send {}.", event.code);
            }
        }
    } catch (std::exception const &ex) {
        spdlog::error("Error: \"{0:s}\" at the main loop.", ex.what());
        if (handle_errors()) {
            return loop();
        }
        return 1;
    } catch (...) {
        spdlog::error(
          "Unknown error occurred. Try reporting this at https://github.com/the-moisrex/foresight.");
        if (handle_errors()) {
            return loop();
        }
        return 1;
    }
    return 0;
}
