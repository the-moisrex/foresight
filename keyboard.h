// Created by moisrex on 9/9/22.

#ifndef SMART_KEYBOARD_EVENTIO_H
#define SMART_KEYBOARD_EVENTIO_H

#include "translate.h"

#include <linux/input.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "evdev.h"

static constexpr std::size_t give_up_limit = 5;

struct keyboard {
              keyboard();
              keyboard(keyboard const &)  = delete;
              keyboard(keyboard &&)       = delete;
    keyboard &operator=(keyboard const &) = delete;
    keyboard &operator=(keyboard &&)      = delete;

    void to_string();

    void check();

    void replace(std::string_view replace_it, std::string_view replace_with);
    void remove(std::string_view text);

    void backspace(std::size_t count = 1);
    void put(std::string_view text);
    void put(input_event event);

    void buffer(input_event &event);

    // this loop is the main loop
    int loop() noexcept;


  private:
    std::vector<input_event> events;
    input_event              event;
    std::string              str;
};


#endif // SMART_KEYBOARD_EVENTIO_H
