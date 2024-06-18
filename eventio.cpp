// Created by moisrex on 9/9/22.

#include "eventio.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <spdlog/spdlog.h>
#include <thread>

eventio::eventio() {
    setbuf(stdin, nullptr);
    setbuf(stdout, nullptr);

    keys.emplace("namesp", "namespace");
}

void eventio::to_string() {
    str.reserve(events.size());
    for ([[maybe_unused]] auto const &[time, type, code, value] : events) {
        char const cur = to_char(code);
        str.push_back(cur);
    }
}

void eventio::check() {
    to_string();
    auto const pos = std::ranges::find_if(keys, [this](auto const &cur_kry_value_pair) {
        auto const &[key, value] = cur_kry_value_pair;
        return str.ends_with(key.data());
    });
    if (pos != keys.end()) {
        replace(*pos);
    }
}

void eventio::replace(std::pair<std::string, std::string> const &req) {
    backspace(req.first.size());
    put(req.second);
    events.clear();
}

void eventio::backspace(std::size_t count) {
    for (; count != 0; count--) {
        put(input_event{.type = EV_KEY, .code = KEY_BACKSPACE});
    }
}

void eventio::put(std::string_view const str) {
    for (auto const cur_char : str) {
        put(input_event{.type  = EV_KEY,                              // key type
                        .code  = static_cast<std::uint8_t>(cur_char), // code
                        .value = KEY_PRESS});
    }
}

void eventio::put(input_event event) {
    event.value = KEY_PRESS;
    if (auto const press_res = std::fwrite(&event, sizeof(event), 1, stdout); press_res == 0) {
        spdlog::error("Failed to press {:d}", event.code);
        return;
    }

    event.value = KEY_RELEASE;
    if (auto const release_res = fwrite(&event, sizeof(event), 1, stdout); release_res == 0) {
        // Oh, SH*T, should we put this in a retry loop until release?
        spdlog::error("Failed to release {:d}", event.code);
    }
}

void eventio::buffer(input_event &event) {
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

int eventio::loop() noexcept {
    try {
        while (std::fread(&event, sizeof(event), 1, stdin) == 1) {
            if (event.type == EV_KEY) {
                buffer(event);
                check();
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
