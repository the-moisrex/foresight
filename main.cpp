#include <algorithm>
#include <iostream>
#include <linux/input.h>
#include <map>
#include <cstdio>
#include <string>
#include <vector>

#include "constants.h"

struct predictor {

    std::map<std::string, std::string> keys;
    std::vector<input_event> events;
    input_event event;
    std::string str;

    predictor() { keys.emplace("namesp", "namespace"); }

    static char to_char(unsigned short c) {
        return c > sizeof(visual_keys) ? null_key : visual_keys[c];
    }

    void to_string() {
        str.reserve(events.size());
        for (auto &event: events) {
            const char c = to_char(event.code);
            str.push_back(c);
        }
        std::cerr << str << std::endl;
    }

    void check() {
        to_string();
        auto it = std::find_if(keys.begin(), keys.end(), [this](auto const &p) {
            auto const &[key, value] = p;
            return str.ends_with(key.data());
        });
        if (it != keys.end()) {
            replace(*it);
        }
    }

    void replace(std::pair<std::string, std::string> const &req) {
        backspace(req.first.size());
        put(req.second);
        events.clear();
    }

    void backspace(std::size_t i) {
        for (; i != 0; i--) {
            put(input_event{.type = EV_KEY, .code = KEY_BACKSPACE});
        }
    }

    void put(std::string const &str) {
        for (auto const &c: str) {
            put(input_event{.type = EV_KEY,
                    .code = static_cast<unsigned short>(c),
                    .value = KEY_PRESS});
        }
    }

    void put(input_event event) {
        event.value = KEY_PRESS;
        fwrite(&event, sizeof(event), 1, stdout);
        event.value = KEY_RELEASE;
        fwrite(&event, sizeof(event), 1, stdout);
    }

    void buffer(input_event &ev) {
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

    void loop() {
        while (fread(&event, sizeof(event), 1, stdin) == 1) {
            if (event.type == EV_KEY) {
                buffer(event);
                check();
            }

            fwrite(&event, sizeof(event), 1, stdout);
        }
    }
};

int main(void) {
    setbuf(stdin, NULL), setbuf(stdout, NULL);

    predictor pdt;
    pdt.loop();
    return 0;
}
