// Created by moisrex on 9/9/22.

module;
#include <linux/input.h>
#include <string>
#include <string_view>
#include <vector>
export module foresight.keyboard;
import foresight.evdev;
import foresight.translate;


export constexpr std::size_t give_up_limit = 5;

export struct keyboard {
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
    input_event              event{};
    std::string              str;
};
