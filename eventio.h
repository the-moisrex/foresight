// Created by moisrex on 9/9/22.

#ifndef SMART_KEYBOARD_EVENTIO_H
#define SMART_KEYBOARD_EVENTIO_H

#include "translate.h"

#include <linux/input.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>



static constexpr std::size_t give_up_limit = 5;

struct eventio {
             eventio();
             eventio(eventio const &)   = delete;
             eventio(eventio &&)        = delete;
    eventio &operator=(eventio const &) = delete;
    eventio &operator=(eventio &&)      = delete;

    void to_string();

    void check();

    void replace(std::pair<std::string, std::string> const &req);

    void backspace(std::size_t count = 1);
    void put(std::string_view str);
    void put(input_event event);

    void buffer(input_event &event);

    // this loop is the main loop
    int loop() noexcept;


  private:
    std::map<std::string, std::string> keys;
    std::vector<input_event>           events;
    input_event                        event;
    std::string                        str;
};


#endif // SMART_KEYBOARD_EVENTIO_H
