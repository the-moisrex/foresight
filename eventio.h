// Created by moisrex on 9/9/22.

#ifndef SMART_KEYBOARD_EVENTIO_H
#define SMART_KEYBOARD_EVENTIO_H

#include "translate.h"
#include <memory>


static constexpr unsigned long long give_up_limit = 5;

struct eventio {
    eventio();

    eventio(eventio const &) = delete;

    eventio(eventio &&) = delete;

private:
    class impl;

    std::unique_ptr<impl> pimpl;

public:
    int loop() noexcept;
};


#endif //SMART_KEYBOARD_EVENTIO_H
