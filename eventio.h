// Created by moisrex on 9/9/22.

#ifndef SMART_KEYBOARD_EVENTIO_H
#define SMART_KEYBOARD_EVENTIO_H

#include <linux/input.h>
#include <map>
#include <cstdio>
#include <string>
#include <vector>
#include "translate.h"


static constexpr std::size_t give_up_limit = 5;

struct eventio {

    std::map<std::string, std::string> keys;
    std::vector<input_event> events;
    input_event event;
    std::string str;

    eventio();

private:
    void to_string();

    void check();

    void replace(std::pair<std::string, std::string> const &req);

    void backspace(std::size_t i);

    void put(std::string const &str);

    void put(input_event event);

    void buffer(input_event &ev);

public:
    int loop() noexcept;
};


#endif //SMART_KEYBOARD_EVENTIO_H
