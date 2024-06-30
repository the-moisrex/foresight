module;
#include <algorithm>
#include <linux/input.h>
#include <ranges>
#include <vector>
module foresight.redirect;

using device_pending = redirector::device_pending;


void redirector::set_input(FILE* inp_in_fd) noexcept {
    inp_fd = inp_in_fd;

    // disable the buffer
    std::setbuf(inp_fd, nullptr);
}

void redirector::append(uinput&& dev) {
    if (started) {
        throw std::invalid_argument(
          "Bad timing; you should not append more devices after you started the loop.");
    }
    devs.emplace_back(std::move(dev));
}

struct failure_type {
    failure_type(device_pending& dev, input_event inp_input) noexcept
      : err{dev.dev.error()},
        dev_ptr{std::addressof(dev)},
        input{inp_input} {}

    bool retry() {
        if (dev_ptr == nullptr) {
            return true;
        }

        if (dev_ptr->dev.write(input)) {
            dev_ptr = nullptr;
            err.clear();
            --dev_ptr->pending;
            return true;
        }
        err = dev_ptr->dev.error();
        return false;
    }

    [[nodiscard]] std::error_code error() const noexcept {
        return err;
    }

    [[nodiscard]] uinput* device_ptr() const noexcept {
        return std::addressof(dev_ptr->dev);
    }

  private:
    std::error_code err;
    device_pending* dev_ptr;
    input_event     input;
};

int redirector::loop() {
    started = true;

    input_event               input;
    std::vector<failure_type> failures;

    while (std::fread(&input, sizeof(input), 1, inp_fd) == 1) {
        for (auto& dev : devs) {
            // add it to the pendings list if the old events are not written:
            if (dev.pending != 0) {
                failures.emplace_back(dev, input);
                continue; // skip writing
            }

            // write the event:
            if (!dev.dev.write(input)) {
                failures.emplace_back(dev, input);
                ++dev.pending;
            }
        }

        // retrying the failed writes:
        auto const [first, last] = std::ranges::remove_if(failures, [](auto& failure) {
            return failure.retry();
        });
        failures.erase(first, last);
    }

    started = false;
    return EXIT_SUCCESS;
}
