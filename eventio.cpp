// Created by moisrex on 9/9/22.

#include "eventio.h"
#include <algorithm>
#include <iostream>
#include <spdlog/spdlog.h>


eventio::eventio() {
    setbuf(stdin, NULL), setbuf(stdout, NULL);

    keys.emplace("namesp", "namespace");
}


void eventio::to_string() {
    str.reserve(events.size());
    for (auto &event: events) {
        const char c = to_char(event.code);
        str.push_back(c);
    }
    std::cerr << str << std::endl;
}

void eventio::check() {
    to_string();
    auto it = std::find_if(keys.begin(), keys.end(), [this](auto const &p) {
        auto const &[key, value] = p;
        return str.ends_with(key.data());
    });
    if (it != keys.end()) {
        replace(*it);
    }
}

void eventio::replace(const std::pair<std::string, std::string> &req) {
    backspace(req.first.size());
    put(req.second);
    events.clear();
}

void eventio::backspace(std::size_t i) {
    for (; i != 0; i--) {
        put(input_event{.type = EV_KEY, .code = KEY_BACKSPACE});
    }
}

void eventio::put(const std::string &str) {
    for (auto const &c: str) {
        put(input_event{.type = EV_KEY,
                .code = static_cast<unsigned short>(c),
                .value = KEY_PRESS});
    }
}

void eventio::put(input_event event) {
    event.value = KEY_PRESS;
    fwrite(&event, sizeof(event), 1, stdout);
    event.value = KEY_RELEASE;
    fwrite(&event, sizeof(event), 1, stdout);
}

void eventio::buffer(input_event &ev) {
    switch (ev.value) {
        case KEY_PRESS: {

            break;
        }
        case KEY_RELEASE: {
            return;
            break;
        }
        case KEY_KEEPING_PRESSED: {
            return;
            break;
        }
    }
    if (events.size() > 500) {
        events.clear();
    }
    events.push_back(ev);
}


// true: try again
// false: give up
bool handle_errors() {
    static std::size_t tries = 1;
    ++tries;
    if (tries == give_up_limit + 1) {
        spdlog::critical("Tried {0:d} times and failed everytime to start the loop (or in the loop); giving up!",
                         tries);
        return false;
    }
    return true;
};

int eventio::loop() noexcept {
    try {
        while (fread(&event, sizeof(event), 1, stdin) == 1) {
            if (event.type == EV_KEY) {
                buffer(event);
                check();
            }

            fwrite(&event, sizeof(event), 1, stdout);
        }
    } catch (std::exception const &ex) {
        spdlog::error("Error: \"{0:s}\" at the main loop.", ex.what());
        if (handle_errors()) {
            loop();
        } else {
            return 1;
        }
    } catch (...) {
        spdlog::error(
                "Unknown error occurred. Try reporting this at https://github.com/the-moisrex/smart-keyboard."
        );
        if (handle_errors()) {
            loop();
        } else {
            return 1;
        }
    }
    return 0;
}
