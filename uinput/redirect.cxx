module;
#include <algorithm>
#include <cassert>
#include <linux/input.h>
#include <ranges>
#include <thread>
#include <vector>
module foresight.redirect;
import foresight.uinput;
using foresight::redirector;

void redirector::set_input(int const inp_in_fd) {
    if (started) {
        throw std::runtime_error("Stop the device first, then change the input source");
    }
    inp_fd = inp_in_fd;

    // disable the buffer
    // std::setbuf(inp_fd, nullptr);
}

void redirector::set_output(uinput&& inp_dev) {
    if (started) {
        throw std::invalid_argument(
          "Bad timing; you should not append more devices after you started the loop.");
    }
    dev = std::move(inp_dev);
}

void redirector::stop() {
    started = false;
}

int redirector::loop() {
    if (!dev.is_ok()) {
        throw std::system_error(dev.error());
    }
    if (started) {
        throw std::logic_error("We're already running the redirector.");
    }

    started = true;

    input_event input;

    while (read(inp_fd, &input, sizeof(input)) == sizeof(input)) {
        // write the event:
        while (!dev.write(input)) [[unlikely]] {
            // todo: log?
            // skip a beat; don't put the pressure on the CPU
            std::this_thread::yield();
        }

        if (!started) [[unlikely]] {
            break;
        }
    }

    started = false;
    return EXIT_SUCCESS;
}
